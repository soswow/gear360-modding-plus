"""Host test for make_dng.py: a zero frame plus a synthetic maker-note buffer."""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "tests"))
import make_dng  # noqa: E402
import stock_iq  # noqa: E402
from test_stock_iq import awb_record, ae_record, ipc_record  # noqa: E402

buf = bytearray(0x10000)
buf[0x14:0x14 + 13000] = awb_record()
buf[0x32DC:0x32DC + 6000] = ae_record()
buf[0x4A4C:0x4A4C + 2048] = ipc_record()
rec = {"awb": stock_iq.decode_awb(bytes(buf[0x14:0x14 + 13000]))}
frame = bytes(range(256)) * (make_dng.STRIDE * make_dng.HEIGHT // 256)
dng = make_dng.build(frame, rec)
tags = {t: (typ, n, v) for t, typ, n, v, _ in stock_iq._ifd(dng, struct.unpack("<I", dng[4:8])[0], "<")}
assert dng[:4] == b"II*\0"
strip = struct.unpack("<I", tags[273][2])[0]
assert dng[strip:] == frame and strip % 4 == 0
assert struct.unpack("<I", tags[256][2])[0] == 5824 and struct.unpack("<I", tags[257][2])[0] == 2896
assert tags[33422][2] == bytes([1, 0, 2, 1]) and struct.unpack("<H", tags[262][2])[0] == 32803
assert struct.unpack("<I", tags[50714][2])[0] == 4106 and struct.unpack("<I", tags[50717][2])[0] == 65472
neutral = [n / d for n, d in struct.iter_unpack("<II", tags[50728][2])]
gains = rec["awb"]["gains_2eb0"]
assert all(abs(neutral[k] * gains[k] - 1) < 1e-5 for k in range(3)), neutral
fm = [n / d for n, d in struct.iter_unpack("<ii", tags[50964][2])]
# ForwardMatrix maps camera white (1,1,1) to D50 white because CCM rows sum to 1.
assert abs(sum(fm[0:3]) - 0.9642) < 2e-3 and abs(sum(fm[3:6]) - 1.0) < 2e-3 and abs(sum(fm[6:9]) - 0.8252) < 2e-3, fm
try:
    make_dng.build(frame[:-2], rec)
    raise AssertionError("short frame accepted")
except ValueError:
    pass
print("make_dng tests passed")
