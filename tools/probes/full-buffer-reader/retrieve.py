#!/usr/bin/env python3
"""Passively copy the fixed 1920x960 buffer to the Mac, then remove the temporary files.

The helper is the only thing that maps /dev/mem. This script never sends the
2.7 MiB body through the HTTP response, and it does not capture or change settings.
"""

import hashlib
import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from ftplib import FTP, error_perm
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
EVIDENCE = REPO / "output/decompile/rea/full-buffer-read-20261016"
CAMERA = "192.168.43.1"
BUFFER_BYTES = 2764800
SUCCESS_DIAGNOSTIC = "UNVERIFIED_FULL_BUFFER physical-offset 0xa77bb000 bytes 2764800\n"
REMOTE_FILES = ("reader", "buffer.bin", "diag.txt", "startup-out", "startup-err")

# One line: the camera CGI drops newlines, which previously glued fi/done together.
HEALTH_COMMAND = (
    "echo HEALTH_BEGIN; date -u; found=0; "
    'for d in /proc/[0-9]*; do c=$(cat "$d/comm" 2>/dev/null || true); '
    'if [ "$c" = "di-camera-app" ]; then found=$((found + 1)); '
    'echo APP_PID=$(basename "$d"); echo APP_COMM=$c; '
    'echo APP_STAT_BEGIN; cat "$d/stat"; echo APP_STAT_END; '
    "echo TRACERS_BEGIN; "
    'for status in "$d"/task/*/status; do grep \'^TracerPid:\' "$status"; done; '
    "echo TRACERS_END; fi; done; "
    "echo APP_COUNT=$found; "
    "echo STAGING_BEGIN; "
    "ls -d /tmp/gear360-ram-reader-* /tmp/gear360-mmio-reader-* /tmp/gear360-preview-reader-* /tmp/gear360-full-buffer-* 2>/dev/null || true; "
    "echo STAGING_END; "
    "echo POWER_BEGIN; "
    "cat /sys/class/power_supply/battery/capacity /sys/class/power_supply/battery/temp 2>/dev/null || true; "
    "echo POWER_END; "
    "echo RAW_ROOT_BEGIN; "
    "ls -l /mnt/mmc/*.raw 2>/dev/null || true; "
    "echo RAW_ROOT_END; echo HEALTH_END"
)


def build_commands(directory, target, observed=False):
    """Shell sent to the camera. The pixel file is redirected, never cat'ed."""
    outfile = f"{directory}/buffer.bin"
    diag = f"{directory}/diag.txt"
    startup_out = f"{directory}/startup-out"
    startup_err = f"{directory}/startup-err"
    return {
        "hash": f"echo HASH_BEGIN; /usr/bin/openssl dgst -sha256 {target}; echo HASH_END",
        "startup": (
            "echo START_BEGIN; "
            f"chmod 500 {target}; "
            f"{target} > {startup_out} 2> {startup_err}; "
            "reader_status=$?; echo READER_EXIT=$reader_status; "
            f"echo STDOUT_BYTES=$(wc -c < {startup_out}); "
            f"echo STDERR_BEGIN; cat {startup_err}; echo STDERR_END; "
            "echo START_END"
        ),
        "read": (
            "echo FULL_BUFFER_BEGIN; "
            f"{target} {'full-buffer-observed' if observed else 'full-buffer'} > {outfile} 2> {diag}; "
            "reader_status=$?; echo READER_EXIT=$reader_status; "
            f"echo DIAG_BEGIN; cat {diag}; echo DIAG_END; "
            f"echo SIZE_BEGIN; wc -c < {outfile}; echo SIZE_END; "
            f"/usr/bin/openssl dgst -sha256 {outfile}; "
            "echo FULL_BUFFER_END"
        ),
    }


def _exact_section(text, name):
    lines = text.splitlines()
    begin = f"{name}_BEGIN"
    end = f"{name}_END"
    try:
        start = lines.index(begin)
        stop = lines.index(end, start + 1)
    except ValueError as exc:
        raise ValueError(f"missing {name} section") from exc
    return lines[start + 1 : stop]


