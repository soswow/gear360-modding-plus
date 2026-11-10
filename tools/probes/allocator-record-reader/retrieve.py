#!/usr/bin/env python3
"""Retrieve a fixed allocator-record metadata prefix without copying pixel bytes."""
import argparse
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from ftplib import error_perm
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
COMMON_DIR = REPO / "tools/probes/full-buffer-reader"
spec = importlib.util.spec_from_file_location("full_buffer_reader_common", COMMON_DIR / "retrieve.py")
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)

PREFIX_BYTES = 0x7000
SUCCESS_DIAGNOSTIC = "UNVERIFIED_ALLOCATOR_RECORDS physical-offset 0x81cbb000 bytes 28672 table-indices 0-1780"
common.REMOTE_FILES = ("reader", "buffer.bin", "diag.txt", "startup-out", "startup-err")
common.HEALTH_COMMAND = common.HEALTH_COMMAND.replace(
    "/tmp/gear360-full-buffer-* 2>/dev/null",
    "/tmp/gear360-full-buffer-* /tmp/gear360-allocator-records-* 2>/dev/null",
)

def build_commands(directory, target):
    outfile, diag = f"{directory}/buffer.bin", f"{directory}/diag.txt"
    startup_out, startup_err = f"{directory}/startup-out", f"{directory}/startup-err"
    return {
        "hash": f"echo HASH_BEGIN; /usr/bin/openssl dgst -sha256 {target}; echo HASH_END",
        "startup": ("echo START_BEGIN; " f"chmod 500 {target}; {target} > {startup_out} 2> {startup_err}; "
                    "reader_status=$?; echo READER_EXIT=$reader_status; "
                    f"echo STDOUT_BYTES=$(wc -c < {startup_out}); echo STDERR_BEGIN; cat {startup_err}; echo STDERR_END; echo START_END"),
        "read": ("echo ALLOCATOR_BEGIN; " f"{target} allocator-records > {outfile} 2> {diag}; "
                 "reader_status=$?; echo READER_EXIT=$reader_status; " f"echo DIAG_BEGIN; cat {diag}; echo DIAG_END; "
                 f"echo SIZE_BEGIN; wc -c < {outfile}; echo SIZE_END; /usr/bin/openssl dgst -sha256 {outfile}; echo ALLOCATOR_END"),
    }

def parse_read_result(text):
    def section(name):
        lines = text.splitlines()
        try:
            return lines[lines.index(name + "_BEGIN") + 1:lines.index(name + "_END")]
        except ValueError as exc:
            raise ValueError(f"missing {name} section") from exc
    try:
        diagnostics = section("DIAG")
        sizes = [line.strip() for line in section("SIZE") if line.strip()]
    except ValueError as exc:
        return {"accept": False, "reason": str(exc)}
    diagnostic = "\n".join(diagnostics) + "\n"
    exits = [line.split("=", 1)[1] for line in text.splitlines() if line.startswith("READER_EXIT=")]
    hashes = re.findall(r"^SHA256\([^\n]+\)= ([0-9a-f]{64})$", text, re.MULTILINE)
    size = int(sizes[0]) if len(sizes) == 1 and sizes[0].isdigit() else None
    movie_before_match = re.fullmatch(r"MOVIE_WORK_BEFORE(?: 0x[0-9a-f]{8}){5}", diagnostics[1]) if len(diagnostics) == 5 else None
    movie_after_match = re.fullmatch(r"MOVIE_WORK_AFTER(?: 0x[0-9a-f]{8}){5}", diagnostics[2]) if len(diagnostics) == 5 else None
    image_before_match = re.fullmatch(r"IMAGE_FIELDS_BEFORE(?: 0x[0-9a-f]{8}){6}", diagnostics[3]) if len(diagnostics) == 5 else None
    image_after_match = re.fullmatch(r"IMAGE_FIELDS_AFTER(?: 0x[0-9a-f]{8}){6}", diagnostics[4]) if len(diagnostics) == 5 else None
    movie_before = [int(value, 16) for value in re.findall(r"0x([0-9a-f]{8})", diagnostics[1])] if movie_before_match else None
    movie_after = [int(value, 16) for value in re.findall(r"0x([0-9a-f]{8})", diagnostics[2])] if movie_after_match else None
    image_before = [int(value, 16) for value in re.findall(r"0x([0-9a-f]{8})", diagnostics[3])] if image_before_match else None
    image_after = [int(value, 16) for value in re.findall(r"0x([0-9a-f]{8})", diagnostics[4])] if image_after_match else None
    diagnostic_ok = (len(diagnostics) == 5 and diagnostics[0] == SUCCESS_DIAGNOSTIC
                     and movie_before_match and movie_after_match and image_before_match and image_after_match)
    accepted = len(exits) == 1 and exits[0] == "0" and size == PREFIX_BYTES and diagnostic_ok and len(hashes) == 1
    return {"accept": accepted, "exit": exits[0] if len(exits) == 1 else None, "size": size,
            "sha256": hashes[0] if len(hashes) == 1 else None, "diagnostic": diagnostic,
            "movie_work_descriptor_before": movie_before,
            "movie_work_descriptor_after": movie_after,
            "movie_work_descriptor_stable": movie_before == movie_after if movie_before is not None and movie_after is not None else None,
            "image_fields_before": image_before,
            "image_fields_after": image_after,
            "image_fields_stable": image_before == image_after if image_before is not None and image_after is not None else None,
            "reason": "ok" if accepted else "exit, size, diagnostic, or camera hash did not match"}

