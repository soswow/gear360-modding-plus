#!/usr/bin/env python3
"""Decode the Gear 360 (SM-R210) lens-module EEPROMs.

Two 24C64 EEPROMs sit at 0x50 on I2C bus 3 (EEPROM "A", front lens, the
left half of the frame) and bus 4 ("B", rear lens, the right half). Linux
exposes them at /sys/devices/platform/drime5-i2c.{3,4}/i2c-{3,4}/{3,4}-0050/eeprom;
the RTOS keeps copies in its adjust-data regions 0x85c20000 (A) and
0x85c40000 (B). Both use the same layout.

Field names, offsets and sizes are the stock factory table in
/usr/lib/libfactory-mode.so (sha256 927c9e04..., 0x60-byte entries from
file offset 0xf7e08: name[0x50], u64 id = region << 32 | offset, u32 size).
The camera's factory log (/opt/share/dfms/dfms.log) shows the same IDs for
EEPROM B. See README.md in this directory for what each part is used for.

    python3 lens_eeprom.py A.bin B.bin [--json]
    python3 lens_eeprom.py --markdown EEPROM_AND_REGISTERS.md [--json]
    python3 lens_eeprom.py --extract-table libfactory-mode.so
"""
import json
import re
import struct
import sys

SIZE = 0x2000
CHECKSUM_OFFSET = 0x7FC

