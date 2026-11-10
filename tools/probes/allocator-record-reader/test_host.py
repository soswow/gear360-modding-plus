#!/usr/bin/env python3
"""Offline contract tests for the allocator metadata-prefix helper."""
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CLANG = Path("/opt/homebrew/opt/llvm/bin/clang")
READELF = Path("/opt/homebrew/opt/llvm/bin/llvm-readelf")
SIZE = Path("/opt/homebrew/opt/llvm/bin/llvm-size")
OBJDUMP = Path("/opt/homebrew/opt/llvm/bin/llvm-objdump")
PREFIX_BYTES = 0x7000

def run(cmd):
    result = subprocess.run(cmd, text=True, capture_output=True)
    if result.returncode:
        raise SystemExit(f"command failed: {cmd}: {result.stderr}")
    return result

def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "host-test-output"
    out.mkdir(parents=True, exist_ok=True)
    host = out / "host-contract"
    run([str(CLANG), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O0", str(ROOT / "host_contract_test.c"), "-o", str(host)])
    result = run([str(host)])
    if "PASS:" not in result.stdout:
        raise SystemExit("host contract tests did not pass")

    source_names = ("allocator_records.c", "start.S", "build.sh", "test_host.py", "retrieve.py")
    source_snapshot = out / "source-snapshot"
    source_snapshot.mkdir(exist_ok=True)
    source_hashes = {}
    for name in source_names:
        source = ROOT / name
        shutil.copy2(source, source_snapshot / name)
        source_hashes[name] = hashlib.sha256(source.read_bytes()).hexdigest()
    (source_snapshot / "sha256.json").write_text(json.dumps(source_hashes, indent=2) + "\n")

    builds = []
    for name in ("build-a", "build-b"):
        build = out / name
        run(["sh", str(ROOT / "build.sh"), str(build)])
        elf = build / "allocator-record-reader.elf"
        blob = elf.read_bytes()
        if blob[:4] != b"\x7fELF" or struct.unpack_from("<H", blob, 18)[0] != 40:
            raise SystemExit("build is not an ARM ELF")
        if b"allocator-records" not in blob or b"/dev/mem" not in blob or b"UNVERIFIED_ALLOCATOR_RECORDS" not in blob:
            raise SystemExit("ELF is missing the mode, device, or diagnostic")
        for value in (0x81CBB000, 0x81C0D7E0, 0x81D230B0, 0x81952E50, 0x81952FA4,
                      0x82246000, 0x8067A158, 0xA6000000, 0xA77BB000, 0x7000, 28672):
            if struct.pack("<I", value) not in blob:
                raise SystemExit(f"ELF is missing fixed contract value {value:#x}")
        if struct.pack("<I", 0x20800340) in blob or struct.pack("<I", 0x20800024) in blob:
            raise SystemExit("ELF contains an MMIO address")
        disassembly = run([str(OBJDUMP), "-d", str(elf)]).stdout
        if "svc" not in disassembly:
            raise SystemExit("ELF has no syscall instruction")
        header = run([str(READELF), "-h", "-l", str(elf)]).stdout
        if "INTERP" in header:
            raise SystemExit("ELF has a dynamic interpreter")
        numbers = [int(value) for value in run([str(SIZE), str(elf)]).stdout.splitlines()[1].split()[:3]]
        if numbers[2] > 65536 or sum(numbers) > 200000:
            raise SystemExit("ELF size is unexpectedly large")
        builds.append(blob)
    if builds[0] != builds[1]:
        raise SystemExit("independent builds differ")
    if any(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest
           for name, digest in source_hashes.items()):
        raise SystemExit("source changed during the two production builds")

    sys.path.insert(0, str(ROOT))
    import retrieve
    good_diag = ("UNVERIFIED_ALLOCATOR_RECORDS physical-offset 0x81cbb000 bytes 28672 table-indices 0-1780\n"
                 "MOVIE_WORK_BEFORE 0x11111111 0x22222222 0x33333333 0x44444444 0x55555555\n"
                 "MOVIE_WORK_AFTER 0x11111111 0x22222222 0x33333333 0x44444444 0x55555555\n"
                 "IMAGE_FIELDS_BEFORE 0x0000002b 0x11223344 0x00000002 0x55667788 0x00123456 0x99aabbcc\n"
                 "IMAGE_FIELDS_AFTER 0x0000002b 0x11223344 0x00000002 0x55667788 0x00123456 0x99aabbcc\n")
    transcript = ("ALLOCATOR_BEGIN\nREADER_EXIT=0\nDIAG_BEGIN\n" + good_diag + "DIAG_END\nSIZE_BEGIN\n28672\nSIZE_END\n"
                  "SHA256(/tmp/allocator-records.bin)= " + "a" * 64 + "\nALLOCATOR_END\n")
    parsed = retrieve.parse_read_result(transcript)
    if (not parsed["accept"] or parsed["movie_work_descriptor_stable"] is not True
            or parsed["image_fields_stable"] is not True or parsed["image_fields_before"][2] != 2):
        raise SystemExit("valid retrieval transcript was rejected")
    changed_diag = good_diag.replace("MOVIE_WORK_AFTER 0x11111111 0x22222222 0x33333333",
                                    "MOVIE_WORK_AFTER 0x11111111 0x22222222 0x33333332")
    changed = retrieve.parse_read_result(transcript.replace(good_diag, changed_diag))
    if not changed["accept"] or changed["movie_work_descriptor_stable"] is not False:
        raise SystemExit("changing movie-work descriptor was treated as a read failure")
    image_changed_diag = good_diag.replace("IMAGE_FIELDS_AFTER 0x0000002b 0x11223344 0x00000002",
                                          "IMAGE_FIELDS_AFTER 0x0000002b 0x11223344 0x00000003")
    image_changed = retrieve.parse_read_result(transcript.replace(good_diag, image_changed_diag))
    if not image_changed["accept"] or image_changed["image_fields_stable"] is not False:
        raise SystemExit("changing image fields was treated as a read failure")
    for broken in (transcript.replace("READER_EXIT=0", "READER_EXIT=1"),
                   transcript.replace("28672\n", "28671\n"),
                   transcript.replace(good_diag, "ERROR guard\n"),
                   transcript.replace("MOVIE_WORK_AFTER", "MOVIE_WORK_CHANGED"),
                   transcript.replace("IMAGE_FIELDS_AFTER", "IMAGE_FIELDS_CHANGED")):
        if retrieve.parse_read_result(broken)["accept"]:
            raise SystemExit("parser accepted a malformed transcript")
    refuse = out / "occupied"
    refuse.mkdir(exist_ok=True)
    marker = refuse / "keep.txt"
    marker.write_text("preserve\n")
    result = subprocess.run([sys.executable, str(ROOT / "retrieve.py"), "--out-dir", str(refuse)], text=True, capture_output=True)
    if result.returncode == 0 or "already exists and is not empty" not in result.stderr or marker.read_text() != "preserve\n":
        raise SystemExit("nonempty evidence directory was not safely refused")
    digest = hashlib.sha256(builds[0]).hexdigest()
    (out / "elf.sha256").write_text(digest + "\n")
    print(f"PASS: host contract, retrieval parser, identical ARM builds {digest}")

if __name__ == "__main__":
    main()
