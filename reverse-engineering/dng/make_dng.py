#!/usr/bin/env python3
"""Write one DNG for the whole RAW frame (both lenses side by side).

    python3 make_dng.py shot-001-frame.bin shot-001-makernote.bin out.dng

The samples are copied unchanged (5824 x 2896, 16-bit little-endian, stride
11648, 10 significant bits in bits 6..15). Tags come from pair P2 and the
shot's own maker-note buffer (README.md):
  CFA GRBG, BlackLevel 4106, WhiteLevel 65472;
  AsShotNeutral = 1 / the AWB gains at 0x2eb0 (one value for both lenses;
    the lenses differ by about 1.7% R / 0.5% B);
  ForwardMatrix1 = Bradford(D65->D50) x sRGB->XYZ x the recorded CCM;
  ColorMatrix1 (D65) = diag(daylight white point P2, mean of both lenses)
    x CCM^-1 x XYZ->sRGB (P2 as D65 is assumed);
  BaselineExposure +0.34 (the measured x1.27 before the stock tone curve).

build_full() also writes the shot's Exif (exposure time, F-number, exposure
program, ISO, dates, focal length, white-balance mode), Orientation,
LensInfo, an optional ImageDescription and the camera's IQ records as a
Samsung Type 2 MakerNote (little-endian, offsets relative to its start, the
same four records as our JPEGs). Its samples start at a fixed offset,
FULL_HEADER_BYTES, so every such DNG has the same size. The C writer
g360_dng_header_full() produces the same bytes.
"""
import struct
import sys
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stock_iq  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
CALIBRATION_C = REPO / "src/lib/g360cap/g360_dng_calibration.c"

WIDTH, HEIGHT, STRIDE = 5824, 2896, 11648
BLACK, WHITE = 4106, 65472
BASELINE_EXPOSURE = 0.34
FULL_HEADER_BYTES = 40960
SOFTWARE_FULL = "gear360-modding g360cap"
# NoiseProfile at ISO 100: gain 0.078 e-/DN, read noise 54 DN, range 61366 DN.
NOISE_S, NOISE_O = 1.0 / (0.078 * 61366.0), (54.0 / 61366.0) * (54.0 / 61366.0)
F_NUMBER, FOCAL_LENGTH, FOCAL_35MM = (22, 10), (12, 10), 6   # as in our JPEG Exif
UNKNOWN = 0xFFFF
# (MakerNote tag, offset in the capture library's buffer, bytes)
MAKERNOTE_RECORDS = ((0x0060, 0x32DC, 6000), (0x00A0, 0x14, 13000), (0x00A1, 0x14, 13000), (0x00C0, 0x4A4C, 2048))
SRGB_TO_XYZ = [[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750],
               [0.0193339, 0.1191920, 0.9503041]]
