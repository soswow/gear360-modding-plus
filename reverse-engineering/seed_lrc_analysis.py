"""Add two analyst function symbols to a copy; preserve all loaded bytes."""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PASS = ROOT / "output/decompile/rea/pass-0f976d1-review-20261024"
SOURCE = ROOT / "output/decompile/rea/pass-minimax-followup-review-20261023/rtos-seeded-analysis.elf"
ORIGINAL = ROOT / "output/decompile/rea/rtos-p6.bin"
DEST = PASS / "rtos-lrc-analysis.elf"
EXPECTED = "8321a21193466694eac6d41d8bd499c4ba3f4061bb780a26e7af885a8632ae45"
EXPECTED_ORIGINAL = "cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align(data, boundary=4):
    data.extend(b"\0" * (-len(data) % boundary))


data = bytearray(SOURCE.read_bytes())
original = ORIGINAL.read_bytes()
assert digest(data) == EXPECTED and digest(original) == EXPECTED_ORIGINAL
assert data[:6] == b"\x7fELF\x01\x01"
phoff, shoff = struct.unpack_from("<II", data, 28)
phentsize, phnum, shentsize, shnum = struct.unpack_from("<HHHH", data, 42)
assert phentsize == 32 and shentsize == 40 and shnum == 6
sections = [list(struct.unpack_from("<10I", data, shoff + i * 40)) for i in range(shnum)]
strings = bytearray(data[sections[3][4]:sections[3][4] + sections[3][5]])
symbols = bytearray(data[sections[4][4]:sections[4][4] + sections[4][5]])
assert sections[3][1] == 3 and sections[4][1] == 2
seeds = {"analysis_material_prepare": 0x80251A28,
         "analysis_material_execute": 0x80251A70}
for name, address in seeds.items():
    name_index = len(strings)
    strings.extend(name.encode("ascii") + b"\0")
    symbols.extend(struct.pack("<IIIBBH", name_index, address, 0, 0x12, 0, 1))
align(data)
sections[3][4:6] = [len(data), len(strings)]
data.extend(strings)
align(data)
sections[4][4:6] = [len(data), len(symbols)]
data.extend(symbols)
align(data)
new_shoff = len(data)
for section in sections:
    data.extend(struct.pack("<10I", *section))
struct.pack_into("<I", data, 32, new_shoff)

checks = []
for i in range(phnum):
    kind, offset, va, _, size, memory_size, flags, _ = struct.unpack_from("<8I", data, phoff + i * 32)
    if kind != 1:
        continue
    source_offset = 0 if va == 0x80000000 else 0x7EF900
    before = SOURCE.read_bytes()[offset:offset + size]
    after = data[offset:offset + size]
    assert after == before == original[source_offset:source_offset + size]
    checks.append({"va": hex(va), "carrier_offset": offset, "source_offset": source_offset,
                   "size": size, "memory_size": memory_size, "flags": flags,
                   "sha256": digest(after), "original_bytes_equal": True})
assert len(checks) == 2
DEST.write_bytes(data)
(PASS / "lrc-carrier-manifest.json").write_text(json.dumps({
    "source": str(SOURCE), "source_sha256": EXPECTED,
    "original": str(ORIGINAL), "original_sha256": EXPECTED_ORIGINAL,
    "carrier": str(DEST), "carrier_sha256": digest(data),
    "analyst_symbols_only": seeds, "segments": checks,
    "meaning": "Analyst seeds identify function entry candidates; names are not native semantic evidence."
}, indent=2) + "\n")
print(digest(data), DEST)
