"""Host test for stock_iq.py: a synthetic stock JPEG with known records."""
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import stock_iq  # noqa: E402


def awb_record() -> bytes:
    b = bytearray(13000)
    b[0:4] = b"AWB\0"
    struct.pack_into("<H", b, 4, 0x105)
    struct.pack_into("<3i", b, 0x2DAC, 67141, 65536, 61688)
    struct.pack_into("<3i", b, 0x2DB8, 66016, 65536, 61985)
    struct.pack_into("<9i", b, 0x2E14, 231, -95, -8, -27, 186, -31, 2, -76, 202)
    struct.pack_into("<9i", b, 0x2E38, 231, -95, -8, -27, 186, -31, 2, -76, 202)
    for flags, a, bb, va, vb in ((0x2E5C, 0x2E60, 0x2E6C, (2816, 4096, 1465), (2864, 4096, 1458)),
                                 (0x2EC4, 0x2EC8, 0x2ED4, (1690, 4096, 2312), (1722, 4096, 2302)),
                                 (0x2EF8, 0x2EFC, 0x2F08, (2081, 4096, 1662), (2117, 4096, 1651)),
                                 (0x2FA4, 0x2FA8, 0x2FB4, (3417, 4096, 1168), (3483, 4096, 1164))):
        b[flags:flags + 4] = bytes((1, 1, 0, 0))
        struct.pack_into("<3i", b, a, *va)
        struct.pack_into("<3i", b, bb, *vb)
    struct.pack_into("<3i", b, 0x2EB0, 115430, 65536, 138595)
    struct.pack_into("<3i", b, 0x2F2C, 113393, 65536, 139874)
    struct.pack_into("<3i", b, 0x2F38, 65536, 65536, 65536)
    struct.pack_into("<3i", b, 0x2F44, 64447, 65536, 65880)
    return bytes(v ^ 0xC8 for v in b)


def ipc_record() -> bytes:
    b = bytearray(2048)
    b[0:4] = b"I!?C"
    struct.pack_into("<9H", b, 0xAE, 418, 555, 256, 425, 552, 256, 418, 555, 256)
    xs = [0, 16, 37, 70, 116, 182, 275, 405, 584, 832, 1182, 1694, 2524, 3966, 6218, 9241, 12545, 15872, 16383]
    ys = [0, 8, 21, 45, 80, 128, 194, 269, 355, 452, 556, 657, 748, 828, 895, 953, 998, 1022, 1023]
    struct.pack_into("<19i", b, 0x174, *xs)
    struct.pack_into("<19i", b, 0x1C0, *ys)
    sat = (128, 199, -40, 128, -59, -63, 128, -30, 170)
    struct.pack_into("<9h", b, 0x218, *sat)
    struct.pack_into("<9h", b, 0x22A, *sat)
    struct.pack_into("<9h", b, 0x23C, 77, 150, 29, -49, -97, 146, 146, -122, -24)
    return bytes(b)


def ae_record() -> bytes:
    b = bytearray(6000)
    struct.pack_into("<I", b, 0, 0xAAFFAAFF)
    struct.pack_into("<9h", b, 0xD1C, 231, -95, -8, -27, 186, -31, 2, -76, 202)
    return bytes(b)


def jpeg(makernote_entries) -> bytes:
    """Big-endian Exif like the camera's, MakerNote offsets relative to the
    MakerNote start."""
    e = ">"
    # MakerNote IFD
    entries = sorted(makernote_entries.items())
    n = len(entries)
    head = 2 + 12 * n + 4
    blob = bytearray()
    ifd = bytearray(struct.pack(e + "H", n))
    for tag, value in entries:
        if len(value) <= 4:
            ifd += struct.pack(e + "HHI", tag, 7, len(value)) + value.ljust(4, b"\0")
        else:
            ifd += struct.pack(e + "HHII", tag, 7, len(value), head + len(blob))
            blob += value
    ifd += b"\0\0\0\0"
    mn = bytes(ifd + blob)
    # TIFF: header, IFD0 (one entry -> Exif IFD), Exif IFD (one entry -> MakerNote)
    ifd0_off = 8
    exif_off = ifd0_off + 2 + 12 + 4
    mn_off = exif_off + 2 + 12 + 4
    tiff = bytearray(b"MM\0\x2a" + struct.pack(e + "I", ifd0_off))
    tiff += struct.pack(e + "H", 1) + struct.pack(e + "HHII", 0x8769, 4, 1, exif_off) + b"\0\0\0\0"
    tiff += struct.pack(e + "H", 1) + struct.pack(e + "HHII", 0x927C, 7, len(mn), mn_off) + b"\0\0\0\0"
    tiff += mn
    app1 = b"Exif\0\0" + bytes(tiff)
    return b"\xff\xd8" + b"\xff\xe1" + struct.pack(">H", len(app1) + 2) + app1 + b"\xff\xd9"