BRADFORD_D65_D50 = [[1.0478112, 0.0228866, -0.0501270], [0.0295424, 0.9904844, -0.0170491],
                    [-0.0092345, 0.0150436, 0.7521316]]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def inv(m):
    (a, b, c), (d, e, f), (g, h, i) = m
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    return [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det],
            [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
            [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]


def matrices(rec):
    ccm = rec["awb"]["ccm"]
    forward = mul(mul(BRADFORD_D65_D50, SRGB_TO_XYZ), ccm)
    p2 = rec["awb"]["white_points"][1]
    neutral = [(p2["unit_a"][k] + p2["unit_b"][k]) / 2 for k in range(3)]
    diag = [[neutral[0], 0, 0], [0, neutral[1], 0], [0, 0, neutral[2]]]
    color = mul(mul(diag, inv(ccm)), inv(SRGB_TO_XYZ))
    gains = rec["awb"]["gains_2eb0"]
    as_shot = [1 / g for g in gains]
    return color, forward, as_shot


def rational(x, signed):
    f = Fraction(x).limit_denominator(1000000)
    n, d = f.numerator, f.denominator
    if not signed and n < 0:
        raise ValueError("negative unsigned rational")
    return struct.pack("<ii" if signed else "<II", n, d)


def build(frame: bytes, rec: dict) -> bytes:
    if len(frame) != STRIDE * HEIGHT:
        raise ValueError("frame is %d bytes, expected %d" % (len(frame), STRIDE * HEIGHT))
    color, forward, as_shot = matrices(rec)
    flat = lambda m: [v for row in m for v in row]  # noqa: E731
    # (tag, type, count, value bytes); types: 1 BYTE 2 ASCII 3 SHORT 4 LONG 5 RATIONAL 10 SRATIONAL
    ascii_ = lambda s: (2, len(s) + 1, s.encode() + b"\0")  # noqa: E731
    entries = [
        (254, 4, 1, struct.pack("<I", 0)),
        (256, 4, 1, struct.pack("<I", WIDTH)),
        (257, 4, 1, struct.pack("<I", HEIGHT)),
        (258, 3, 1, struct.pack("<H", 16)),
        (259, 3, 1, struct.pack("<H", 1)),
        (262, 3, 1, struct.pack("<H", 32803)),
        (271, *ascii_("SAMSUNG")),
        (272, *ascii_("SM-R210")),
        (273, 4, 1, None),  # strip offset, filled below
        (277, 3, 1, struct.pack("<H", 1)),
        (278, 4, 1, struct.pack("<I", HEIGHT)),
        (279, 4, 1, struct.pack("<I", STRIDE * HEIGHT)),
        (284, 3, 1, struct.pack("<H", 1)),
        (305, *ascii_("gear360 make_dng.py")),
        (33421, 3, 2, struct.pack("<2H", 2, 2)),
        (33422, 1, 4, bytes([1, 0, 2, 1])),          # G R / B G
        (50706, 1, 4, bytes([1, 4, 0, 0])),
        (50707, 1, 4, bytes([1, 3, 0, 0])),
        (50708, *ascii_("Samsung Gear 360 SM-R210")),
        (50710, 1, 3, bytes([0, 1, 2])),
        (50711, 3, 1, struct.pack("<H", 1)),
        (50714, 4, 1, struct.pack("<I", BLACK)),
        (50717, 4, 1, struct.pack("<I", WHITE)),
        (50721, 10, 9, b"".join(rational(v, True) for v in flat(color))),
        (50728, 5, 3, b"".join(rational(v, False) for v in as_shot)),
        (50730, 10, 1, rational(BASELINE_EXPOSURE, True)),
        (50778, 3, 1, struct.pack("<H", 21)),       # D65
        (50964, 10, 9, b"".join(rational(v, True) for v in flat(forward))),
    ]
    entries.sort(key=lambda e: e[0])
    ifd_size = 2 + 12 * len(entries) + 4
    data_at = 8 + ifd_size
    blobs = b""
    strip_at = None
    for tag, typ, count, value in entries:
        if value is not None and len(value) > 4:
            blobs += value + (b"\0" if len(value) % 2 else b"")
    strip_at = data_at + len(blobs)
    strip_at += (-strip_at) % 4
    ifd = struct.pack("<H", len(entries))
    off = data_at
    for tag, typ, count, value in entries:
        if value is None:
            value = struct.pack("<I", strip_at)
        if len(value) <= 4:
            ifd += struct.pack("<HHI", tag, typ, count) + value.ljust(4, b"\0")
        else:
            ifd += struct.pack("<HHII", tag, typ, count, off)
            off += len(value) + (len(value) % 2)
    ifd += struct.pack("<I", 0)
    head = b"II*\0" + struct.pack("<I", 8) + ifd + blobs
    return head + b"\0" * (strip_at - len(head)) + frame


def opcode_lists():
    """The opcode lists the C writer stores after the samples: hot pixels,
    and vignetting when G360_DNG_VIGNETTING is 1 (else an empty second list)."""
    sys.path.insert(0, str(REPO / "gear360-calibration/dng-calibration"))
    import dng_calibration  # noqa: E402
    list1, list2 = dng_calibration.parse_c(CALIBRATION_C.read_text())
    header = CALIBRATION_C.with_suffix(".h").read_text()
    return list1, (list2 if "#define G360_DNG_VIGNETTING 1" in header else b"")


def trailer_pad():
    """Zero bytes after the opcode lists (macOS refuses a list ending at EOF)."""
    import re
    header = CALIBRATION_C.with_suffix(".h").read_text()
    return b"\0" * int(re.search(r"G360_DNG_TRAILER_PAD (\d+)u", header).group(1))


def _ifd(entries, at):
    """One little-endian IFD at absolute offset `at`, followed by its values
    longer than 4 bytes in tag order, each padded to an even length. A value
    given as ("@", offset) is stored elsewhere in the file, at that offset."""
    entries = sorted(entries, key=lambda e: e[0])
    data_at = at + 2 + 12 * len(entries) + 4
    ifd, blobs = struct.pack("<H", len(entries)), b""
    for tag, typ, count, value in entries:
        if isinstance(value, tuple):
            ifd += struct.pack("<HHII", tag, typ, count, value[1])
        elif len(value) <= 4:
            ifd += struct.pack("<HHI", tag, typ, count) + value.ljust(4, b"\0")
        else:
            ifd += struct.pack("<HHII", tag, typ, count, data_at + len(blobs))
            blobs += value + (b"\0" if len(value) % 2 else b"")
    return ifd + struct.pack("<I", 0) + blobs


def makernote(buffer: bytes) -> bytes:
    """The Samsung Type 2 MakerNote of our files, little-endian."""
    if len(buffer) < 0x5314:
        raise ValueError("maker-note buffer is %d bytes, expected at least 0x5314" % len(buffer))
    entries = [(0x0001, 7, 4, b"0100")]
    entries += [(tag, 7, size, buffer[at:at + size]) for tag, at, size in MAKERNOTE_RECORDS]
    return _ifd(entries, 0)


def _datetime_ok(text):
    return len(text) == 19 and all((c == p) if p != "0" else c.isdigit()
                                   for c, p in zip(text, "0000:00:00 00:00:00"))


def build_full(frame: bytes, rec: dict, buffer: bytes, exif: dict, description: str = "") -> bytes:
    """exif: exposure (numerator, denominator) or None, iso (0 unknown),
    program and white_balance (UNKNOWN 0xFFFF when not known), datetime
    "YYYY:MM:DD HH:MM:SS" or ""."""
    if len(frame) != STRIDE * HEIGHT:
        raise ValueError("frame is %d bytes, expected %d" % (len(frame), STRIDE * HEIGHT))
    datetime = exif.get("datetime", "")
    if datetime and not _datetime_ok(datetime):
        raise ValueError("bad datetime %r" % datetime)
    if len(description) > 63 or any(not 32 <= ord(c) < 127 for c in description):
        raise ValueError("description must be at most 63 printable ASCII characters")
    color, forward, as_shot = matrices(rec)
    flat = lambda m: [v for row in m for v in row]  # noqa: E731
    ascii_ = lambda t: (2, len(t) + 1, t.encode() + b"\0")  # noqa: E731
    ratio = lambda n, d: struct.pack("<II", n, d)  # noqa: E731
    ifd0 = [
        (254, 4, 1, struct.pack("<I", 0)),
        (256, 4, 1, struct.pack("<I", WIDTH)),
        (257, 4, 1, struct.pack("<I", HEIGHT)),
        (258, 3, 1, struct.pack("<H", 16)),
        (259, 3, 1, struct.pack("<H", 1)),
        (262, 3, 1, struct.pack("<H", 32803)),
        (271, *ascii_("SAMSUNG")),
        (272, *ascii_("SM-R210")),
        (273, 4, 1, struct.pack("<I", FULL_HEADER_BYTES)),
        (274, 3, 1, struct.pack("<H", 1)),
        (277, 3, 1, struct.pack("<H", 1)),
        (278, 4, 1, struct.pack("<I", HEIGHT)),
        (279, 4, 1, struct.pack("<I", STRIDE * HEIGHT)),
        (284, 3, 1, struct.pack("<H", 1)),
        (305, *ascii_(SOFTWARE_FULL)),
        (33421, 3, 2, struct.pack("<2H", 2, 2)),
        (33422, 1, 4, bytes([1, 0, 2, 1])),
        (34665, 4, 1, None),                         # Exif IFD, filled below
        (50706, 1, 4, bytes([1, 4, 0, 0])),
        (50707, 1, 4, bytes([1, 3, 0, 0])),
        (50708, *ascii_("Samsung Gear 360 SM-R210")),
        (50710, 1, 3, bytes([0, 1, 2])),
        (50711, 3, 1, struct.pack("<H", 1)),
        (50714, 4, 1, struct.pack("<I", BLACK)),
        (50717, 4, 1, struct.pack("<I", WHITE)),
        (50721, 10, 9, b"".join(rational(v, True) for v in flat(color))),
        (50728, 5, 3, b"".join(rational(v, False) for v in as_shot)),
        (50730, 10, 1, rational(BASELINE_EXPOSURE, True)),
        (50736, 5, 4, ratio(*FOCAL_LENGTH) + ratio(*FOCAL_LENGTH) + ratio(*F_NUMBER) + ratio(*F_NUMBER)),
        (50778, 3, 1, struct.pack("<H", 21)),       # D65
        (50964, 10, 9, b"".join(rational(v, True) for v in flat(forward))),
    ]
    list1, list2 = opcode_lists()
    after = FULL_HEADER_BYTES + STRIDE * HEIGHT
    ifd0.append((51008, 7, len(list1), ("@", after)))
    if list2:
        ifd0.append((51009, 7, len(list2), ("@", after + len(list1))))
    if exif.get("iso", 0) == 100:
        ifd0.append((51041, 12, 2, struct.pack("<2d", NOISE_S, NOISE_O)))
    if description:
        ifd0.append((270, *ascii_(description)))
    if datetime:
        ifd0.append((306, *ascii_(datetime)))
    exif_entries = [
        (33437, 5, 1, ratio(*F_NUMBER)),
        (36864, 7, 4, b"0230"),
        (37386, 5, 1, ratio(*FOCAL_LENGTH)),
        (37500, 7, None, makernote(buffer)),
        (41989, 3, 1, struct.pack("<H", FOCAL_35MM)),
    ]
    exposure = exif.get("exposure")
    if exposure and exposure[0] and exposure[1]:
        exif_entries.append((33434, 5, 1, ratio(*exposure)))
    if exif.get("program", UNKNOWN) not in (UNKNOWN,) and exif["program"] <= 0xFFFF:
        exif_entries.append((34850, 3, 1, struct.pack("<H", exif["program"])))
    if 0 < exif.get("iso", 0) <= 0xFFFF:
        exif_entries.append((34855, 3, 1, struct.pack("<H", exif["iso"])))
    if datetime:
        exif_entries += [(36867, *ascii_(datetime)), (36868, *ascii_(datetime))]
    if exif.get("white_balance", UNKNOWN) not in (UNKNOWN,) and exif["white_balance"] <= 0xFFFF:
        exif_entries.append((41987, 3, 1, struct.pack("<H", exif["white_balance"])))
    exif_entries = [(t, ty, len(v) if n is None else n, v) for t, ty, n, v in exif_entries]
    # IFD0 first with a placeholder, so its size (and the Exif IFD's offset) is known.
    placeholder = [(t, ty, n, struct.pack("<I", 0) if v is None else v) for t, ty, n, v in ifd0]
    exif_at = 8 + len(_ifd(placeholder, 8))
    first = [(t, ty, n, struct.pack("<I", exif_at) if v is None else v) for t, ty, n, v in ifd0]
    head = b"II*\0" + struct.pack("<I", 8) + _ifd(first, 8) + _ifd(exif_entries, exif_at)
    if len(head) > FULL_HEADER_BYTES:
        raise ValueError("header is %d bytes, over %d" % (len(head), FULL_HEADER_BYTES))
    return head + b"\0" * (FULL_HEADER_BYTES - len(head)) + frame + list1 + list2 + trailer_pad()


def read_full_exif(dng: bytes) -> tuple:
    """The exif dict and description build_full() was given, read back from
    a DNG it wrote (for rebuilding and comparing)."""
    ifd0 = {t: (typ, n, v) for t, typ, n, v, _ in stock_iq._ifd(dng, struct.unpack_from("<I", dng, 4)[0], "<")}
    at = struct.unpack("<I", ifd0[34665][2])[0]
    tags = {t: (typ, n, v) for t, typ, n, v, _ in stock_iq._ifd(dng, at, "<")}
    exif = {"datetime": tags[36867][2].rstrip(b"\0").decode() if 36867 in tags else "",
            "program": struct.unpack("<H", tags[34850][2][:2])[0] if 34850 in tags else UNKNOWN,
            "white_balance": struct.unpack("<H", tags[41987][2][:2])[0] if 41987 in tags else UNKNOWN,
            "iso": struct.unpack("<H", tags[34855][2][:2])[0] if 34855 in tags else 0,
            "exposure": struct.unpack("<II", tags[33434][2]) if 33434 in tags else None}
    description = ifd0[270][2].rstrip(b"\0").decode() if 270 in ifd0 else ""
    return exif, description


def main(argv) -> int:
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    frame = Path(argv[0]).read_bytes()
    rec = stock_iq.read(argv[1])
    Path(argv[2]).write_bytes(build(frame, rec))
    color, forward, as_shot = matrices(rec)
    print("wrote %s: AsShotNeutral %s" % (argv[2], " ".join("%.4f" % v for v in as_shot)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
