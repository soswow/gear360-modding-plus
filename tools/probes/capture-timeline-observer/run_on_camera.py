#!/usr/bin/env python3
"""Stage the read-only capture timeline observer on the camera, run it, fetch the log.

Modes:
  probe                      print the live manager pointer and pool guards
  timeline [--shutter]       record allocator/pixel changes; with --shutter, issue one
                             stock software shutter (`st key click s2`) two seconds in

The helper only reads /dev/mem. Temporary files live in one /tmp directory that is
removed before the script exits, and its absence is verified.
"""
import argparse
import hashlib
import io
import json
import re
import urllib.parse
import urllib.request
import uuid
from datetime import datetime, timezone
from ftplib import FTP
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
CAMERA = "192.168.43.1"
HELPER = REPO / "output/capture-timeline-observer-build/capture-timeline.elf"
EXPECTED_APP_COMMAND = "di-camera-app"


def execute(command, timeout=120):
    """Run one shell command through the camera's mod web endpoint."""
    url = f"http://{CAMERA}:8888/cgi-bin/execute?rawcmd=" + urllib.parse.quote(command)
    text = urllib.request.urlopen(url, timeout=timeout).read().decode("utf-8", "replace")
    match = re.search(r"<pre>\n?(.*)</pre>", text, re.S)
    return match.group(1) if match else text


def app_identity():
    """Return (pid, start tick) of the single running camera app, or raise."""
    text = execute(
        'for d in /proc/[0-9]*; do c=$(cat $d/comm 2>/dev/null); '
        f'[ "$c" = "{EXPECTED_APP_COMMAND}" ] && echo APP $(basename $d) $(cut -d" " -f22 $d/stat); done'
    )
    apps = re.findall(r"^APP (\d+) (\d+)$", text, re.M)
    if len(apps) != 1:
        raise RuntimeError(f"expected one camera app, saw {apps}")
    return apps[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=["probe", "timeline"])
    parser.add_argument("--manager", help="manager pointer from a fresh probe (hex)")
    parser.add_argument("--duration-ms", type=int, default=20000)
    parser.add_argument("--period-us", type=int, default=1000)
    parser.add_argument("--shutter", action="store_true", help="issue st key click s2 two seconds in")
    parser.add_argument("--trigger", choices=["key"], default="key",
                        help="key: st key click s2 (stock UI key); request-level triggers are disabled")
    parser.add_argument("--out-dir", type=Path, required=True)
    arguments = parser.parse_args()

    arguments.out_dir.mkdir(parents=True, exist_ok=False)
    binary = HELPER.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    name = "gear360-timeline-" + uuid.uuid4().hex[:10]
    remote = f"/tmp/{name}"
    record = {
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "mode": arguments.mode,
        "helper_sha256": digest,
        "remote_directory": remote,
    }
    (arguments.out_dir / "stage.json").write_text(json.dumps(record, indent=2))

    identity_before = app_identity()
    record["app_before"] = identity_before
    created = False
    try:
        with FTP(CAMERA) as ftp:
            ftp.login("root", "")
            ftp.cwd("/tmp")
            ftp.mkd(name)
            created = True
            ftp.cwd(name)
            ftp.storbinary("STOR observer", io.BytesIO(binary))
            check = io.BytesIO()
            ftp.retrbinary("RETR observer", check.write)
            if check.getvalue() != binary:
                raise RuntimeError("helper upload mismatch")
        hashed = execute(f"/usr/bin/openssl dgst -sha256 {remote}/observer")
        if digest not in hashed:
            raise RuntimeError("camera helper hash mismatch")
        execute(f"chmod 500 {remote}/observer")

        if arguments.mode == "probe":
            command = f"{remote}/observer probe > {remote}/out 2> {remote}/err; echo RC=$?"
        else:
            if not arguments.manager:
                raise SystemExit("--manager is required for timeline")
            shutter = "sleep 2; st key click s2; " if arguments.shutter else ""
            seconds = arguments.duration_ms // 1000 + 3
            command = (
                f"({remote}/observer timeline {arguments.manager} {arguments.duration_ms} "
                f"{arguments.period_us} > {remote}/out 2> {remote}/err; echo $? > {remote}/rc) & "
                f"{shutter}sleep {seconds}; cat {remote}/rc; cat /proc/uptime"
            )
        record["command"] = command
        record["response"] = execute(command, timeout=arguments.duration_ms // 1000 + 60)
        for remote_file in ("out", "err"):
            buffer = io.BytesIO()
            with FTP(CAMERA) as ftp:
                ftp.login("root", "")
                ftp.retrbinary(f"RETR {remote}/{remote_file}", buffer.write)
            (arguments.out_dir / remote_file).write_bytes(buffer.getvalue())
            record[f"{remote_file}_sha256"] = hashlib.sha256(buffer.getvalue()).hexdigest()
            record[f"{remote_file}_bytes"] = len(buffer.getvalue())
    finally:
        if created:
            execute(f"rm -rf {remote}")
            listing = execute(f"ls -d {remote} 2>&1; echo DONE")
            record["cleanup_verified"] = "No such file" in listing
        record["app_after"] = app_identity()
        record["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (arguments.out_dir / "run.json").write_text(json.dumps(record, indent=2))
    if record["app_after"] != identity_before:
        raise RuntimeError("camera app identity changed during the run")
    print(json.dumps({k: record[k] for k in ("response", "cleanup_verified", "app_before", "app_after")}, indent=2))
    print((arguments.out_dir / "out").read_text()[:3000])


if __name__ == "__main__":
    main()