def parse_health(text):
    stat_lines = [line for line in _exact_section(text, "APP_STAT") if line.strip()]
    tracer_lines = _exact_section(text, "TRACERS")
    staging = [line for line in _exact_section(text, "STAGING") if line.strip()]
    raw_files = [line for line in _exact_section(text, "RAW_ROOT") if line.strip()]
    power = [line for line in _exact_section(text, "POWER") if line.strip()]
    pids = []
    comms = []
    count = None
    for line in text.splitlines():
        if line.startswith("APP_PID=") and line[8:].isdigit():
            pids.append(line[8:])
        elif line.startswith("APP_COMM="):
            comms.append(line[9:])
        elif line.startswith("APP_COUNT=") and line[10:].isdigit():
            count = int(line[10:])
    starttime = None
    if stat_lines:
        stat = stat_lines[0]
        fields = stat[stat.rfind(")") + 2 :].split()
        if len(fields) < 20:
            raise ValueError("camera stat line is missing starttime")
        starttime = fields[19]
    tracers = []
    for line in tracer_lines:
        if line.startswith("TracerPid:"):
            tracers.append(line.split(":", 1)[1].strip())
    return {
        "pid": pids[0] if pids else None,
        "pids": pids,
        "comm": comms[0] if comms else None,
        "app_count": count,
        "starttime": starttime,
        "tracers": tracers,
        "staging": staging,
        "raw_files": raw_files,
        "power": power,
    }


def parse_read_result(text, observed=False):
    exits = [line[12:] for line in text.splitlines() if line.startswith("READER_EXIT=") and line[12:].isdigit()]
    try:
        diagnostic = "\n".join(_exact_section(text, "DIAG")) + "\n"
        size_lines = [line.strip() for line in _exact_section(text, "SIZE") if line.strip()]
    except ValueError as exc:
        return {"accept": False, "reason": str(exc)}
    hashes = []
    for line in text.splitlines():
        if line.startswith("SHA256(") and "= " in line:
            digest = line.split("= ", 1)[1].strip()
            if len(digest) == 64 and all(ch in "0123456789abcdef" for ch in digest):
                hashes.append(digest)
    size = int(size_lines[0]) if len(size_lines) == 1 and size_lines[0].isdigit() else None
    exit_ok = len(exits) == 1 and exits[0] == "0"
    metrics = None
    diagnostic_ok = diagnostic == SUCCESS_DIAGNOSTIC
    if observed:
        match = re.fullmatch(
            r"UNVERIFIED_FULL_BUFFER_OBSERVED physical-offset 0xa77bb000 bytes 2764800 "
            r"vd_falling_before 0x([0-9a-f]{8}) vd_falling_after 0x([0-9a-f]{8})\n",
            diagnostic,
        )
        diagnostic_ok = match is not None
        if match:
            before = int(match.group(1), 16)
            after = int(match.group(2), 16)
            in_range = before < 10000 and after < 10000
            metrics = {
                "counter": "vd_falling",
                "before": before,
                "after": after,
                "delta_mod_10000": (after - before) % 10000 if in_range else None,
                "range_status": "in_range" if in_range else "unexpected_counter_value",
                "meaning": (
                    "difference modulo the documented 10000 counter wrap; multiple full wraps are indistinguishable. "
                    "A positive difference means the observed cadence counter advanced during the copy, "
                    "but does not prove DMA wrote the copied bytes or establish WDMA completion or coherence."
                ),
            }
    accept = (
        exit_ok
        and size == BUFFER_BYTES
        and diagnostic_ok
        and len(hashes) == 1
        and "ERROR" not in diagnostic
    )
    return {
        "accept": accept,
        "exit": exits[0] if len(exits) == 1 else None,
        "size": size,
        "sha256": hashes[0] if len(hashes) == 1 else None,
        "diagnostic": diagnostic,
        "observed_metrics": metrics,
        "reason": "ok" if accept else "exit, size, diagnostic, or camera hash did not match",
    }


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_probe(evidence, name, command, timeout):
    (evidence / f"{name}.command").write_text(command)
    start = datetime.now(timezone.utc).isoformat()
    args = [
        "curl",
        "--noproxy",
        "*",
        "--silent",
        "--show-error",
        "--connect-timeout",
        "3",
        "--max-time",
        str(timeout),
        "--max-filesize",
        "1048576",
        "--dump-header",
        str(evidence / f"{name}.headers"),
        "--output",
        str(evidence / f"{name}.response"),
        "--get",
        "--data-urlencode",
        "rawcmd=" + command,
        f"http://{CAMERA}:8888/cgi-bin/execute",
    ]
    result = subprocess.run(args, capture_output=True, text=True)
    record = {
        "host_start_utc": start,
        "host_end_utc": datetime.now(timezone.utc).isoformat(),
        "method": "GET",
        "endpoint": f"http://{CAMERA}:8888/cgi-bin/execute",
        "rawcmd": command,
        "returncode": result.returncode,
        "stderr": result.stderr,
        "max_filesize": 1048576,
        "timeout_seconds": timeout,
    }
    (evidence / f"{name}.request.json").write_text(json.dumps(record, indent=2) + "\n")
    if result.returncode != 0:
        raise RuntimeError(f"{name} curl failed: {result.stderr.strip()}")
    return (evidence / f"{name}.response").read_text(errors="replace")


