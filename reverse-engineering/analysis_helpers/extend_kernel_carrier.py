"""Append exact original pages and neutral symbols to a sparse ARM analysis ELF.

Offline carrier construction only: no instruction decoding or target execution.
The JSON specification pins input hashes, original VA base, pages and symbols.
All existing and appended PT_LOAD bytes must match the original flat image.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path


def extend(spec):
    original = Path(spec["original"]).read_bytes()
    source = Path(spec["carrier"]).read_bytes()
    assert hashlib.sha256(original).hexdigest() == spec["original_sha256"]
    assert hashlib.sha256(source).hexdigest() == spec["carrier_sha256"]
    header = list(struct.unpack_from("<16sHHIIIIIHHHHHH", source))
    assert header[0][:6] == b"\x7fELF\x01\x01" and header[2] == 40
    assert header[9] == 32 and header[11] == 40
    programs = [list(struct.unpack_from("<8I", source, header[5] + i * 32))
                for i in range(header[10])]
    sections = [list(struct.unpack_from("<10I", source, header[6] + i * 40))
                for i in range(header[12])]
    assert header[5] + (len(programs) + len(spec["pages"])) * 32 <= min(
        p[1] for p in programs if p[0] == 1)
    data = bytearray(source)

    def append(payload, alignment=4):
        data.extend(b"\0" * (-len(data) % alignment))
        offset = len(data)
        data.extend(payload)
        return offset

    names_section = sections[header[13]]
    names = bytearray(source[names_section[4]:names_section[4] + names_section[5]])
    added = {}
    for page in spec["pages"]:
        address, length = page["address"], page["length"]
        assert address % 4096 == 0 and length > 0 and length % 4096 == 0
        assert all(not (address < p[2] + p[5] and p[2] < address + length)
                   for p in programs if p[0] == 1)
        offset = address - spec["base"]
        assert 0 <= offset <= len(original) - length
        payload = original[offset:offset + length]
        position = append(payload, 4096)
        programs.append([1, position, address, address, length, length,
                         page["flags"], 4096])
        name_offset = len(names)
        names.extend(page["name"].encode() + b"\0")
        added[page["name"]] = len(sections)
        sections.append([name_offset, 1, page["section_flags"], address,
                         position, length, 0, 0, 4, 0])
    symbol_section = next(s for s in sections if s[1] == 2)
    assert symbol_section[9] == 16
    string_section = sections[symbol_section[6]]
    strings = bytearray(source[string_section[4]:string_section[4] + string_section[5]])
    symbols = bytearray(source[symbol_section[4]:symbol_section[4] + symbol_section[5]])
    for symbol in spec["symbols"]:
        section_index = added[symbol["section"]]
        section = sections[section_index]
        assert section[3] <= symbol["address"] < section[3] + section[5]
        name_offset = len(strings)
        strings.extend(symbol["name"].encode() + b"\0")
        # Global symbol, size zero: metadata does not invent a body extent.
        symbols.extend(struct.pack("<IIIBBH", name_offset, symbol["address"],
                                   0, 0x10 | symbol["type"], 0, section_index))
    string_section[4], string_section[5] = append(strings), len(strings)
    symbol_section[4], symbol_section[5] = append(symbols), len(symbols)
    names_section[4], names_section[5] = append(names), len(names)
    header[6] = append(b"".join(struct.pack("<10I", *s) for s in sections))
    header[10], header[12] = len(programs), len(sections)
    struct.pack_into("<16sHHIIIIIHHHHHH", data, 0, *header)
    for i, program in enumerate(programs):
        struct.pack_into("<8I", data, header[5] + i * 32, *program)
        if program[0] == 1:
            offset = program[2] - spec["base"]
            assert 0 <= offset <= len(original) - program[4]
            assert data[program[1]:program[1] + program[4]] == original[offset:offset + program[4]]
    output = Path(spec["output"])
    with output.open("xb") as handle:
        handle.write(data)
    return {"output": str(output), "sha256": hashlib.sha256(data).hexdigest(),
            "load_segments": sum(p[0] == 1 for p in programs),
            "original_sha256": spec["original_sha256"],
            "all_file_payloads_match_original": True}


if __name__ == "__main__":
    print(json.dumps(extend(json.loads(Path(sys.argv[1]).read_text())), indent=2))
