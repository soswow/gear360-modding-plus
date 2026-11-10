#!/usr/bin/env python3
"""Independent APP1 layout, Samsung records, optional fields and refusals."""
import ctypes as C
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

from test_dng import Image, HERE, maker, fixture, stock_iq


class Info(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ("width", "height", "exposure_numerator",
                "exposure_denominator", "iso", "exposure_program", "white_balance")] + [("datetime", C.c_char * 20)]


def reference(note, info):
    # Use the existing independent stock-JPEG fixture for the nested MakerNote.
    tags = {1: b"0100", 0x60: note[0x32dc:0x32dc+6000],
            0xa0: note[0x14:0x14+13000], 0xa1: note[0x14:0x14+13000],
            0xc0: note[0x4a4c:0x4a4c+2048]}
    old = stock_iq.exif_tiff(fixture.jpeg(tags))
    old_exif = struct.unpack_from(">I", old, 18)[0]
    mn = next(v for tag, _, _, v, _ in stock_iq._ifd(old, old_exif, ">") if tag == 0x927c)
    short = lambda n: struct.pack(">H", n)
    long = lambda n: struct.pack(">I", n)
    rational = lambda n, d: struct.pack(">II", n, d)
    d0 = [(0x10f,2,8,b"SAMSUNG\0"),(0x110,2,8,b"SM-R210\0"),
          (0x112,3,1,short(1)),(0x11a,5,1,rational(350,1)),(0x11b,5,1,rational(350,1)),
          (0x128,3,1,short(2)),(0x131,2,8,b"g360cap\0"),(0x213,3,1,short(2))]
    de = [(0x829d,5,1,rational(22,10)),(0x9000,7,4,b"0230"),(0x9101,7,4,b"\1\2\3\0"),
          (0x920a,5,1,rational(12,10)),(0x927c,7,len(mn),mn),(0xa000,7,4,b"0100"),
          (0xa001,3,1,short(1)),(0xa002,4,1,long(info.width)),(0xa003,4,1,long(info.height)),
          (0xa405,3,1,short(6))]
    if info.exposure_numerator:
        de.append((0x829a,5,1,rational(info.exposure_numerator,info.exposure_denominator)))
    if info.iso: de.append((0x8827,3,1,short(info.iso)))
    if info.exposure_program != 0xffff: de.append((0x8822,3,1,short(info.exposure_program)))
    if info.white_balance != 0xffff: de.append((0xa403,3,1,short(info.white_balance)))
    if info.datetime:
        date = bytes(info.datetime) + b"\0"
        d0.append((0x132,2,20,date)); de.extend([(0x9003,2,20,date),(0x9004,2,20,date)])
    exif_at = 8 + 6 + 12 * (len(d0) + 1)
    d0.append((0x8769,4,1,long(exif_at)))
    offset = exif_at + 6 + 12 * len(de)
    blobs = bytearray()
    directories = []
    for entries in (sorted(d0), sorted(de)):
        directory = bytearray(short(len(entries)))
        for tag, typ, count, value in entries:
            directory += struct.pack(">HHI",tag,typ,count)
            if len(value) <= 4: directory += value.ljust(4,b"\0")
            else:
                directory += long(offset + len(blobs))
                blobs += value + b"\0" * (len(value) % 2)
        directories.append(directory + b"\0" * 4)
    tiff = b"MM\0*" + long(8) + b"".join(directories) + blobs
    return b"\xff\xe1" + short(len(tiff)+8) + b"Exif\0\0" + tiff