def ftp_session():
    session = FTP()
    session.connect(CAMERA, 21, timeout=60)
    session.login("root", "")
    session.voidcmd("TYPE I")
    return session


def remove_remote(directory):
    events = []
    session = ftp_session()
    try:
        events.append(session.pwd())
        for name in REMOTE_FILES:
            try:
                events.append(session.delete(f"{directory}/{name}"))
            except error_perm as exc:
                events.append(f"delete {name}: {exc}")
        try:
            events.append(session.rmd(directory))
        except error_perm as exc:
            events.append(f"rmd: {exc}")
        try:
            leftover = session.nlst(directory)
            events.append(f"still-present {leftover}")
        except error_perm as exc:
            events.append(f"directory absent ({exc})")
        events.append(session.quit())
    finally:
        try:
            session.close()
        except Exception:
            pass
    return events


def require_identity(health, label):
    if health["app_count"] != 1 or len(health["pids"]) != 1:
        raise RuntimeError(f"{label}: expected one di-camera-app, got {health['app_count']} {health['pids']}")
    if health["comm"] != "di-camera-app":
        raise RuntimeError(f"{label}: comm is {health['comm']}")
    if not health["tracers"] or any(value != "0" for value in health["tracers"]):
        raise RuntimeError(f"{label}: tracer attached: {health['tracers']}")
    if health["starttime"] is None:
        raise RuntimeError(f"{label}: starttime missing")


