#!/usr/bin/env python3
"""Decode retained observer logs offline, without assigning buffer semantics.

Derived from MiniMax's decode_lease.py and decode_structures.py at a18b965.
This module reads local text only. A parsed record is not a coherent snapshot,
an owned result, a lease, or evidence that any field contains sensor pixels.
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path


FRAME = re.compile(
    r"^T\s+(\d+)\s+(?:ring\s+)?([A-D])\s+W\s+(\d+)\s+R\s+(\d+)"
    r"\s+D\s+(\d+)\s+L\s+(\d+)\s+B(\S*)$"
)
OBJECT = re.compile(r"^OBJECT\s+(\S+)\s+len\s+(\d+)$")
FOLLOW = re.compile(r"^FOLLOW\s+(\d+)\s+->\s+(\S+)$")


def number(token):
    """The helper prints addresses in decimal; only 0x selects hexadecimal."""
    value = int(token, 16 if token.lower().startswith("0x") else 10)
    if not 0 <= value <= 0xffffffff:
        raise ValueError("address is outside u32")
    return value


def words(data):
    return [item[0] for item in struct.iter_unpack("<I", data[:len(data) // 4 * 4])]


def decode_frames(text):
    for line_number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        match = FRAME.fullmatch(line)
        if not match:
            # Initial cursor/status lines contain no recorded frame.
            if line.startswith("T ") and re.search(r"\sL\s+\d+\s+B", line):
                raise ValueError(f"line {line_number}: malformed frame record")
            continue
        tick, ring, write, read, delta, inner, encoded = match.groups()
        inner = int(inner)
        try:
            data = bytes.fromhex(encoded)
        except ValueError as error:
            raise ValueError(f"line {line_number}: invalid hex") from error
        if inner < 10 or len(data) < inner:
            raise ValueError(f"line {line_number}: truncated or short inner frame")
        route, category, attribute, body_length = struct.unpack("<HHHH", data[:8])
        if inner != body_length + 10:
            raise ValueError(f"line {line_number}: inner/body length mismatch")
        body = data[8:8 + body_length]
        yield {
            "line": line_number, "tick": int(tick), "ring": ring,
            "write": int(write), "read": int(read), "delta": int(delta),
            "inner_length": inner, "route": route, "category": category,
            "attribute": attribute, "body_hex": body.hex(),
            "body_words_le": words(body), "trailer_hex": data[inner - 2:inner].hex(),
            "extra_printed_bytes": len(data) - inner,
        }


def decode_objects(text):
    lines = text.splitlines()
    index = 0
    while index < len(lines):
        line_number = index + 1
        line = lines[index].strip()
        index += 1
        follow = FOLLOW.fullmatch(line)
        if follow:
            yield {"line": line_number, "kind": "follow",
                   "source_word": int(follow[1]), "address": number(follow[2])}
            continue
        match = OBJECT.fullmatch(line)
        if not match:
            continue
        address = number(match[1])
        length = int(match[2])
        if not 1 <= length <= 4096:
            raise ValueError(f"line {line_number}: object length outside helper range")
        rows = []
        while index < len(lines) and re.fullmatch(r"[0-9a-fA-F]+", lines[index]):
            rows.append(lines[index])
            index += 1
        try:
            data = bytes.fromhex("".join(rows))
        except ValueError as error:
            raise ValueError(f"line {line_number}: invalid object hex") from error
        if len(data) != length:
            raise ValueError(f"line {line_number}: declared/recorded object length mismatch")
        yield {"line": line_number, "kind": "object", "address": address,
               "length": length, "words_le": words(data),
               "tail_hex": data[len(data) // 4 * 4:].hex()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("frames", "objects"))
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args(argv)
    decode = decode_frames if args.mode == "frames" else decode_objects
    try:
        for path in args.files:
            for record in decode(path.read_text()):
                print(json.dumps(dict(source=str(path), **record)))
    except (OSError, ValueError) as error:
        print(f"decode failed: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