def main():
    with tempfile.TemporaryDirectory(prefix="g360-exif-") as td:
        work = Path(td)
        flags = [os.environ.get("CC","cc"),"-std=c99","-Wall","-Wextra","-Werror","-pedantic",
                 "-I"+str(HERE),"-I"+str(HERE.parent/"capture-session")]
        path = work/"exif.so"
        subprocess.run(flags+["-O2","-shared","-fPIC",str(HERE/"g360_exif.c"),"-o",str(path)],check=True)
        lib = C.CDLL(str(path))
        lib.g360_exif_app1.argtypes = [C.POINTER(Image),C.POINTER(Info),C.POINTER(C.c_ubyte),C.c_size_t,C.POINTER(C.c_size_t)]
        note_bytes = maker()
        buf = C.create_string_buffer(note_bytes)
        note = Image(C.cast(buf,C.POINTER(C.c_ubyte)),len(note_bytes),0,0,0,0,0,2)
        info = Info(8,8,10,100,400,3,1,b"2026:11:05 09:30:00")

        def call(info_=info, note_=note, capacity=40000):
            out = (C.c_ubyte*(capacity+16))(*([0xa5]*(capacity+16)))
            size = C.c_size_t(999)
            status = lib.g360_exif_app1(None if note_ is None else C.byref(note_),
                                       None if info_ is None else C.byref(info_),out,capacity,C.byref(size))
            assert bytes(out)[capacity:] == b"\xa5"*16
            if status: assert size.value == 0
            return status,bytes(out)[:size.value]

        status, data = call()
        assert status == 0 and data == reference(note_bytes,info)
        assert call(capacity=len(data)) == (0,data)
        assert call(capacity=len(data)-1)[0] == 1
        # Real tiny JPEG, encoded by macOS; no photographs/dependencies.
        def chunk(tag,value):
            return struct.pack(">I",len(value))+tag+value+struct.pack(">I",zlib.crc32(tag+value)&0xffffffff)
        png = b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",8,8,8,0,0,0,0))
        png += chunk(b"IDAT",zlib.compress((b"\0"+bytes([120])*8)*8))+chunk(b"IEND",b"")
        (work/"tiny.png").write_bytes(png)
        subprocess.run(["sips","-s","format","jpeg",str(work/"tiny.png"),"--out",str(work/"tiny.jpg")],check=True,capture_output=True)
        original = (work/"tiny.jpg").read_bytes()
        # sips adds its own Exif; the camera encoder supplies a bare stream.
        bare = original[:2]; pos = 2
        while pos + 4 <= len(original) and original[pos:pos+2] != b"\xff\xda":
            end = pos + 2 + struct.unpack_from(">H", original, pos + 2)[0]
            segment = original[pos:end]
            if segment[:2] != b"\xff\xe1" or segment[4:10] != b"Exif\0\0": bare += segment
            pos = end
        original = bare + original[pos:]
        jpg = original[:2]+data+original[2:]
        path = work/"with-exif.jpg"; path.write_bytes(jpg)
        assert stock_iq.makernote(jpg) == {1:b"0100",0x60:fixture.ae_record(),
            0xa0:fixture.awb_record(),0xa1:fixture.awb_record(),0xc0:fixture.ipc_record()}
        assert stock_iq.read(str(path))["awb"]["version"] == 0x105
        if shutil.which("exiftool"):
            r = subprocess.run(["exiftool","-j","-n","-validate","-warning","-error","-Make","-Model",
                 "-ExposureTime","-ExposureProgram","-ISO","-FNumber","-FocalLength","-WhiteBalance",str(path)],
                 check=True,capture_output=True,text=True)
            fields = json.loads(r.stdout)[0]
            assert fields["Validate"] in (0,"0 0 0") and "Warning" not in fields and "Error" not in fields,fields
            assert fields["Make"] == "SAMSUNG" and fields["Model"] == "SM-R210"
            assert fields["ExposureTime"] == .1 and fields["ISO"] == 400
            assert fields["FNumber"] == 2.2 and fields["FocalLength"] == 1.2
            assert fields["ExposureProgram"] == 3 and fields["WhiteBalance"] == 1
        minimal = Info(5792,2896,0,0,0,0xffff,0xffff,b"")
        status, small = call(minimal)
        assert status == 0 and small == reference(note_bytes,minimal) and len(small)<len(data)
        assert call() == (0,data), "optional-field call retained state"
        for date in (b"short",b"2026-11-05 09:30:00",b"2026:11:05 09:30:00X",b"x"*20):
            bad = Info.from_buffer_copy(info); bad.datetime=date
            assert call(bad)[0] == 1
        for name,value in (("width",0),("height",0),("iso",65536),("exposure_program",65536),
                           ("white_balance",65536),("exposure_numerator",0),("exposure_denominator",0)):
            bad = Info.from_buffer_copy(info); setattr(bad,name,value)
            assert call(bad)[0] == 1,(name,value)
        assert call(None)[0] == 1 and call(note_=None)[0] == 1
        assert call(capacity=0)[0] == 1
        for length in (0,0x14,0x5313):
            bad = Image.from_buffer_copy(note); bad.size=length
            assert call(note_=bad)[0] == 14
        bad = Image.from_buffer_copy(note); bad.flags=0
        assert call(note_=bad)[0] == 14
        for offset in (0x14,0x17,0x32dc,0x4a4c):
            buf[offset] = bytes([note_bytes[offset]^0xff])
            assert call()[0] == 14
            buf[offset] = bytes([note_bytes[offset]])
        assert bytes(buf)[:-1] == note_bytes
        # The real exposure time from the manual TV (trial RANGE14's model).
        lib.g360_exif_exposure_from_tv.argtypes = [C.c_int32, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]
        def real(tv):
            n, d = C.c_uint32(), C.c_uint32()
            lib.g360_exif_exposure_from_tv(tv, C.byref(n), C.byref(d))
            return n.value, d.value
        for tv in range(-200, 400):
            n, d = real(tv)
            want = round(min(max(1e7 * 2 ** (-tv / 16), 788), 1660000)) - 50 if tv >= 0 else 1660000 - 50
            # Within a tenth of a microsecond (the 16-bit fixed point), in lowest terms.
            assert abs(n * 10000000 // d - want) <= 1 and 10000000 % d == 0 and math.gcd(n, d) == 1, (tv, n, d, want)
        assert real(41) == (33199, 200000) and real(218) == (371, 5000000) and real(112) == (3123, 400000)
        assert real(-37) == real(0) == real(40) == (33199, 200000) and real(300) == real(219) == (369, 5000000)
        lib.g360_exif_exposure_from_tv(41, None, None)  # null pointers: nothing written, no crash
        print("Exif reference bytes, stock_iq, ExifTool, optional fields, bounded refusals and real exposure times passed")


if __name__ == "__main__":
    main()
