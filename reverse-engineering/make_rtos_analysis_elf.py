#!/usr/bin/env python3
"""Make a partial ELF analysis carrier from exact SM-R210 RTOS byte ranges.

The ELF metadata and function names are analyst-created. No code is changed,
executed, or uploaded. Virtual addresses use the independently established
0x80000000 RTOS link base. Custom read-only data ranges may specify a
virtual_address for a separately established startup-copy destination; this
does not observe live RAM. The manifest binds every segment to the source.
"""

import argparse
import hashlib
import json
import struct
from pathlib import Path

BASE = 0x80000000
RANGES = [
    (".text.liveview", 0x15C000, 0x160000, 5),
    (".text.sensor", 0x240000, 0x248000, 5),
    (".text.image", 0x279000, 0x27D000, 5),
    (".text.pref", 0x526000, 0x527000, 5),
    (".rodata", 0x5E0000, 0x680000, 4),
]
SYMBOLS = [
    ("analyst_CLiveviewImageCallback_Publish", 0x15E424, 0x474, 1),
    ("analyst_SensorDriver_log_method", 0x242018, 0, 2),
    ("analyst_CCapImageUtility_imageInfoConvert", 0x27A7B0, 0, 3),
    ("analyst_unresolved_image_event_handler", 0x27BC00, 0xAC, 3),
    ("analyst_pref_rtos_set_bytes", 0x52643C, 0x64, 4),
]
EXTRA_RANGES = [
    (".text.rawmanager", 0x13B000, 0x13E000, 5),
    (".text.sensorbus", 0x30E000, 0x30F000, 5),
    (".text.rawend", 0x32B000, 0x32D000, 5),
    (".text.lowbus", 0x34000, 0x36000, 5),
    (".text.imagegetters", 0x541000, 0x542000, 5),
    (".text.buffergetters", 0x560000, 0x565000, 5),
    (".rodata.raw", 0x6B0000, 0x6B2000, 4),
]
EXTRA_SYMBOLS = [
    ("analyst_SetMainRawImage", 0x13B098, 0xB4),
    ("analyst_SetMainYccImage", 0x13B4B0, 0),
    ("analyst_sensor_register_write", 0x243A04, 0),
    ("analyst_sensor_register_write_helper", 0x243928, 0x64),
    ("analyst_sensor_bus_serialize", 0x30E61C, 0xDC),
    ("analyst_low_level_bus_request", 0x35224, 0),
    ("analyst_Raw_End", 0x32BA2C, 0xA8),
    ("analyst_Ipc_End_raw_store_candidate", 0x32BE1C, 0xFC),
    ("analyst_Raw_End_ExecptDelImg", 0x32C140, 0),
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def align(buf, boundary):
    buf.extend(b"\0" * (-len(buf) % boundary))


def build(source, destination, expanded=False, legacy_labels=False, config=None):
    raw = source.read_bytes()
    expected = "cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7"
    if sha(raw) != expected:
        raise ValueError("Source digest differs from the captured R210 RTOS image")
    ranges = RANGES + (EXTRA_RANGES if expanded else [])
    mapped_addresses = {}
    symbols = [(n, a, s) for n, a, s, _ in SYMBOLS]
    if expanded:
        symbols += EXTRA_SYMBOLS
        if legacy_labels:
            # Replay the first analysis carrier, whose two provisional labels
            # were corrected after inspecting log-string references/getters.
            symbols = [("analyst_MainRaw_type_check" if a == 0x13B4B0 else
                        "analyst_Raw_End_ExecptDelImg" if a == 0x32BE1C else n, a, s)
                       for n, a, s in symbols if a != 0x32C140]
    if config is not None:
        def number(value):
            return int(value, 0) if isinstance(value, str) else int(value)
        ranges = [(r['name'], number(r['start']), number(r['end']), number(r.get('flags', 5)))
                  for r in config['ranges']]
        symbols = [(s['name'], number(s['offset']), number(s.get('size', 0)))
                   for s in config['symbols']]
        if not ranges or not symbols:
            raise ValueError('Custom view needs ranges and at least one entry symbol')
        for i, (_, start, end, flags) in enumerate(ranges):
            if start < 0 or start % 0x1000 or not start < end <= len(raw) or flags not in (4, 5):
                raise ValueError('Custom ranges must be bounded, page-aligned, and read-only')
            if any(start < other_end and other_start < end for _, other_start, other_end, _ in ranges[:i]):
                raise ValueError('Custom ranges overlap')
            mapping = config['ranges'][i].get('virtual_address')
            if mapping is not None:
                address = number(mapping)
                if flags != 4 or address % 4 or not 0 <= address < address + end - start <= 0x100000000:
                    raise ValueError('Explicit mappings must be aligned read-only data within uint32')
                mapped_addresses[start] = address
        virtual_ranges = [(mapped_addresses.get(a, BASE + a), b - a) for _, a, b, _ in ranges]
        for i, (address, length) in enumerate(virtual_ranges):
            if any(address < other + size and other < address + length
                   for other, size in virtual_ranges[:i]):
                raise ValueError('Custom virtual ranges overlap')
        for _, address, size in symbols:
            if size < 0 or not any(a <= address < b and address + size <= b for _, a, b, _ in ranges):
                raise ValueError('Custom symbol lies outside selected ranges')
    carrier = bytearray(52 + 32 * len(ranges))
    def mapped_symbol(offset):
        _, start, _, _ = next(r for r in ranges if r[1] <= offset < r[2])
        return mapped_addresses.get(start, BASE + start) + offset - start
    phdrs = []
    sections = [("", 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    segment_manifest = []
    for name, start, end, flags in ranges:
        align(carrier, 0x1000)
        offset = len(carrier)
        payload = raw[start:end]
        if len(payload) != end - start:
            raise ValueError("Source too short")
        carrier.extend(payload)
        virtual_address = mapped_addresses.get(start, BASE + start)
        segment_alignment = 0x1000 if offset % 0x1000 == virtual_address % 0x1000 else 4
        phdrs.append((1, offset, virtual_address, virtual_address,
                      len(payload), len(payload), flags, segment_alignment))
        shflags = 2 | (4 if flags & 1 else 0)
        sections.append((name, 1, shflags, virtual_address, offset, len(payload), 0, 0, 4, 0))
        segment_manifest.append({"section": name, "source_start": hex(start),
                                 "source_end_exclusive": hex(end),
                                 "virtual_address": hex(virtual_address),
                                 "mapping": "explicit-config" if start in mapped_addresses else "link-base",
                                 "carrier_offset": hex(offset), "sha256": sha(payload)})
    strings = bytearray(b"\0")
    symtab = bytearray(16)
    for name, offset, size in symbols:
        section = next(i + 1 for i, (_, a, b, _) in enumerate(ranges) if a <= offset < b)
        name_offset = len(strings)
        strings.extend(name.encode() + b"\0")
        symtab.extend(struct.pack("<IIIBBH", name_offset, mapped_symbol(offset), size, 0x12, 0, section))
    align(carrier, 4)
    sym_offset = len(carrier)
    carrier.extend(symtab)
    sym_section = len(sections)
    str_section = sym_section + 1
    sections.append((".symtab", 2, 0, 0, sym_offset, len(symtab), str_section, 1, 4, 16))
    str_offset = len(carrier)
    carrier.extend(strings)
    sections.append((".strtab", 3, 0, 0, str_offset, len(strings), 0, 0, 1, 0))
    shnames = bytearray(b"\0")
    name_offsets = {"": 0}
    for name in [s[0] for s in sections[1:]] + [".shstrtab"]:
        name_offsets[name] = len(shnames)
        shnames.extend(name.encode() + b"\0")
    shstr_offset = len(carrier)
    carrier.extend(shnames)
    shstr_section = len(sections)
    sections.append((".shstrtab", 3, 0, 0, shstr_offset, len(shnames), 0, 0, 1, 0))
    align(carrier, 4)
    shoff = len(carrier)
    for name, *values in sections:
        carrier.extend(struct.pack("<10I", name_offsets[name], *values))
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    carrier[:52] = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, 40, 1,
                               mapped_symbol(symbols[0][1]), 52, shoff, 0x05000000,
                               52, 32, len(phdrs), 40, len(sections), shstr_section)
    for i, phdr in enumerate(phdrs):
        carrier[52 + 32 * i:84 + 32 * i] = struct.pack("<8I", *phdr)
    for _, start, end, _ in ranges:
        section = next(s for s in sections if s[3] == mapped_addresses.get(start, BASE + start))
        assert bytes(carrier[section[4]:section[4] + section[5]]) == raw[start:end]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(carrier)
    manifest = {"source": str(source.resolve()), "source_sha256": sha(raw),
                "carrier": str(destination.resolve()), "carrier_sha256": sha(carrier),
                "load_base": hex(BASE), "segments": segment_manifest,
                "symbols": [{"name": n, "address": hex(mapped_symbol(a)), "size": s}
                            for n, a, s in symbols],
                "limitations": ["Partial derived analysis carrier; not shipped ELF",
                                "All ELF headers and symbols are analyst-created",
                                "Code outside selected ranges and runtime state are absent",
                                "Zero symbol size means bounds are not asserted"]}
    destination.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"carrier": str(destination), "sha256": sha(carrier),
                      "bytes": len(carrier), "source_bytes_verified": True}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--expanded", action="store_true", help="Include sensor bus and RAW handoff")
    parser.add_argument("--legacy-labels", action="store_true", help="Reproduce initial provisional symbol labels")
    parser.add_argument("--config", type=Path, help="JSON with source-offset ranges and analyst symbols")
    args = parser.parse_args()
    config = json.loads(args.config.read_text()) if args.config else None
    build(args.source, args.destination, args.expanded, args.legacy_labels, config)
