"""Host tests for lens_eeprom.py, on the two images kept as hex rows in
output/peripherals-20261106/EEPROM_AND_REGISTERS.md (survey of 6 November)."""
import hashlib
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(HERE))
import lens_eeprom  # noqa: E402

MD = REPO / "output/peripherals-20261106/EEPROM_AND_REGISTERS.md"
SHA = {
    "front": "d651b1f527eccdcebb54f51ddc56a0be3eba9570881e8d19a8fc4f6f4a75e1a2",
    "rear": "a4491590fce1bfba12c94a7180aacdc7e84d7085b4c4d77cbf7f69deaf72b097",
}
# From the stock JPEG output/360_2113.JPG (local): AWB record 0x2dac / 0x2db8
# (lens a, lens b), its four white points per lens, and the sha256 of
# MakerNote tag 0x0012.
JPEG_LENS_ADJUST = {"front": (67141, 65536, 61688), "rear": (66016, 65536, 61985)}
JPEG_WHITE_POINTS = {
    "front": [(2816, 4096, 1465), (1690, 4096, 2312), (2081, 4096, 1662), (3417, 4096, 1168)],
    "rear": [(2864, 4096, 1458), (1722, 4096, 2302), (2117, 4096, 1651), (3483, 4096, 1164)],
}
JPEG_TAG_0012_SHA = "b4519cd3e9b51609f6dda5c7a5b46ae074d9d274d06d08a9551f3cb139465724"


def images():
    return lens_eeprom.rebuild_from_markdown(MD.read_text())


def test_rebuild_matches_survey_hashes():
    imgs = images()
    for k in ("front", "rear"):
        assert len(imgs[k]) == 8192
        assert hashlib.sha256(imgs[k]).hexdigest() == SHA[k]


def test_checksum_is_word_sum():
    for img in images().values():
        assert lens_eeprom.checksum(img) == lens_eeprom.stored_checksum(img)
        bad = bytearray(img)
        bad[0x40] ^= 1
        assert lens_eeprom.checksum(bytes(bad)) != lens_eeprom.stored_checksum(bytes(bad))


def test_field_table_is_contiguous():
    end = 0
    for off, size, _ in lens_eeprom.FIELDS:
        assert off >= end, hex(off)
        end = off + size
    assert end == 0x800
    names = [n for _, _, n in lens_eeprom.FIELDS]
    assert len(names) == len(set(names)) == 179


def test_named_fields():
    f = lens_eeprom.fields(images()["front"])
    assert f["CREATE_DATE"] == "20170527"
    assert f["SET_ID"] == "CFN000805E48254"
    assert f["MODULE_ID"] == "OVKER 0174000101"
    assert (f["LIVE_ISO_GAIN_X1"], f["LIVE_ISO_GAIN_X2"]) == (143, 174)
    assert f["NV_ISO"] == "P" and f["NV_AWB1"] == "N"
    assert "AWB_MODULE_POINT2_GAIN_R" not in f  # never written (0xff)


def test_awb_points_are_the_jpeg_white_points():
    for k, img in images().items():
        p = lens_eeprom.awb_points(img)
        got = [p["point1_low"], p["point2"], p["point3"], p["point4"]]
        assert got == JPEG_WHITE_POINTS[k]


def test_lens_adjust_reproduces_the_jpeg_record():
    for k, img in images().items():
        assert lens_eeprom.lens_adjust(img) == JPEG_LENS_ADJUST[k]


def test_optical_axis_is_makernote_0012():
    imgs = images()
    ax = lens_eeprom.optical_axis(imgs["front"])
    assert hashlib.sha256(bytes.fromhex(ax["makernote_0x0012"])).hexdigest() == JPEG_TAG_0012_SHA
    assert ax["version"] == 2 and ax["flag"] == 2
    assert abs(ax["values"][0] - 1446.105835) < 1e-6
    # Written to both modules alike.
    assert imgs["front"][0xD0:0x170] == imgs["rear"][0xD0:0x170]


def test_extract_table_from_synthetic_library():
    rows = [(0x85C20000, 0x000, 2, "EEPA_TABLE_INFO_VER"),
            (0x85C20000, 0x080, 40, "EEPA_MOT_PIMA"),
            (0x85C20000, 0x7FC, 4, "EEPA_CHECKSUM")]
    blob = bytearray(b"\x11" * 0x40)
    for region, off, size, name in rows:
        entry = bytearray(0x60)
        entry[:len(name)] = name.encode()
        struct.pack_into("<QI", entry, 0x50, region << 32 | off, size)
        blob += entry
    blob += b"\0" * 0x60
    assert lens_eeprom.extract_table(bytes(blob)) == rows


def test_fields_md_is_current():
    imgs = images()
    table = lens_eeprom.fields_markdown(imgs["front"], imgs["rear"])
    assert table.count("\n") == 2 + 179
    assert table in (HERE / "FIELDS.md").read_text()
