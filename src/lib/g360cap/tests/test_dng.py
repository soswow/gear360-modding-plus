#!/usr/bin/env python3
"""C header/reference DNG parity, numeric edge cases and input refusals.

No camera input is used. Test pixels and IQ records are synthetic; generated
files and the host shared library live in a TemporaryDirectory.
"""
import ctypes as C
from fractions import Fraction
import importlib.util
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
DNG = HERE.parents[2] / "reverse-engineering" / "dng"
sys.path.insert(0, str(DNG))
import make_dng  # noqa: E402
import stock_iq  # noqa: E402

spec = importlib.util.spec_from_file_location("iq_fixture", DNG / "tests/test_stock_iq.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class Image(C.Structure):
    _fields_ = [("data", C.POINTER(C.c_ubyte)), ("size", C.c_size_t)] + [
        (name, C.c_uint32) for name in ("width", "height", "stride_x", "stride_y",
                                      "camera_address", "flags")]


class Info(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ("width", "height", "exposure_numerator",
                "exposure_denominator", "iso", "exposure_program", "white_balance")] + [("datetime", C.c_char * 20)]


class Frame(C.Structure):
    _fields_ = [("image", Image)] + [(name, C.c_uint32) for name in ("physical", "hold_us", "mode")]


def maker(awb=None):
    buf = bytearray(0x5314)
    buf[0x14:0x14 + 13000] = fixture.awb_record() if awb is None else awb
    buf[0x32DC:0x32DC + 6000] = fixture.ae_record()
    buf[0x4A4C:0x4A4C + 2048] = fixture.ipc_record()
    return bytes(buf)


def main():
    rng = random.Random(360)
    with tempfile.TemporaryDirectory(prefix="g360-dng-") as td:
        work = Path(td)
        # Include the implementation in this test-only translation unit to
        # exercise its exact rational rounding without adding a public API.
        wrapper = work / "rational.c"
        wrapper.write_text('#include "g360_dng.c"\n'
                           'int test_rational(double x, int s, unsigned char *p) {return rational(x,s,p);}\n')
        flags = [os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                 "-ffp-contract=off", "-I" + str(HERE), "-I" + str(HERE.parent / "capture-session")]
        libpath = work / "dng.so"
        subprocess.run(flags + ["-O2", "-shared", "-fPIC", str(wrapper), "-o", str(libpath)], check=True)
        lib = C.CDLL(str(libpath))
        header = lib.g360_dng_header
        header.argtypes = [C.POINTER(Frame), C.POINTER(Image), C.POINTER(C.c_ubyte), C.c_size_t,
                           C.POINTER(C.c_size_t)]
        header.restype = C.c_int
        rat = lib.test_rational
        rat.argtypes = [C.c_double, C.c_int, C.POINTER(C.c_ubyte)]
        rat.restype = C.c_int
        frame = Frame(Image(C.cast(C.c_void_p(1), C.POINTER(C.c_ubyte)),
                            11648 * 2896, 5824, 2896, 11648, 16, 0, 0x102), 0, 0, 3)
        buf = C.create_string_buffer(maker())
        note = Image(C.cast(buf, C.POINTER(C.c_ubyte)), len(maker()), 0, 0, 0, 0, 0, 2)

        def call(frame_=frame, note_=note, capacity=588):
            out = (C.c_ubyte * 604)(*([0xA5] * 604))
            size = C.c_size_t(999)
            status = header(None if frame_ is None else C.byref(frame_),
                            None if note_ is None else C.byref(note_), out, capacity, C.byref(size))
            assert bytes(out)[588:] == b"\xA5" * 16, "header exceeded its bound"
            if status:
                assert size.value == 0
            return status, bytes(out)[:size.value]

        # Full-file parity, using unchanged nonzero synthetic sample bytes.
        frame_bytes = bytes(range(256)) * (11648 * 2896 // 256)
        rec = {"awb": stock_iq.decode_awb(fixture.awb_record())}
        status, head = call()
        assert status == 0 and len(head) == 588
        expected = make_dng.build(frame_bytes, rec)
        actual = head + frame_bytes
        assert actual == expected, "C DNG differs from Python reference"
        dng_path = work / "synthetic.dng"
        dng_path.write_bytes(actual)
        # The formats differ only in their tiny metadata header, so exercise
        # per-shot CCM, white point and AWB changes against the full reference.
        for n in range(32):
            raw_awb = bytearray(v ^ 0xC8 for v in fixture.awb_record())
            matrix = [231, -95, -8, -27, 186, -31, 2, -76, 202]
            for row in range(3):
                da, db = rng.randrange(-20, 21), rng.randrange(-20, 21)
                matrix[row * 3] += da
                matrix[row * 3 + 1] += db
                matrix[row * 3 + 2] -= da + db
            struct.pack_into("<9i", raw_awb, 0x2E14, *matrix)
            struct.pack_into("<3i", raw_awb, 0x2EB0,
                             rng.randrange(40000, 200000), 65536, rng.randrange(40000, 200000))
            struct.pack_into("<3i", raw_awb, 0x2EC8,
                             rng.randrange(1000, 5000), 4096, rng.randrange(1000, 5000))
            masked = bytes(v ^ 0xC8 for v in raw_awb)
            data = maker(masked)
            C.memmove(buf, data, len(data))
            status, head = call()
            assert status == 0, (n, status)
            expected = make_dng.build(frame_bytes, {"awb": stock_iq.decode_awb(masked)})
            assert head == expected[:588], ("matrix parity", n)
        C.memmove(buf, maker(), len(maker()))

        # The full header (Exif, MakerNote, description) against build_full.
        full = lib.g360_dng_header_full
        full.argtypes = [C.POINTER(Frame), C.POINTER(Image), C.POINTER(Info), C.c_char_p,
                         C.POINTER(C.c_ubyte), C.c_size_t, C.POINTER(C.c_size_t)]
        full.restype = C.c_int
        FULL = make_dng.FULL_HEADER_BYTES

        def call_full(info, description=None, capacity=FULL, frame_=frame):
            out = (C.c_ubyte * (FULL + 16))(*([0xA5] * (FULL + 16)))
            size = C.c_size_t(999)
            status = full(C.byref(frame_), C.byref(note), None if info is None else C.byref(info),
                          description, out, capacity, C.byref(size))
            assert bytes(out)[FULL:] == b"\xA5" * 16, "full header exceeded its bound"
            if status:
                assert size.value == 0
            return status, bytes(out)[:size.value]

        cases = [
            (Info(5792, 2896, 1, 125, 100, 1, 0, b"2026:11:06 08:12:30"), b"HDR SWEEP 20261106-081230 FRAME 4/6"),
            (Info(0, 0, 1, 8, 1600, 2, 1, b"2026:11:05 12:55:37"), None),
            (Info(0, 0, 0, 0, 0, 0xFFFF, 0xFFFF, b""), b""),          # nothing known
            (Info(0, 0, 10, 0, 70000, 3, 0xFFFF, b""), b"X"),          # half an exposure, ISO too big
        ]
        for info, description in cases:
            status, head = call_full(info, description)
            assert status == 0 and len(head) == FULL, (status, len(head))
            exif = {"exposure": (info.exposure_numerator, info.exposure_denominator), "iso": info.iso,
                    "program": info.exposure_program, "white_balance": info.white_balance,
                    "datetime": info.datetime.decode()}
            expected = make_dng.build_full(frame_bytes, rec, maker(), exif, (description or b"").decode())
            assert head + frame_bytes == expected, ("full parity", info.datetime, description)
            back, text = make_dng.read_full_exif(expected)
            assert text == (description or b"").decode() and back["datetime"] == info.datetime.decode()
            tags = {t: v for t, _, _, v, _ in stock_iq._ifd(expected, struct.unpack_from("<I", expected, 4)[0], "<")}
            exif_at = struct.unpack("<I", tags[34665])[0]
            etags = {t: (n, v) for t, _, n, v, _ in stock_iq._ifd(expected, exif_at, "<")}
            # The MakerNote carries the buffer's four IQ records unchanged.
            mn = etags[37500][1]
            notes = {t: v for t, _, _, v, _ in stock_iq._ifd(mn, 0, "<")}
            record = maker()
            assert notes[0x0001] == b"0100" and notes[0x0060] == record[0x32DC:0x32DC + 6000]
            assert notes[0x00A0] == notes[0x00A1] == record[0x14:0x14 + 13000]
            assert notes[0x00C0] == record[0x4A4C:0x4A4C + 2048]
            assert struct.unpack("<I", tags[273])[0] == FULL
        dng_full_path = work / "synthetic-full.dng"
        status, head = call_full(*cases[0])
        dng_full_path.write_bytes(head + frame_bytes)
        # Refusals: bad dates and descriptions, no info, small capacity, a bad frame.
        good = cases[0][0]
        for bad_date in (b"2026-11-06 08:12:30", b"2026:11:06 08:12:3x", b"2026:11:06"):
            bad = Info(*[getattr(good, n) for n, _ in Info._fields_[:-1]], bad_date)
            assert call_full(bad)[0] == 1, bad_date
        for bad_text in (b"x" * 64, b"tab\there", b"\x7f"):
            assert call_full(good, bad_text)[0] == 1, bad_text
        assert call_full(good, b"y" * 63)[0] == 0
        assert call_full(None)[0] == 1 and call_full(good, capacity=FULL - 1)[0] == 1
        bad_frame = Frame(Image(frame.image.data, frame.image.size - 2, 5824, 2896, 11648, 16, 0, 0x102), 0, 0, 3)
        assert call_full(good, frame_=bad_frame)[0] == 14  # G360_E_IMAGE

        # Exact-double rational reference, including tiny numbers, signed
        # limits, denominator boundaries and adjacent values at a rounding tie.
        values = [0.0, -0.0, 0.34, 1.0, -1.0, 1 / 3, -1 / 3, 1e-30, -1e-30,
                  1 / 2000000, math.nextafter(1 / 2000000, 0),
                  math.nextafter(1 / 2000000, 1), 1 / 1000001, 65536.0,
                  2147483647.0, -2147483648.0, 4294967295.0,
                  math.nextafter(2147483647.0, math.inf),
                  math.nextafter(-2147483648.0, -math.inf),
                  math.nextafter(4294967295.0, math.inf),
                  float("nan"), float("inf"), -float("inf")]
        values += [math.ldexp(rng.random() + 0.5, rng.randrange(-80, 33)) * rng.choice([-1, 1])
                   for _ in range(10000)]
        for x in values:
            for signed in (0, 1):
                out = (C.c_ubyte * 8)()
                status = rat(x, signed, out)
                try:
                    f = Fraction(x).limit_denominator(1000000)
                    if not signed and f.numerator < 0:
                        raise ValueError("negative unsigned")
                    expected = struct.pack("<ii" if signed else "<II", f.numerator, f.denominator)
                except (ValueError, OverflowError, struct.error):
                    assert status != 0, (x, signed, "should refuse")
                else:
                    assert status == 0 and bytes(out) == expected, (x, signed, bytes(out), expected)

        # Required pointers, exact extent/layout, captured-data checks and
        # colour-record integrity. Failure must not expose a partial header.
        assert call(frame_=None)[0] == 1 and call(note_=None)[0] == 1
        assert call(capacity=587)[0] == 1
        assert header(C.byref(frame), C.byref(note), None, 588, C.byref(C.c_size_t())) == 1
        assert header(C.byref(frame), C.byref(note), (C.c_ubyte * 588)(), 588, None) == 1
        for field, value in [("size", 1), ("width", 1), ("height", 1), ("stride_x", 1),
                             ("stride_y", 12), ("flags", 0), ("flags", 2),
                             ("flags", 0x302), ("flags", 0x502)]:
            old = getattr(frame.image, field)
            setattr(frame.image, field, value)
            assert call()[0] == 14, (field, value)
            setattr(frame.image, field, old)
        for field, value in [("size", 0x5313), ("flags", 0)]:
            old = getattr(note, field)
            setattr(note, field, value)
            assert call()[0] == 14, ("note", field)
            setattr(note, field, old)
        bad_awbs = []
        bad = bytearray(v ^ 0xC8 for v in fixture.awb_record())
        bad[0] = ord("X")
        bad_awbs.append(bad)
        bad = bytearray(v ^ 0xC8 for v in fixture.awb_record())
        bad[4] = 6
        bad_awbs.append(bad)
        for off, value in [(0x2EB0, 0), (0x2EB0, -1), (0x2ECC, 0), (0x2ED8, -1),
                           (0x2EC8, 0), (0x2EDC, -1)]:
            bad = bytearray(v ^ 0xC8 for v in fixture.awb_record())
            struct.pack_into("<i", bad, off, value)
            bad_awbs.append(bad)
        bad = bytearray(v ^ 0xC8 for v in fixture.awb_record())
        bad[0x2EC4] = 0
        bad_awbs.append(bad)
        bad = bytearray(v ^ 0xC8 for v in fixture.awb_record())
        struct.pack_into("<9i", bad, 0x2E14, *([0] * 9))
        bad_awbs.append(bad)
        # Invertible but near-singular, unrepresentable TIFF rational values.
        bad = bytearray(v ^ 0xC8 for v in fixture.awb_record())
        struct.pack_into("<9i", bad, 0x2E14, 1, 1000000, 0, 0, 1, 1000000, 0, 0, 1)
        bad_awbs.append(bad)
        for raw in bad_awbs:
            data = maker(bytes(v ^ 0xC8 for v in raw))
            C.memmove(buf, data, len(data))
            assert call()[0] == 14
        C.memmove(buf, maker(), len(maker()))

        # Run the bounded reader/writer under ASan/UBSan as a standalone C
        # executable too. The sentinel frame pointer establishes that header
        # generation never reads the 33.7 MB pixel payload. The note is an
        # exact-size allocation, and the output has a checked trailing guard.
        fixture_path = work / "maker.bin"
        fixture_path.write_bytes(maker())
        sanitized = work / "sanitized.c"
        sanitized.write_text(r'''
#include "g360_dng.c"
int main(int argc, char **argv) {
    g360_frame frame;
    g360_image note;
    unsigned char out[604];
    size_t size = 999, k;
    capture_file *file;
    uint64_t random_bits = UINT64_C(0x123456789abcde);
    if (argc != 2) return 1;
    memset(&frame, 0, sizeof frame); memset(&note, 0, sizeof note);
    frame.image.data = (unsigned char *)(uintptr_t)1;
    frame.image.size = 11648u * 2896u;
    frame.image.width = 5824; frame.image.height = 2896;
    frame.image.stride_x = 11648; frame.image.stride_y = 16;
    frame.image.flags = G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK;
    note.data = malloc(0x5314); note.size = 0x5314; note.flags = G360_IMAGE_COPIED;
    file = fopen(argv[1], "rb");
    if (file == 0 || note.data == 0 || fread(note.data, 1, note.size, file) != note.size) return 2;
    if (fclose(file) != 0) return 3;
    memset(out, 0xa5, sizeof out);
    if (g360_dng_header(&frame, &note, out, 588, &size) != G360_OK || size != 588) return 4;
    for (k = 588; k < sizeof out; ++k) if (out[k] != 0xa5) return 5;
    if (g360_dng_header(&frame, &note, out, 587, &size) != G360_E_ARGUMENT || size != 0) return 6;
    for (k = 0; k < note.size; ++k) {
        unsigned char saved = note.data[k];
        note.data[k] ^= 0xff;
        (void)g360_dng_header(&frame, &note, out, 588, &size);
        note.data[k] = saved;
    }
    for (k = 0; k < 100000; ++k) {
        union { uint64_t bits; double value; } x;
        random_bits ^= random_bits << 13;
        random_bits ^= random_bits >> 7;
        random_bits ^= random_bits << 17;
        x.bits = random_bits;
        (void)rational(x.value, 1, out);
        (void)rational(x.value, 0, out);
    }
    {   /* The full header under the sanitizers, with an exact-size output. */
        g360_exif_info info;
        unsigned char *big = malloc(G360_DNG_FULL_HEADER_BYTES);
        if (big == 0) return 7;
        memset(&info, 0, sizeof info);
        info.exposure_numerator = 1; info.exposure_denominator = 125; info.iso = 100;
        info.exposure_program = 1; info.white_balance = 0;
        memcpy(info.datetime, "2026:11:06 08:12:30", 20);
        if (g360_dng_header_full(&frame, &note, &info, "HDR SWEEP 1/6", big, G360_DNG_FULL_HEADER_BYTES, &size)
            != G360_OK || size != G360_DNG_FULL_HEADER_BYTES) return 8;
        info.datetime[0] = '\0';
        if (g360_dng_header_full(&frame, &note, &info, 0, big, G360_DNG_FULL_HEADER_BYTES, &size) != G360_OK) return 9;
        free(big);
    }
    free(note.data);
    return 0;
}
''')
        exe = work / "sanitized"
        subprocess.run(flags + ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                                str(sanitized), "-o", str(exe)], check=True)
        subprocess.run([str(exe), str(fixture_path)], check=True)

        # exiftool is useful independent format validation when installed.
        import shutil
        if shutil.which("exiftool"):
            for path in (dng_path, dng_full_path):
                result = subprocess.run(["exiftool", "-validate", "-warning", "-error", str(path)],
                                        check=True, capture_output=True, text=True)
                assert "Validate" in result.stdout and "OK" in result.stdout, result.stdout + result.stderr
            result = subprocess.run(["exiftool", "-s", "-s", "-s", "-ExposureTime", "-ISO", "-FNumber",
                                     "-DateTimeOriginal", "-ImageDescription", "-MakerNoteVersion", str(dng_full_path)],
                                    check=True, capture_output=True, text=True)
            assert result.stdout.split("\n")[:6] == ["1/125", "100", "2.2", "2026:11:06 08:12:30",
                                                     "HDR SWEEP 20261106-081230 FRAME 4/6", "0100"], result.stdout
        print("DNG C/Python full-file parity (plain and full metadata), 32 per-shot variants, %d rational cases, refusals and ASan/UBSan passed" %
              (len(values) * 2))


if __name__ == "__main__":
    main()