# offset size name (EEPROM A's table; "EEPA_" dropped). OPTICAL_AXIS_01..40
# (8 bytes each from 0x0d0) are added below.
_TABLE = """
0x000    2 TABLE_INFO_VER
0x002   12 CREATE_DATE
0x00e    1 NV_DUST
0x00f    1 NV_NOISE
0x010    1 NV_ISO
0x011    1 NV_MOT
0x012    1 NV_AWB1
0x013    1 NV_AWB2
0x014    1 NV_AWB3
0x015    1 NV_AWB4
0x016    2 RESERVED1
0x018    2 ISO_ASSET_NO
0x01a    2 MOT_ASSET_NO
0x01c    2 DUST_ASSET_NO
0x01e    2 NOISE_ASSET_NO
0x020    2 AWB1_ASSET_NO
0x022    1 DECENTER_X
0x023    1 DECENTER_Y
0x024    2 AWB2_ASSET_NO
0x026    4 RESERVED2
0x02a    3 AWB1_FW_VER
0x02d    3 AWB2_FW_VER
0x030    4 LIVE_ISO_GAIN_X1
0x034    4 LIVE_ISO_GAIN_X2
0x038    4 ISO_FLAG
0x03c    4 ISO_VERSION
0x040    2 AWB_LOW_GAIN_R
0x042    2 AWB_LOW_GAIN_G
0x044    2 AWB_LOW_GAIN_B
0x046    2 AWB_HIGH_GAIN_R
0x048    2 AWB_HIGH_GAIN_G
0x04a    2 AWB_HIGH_GAIN_B
0x04c   52 RESERVED3
0x080   40 MOT_PIMA
0x0a8    4 HISTOGRAM_PASS_AMOUNT
0x0ac    4 RESERVED4
0x0b0    3 ISO_FW_VER
0x0b3    3 MOT_FW_VER
0x0b6    3 DUST_FW_VER
0x0b9    3 NOISE_FW_VER
0x0bc    5 RESERVED5
0x0c1   15 SET_ID
0x210    1 OPTICAL_AXIS_FLAG
0x211    1 LENS_GROUP_NO
0x212    2 LENS_OFFSET_R
0x214    2 LENS_OFFSET_B
0x216    2 AWB_MODULE_POINT2_GAIN_R
0x218    2 AWB_MODULE_POINT2_GAIN_G
0x21a    2 AWB_MODULE_POINT2_GAIN_B
0x21c    2 AWB_MODULE_POINT3_GAIN_R
0x21e    2 AWB_MODULE_POINT3_GAIN_G
0x220    2 AWB_MODULE_POINT3_GAIN_B
0x222    2 AWB_MODULE_POINT4_GAIN_R
0x224    2 AWB_MODULE_POINT4_GAIN_G
0x226    2 AWB_MODULE_POINT4_GAIN_B
0x228    2 AWB_EEPROM_POINT2_GAIN_R
0x22a    2 AWB_EEPROM_POINT2_GAIN_G
0x22c    2 AWB_EEPROM_POINT2_GAIN_B
0x22e    2 AWB_EEPROM_POINT3_GAIN_R
0x230    2 AWB_EEPROM_POINT3_GAIN_G
0x232    2 AWB_EEPROM_POINT3_GAIN_B
0x234    2 AWB_EEPROM_POINT4_GAIN_R
0x236    2 AWB_EEPROM_POINT4_GAIN_G
0x238    2 AWB_EEPROM_POINT4_GAIN_B
0x23a    2 AWB_MODULE_LOW_GAIN_R
0x23c    2 AWB_MODULE_LOW_GAIN_G
0x23e    2 AWB_MODULE_LOW_GAIN_B
0x240    2 AWB_DELTA_E
0x242   30 RESERVED6
0x260    4 MODULE_COLOR_NOISE_R_G_DIFF
0x264    4 MODULE_COLOR_NOISE_B_G_DIFF
0x268    4 MODULE_DOT_NOISE_YMAX
0x26c    4 MODULE_DOT_NOISE_YMIN
0x270    4 MODULE_DOT_NOISE_YDIFF
0x274    4 MODULE_DOT_NOISE_YNMAX
0x278    4 MODULE_DOT_NOISE_YNMIN
0x27c    4 MODULE_DOT_NOISE_YNDIFF
0x280    4 MODULE_COLOR_NOISE_RMAX
0x284    4 MODULE_COLOR_NOISE_RMIN
0x288    4 MODULE_COLOR_NOISE_GMAX
0x28c    4 MODULE_COLOR_NOISE_GMIN
0x290    4 MODULE_COLOR_NOISE_BMAX
0x294    4 MODULE_COLOR_NOISE_BMIN
0x298    4 MODULE_LINE_NOISE1X
0x29c    4 MODULE_LINE_NOISE2X
0x2a0    4 MODULE_LINE_NOISE3X
0x2a4    4 MODULE_SHADING_DIFF
0x2a8    4 MODULE_EXPOSURE_AVG
0x2ac    4 MODULE_DUST_1X
0x2b0    4 MODULE_DUST_1Y
0x2b4    4 MODULE_DUST_1MIN
0x2b8    4 MODULE_DUST_1MAX
0x2bc    4 MODULE_DUST_2X
0x2c0    4 MODULE_DUST_2Y
0x2c4    4 MODULE_DUST_2MIN
0x2c8    4 MODULE_DUST_2MAX
0x2cc    4 MODULE_DUST_3X
0x2d0    4 MODULE_DUST_3Y
0x2d4    4 MODULE_DUST_3MIN
0x2d8    4 MODULE_DUST_3MAX
0x2dc   36 RESERVED7
0x300    4 BAT_CHARGE_COUNT
0x304    4 TA_INSERT_COUNT
0x308   13 CHIP_ID
0x315   10 MODULE_ID
0x31f    1 RESERVED8
0x320    4 SET_COLOR_NOISE_R_G_DIFF
0x324    4 SET_COLOR_NOISE_B_G_DIFF
0x328    4 SET_DOT_NOISE_YMAX
0x32c    4 SET_DOT_NOISE_YMIN
0x330    4 SET_DOT_NOISE_YDIFF
0x334    4 SET_DOT_NOISE_YNMAX
0x338    4 SET_DOT_NOISE_YNMIN
0x33c    4 SET_DOT_NOISE_YNDIFF
0x340    4 SET_COLOR_NOISE_RMAX
0x344    4 SET_COLOR_NOISE_RMIN
0x348    4 SET_COLOR_NOISE_GMAX
0x34c    4 SET_COLOR_NOISE_GMIN
0x350    4 SET_COLOR_NOISE_BMAX
0x354    4 SET_COLOR_NOISE_BMIN
0x358    4 SET_LINE_NOISE1X
0x35c    4 SET_LINE_NOISE2X
0x360    4 SET_LINE_NOISE3X
0x364    4 SET_SHADING_DIFF
0x368    4 SET_EXPOSURE_AVG
0x36c    4 SET_DUST_1X
0x370    4 SET_DUST_1Y
0x374    4 SET_DUST_1MIN
0x378    4 SET_DUST_1MAX
0x37c    4 SET_DUST_2X
0x380    4 SET_DUST_2Y
0x384    4 SET_DUST_2MIN
0x388    4 SET_DUST_2MAX
0x38c    4 SET_DUST_3X
0x390    4 SET_DUST_3Y
0x394    4 SET_DUST_3MIN
0x398    4 SET_DUST_3MAX
0x39c 1120 RESERVED9
0x7fc    4 CHECKSUM
"""