def close(a, b, tol=1e-4):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def main() -> None:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp")
    data = jpeg({0x0001: b"0100", 0x00A0: awb_record(), 0x00A1: awb_record(),
                 0x00C0: ipc_record(), 0x0060: ae_record()})
    path = work / "synthetic.jpg"
    path.write_bytes(data)
    rec = stock_iq.read(str(path))
    a = rec["awb"]
    assert a["version"] == 0x105 and rec["awb_copies_equal"]
    assert close(a["gains_2eb0"], [115430 / 65536, 1.0, 138595 / 65536])
    assert close(a["lens_adjust_a"], [67141 / 65536, 1.0, 61688 / 65536])
    assert close(a["white_points"][1]["unit_a"], [1690 / 4096, 1.0, 2312 / 4096])
    assert a["white_points"][3]["valid"] == [1, 1]
    assert close(a["ccm"][0], [231 / 128, -95 / 128, -8 / 128]) and a["ccm_copy_equal"]
    for row in a["ccm"]:
        assert abs(sum(row) - 1.0) < 1e-9
    p = rec["ipc"]
    assert p["lens_gain_triples"] == [[418, 555, 256], [425, 552, 256], [418, 555, 256]]
    pts = p["tone_curve"]["points"]
    assert len(pts) == 19 and pts[0] == (0, 0) and pts[-1] == (16383, 1023)
    assert p["saturation_copy_equal"] and close(p["rgb_to_ycc"][0], [77 / 256, 150 / 256, 29 / 256])
    assert rec["ae_ccm"] == a["ccm"]
    # CLI: summary and JSON.
    cli = [sys.executable, str(HERE / "stock_iq.py")]
    r = subprocess.run(cli + [str(path)], capture_output=True, text=True)
    assert r.returncode == 0 and "AWB gains (2eb0) [1.7613, 1.0000, 2.1148]" in r.stdout, r.stdout + r.stderr
    r = subprocess.run(cli + [str(path), "--json"], capture_output=True, text=True)
    assert r.returncode == 0 and '"tone_curve"' in r.stdout
    # The camera's maker-note buffer as g360-shot saves it: the IQ block's
    # records at the RTOS offsets (AWB 0x14, AE 0x32dc, colour 0x4a4c).
    buf = bytearray(0x10000)
    buf[0x14:0x14 + 13000] = awb_record()
    buf[0x32DC:0x32DC + 6000] = ae_record()
    buf[0x4A4C:0x4A4C + 2048] = ipc_record()
    (work / "shot-001-makernote.bin").write_bytes(bytes(buf))
    rec2 = stock_iq.read(str(work / "shot-001-makernote.bin"))
    assert rec2["awb"] == a and rec2["ipc"] == p and rec2["ae_ccm"] == rec["ae_ccm"]
    (work / "short.bin").write_bytes(bytes(0x5000))
    try:
        stock_iq.read(str(work / "short.bin"))
        raise AssertionError("short buffer accepted")
    except stock_iq.NotStockJpeg:
        pass
    # Refusals: wrong mask, missing MakerNote, not a JPEG.
    bad = bytearray(awb_record())
    bad[0] ^= 0xFF
    try:
        stock_iq.decode_awb(bytes(bad))
        raise AssertionError("bad AWB signature accepted")
    except stock_iq.NotStockJpeg:
        pass
    (work / "plain.jpg").write_bytes(b"\xff\xd8\xff\xd9")
    (work / "text.jpg").write_bytes(b"hello")
    r = subprocess.run(cli + [str(work / "plain.jpg"), str(work / "text.jpg")], capture_output=True, text=True)
    # A file that is not a JPEG is read as a maker-note buffer.
    assert r.returncode == 1 and "no Exif segment" in r.stderr and "shorter than the IQ block" in r.stderr, r.stderr
    print("stock_iq tests passed")


if __name__ == "__main__":
    main()
