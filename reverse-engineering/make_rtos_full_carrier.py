#!/usr/bin/env python3
"""Build a whole-image ELF analysis carrier for the SM-R210 RTOS.

The carrier maps the code/rodata part of rtos-p6.bin at the 0x80000000 link base
and the startup-copied data bank at its established destination 0x81006000
(source 0x807ef900..0x80c31840 -> 0x81006000..0x81447f40). A zero-filled
segment after it gives BSS globals an address so that decompiler references
resolve. ELF headers are analyst-created; the payload bytes are unchanged.
The carrier is for offline disassembly only and is never executed.
"""

import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_SHA256 = "cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7"
LINK_BASE = 0x80000000
COPY_SOURCE_START = 0x7EF900
COPY_SOURCE_END = 0xC31840
COPY_DESTINATION = 0x81006000
BSS_END = 0x81E00000

PF_X, PF_W, PF_R = 1, 2, 4


def build(source: Path, destination: Path) -> dict:
    raw = source.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"unexpected RTOS digest {digest}")

    code = raw[:COPY_SOURCE_START]
    data = raw[COPY_SOURCE_START:COPY_SOURCE_END]
    data_end = COPY_DESTINATION + len(data)
    bss_size = BSS_END - data_end

    header_size = 52
    program_header_size = 32
    program_header_count = 2
    payload_offset = 0x1000
    code_offset = payload_offset
    data_offset = code_offset + len(code)
    data_offset += -data_offset % 0x1000

    segments = [
        # (offset, vaddr, filesz, memsz, flags)
        (code_offset, LINK_BASE, len(code), len(code), PF_R | PF_X),
        (data_offset, COPY_DESTINATION, len(data), len(data) + bss_size, PF_R | PF_W),
    ]

    elf = bytearray()
    elf += b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\0" * 8
    # ET_EXEC, EM_ARM, version 1, entry from the boot jump literal, EABI5 flags.
    elf += struct.pack(
        "<HHIIIIIHHHHHH",
        2, 40, 1, 0x80000E38, header_size, 0, 0x05000000,
        header_size, program_header_size, program_header_count, 0, 0, 0,
    )
    for offset, vaddr, filesz, memsz, flags in segments:
        elf += struct.pack("<IIIIIIII", 1, offset, vaddr, vaddr, filesz, memsz, flags, 0x1000)
    elf += b"\0" * (code_offset - len(elf))
    elf += code
    elf += b"\0" * (data_offset - len(elf))
    elf += data
    destination.write_bytes(elf)

    return {
        "source": str(source),
        "source_sha256": digest,
        "carrier": str(destination),
        "carrier_sha256": hashlib.sha256(elf).hexdigest(),
        "segments": [
            {
                "source_range": [0, COPY_SOURCE_START],
                "virtual_address": hex(LINK_BASE),
                "flags": "RX",
                "sha256": hashlib.sha256(code).hexdigest(),
            },
            {
                "source_range": [COPY_SOURCE_START, COPY_SOURCE_END],
                "virtual_address": hex(COPY_DESTINATION),
                "flags": "RW",
                "zero_fill_to": hex(BSS_END),
                "sha256": hashlib.sha256(data).hexdigest(),
            },
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--manifest", type=Path)
    arguments = parser.parse_args()
    manifest = build(arguments.source, arguments.destination)
    text = json.dumps(manifest, indent=2)
    if arguments.manifest:
        arguments.manifest.write_text(text + "\n")
    print(text)


if __name__ == "__main__":
    main()