FIELDS = [(int(o, 16), int(s), n) for o, s, n in (line.split() for line in _TABLE.strip().splitlines())]
FIELDS += [(0xD0 + 8 * i, 8, "OPTICAL_AXIS_%02d" % (i + 1)) for i in range(40)]
FIELDS.sort()

TEXT_FIELDS = {"CREATE_DATE", "SET_ID", "CHIP_ID", "MODULE_ID"}

# RTOS golden sample for the AWB adjust point, 1/65536 units with G = 1.0:
# tuning data at 0x806d5450 +0x144 (R) and +0x14c (B) in the saved RTOS image
# (read from output/decompile/rea/pass-opus-engine-20261019/rtos/rtos-full.elf).
# 46160 / 16 = 2885 and 22064 / 16 = 1379 in the EEPROM's 1/4096 units.
GOLDEN_LOW_R, GOLDEN_LOW_B = 46160, 22064


def checksum(img: bytes) -> int:
    """The factory checksum stored at 0x7fc: the sum of the little-endian
    u32 words at 0x000-0x7fb, modulo 2**32."""
    return sum(struct.unpack_from("<%dI" % (CHECKSUM_OFFSET // 4), img, 0)) & 0xFFFFFFFF


def stored_checksum(img: bytes) -> int:
    return struct.unpack_from("<I", img, CHECKSUM_OFFSET)[0]


def _value(img: bytes, off: int, size: int, name: str):
    raw = img[off:off + size]
    if all(b == 0xFF for b in raw):
        return None  # never written
    if name in TEXT_FIELDS or name.endswith(("_FW_VER", "_ASSET_NO")):
        text = re.match(rb"[\x20-\x7e]*", raw).group(0)
        rest = raw[len(text):]
        if rest.strip(b"\0"):
            return text.decode() + " " + rest.hex()  # MODULE_ID: "OVKER" + binary
        return text.decode()
    if name.startswith("NV_"):
        return chr(raw[0])  # factory step result: "P" or "N"
    if name.startswith("OPTICAL_AXIS_") and name != "OPTICAL_AXIS_FLAG":
        return struct.unpack("<d", raw)[0]
    if size in (1, 2, 4):
        return int.from_bytes(raw, "little")
    return raw.hex()


def fields(img: bytes) -> dict:
    """Every named field that was written ({name: value}); reserved and
    never-written (0xff) fields are left out."""
    out = {}
    for off, size, name in FIELDS:
        if name.startswith("RESERVED"):
            continue
        v = _value(img, off, size, name)
        if v is not None:
            out[name] = v
    return out


def _rgb(img: bytes, off: int):
    r, g, b = struct.unpack_from("<3H", img, off)
    return None if (r, g, b) == (0xFFFF,) * 3 else (r, g, b)


def awb_points(img: bytes) -> dict:
    """The white points the RTOS reads at run time (G = 4096). Point 1 is
    AWB_LOW_GAIN (0x040), points 2-4 are AWB_EEPROM_POINT2-4 (0x228-0x239).
    They are the AWB record's four per-lens white points in the stock JPEG,
    in the same order. module_low is the module maker's own low-point
    measurement (0x23a), which the RTOS reads only in factory adjust mode."""
    return {
        "point1_low": _rgb(img, 0x40),
        "point2": _rgb(img, 0x228),
        "point3": _rgb(img, 0x22E),
        "point4": _rgb(img, 0x234),
        "module_low": _rgb(img, 0x23A),
    }


def lens_adjust(img: bytes):
    """The per-lens AWB adjustment the RTOS derives (calcBodyAdjustGain,
    FUN_803bb4d8 / FUN_803c0fe8): golden / AWB_LOW_GAIN for R and B, as a
    truncated 1/65536 integer, G = 65536. This is what the stock JPEG's AWB
    record carries at 0x2dac (lens a = EEPROM A) and 0x2db8 (lens b)."""
    r, g, b = _rgb(img, 0x40)
    # The RTOS works on 1/65536 values (EEPROM x 16, low nibble cleared);
    # the ratio is the same.
    return (int(GOLDEN_LOW_R / (r * 16) * 65536), 65536, int(GOLDEN_LOW_B / (b * 16) * 65536))


def optical_axis(img: bytes) -> dict:
    """The optical-axis block (0x0d0-0x16f) that the stock app copies, from
    EEPROM A only, into every JPEG as Samsung MakerNote tag 0x0012 ("OPAX",
    160 bytes; ADE_ImageStitch_get_OPAX). Slot 01 holds the u32 format
    version (2); slots 02-16 the doubles. The labels are inferred, not
    named by the firmware: see README.md."""
    version = struct.unpack_from("<I", img, 0xD0)[0]
    d = struct.unpack_from("<15d", img, 0xD8)
    return {
        "version": version,
        "flag": img[0x210],
        "values": list(d),
        "inferred": {
            "lens1_centre_xy": d[0:2],
            "lens2_centre_xy": d[2:4],
            "lens1_radius": d[4],
            "lens2_radius": d[5],
            "set6": d[6:12],
            "set3": d[12:15],
        },
        "makernote_0x0012": img[0xD0:0x170].hex(),
    }


def rebuild_from_markdown(text: str) -> dict:
    """Rebuild both images from the hex rows in EEPROM_AND_REGISTERS.md
    (every row not listed is 0xff). Returns {"front": bytes, "rear": bytes}."""
    images = {}
    current = None
    for line in text.splitlines():
        if line.startswith("### Front"):
            current = "front"
        elif line.startswith("### Rear"):
            current = "rear"
        elif line.startswith("## ") and current:
            current = None
        m = re.match(r"^([0-9a-f]{8}): ((?:[0-9a-f]{4} ){7}[0-9a-f]{4})", line)
        if current and m:
            img = images.setdefault(current, bytearray(b"\xff" * SIZE))
            off = int(m.group(1), 16)
            img[off:off + 16] = bytes.fromhex(m.group(2).replace(" ", ""))
    return {k: bytes(v) for k, v in images.items()}


def extract_table(lib: bytes):
    """Read the factory field table out of libfactory-mode.so."""
    anchor = lib.find(struct.pack("<Q", 0x85C2000000000080))
    if anchor < 0:
        raise ValueError("table not found")
    start = anchor - 0x50
    ok = lambda p: re.fullmatch(rb"EEP[AB]_[A-Z0-9_]+", lib[p:p + 0x50].split(b"\0")[0] or b"-")
    while ok(start - 0x60):
        start -= 0x60
    rows = []
    while ok(start):
        name = lib[start:start + 0x50].split(b"\0")[0].decode()
        ident, size = struct.unpack_from("<QI", lib, start + 0x50)
        rows.append((ident >> 32, ident & 0xFFFFFFFF, size, name))
        start += 0x60
    return rows


# Short notes for FIELDS.md (what is known; README.md has the evidence).
NOTES = {
    "TABLE_INFO_VER": "layout version",
    "NV_DUST": "factory step result, P or N",
    "DECENTER_X": "lens decenter",
    "LIVE_ISO_GAIN_X1": "ISO line-adjust result",
    "LIVE_ISO_GAIN_X2": "ISO line-adjust result",
    "ISO_FLAG": "ISO adjust result nibble: 0xa pass, 0x5 fail (A low, B high)",
    "AWB_LOW_GAIN_R": "white point 1 (the adjust point), JPEG AWB record P1",
    "MOT_PIMA": "10 x u32; probably resolution-chart results",
    "SET_ID": "unit number, also memory/private/pmode_uniqueno",
    "OPTICAL_AXIS_01": "u32 format version (2); 0x0d0-0x16f is JPEG MakerNote tag 0x0012",
    "OPTICAL_AXIS_02": "lens 1 (front) centre x? (inferred)",
    "OPTICAL_AXIS_03": "lens 1 centre y? (inferred)",
    "OPTICAL_AXIS_04": "lens 2 (rear) centre x? (inferred)",
    "OPTICAL_AXIS_05": "lens 2 centre y? (inferred)",
    "OPTICAL_AXIS_06": "lens 1 image-circle radius? (inferred)",
    "OPTICAL_AXIS_07": "lens 2 image-circle radius, clipped to the frame? (inferred)",
    "AWB_EEPROM_POINT2_GAIN_R": "white point 2, JPEG AWB record P2",
    "AWB_EEPROM_POINT3_GAIN_R": "white point 3, JPEG AWB record P3",
    "AWB_EEPROM_POINT4_GAIN_R": "white point 4, JPEG AWB record P4",
    "AWB_MODULE_LOW_GAIN_R": "module maker's point 1; read only in factory mode",
    "MODULE_COLOR_NOISE_R_G_DIFF": "also read for JPEG MakerNote tag 0x0013 (LSC info)",
    "MODULE_ID": "maker code + serial",
    "CHECKSUM": "sum of u32 words 0x000-0x7fb",
}


def _cell(img: bytes, off: int, size: int, name: str) -> str:
    raw = img[off:off + size]
    if all(b == 0xFF for b in raw):
        return "·"
    if all(b == 0 for b in raw) and (name.startswith("RESERVED") or size > 8):
        return "0 (all %d bytes)" % size if size > 4 else "0"
    if name.startswith("RESERVED") or size > 15:
        if size % 4 == 0 and name == "MOT_PIMA":
            return " ".join(str(v) for v in struct.unpack("<%dI" % (size // 4), raw))
        return "`" + raw.hex() + "`" if size <= 8 else "`%s…` (%d bytes)" % (raw[:8].hex(), size)
    if name == "OPTICAL_AXIS_01":
        return "%d (u32)" % struct.unpack_from("<I", raw)[0]
    v = _value(img, off, size, name)
    if isinstance(v, float):
        return "%.9g" % v
    if name in ("ISO_FLAG", "CHECKSUM"):
        return "0x%x" % v
    if isinstance(v, str):
        return "`%s`" % v
    return str(v)


def fields_markdown(front: bytes, rear: bytes) -> str:
    """Every factory field with both modules' values, as a Markdown table."""
    lines = ["| Offset | Size | Factory name | Front (A, bus 3) | Rear (B, bus 4) | Note |",
             "|---|---|---|---|---|---|"]
    for off, size, name in FIELDS:
        lines.append("| 0x%03x | %d | %s | %s | %s | %s |" % (
            off, size, name, _cell(front, off, size, name), _cell(rear, off, size, name),
            NOTES.get(name, "")))
    return "\n".join(lines) + "\n"


def describe(img: bytes) -> dict:
    return {
        "checksum_ok": checksum(img) == stored_checksum(img),
        "checksum": "0x%08x" % stored_checksum(img),
        "fields": fields(img),
        "awb_points": awb_points(img),
        "lens_adjust": lens_adjust(img),
        "optical_axis": optical_axis(img),
    }


def main(argv) -> int:
    as_json = "--json" in argv
    argv = [a for a in argv if a != "--json"]
    if argv[:1] == ["--extract-table"]:
        for region, off, size, name in extract_table(open(argv[1], "rb").read()):
            print("%#x %#05x %4d %s" % (region, off, size, name))
        return 0
    if argv[:1] == ["--fields-md"]:
        # --fields-md EEPROM_AND_REGISTERS.md: the full field list, both modules
        images = rebuild_from_markdown(open(argv[1]).read())
        sys.stdout.write(fields_markdown(images["front"], images["rear"]))
        return 0
    if argv[:1] == ["--markdown"]:
        images = rebuild_from_markdown(open(argv[1]).read())
    else:
        images = {p: open(p, "rb").read() for p in argv}
    if not images:
        print(__doc__)
        return 2
    out = {k: describe(v) for k, v in images.items()}
    if as_json:
        print(json.dumps(out, indent=1, default=list))
        return 0
    for k, d in out.items():
        print("== %s (checksum %s, %s)" % (k, d["checksum"], "OK" if d["checksum_ok"] else "BAD"))
        for name, v in d["fields"].items():
            if name.startswith("OPTICAL_AXIS_"):
                continue
            print("  %-34s %s" % (name, v))
        for name, v in d["awb_points"].items():
            if v:
                print("  awb %-12s R/G %.4f B/G %.4f  %s" % (name, v[0] / v[1], v[2] / v[1], v))
        r, g, b = d["lens_adjust"]
        print("  lens adjust (RTOS)  R x%.6f  B x%.6f  (%d, %d, %d)" % (r / g, b / g, r, g, b))
        ax = d["optical_axis"]
        print("  optical axis v%d flag %d: %s" % (ax["version"], ax["flag"],
                                                 " ".join("%.9g" % x for x in ax["values"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

