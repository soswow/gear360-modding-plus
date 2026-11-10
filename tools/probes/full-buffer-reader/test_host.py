#!/usr/bin/env python3
"""Host checks for the full-buffer reader. Does not execute the ARM ELF or the camera."""

import hashlib
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CLANG = Path("/opt/homebrew/opt/llvm/bin/clang")
READELF = Path("/opt/homebrew/opt/llvm/bin/llvm-readelf")
SIZE = Path("/opt/homebrew/opt/llvm/bin/llvm-size")
OBJDUMP = Path("/opt/homebrew/opt/llvm/bin/llvm-objdump")


def run(cmd, log):
    result = subprocess.run(cmd, text=True, capture_output=True)
    log.append("$ " + " ".join(str(part) for part in cmd))
    log.append(result.stdout)
    log.append(result.stderr)
    log.append(f"exit {result.returncode}")
    if result.returncode != 0:
        raise SystemExit(f"command failed: {cmd[0]}")
    return result


def assert_elf_is_unexecuted_arm(path, log):
    blob = path.read_bytes()
    if blob[:4] != b"\x7fELF":
        raise SystemExit("build did not produce an ELF")
    machine = struct.unpack_from("<H", blob, 18)[0]
    if machine != 40:
        raise SystemExit(f"ELF machine {machine} is not ARM")
    text = run([str(SIZE), str(path)], log).stdout
    # text data bss
    numbers = [int(part) for part in text.splitlines()[1].split()[:3]]
    text_size, data_size, bss_size = numbers
    if bss_size > 65536 or text_size + data_size + bss_size > 200000:
        raise SystemExit(f"ELF is unexpectedly large: text={text_size} data={data_size} bss={bss_size}")
    if b"full-buffer" not in blob or b"/dev/mem" not in blob or b"UNVERIFIED_FULL_BUFFER" not in blob:
        raise SystemExit("ELF is missing the fixed mode, device, or diagnostic marker")
    if b"preview-sample" in blob or b"UNVERIFIED_PREVIEW_SAMPLE" in blob:
        raise SystemExit("full-buffer ELF still contains the 4096-byte preview mode")
    # Hardware window from the earlier imaging snapshot. This reader must not reference it.
    if blob.find(struct.pack("<I", 0x20800340)) >= 0 or blob.find(struct.pack("<I", 0x20800024)) >= 0:
        raise SystemExit("ELF contains an MMIO address")
    disassembly = run([str(OBJDUMP), "-d", str(path)], log).stdout
    if "svc" not in disassembly:
        raise SystemExit("ELF disassembly has no svc")
    header = run([str(READELF), "-h", "-l", str(path)], log).stdout
    if "INTERP" in header:
        raise SystemExit("ELF has a dynamic interpreter")
    if re.search(r"\b(?:vld[0-9]*|vst[0-9]*|vldr|vstr|q[0-9]+)\b", disassembly.lower()):
        raise SystemExit("optimized ELF contains a SIMD/NEON memory instruction")
    log.append(
        "The ARM ELF was inspected with readelf, size, and objdump. It was not executed."
    )


