#!/usr/bin/env python3
"""Decode an `ipccrec record` file into one line per IPCC frame.

Frame layout (observer-trace/README.md): ff 7f | inner_len u16 | route[2] |
category u16 | attribute u16 | payload_len u16 | payload | trailer[2].
Ring A is RTOS->Linux, ring B is Linux->RTOS (checked 2 November 2026 from
live ring contents: `st cap` shell commands appear in ring B).

    python3 decode_ipcc.py <record-file>
"""

import struct
import sys

DIRECTION = {"A": "RTOS->Linux", "B": "Linux->RTOS"}


def frames(data: bytes):
    i = 0
    while i + 12 <= len(data):
        if data[i:i + 2] != b"\xff\x7f":
            yield ("junk", data[i:].hex())
            return
        inner = struct.unpack_from("<H", data, i + 2)[0]
        route = data[i + 4:i + 6].hex()
        category, attribute, plen = struct.unpack_from("<HHH", data, i + 6)
        payload = data[i + 12:i + 12 + plen]
        yield ("frame", inner, route, category, attribute, payload)
        i += 4 + inner


def describe(payload: bytes) -> str:
    if len(payload) % 4 == 0 and payload:
        words = struct.unpack("<%dI" % (len(payload) // 4), payload)
        return " ".join(f"0x{w:08x}" for w in words)
    text = payload.decode("ascii", "replace")
    return payload.hex() + (f'  "{text}"' if payload and all(32 <= b < 127 or b == 0 for b in payload) else "")


def main() -> int:
    for line in open(sys.argv[1], encoding="ascii", errors="replace"):
        parts = line.split()
        if len(parts) != 6 or not parts[0].isdigit():
            print("#", line.rstrip())
            continue
        t_us, ring, _, _, _, hexbytes = parts
        if hexbytes == "BADCURSOR":
            print(f"{int(t_us)/1e6:9.4f} {ring} BADCURSOR")
            continue
        for item in frames(bytes.fromhex(hexbytes)):
            if item[0] == "junk":
                print(f"{int(t_us)/1e6:9.4f} {ring} {DIRECTION.get(ring, '?'):12} junk {item[1]}")
                continue
            _, inner, route, category, attribute, payload = item
            print(f"{int(t_us)/1e6:9.4f} {ring} {DIRECTION.get(ring, '?'):12} route {route} "
                  f"cat 0x{category:04x} attr 0x{attribute:04x} len {len(payload):3} {describe(payload)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
