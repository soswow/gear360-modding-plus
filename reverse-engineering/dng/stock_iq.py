#!/usr/bin/env python3
"""Read the image-processing records the Gear 360 writes into every stock JPEG.

The camera stores its 3A and colour state in the Samsung "Type 2" MakerNote
(Exif tag 0x927c). Three records matter for DNG making:

  0x00a0 / 0x00a1  AWB record, 13,000 bytes, XOR-masked with 0xC8. Starts
                   "AWB\\0", version 0x105 (RTOS writer FUN_803aa714,
                   called from IQ_MN_GetMNData_1). The two copies have
                   been identical in every JPEG checked.
  0x00c0           Colour-processing record, 2,048 bytes, header "I!?C"
                   (RTOS class CIpcParamD5, header written by FUN_803d647c).
  0x0060           AE record, 6,000 bytes, marker 0xaaffaaff
                   (CAE::GetMakerNote1, FUN_803b0a7c).

Field meanings are from comparing 721 stock JPEGs plus the RTOS writers;
README.md says which are observed and which are inferred.

It also reads the camera's maker-note buffer that g360-shot saves as
NAME-NNN-makernote.bin (our JPEGs carry no Exif). That buffer starts with
the RTOS's IQ block, laid out by its offset table at 0x806c21d8: AWB at
0x14, AE at 0x32dc, colour record at 0x4a4c.

    python3 stock_iq.py FILE.JPG|NAME-makernote.bin [...] [--json]
"""
import json
import struct
import sys

AWB_TAG, AWB_COPY_TAG, IPC_TAG, AE_TAG = 0x00a0, 0x00a1, 0x00c0, 0x0060
AWB_MASK = 0xC8
# CIQIfMakerNote offsets in the camera's maker-note buffer (RTOS table at
# 0x806c21d8; sizes from CIQIfMakerNote::GetSize).
BUFFER_RECORDS = {AWB_TAG: (0x14, 13000), AE_TAG: (0x32DC, 6000), IPC_TAG: (0x4A4C, 0x800)}
TYPE_SIZES = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 6: 1, 7: 1, 8: 2, 9: 4, 10: 8, 11: 4, 12: 8}


class NotStockJpeg(ValueError):
    pass


# ---- Exif / MakerNote -------------------------------------------------------

def exif_tiff(data: bytes) -> bytes:
    """The TIFF block inside the JPEG's APP1 Exif segment."""
    if data[:2] != b"\xff\xd8":
        raise NotStockJpeg("not a JPEG")
    i = 2
    while i + 4 <= len(data) and data[i] == 0xFF:
        marker = data[i + 1]
        length = struct.unpack(">H", data[i + 2:i + 4])[0]
        if marker == 0xE1 and data[i + 4:i + 10] == b"Exif\0\0":
            return data[i + 10:i + 2 + length]
        if marker == 0xDA:
            break
        i += 2 + length
    raise NotStockJpeg("no Exif segment")


def _ifd(tiff: bytes, offset: int, endian: str, base: int = 0):
    """Entries of one IFD as (tag, type, count, value bytes). `base` is added
    to value offsets (Samsung Type 2 MakerNote offsets are relative to the
    MakerNote's own start)."""
    if offset + 2 > len(tiff):
        raise NotStockJpeg("IFD outside the Exif block")
    count = struct.unpack(endian + "H", tiff[offset:offset + 2])[0]
    entries = []
    for k in range(count):
        p = offset + 2 + 12 * k
        tag, typ, n = struct.unpack(endian + "HHI", tiff[p:p + 8])
        size = TYPE_SIZES.get(typ, 1) * n
        if size <= 4:
            start = p + 8
        else:
            start = base + struct.unpack(endian + "I", tiff[p + 8:p + 12])[0]
        entries.append((tag, typ, n, tiff[start:start + size], p))
    return entries


def makernote(data: bytes) -> dict:
    """MakerNote entries {tag: raw value bytes} of a stock Gear 360 JPEG."""
    tiff = exif_tiff(data)
    endian = {b"MM": ">", b"II": "<"}.get(tiff[:2])
    if endian is None:
        raise NotStockJpeg("bad TIFF header")
    ifd0 = _ifd(tiff, struct.unpack(endian + "I", tiff[4:8])[0], endian)
    exif_ptr = [v for tag, _, _, v, _ in ifd0 if tag == 0x8769]
    if not exif_ptr:
        raise NotStockJpeg("no Exif IFD")
    exif = _ifd(tiff, struct.unpack(endian + "I", exif_ptr[0])[0], endian)
    mn = [(n, p) for tag, _, n, _, p in exif if tag == 0x927C]
    if not mn:
        raise NotStockJpeg("no MakerNote")
    start = struct.unpack(endian + "I", tiff[mn[0][1] + 8:mn[0][1] + 12])[0]
    entries = _ifd(tiff, start, endian, base=start)
    tags = {tag: value for tag, _, _, value, _ in entries}
    if tags.get(0x0001) != b"0100":
        raise NotStockJpeg("not a Samsung Type 2 MakerNote")
    return tags


# ---- Records ------------------------------------------------------------------

def _i32(b: bytes, off: int, n: int = 1):
    return list(struct.unpack_from("<%di" % n, b, off))


def _i16(b: bytes, off: int, n: int = 1):
    return list(struct.unpack_from("<%dh" % n, b, off))


def _matrix(values, scale):
    return [[values[r * 3 + c] / scale for c in range(3)] for r in range(3)]