def test_acceptance_parser():
    sys.path.insert(0, str(ROOT))
    import retrieve

    good_diag = "UNVERIFIED_FULL_BUFFER physical-offset 0xa77bb000 bytes 2764800\n"
    good = (
        "FULL_BUFFER_BEGIN\n"
        "READER_EXIT=0\n"
        "DIAG_BEGIN\n"
        f"{good_diag}"
        "DIAG_END\n"
        "SIZE_BEGIN\n"
        "2764800\n"
        "SIZE_END\n"
        "SHA256(/tmp/buffer.bin)= 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
        "FULL_BUFFER_END\n"
    )
    parsed = retrieve.parse_read_result(good)
    if not parsed["accept"]:
        raise SystemExit(f"success transcript rejected: {parsed}")
    for broken, reason in (
        (good.replace("READER_EXIT=0", "READER_EXIT=1"), "nonzero"),
        (good.replace("2764800\n", "4096\n"), "size"),
        (good.replace(good_diag, "ERROR guard\n"), "diagnostic"),
        (good.replace("DIAG_BEGIN\n" + good_diag, "DIAG_BEGIN\n" + good_diag + "ERROR late\n"), "error"),
    ):
        rejected = retrieve.parse_read_result(broken)
        if rejected["accept"]:
            raise SystemExit(f"parser accepted a {reason} failure")
    observed_diag = (
        "UNVERIFIED_FULL_BUFFER_OBSERVED physical-offset 0xa77bb000 bytes 2764800 "
        "vd_falling_before 0x00001234 vd_falling_after 0x00001235\n"
    )
    observed_good = good.replace(good_diag, observed_diag)
    observed = retrieve.parse_read_result(observed_good, observed=True)
    if not observed["accept"] or observed["observed_metrics"]["before"] != 0x1234 or observed["observed_metrics"]["after"] != 0x1235:
        raise SystemExit(f"observed diagnostic rejected or misparsed: {observed}")
    if observed["observed_metrics"]["delta_mod_10000"] != 1:
        raise SystemExit("ordinary counter delta was not parsed")
    meaning = observed["observed_metrics"]["meaning"]
    if "does not prove DMA wrote" not in meaning or "multiple full wraps are indistinguishable" not in meaning:
        raise SystemExit("observed metric lost its cadence-only limitation")
    wrapped_diag = observed_diag.replace("0x00001234", "0x0000270e").replace("0x00001235", "0x00000002")
    wrapped = retrieve.parse_read_result(observed_good.replace(observed_diag, wrapped_diag), observed=True)
    if not wrapped["accept"] or wrapped["observed_metrics"]["delta_mod_10000"] != 4:
        raise SystemExit(f"counter wrap was not handled modulo 10000: {wrapped}")
    unexpected_diag = observed_diag.replace("0x00001234", "0x00002710")
    unexpected = retrieve.parse_read_result(observed_good.replace(observed_diag, unexpected_diag), observed=True)
    if not unexpected["accept"] or unexpected["observed_metrics"]["delta_mod_10000"] is not None:
        raise SystemExit(f"unexpected counter value faulted or got a modular delta: {unexpected}")
    if unexpected["observed_metrics"]["range_status"] != "unexpected_counter_value":
        raise SystemExit("unexpected counter value was not marked")
    for bad_diag in (
        observed_diag.replace("vd_falling_after 0x00001235", "vd_falling_after 0x1235"),
        observed_diag.replace("vd_falling_before", "other_counter"),
        observed_diag + "ERROR late\n",
    ):
        if retrieve.parse_read_result(observed_good.replace(observed_diag, bad_diag), observed=True)["accept"]:
            raise SystemExit("observed parser accepted malformed diagnostics")
    if retrieve.parse_read_result(observed_good, observed=False)["accept"]:
        raise SystemExit("baseline parser accepted observed diagnostics")
    health = """HEALTH_BEGIN
APP_COUNT=1
APP_PID=254
APP_STAT_BEGIN
254 (di-camera-app) S 1 254 254 0 -1 4194560 9474 748 203 0 3584 1701 0 3 20 0 21 0 1042 1109700608 4065
APP_STAT_END
TRACERS_BEGIN
TracerPid:\t0
TracerPid:\t0
TRACERS_END
STAGING_BEGIN
STAGING_END
POWER_BEGIN
100
390
POWER_END
RAW_ROOT_BEGIN
RAW_ROOT_END
HEALTH_END
"""
    identity = retrieve.parse_health(health)
    if identity["pid"] != "254" or identity["starttime"] != "1042" or identity["tracers"] != ["0", "0"]:
        raise SystemExit(f"health parser mismatch: {identity}")
    if identity["staging"]:
        raise SystemExit("empty staging was not empty")
    commands = retrieve.build_commands("/tmp/gear360-full-buffer-test", "/tmp/gear360-full-buffer-test/reader")
    blob = "\n".join(commands.values()) + "\n" + retrieve.HEALTH_COMMAND
    if "cat /tmp/gear360-full-buffer-test/buffer.bin" in blob or "cat buffer.bin" in blob:
        raise SystemExit("a camera command would print the buffer through HTTP")
    for forbidden in ("/dev/mem", "strace", "reboot", "20800340", "kill "):
        if forbidden in blob:
            raise SystemExit(f"camera shell command contains {forbidden}")
    if "> /tmp/gear360-full-buffer-test/buffer.bin" not in commands["read"]:
        raise SystemExit("read command does not redirect binary stdout")
    observed_commands = retrieve.build_commands("/tmp/gear360-full-buffer-test", "/tmp/gear360-full-buffer-test/reader", True)
    if "reader full-buffer-observed >" not in observed_commands["read"]:
        raise SystemExit("observed mode was not selected")
    if any("\n" in command for command in (*commands.values(), retrieve.HEALTH_COMMAND)):
        raise SystemExit("camera shell commands must be one line; the CGI drops newlines")
    print("PASS: acceptance and health parsers")


