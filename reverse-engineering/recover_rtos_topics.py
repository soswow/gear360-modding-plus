#!/usr/bin/env python3
"""Recover exact numeric/name bindings from a bounded RTOS ARM switch.

This evaluates only the formatter's comparisons and branches, never firmware
execution, IO, or callbacks. Unknown instruction forms fail closed. Each
result retains the source digest and original instruction-word path.
"""

import argparse
import hashlib
import json
import struct
from pathlib import Path

BASE = 0x80000000
DIGEST = "cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7"
DEFAULT_TOPICS = [0x02010001, 0x02010002, 0x02030001, 0x02030002, 0x02030003, 0x03010002]


def ror32(value, shift):
    return ((value >> shift) | (value << (32 - shift))) & 0xFFFFFFFF if shift else value


def recover(raw, topic):
    registers = {2: 0, 3: topic}
    pc, zero, carry = 0x6F170, False, False
    trace = []
    for _ in range(250):
        word = struct.unpack_from("<I", raw, pc)[0]
        condition = word >> 28
        if condition not in (0, 1, 3, 8, 14):
            raise ValueError(f"Unsupported condition {condition} at {pc:#x}")
        take = {0: zero, 1: not zero, 3: not carry, 8: carry and not zero, 14: True}[condition]
        trace.append({"file_offset": hex(pc), "word": hex(word),
                      "condition": condition, "condition_passed": take})
        if not take:
            pc += 4
            continue
        if word & 0x0F000000 == 0x0A000000:
            immediate = word & 0xFFFFFF
            if immediate & 0x800000:
                immediate -= 1 << 24
            pc += 8 + immediate * 4
        elif word & 0x0FF00000 in (0x03000000, 0x03400000):
            register = (word >> 12) & 15
            immediate = ((word >> 4) & 0xF000) | (word & 0xFFF)
            registers[register] = (immediate if word & 0x0FF00000 == 0x03000000 else
                                   (registers[register] & 0xFFFF) | (immediate << 16))
            pc += 4
        elif word & 0x0FFF0000 == 0x03A00000:
            registers[(word >> 12) & 15] = ror32(word & 255, ((word >> 8) & 15) * 2)
            pc += 4
        elif word & 0x0FF00FF0 == 0x01500000:
            left, right = registers[(word >> 16) & 15], registers[word & 15]
            zero, carry = left == right, left >= right
            pc += 4
        elif word & 0x0FF00000 == 0x03500000:
            left = registers[(word >> 16) & 15]
            right = ror32(word & 255, ((word >> 8) & 15) * 2)
            zero, carry = left == right, left >= right
            pc += 4
        elif pc == 0x6FF88 and word == 0xE1A00003:
            offset = registers[3] - BASE
            if not 0 <= offset < len(raw):
                raise ValueError("Returned string address lies outside captured image")
            end = raw.find(b"\0", offset, offset + 256)
            if end == -1:
                raise ValueError("Returned string is not bounded")
            return {"topic": hex(topic), "name": raw[offset:end].decode("ascii"),
                    "string_file_offset": hex(offset), "string_address": hex(registers[3]),
                    "path": trace}
        else:
            raise ValueError(f"Unsupported instruction {word:#x} at {pc:#x}")
    raise ValueError("Formatter step limit exceeded")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("topics", nargs="*", type=lambda text: int(text, 0))
    args = parser.parse_args()
    raw = args.source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != DIGEST:
        raise ValueError("Source digest differs from captured R210 RTOS")
    topics = args.topics or DEFAULT_TOPICS
    if any(not 0 <= topic <= 0xFFFFFFFF for topic in topics):
        raise ValueError("Topic must fit uint32")
    result = {"source": str(args.source.resolve()), "source_sha256": DIGEST,
              "load_base": hex(BASE), "coverage": "selected formatter paths only",
              "limitations": ["Does not prove event production or delivery",
                              "Load base established separately from boot and absolute pointers"],
              "bindings": [recover(raw, topic) for topic in topics]}
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