def main():
    global EVIDENCE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, help="new or empty evidence directory")
    parser.add_argument("--observed", action="store_true", help="sample the known VD-falling counter around the copy")
    args = parser.parse_args()
    if args.out_dir:
        EVIDENCE = args.out_dir.expanduser().resolve()
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        EVIDENCE = REPO / "output/decompile/rea" / f"full-buffer-read-{stamp}"
        suffix = 1
        while EVIDENCE.exists():
            EVIDENCE = REPO / "output/decompile/rea" / f"full-buffer-read-{stamp}-{suffix}"
            suffix += 1
    if EVIDENCE.exists() and (not EVIDENCE.is_dir() or any(EVIDENCE.iterdir())):
        raise SystemExit(f"evidence destination already exists and is not empty: {EVIDENCE}")
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    test = subprocess.run(
        [sys.executable, str(ROOT / "test_host.py"), str(EVIDENCE / "host-test-output")],
        text=True,
    )
    if test.returncode != 0:
        raise SystemExit(test.returncode)
    elf_sha = (EVIDENCE / "host-test-output/elf.sha256").read_text().strip()
    elf = EVIDENCE / "host-test-output/build-a/full-buffer-reader.elf"
    if sha256_file(elf) != elf_sha:
        raise SystemExit("elf hash changed after the host test")

    directory = "/tmp/gear360-full-buffer-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")
    target = directory + "/reader"
    commands = build_commands(directory, target, args.observed)
    created = False
    pre = None
    local = None
    pending = None
    caught = None
    try:
        run_probe(EVIDENCE, "connect", "echo CONNECT_BEGIN; date -u; echo CONNECT_END", 15)
        pre = parse_health(run_probe(EVIDENCE, "pre-health", HEALTH_COMMAND, 20))
        require_identity(pre, "pre-health")
        (EVIDENCE / "pre-health.identity.json").write_text(json.dumps(pre, indent=2) + "\n")
        if any(directory in line for line in pre["staging"]):
            raise RuntimeError("staging directory already exists")

        payload = elf.read_bytes()
        (EVIDENCE / "stage.json").write_text(
            json.dumps(
                {
                    "directory": directory,
                    "target": target,
                    "bytes": len(payload),
                    "sha256": elf_sha,
                    "download_verified": False,
                    "upload_started": False,
                },
                indent=2,
            )
            + "\n"
        )
        session = ftp_session()
        try:
            stage = json.loads((EVIDENCE / "stage.json").read_text())
            stage["upload_started"] = True
            (EVIDENCE / "stage.json").write_text(json.dumps(stage, indent=2) + "\n")
            session.mkd(directory)
            created = True
            session.storbinary("STOR " + target, PathBuffer(payload))
            downloaded = bytearray()
            session.retrbinary("RETR " + target, downloaded.extend)
            if bytes(downloaded) != payload:
                raise RuntimeError("uploaded helper did not download identically")
            session.quit()
        finally:
            session.close()
        stage = json.loads((EVIDENCE / "stage.json").read_text())
        stage["download_verified"] = True
        (EVIDENCE / "stage.json").write_text(json.dumps(stage, indent=2) + "\n")

        hashed = run_probe(EVIDENCE, "hash", commands["hash"], 20)
        if elf_sha not in hashed:
            raise RuntimeError("camera openssl hash does not match the rebuilt helper")
        started = run_probe(EVIDENCE, "startup", commands["startup"], 20)
        flat_started = "".join(started.split())
        if "READER_EXIT=2" not in flat_started or "STDOUT_BYTES=0" not in flat_started:
            raise RuntimeError("startup probe did not reject missing mode before opening /dev/mem")
        if "ERROR: expected exactly one mode: full-buffer" not in started:
            raise RuntimeError("startup probe did not print the fixed usage diagnostic")
        read_text = run_probe(EVIDENCE, "read", commands["read"], 120)
        parsed = parse_read_result(read_text, observed=args.observed)
        (EVIDENCE / "read.parsed.json").write_text(json.dumps(parsed, indent=2) + "\n")
        if not parsed["accept"]:
            raise RuntimeError(parsed["reason"])

        local = EVIDENCE / "UNVERIFIED-full-buffer.bin.partial"
        session = ftp_session()
        try:
            with local.open("wb") as handle:
                session.retrbinary("RETR " + directory + "/buffer.bin", handle.write)
            session.quit()
        finally:
            session.close()
        local_sha = sha256_file(local)
        if local_sha != parsed["sha256"] or local.stat().st_size != BUFFER_BYTES:
            local.rename(EVIDENCE / "REJECTED-transfer.bin")
            raise RuntimeError(f"Mac hash {local_sha} does not match camera hash {parsed['sha256']}")
        pending = {
            "label": "UNVERIFIED",
            "bytes": BUFFER_BYTES,
            "sha256": local_sha,
            "camera_sha256": parsed["sha256"],
            "physical_offset": "0xa77bb000",
            "width": 1920,
            "height": 960,
            "stride": 2880,
            "helper_sha256": elf_sha,
            "observed_metrics": parsed["observed_metrics"],
            "pre_pid": pre["pid"],
            "pre_starttime": pre["starttime"],
            "limits": (
                "Passive snapshot of the fixed ordinary-RAM extent. "
                "Guards matched before and after the copy. They do not prove frame coherence, "
                "sensor origin, fresh completion, or a lease."
            ),
        }
    except Exception as exc:
        caught = exc
    finally:
        if created:
            events = remove_remote(directory)
            (EVIDENCE / "cleanup.json").write_text(
                json.dumps(
                    {
                        "host_utc": datetime.now(timezone.utc).isoformat(),
                        "directory": directory,
                        "events": events,
                    },
                    indent=2,
                )
                + "\n"
            )

    if pre is not None:
        try:
            post = parse_health(run_probe(EVIDENCE, "post-health", HEALTH_COMMAND, 20))
            (EVIDENCE / "post-health.identity.json").write_text(json.dumps(post, indent=2) + "\n")
            require_identity(post, "post-health")
            if post["pid"] != pre["pid"] or post["starttime"] != pre["starttime"] or post["tracers"] != pre["tracers"]:
                if local is not None and local.exists():
                    local.rename(EVIDENCE / "REJECTED-app-identity-changed.bin")
                raise RuntimeError("camera-app identity changed during the read")
            if any(directory in line for line in post["staging"]):
                if local is not None and local.exists():
                    local.rename(EVIDENCE / "REJECTED-staging-leftover.bin")
                raise RuntimeError("temporary directory still present after cleanup")
        except Exception as exc:
            caught = exc if caught is None else RuntimeError(f"{caught}; {exc}")
    if caught is not None:
        raise caught
    final = EVIDENCE / "UNVERIFIED-full-buffer.bin"
    local.replace(final)
    pending["path"] = str(final)
    pending["post_pid"] = post["pid"]
    pending["post_starttime"] = post["starttime"]
    pending["tracer_count"] = len(post["tracers"])
    (EVIDENCE / "metadata.json").write_text(json.dumps(pending, indent=2) + "\n")
    print(f"saved {final}")


class PathBuffer:
    """Small file-like wrapper so FTP can read an already-loaded helper."""

    def __init__(self, payload):
        self.payload = payload
        self.offset = 0

    def read(self, count):
        block = self.payload[self.offset : self.offset + count]
        self.offset += len(block)
        return block


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        failure = {
            "host_utc": datetime.now(timezone.utc).isoformat(),
            "error": str(exc),
            "note": "No full buffer was promoted unless UNVERIFIED-full-buffer.bin exists and metadata matches it.",
        }
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        (EVIDENCE / "failure.json").write_text(json.dumps(failure, indent=2) + "\n")
        print(f"FULL BUFFER READ FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