def main():
    log = []
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "host-test-output"
    out.mkdir(parents=True, exist_ok=True)
    host_bin = out / "host_contract_tests"
    run(
        [str(CLANG), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", str(ROOT / "host_contract_test.c"), "-o", str(host_bin)],
        log,
    )
    result = run([str(host_bin)], log)
    if "PASS:" not in result.stdout:
        raise SystemExit("host contract test did not pass")
    build_a = out / "build-a"
    build_b = out / "build-b"
    run(["sh", str(ROOT / "build.sh"), str(build_a)], log)
    run(["sh", str(ROOT / "build.sh"), str(build_b)], log)
    elf_a = build_a / "full-buffer-reader.elf"
    elf_b = build_b / "full-buffer-reader.elf"
    if elf_a.read_bytes() != elf_b.read_bytes():
        raise SystemExit("independent rebuilds differ")
    digest = hashlib.sha256(elf_a.read_bytes()).hexdigest()
    log.append(f"elf sha256 {digest}")
    assert_elf_is_unexecuted_arm(elf_a, log)
    disassembly = run([str(OBJDUMP), "-d", str(elf_a)], log).stdout
    review_dir = out / "optimized-reader-review"
    review_dir.mkdir(parents=True, exist_ok=True)
    (review_dir / "full-buffer-reader.disasm.txt").write_text(disassembly)
    fragment = (
        ROOT.parents[2]
        / "output/decompile/rea/pass-completion-20261016/preview-sample-runtime/UNVERIFIED-preview-sample.bin"
    )
    run([sys.executable, str(ROOT / "analyze_buffer.py"), "--self-check", "--fragment", str(fragment)], log)
    test_acceptance_parser()
    refuse = out / "occupied-evidence"
    refuse.mkdir(parents=True, exist_ok=True)
    sentinel = refuse / "keep.txt"
    sentinel.write_text("existing evidence\n")
    result = subprocess.run(
        [sys.executable, str(ROOT / "retrieve.py"), "--out-dir", str(refuse)],
        text=True,
        capture_output=True,
    )
    if result.returncode == 0 or "already exists and is not empty" not in result.stderr:
        raise SystemExit("retrieval did not refuse a nonempty evidence destination")
    if sentinel.read_text() != "existing evidence\n" or len(list(refuse.iterdir())) != 1:
        raise SystemExit("evidence refusal modified the existing directory")
    (out / "host_contract_test.log").write_text("\n".join(log) + "\n")
    (out / "elf.sha256").write_text(digest + "\n")
    print(f"PASS: host contract, identical rebuilds, static ELF inspection sha256 {digest}")


if __name__ == "__main__":
    main()