def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()
    if args.out_dir:
        evidence = args.out_dir.expanduser().resolve()
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        evidence = REPO / "output/decompile/rea" / f"allocator-record-read-{stamp}"
        suffix = 1
        while evidence.exists():
            evidence = REPO / "output/decompile/rea" / f"allocator-record-read-{stamp}-{suffix}"
            suffix += 1
    if evidence.exists() and (not evidence.is_dir() or any(evidence.iterdir())):
        raise SystemExit(f"evidence destination already exists and is not empty: {evidence}")
    evidence.mkdir(parents=True, exist_ok=True)
    common.EVIDENCE = evidence
    test = subprocess.run([sys.executable, str(ROOT / "test_host.py"), str(evidence / "host-test-output")])
    if test.returncode:
        raise SystemExit(test.returncode)
    elf = evidence / "host-test-output/build-a/allocator-record-reader.elf"
    elf_sha = (evidence / "host-test-output/elf.sha256").read_text().strip()
    if sha256_file(elf) != elf_sha:
        raise RuntimeError("helper hash changed after host checks")
    directory = "/tmp/gear360-allocator-records-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")
    target = directory + "/reader"
    commands = build_commands(directory, target)
    created = False
    pre = None
    local = None
    pending = None
    caught = None
    try:
        common.run_probe(evidence, "connect", "echo CONNECT_BEGIN; date -u; echo CONNECT_END", 15)
        pre = common.parse_health(common.run_probe(evidence, "pre-health", common.HEALTH_COMMAND, 20))
        common.require_identity(pre, "pre-health")
        (evidence / "pre-health.identity.json").write_text(json.dumps(pre, indent=2) + "\n")
        if any(directory in line for line in pre["staging"]):
            raise RuntimeError("staging directory already exists")
        payload = elf.read_bytes()
        stage = {"directory": directory, "target": target, "bytes": len(payload), "sha256": elf_sha,
                 "upload_started": False, "download_verified": False}
        (evidence / "stage.json").write_text(json.dumps(stage, indent=2) + "\n")
        session = common.ftp_session()
        try:
            stage["upload_started"] = True
            (evidence / "stage.json").write_text(json.dumps(stage, indent=2) + "\n")
            session.mkd(directory); created = True
            session.storbinary("STOR " + target, PathBuffer(payload))
            uploaded = bytearray(); session.retrbinary("RETR " + target, uploaded.extend)
            if bytes(uploaded) != payload:
                raise RuntimeError("uploaded helper did not download identically")
            session.quit()
        finally:
            session.close()
        stage["download_verified"] = True
        (evidence / "stage.json").write_text(json.dumps(stage, indent=2) + "\n")
        if elf_sha not in common.run_probe(evidence, "hash", commands["hash"], 20):
            raise RuntimeError("camera helper hash mismatch")
        startup = common.run_probe(evidence, "startup", commands["startup"], 20)
        if "READER_EXIT=2" not in "".join(startup.split()) or "STDOUT_BYTES=0" not in "".join(startup.split()):
            raise RuntimeError("startup probe failed to reject missing mode before opening /dev/mem")
        if "ERROR: expected exactly one mode: allocator-records" not in startup:
            raise RuntimeError("startup usage diagnostic mismatch")
        read_text = common.run_probe(evidence, "read", commands["read"], 30)
        parsed = parse_read_result(read_text)
        (evidence / "read.parsed.json").write_text(json.dumps(parsed, indent=2) + "\n")
        if not parsed["accept"]:
            raise RuntimeError(parsed["reason"])
        local = evidence / "UNVERIFIED-allocator-records.bin.partial"
        session = common.ftp_session()
        try:
            with local.open("wb") as output:
                session.retrbinary("RETR " + directory + "/buffer.bin", output.write)
            session.quit()
        finally:
            session.close()
        local_sha = sha256_file(local)
        if local.stat().st_size != PREFIX_BYTES or local_sha != parsed["sha256"]:
            local.rename(evidence / "REJECTED-transfer.bin")
            raise RuntimeError("retrieved prefix size or Mac hash does not match camera hash")
        pending = {"label": "UNVERIFIED", "bytes": PREFIX_BYTES, "sha256": local_sha,
                   "camera_sha256": parsed["sha256"], "helper_sha256": elf_sha,
                   "physical_offset": "0x81cbb000", "table_indices": [0, 1780],
                   "movie_work_descriptor_before": parsed["movie_work_descriptor_before"],
                   "movie_work_descriptor_after": parsed["movie_work_descriptor_after"],
                   "movie_work_descriptor_stable": parsed["movie_work_descriptor_stable"],
                   "image_fields_before": parsed["image_fields_before"],
                   "image_fields_after": parsed["image_fields_after"],
                   "image_fields_stable": parsed["image_fields_stable"],
                   "limits": "Fixed allocator metadata prefix only. Records are unsynchronized observations and do not prove a lease or allocation ownership."}
    except Exception as exc:
        caught = exc
    finally:
        if created:
            (evidence / "cleanup.json").write_text(json.dumps({"host_utc": datetime.now(timezone.utc).isoformat(),
                "directory": directory, "events": common.remove_remote(directory)}, indent=2) + "\n")
    if pre is not None:
        try:
            post = common.parse_health(common.run_probe(evidence, "post-health", common.HEALTH_COMMAND, 20))
            (evidence / "post-health.identity.json").write_text(json.dumps(post, indent=2) + "\n")
            common.require_identity(post, "post-health")
            if post["pid"] != pre["pid"] or post["starttime"] != pre["starttime"] or post["tracers"] != pre["tracers"]:
                if local and local.exists(): local.rename(evidence / "REJECTED-app-identity-changed.bin")
                raise RuntimeError("camera-app identity changed during read")
            if any(directory in line for line in post["staging"]):
                if local and local.exists(): local.rename(evidence / "REJECTED-staging-leftover.bin")
                raise RuntimeError("temporary staging remains after cleanup")
        except Exception as exc:
            caught = exc if caught is None else RuntimeError(f"{caught}; {exc}")
    if caught is not None:
        raise caught
    final = evidence / "UNVERIFIED-allocator-records.bin"
    local.replace(final)
    pending["path"] = str(final)
    pending["pre_pid"] = pre["pid"]
    pending["post_pid"] = post["pid"]
    pending["pre_starttime"] = pre["starttime"]
    pending["post_starttime"] = post["starttime"]
    (evidence / "metadata.json").write_text(json.dumps(pending, indent=2) + "\n")
    print(f"saved {final}")

class PathBuffer:
    def __init__(self, payload): self.payload, self.offset = payload, 0
    def read(self, count):
        block = self.payload[self.offset:self.offset + count]
        self.offset += len(block)
        return block

if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception as exc:
        print(f"ALLOCATOR RECORD READ FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