def decode_awb(masked: bytes) -> dict:
    """The AWB record (tag 0x00a0). Gains are R, G, B with G = 1.0."""
    b = bytes(v ^ AWB_MASK for v in masked)
    if b[:4] != b"AWB\0":
        raise NotStockJpeg("AWB record signature missing")
    one = 65536.0

    def gains(off):
        r, g, bl = _i32(b, off, 3)
        return [r / one, g / one, bl / one]

    def white(off):
        r, g, bl = _i32(b, off, 3)
        return [r / g, 1.0, bl / g] if g else None

    points = []
    for flags_off, a_off, b_off in ((0x2E5C, 0x2E60, 0x2E6C), (0x2EC4, 0x2EC8, 0x2ED4),
                                    (0x2EF8, 0x2EFC, 0x2F08), (0x2FA4, 0x2FA8, 0x2FB4)):
        points.append({"valid": list(b[flags_off:flags_off + 2]),
                       "unit_a": white(a_off), "unit_b": white(b_off)})
    return {
        "version": struct.unpack_from("<H", b, 4)[0],
        # Per-shot gains (observed to vary per shot; which one the ISP
        # applies to RAW is not established yet).
        "gains_2eb0": gains(0x2EB0),
        "gains_2f2c": gains(0x2F2C),
        "factors_2f38": gains(0x2F38),
        "factors_2f44": gains(0x2F44),
        "values_2ea8": _i32(b, 0x2EA8, 2),
        "value_2ddc": struct.unpack_from("<H", b, 0x2DDC)[0],
        # Constant per camera: the two lens adjustments and four white
        # points per lens (raw R/G and B/G of a neutral; G = 1).
        "lens_adjust_a": gains(0x2DAC),
        "lens_adjust_b": gains(0x2DB8),
        "white_points": points,
        # Camera RGB (white-balanced) -> output RGB, rows sum to 1.
        "ccm": _matrix(_i32(b, 0x2E14, 9), 128.0),
        "ccm_copy_equal": _i32(b, 0x2E14, 9) == _i32(b, 0x2E38, 9),
    }


def decode_ipc(b: bytes) -> dict:
    """The colour-processing record (tag 0x00c0, CIpcParamD5)."""
    if b[:4] != b"I!?C":
        raise NotStockJpeg("colour record signature missing")
    curve_in = _i32(b, 0x174, 19)
    curve_out = _i32(b, 0x1C0, 19)
    return {
        "lens_gain_triples": [list(struct.unpack_from("<3H", b, off)) for off in (0xAE, 0xB4, 0xBA)],
        "tone_curve": {"in_max": 16383, "out_max": 1023, "points": list(zip(curve_in, curve_out))},
        "saturation_matrix": _matrix(_i16(b, 0x218, 9), 128.0),
        "saturation_copy_equal": _i16(b, 0x218, 9) == _i16(b, 0x22A, 9),
        "rgb_to_ycc": _matrix(_i16(b, 0x23C, 9), 256.0),
    }


def decode_ae_ccm(b: bytes) -> list:
    """The colour matrix copy inside the AE record (tag 0x0060)."""
    if struct.unpack_from("<I", b, 0)[0] != 0xAAFFAAFF:
        raise NotStockJpeg("AE record marker missing")
    return _matrix(_i16(b, 0xD1C, 9), 128.0)


def buffer_records(data: bytes) -> dict:
    """The records of a maker-note buffer saved by g360-shot, keyed like
    the JPEG MakerNote tags."""
    if len(data) < 0x5314:
        raise NotStockJpeg("maker-note buffer shorter than the IQ block")
    tags = {tag: data[off:off + size] for tag, (off, size) in BUFFER_RECORDS.items()}
    tags[AWB_COPY_TAG] = tags[AWB_TAG]
    return tags


def read(path: str) -> dict:
    with open(path, "rb") as f:
        data = f.read()
    tags = makernote(data) if data[:2] == b"\xff\xd8" else buffer_records(data)
    out = {"file": path}
    if AWB_TAG in tags:
        out["awb"] = decode_awb(tags[AWB_TAG])
        out["awb_copies_equal"] = tags.get(AWB_COPY_TAG) == tags[AWB_TAG]
    if IPC_TAG in tags:
        out["ipc"] = decode_ipc(tags[IPC_TAG])
    if AE_TAG in tags:
        out["ae_ccm"] = decode_ae_ccm(tags[AE_TAG])
    return out


def _fmt(v):
    return "[" + ", ".join("%.4f" % x for x in v) + "]"


def summary(rec: dict) -> str:
    lines = [rec["file"]]
    a = rec.get("awb")
    if a:
        lines.append("  AWB gains (2eb0) " + _fmt(a["gains_2eb0"]) + "   (2f2c) " + _fmt(a["gains_2f2c"]))
        lines.append("  lens adjust a " + _fmt(a["lens_adjust_a"]) + "   b " + _fmt(a["lens_adjust_b"]))
        for i, p in enumerate(a["white_points"]):
            lines.append("  white point %d  a %s  b %s" % (i + 1, _fmt(p["unit_a"]), _fmt(p["unit_b"])))
        lines.append("  CCM " + " ".join(_fmt(r) for r in a["ccm"]))
    p = rec.get("ipc")
    if p:
        lines.append("  lens gain triples " + str(p["lens_gain_triples"]))
        lines.append("  tone curve " + " ".join("%d>%d" % xy for xy in p["tone_curve"]["points"]))
        lines.append("  saturation matrix " + " ".join(_fmt(r) for r in p["saturation_matrix"]))
    return "\n".join(lines)


def main(argv) -> int:
    as_json = "--json" in argv
    paths = [a for a in argv if a != "--json"]
    if not paths:
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        return 2
    status = 0
    records = []
    for path in paths:
        try:
            records.append(read(path))
        except (OSError, NotStockJpeg, struct.error) as e:
            print("%s: %s" % (path, e), file=sys.stderr)
            status = 1
    if as_json:
        print(json.dumps(records, indent=1))
    else:
        print("\n".join(summary(r) for r in records))
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
