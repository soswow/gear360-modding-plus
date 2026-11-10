#!/usr/bin/env python3
"""Runner for the experimental raw-grab diagnostic.

Host argument construction is tested. Sensor identity, request association,
setting restoration and safe capture/copy lifetime remain unresolved; see
README.md before target work. A newly listed JPEG is a candidate output,
not an automatically correlated result. No successful target capture from
this version is claimed.
"""
import argparse
import hashlib
import io
import json
import re
import shlex
import urllib.parse
import urllib.request
import uuid
from datetime import datetime, timezone
from ftplib import FTP
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
CAMERA = "192.168.43.1"
HELPER = REPO / "output/ipcc-raw-grab-build/ipcc-raw-grab.elf"
PHOTO_DIRS = ["/opt/storage/sdcard/DCIM/101PHOTO", "/opt/storage/sdcard/DCIM/104PHOTO"]
# The progress log lives on the camera's own ext4 filesystem, not on the
# card: the card is FAT32, and a hard reboot loses its cache, so a log
# written there vanishes with the run that wrote it.  Each run gets its own
# directory under it, so two runs cannot overwrite each other's record.
LOG_ROOT = "/opt/gear360-grab"

# The modes that take a lease of their own, the modes that send a still, and
# the mode that copies a region.  The helper only reads the options that
# belong to the mode it is in.
LEASE_MODES = {"acquire", "compare", "grab"}
STILL_MODES = {"compare", "control", "capture", "grab", "still"}
BENCH_MODES = {"bench"}

# The helper reports the state of the capture system's RAW switch on its own
# output: "SSIF_RAW on" when it turns it on, "SSIF_RAW off" when it puts it
# back.
SSIF_PATTERN = re.compile(r"SSIF_RAW (on|off)")


def number(text):
    """A numeric option: decimal, or hexadecimal with a 0x prefix."""
    try:
        return int(text, 0)
    except (TypeError, ValueError):
        raise argparse.ArgumentTypeError("not a number: %r" % (text,))


def hexvalue(value):
    """A numeric option as the helper reads it: hexadecimal."""
    return "0x%x" % (value,)


def build_command(remote, mode, options, log_path):
    """The helper's command line for one mode.

    Only the flags the mode uses are emitted.  The helper checks its own
    options and refuses the ones it cannot use, so a flag that does not
    belong to the mode is a chance to fail the run before it starts.  Paths
    are quoted, because the command is run through a shell.
    """
    parts = [shlex.quote(f"{remote}/grab"),
             "--mode", mode,
             "--max-seconds", str(options.max_seconds),
             "--log", shlex.quote(log_path)]
    if mode in LEASE_MODES:
        parts += ["--size", hexvalue(options.size),
                  "--bank", str(options.bank)]
    if mode in STILL_MODES:
        parts += ["--command", hexvalue(options.command),
                  "--remaining", str(options.remaining),
                  "--free", str(options.free),
                  "--tmo", str(options.timeout_ms),
                  "--per", str(options.period_us),
                  "--set", str(options.settle_ms)]
    if mode in BENCH_MODES:
        parts += ["--bench-address", hexvalue(options.bench_address),
                  "--bench-length", hexvalue(options.bench_length)]
    if mode == "grab":
        parts += ["--out", shlex.quote(options.output)]
        if options.no_copy:
            parts += ["--no-copy"]
    if options.log_dump:
        parts += ["--log-dump", str(options.log_dump)]
    return " ".join(parts)


def last_ssif_state(*texts):
    """The last RAW switch state the helper reported, or None.

    A disable is attempted only when the helper reported an enable. A blanket
    write would overwrite a setting the camera had before the run, and the
    run that never selected the setting must not write to it at all.
    """
    state = None
    for text in texts:
        for match in SSIF_PATTERN.finditer(text or ""):
            state = match.group(1)
    return state


def execute(command, timeout=180):
    """Run one shell command through the camera's mod web endpoint."""
    url = f"http://{CAMERA}:8888/cgi-bin/execute?rawcmd=" + urllib.parse.quote(command)
    text = urllib.request.urlopen(url, timeout=timeout).read().decode("utf-8", "replace")
    match = re.search(r"<pre>\n?(.*)</pre>", text, re.S)
    return match.group(1) if match else text


def photo_listing():
    """The photo files the card holds, per folder."""
    listing = {}
    for directory in PHOTO_DIRS:
        text = execute(f"ls -1 {directory} 2>/dev/null | sort")
        listing[directory] = [line for line in text.split() if line.endswith(".JPG")]
    return listing


def new_photos(before, after):
    """The files that appeared between two listings."""
    found = []
    for directory, names in after.items():
        earlier = set(before.get(directory, []))
        for name in names:
            if name not in earlier:
                found.append(f"{directory}/{name}")
    return found


def build_parser():
    """The runner's options, with the numeric ones typed."""
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--mode", default="grab",
                        choices=["grab", "capture", "acquire", "compare", "control",
                                 "bench", "still"])
    parser.add_argument("--size", type=number, default=0x00100000)
    parser.add_argument("--bank", type=number, default=2)
    parser.add_argument("--command", "--cmd", dest="command", type=number,
                        default=0x01010016)
    parser.add_argument("--remaining", "--rem", dest="remaining", type=number,
                        default=2518)
    parser.add_argument("--free", type=number, default=7687503872)
    parser.add_argument("--out", dest="output", default="/tmp/sensor.raw")
    parser.add_argument("--timeout-ms", "--tmo", dest="timeout_ms", type=number,
                        default=25000)
    parser.add_argument("--bench-address", dest="bench_address", type=number, default=0)
    parser.add_argument("--bench-length", dest="bench_length", type=number,
                        default=0xfff200)
    parser.add_argument("--period-us", "--per", dest="period_us", type=number,
                        default=500)
    parser.add_argument("--settle-ms", "--set", dest="settle_ms", type=number,
                        default=2000)
    parser.add_argument("--max-seconds", dest="max_seconds", type=number, default=90)
    parser.add_argument("--log-dump", dest="log_dump", type=number, default=0)
    parser.add_argument("--no-copy", dest="no_copy", action="store_true")
    return parser


def main():
    arguments = build_parser().parse_args()

    arguments.out_dir.mkdir(parents=True, exist_ok=False)
    binary = HELPER.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    name = "gear360-grab-" + uuid.uuid4().hex[:10]
    remote = f"/tmp/{name}"
    log_dir = f"{LOG_ROOT}/{name}"
    log_path = f"{log_dir}/progress.log"
    record = {
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "helper_sha256": digest,
        "remote_directory": remote,
        "log_directory": log_dir,
        "arguments": {k: v for k, v in vars(arguments).items() if k != "out_dir"},
    }
    (arguments.out_dir / "stage.json").write_text(json.dumps(record, indent=2))

    created = False
    try:
        record["uptime_before"] = execute("cat /proc/uptime")
        photos_before = photo_listing()
        record["photos_before"] = {k: len(v) for k, v in photos_before.items()}
        with FTP(CAMERA) as ftp:
            ftp.login("root", "")
            ftp.cwd("/tmp")
            ftp.mkd(name)
            created = True
            ftp.cwd(name)
            ftp.storbinary("STOR grab", io.BytesIO(binary))
            check = io.BytesIO()
            ftp.retrbinary("RETR grab", check.write)
            if check.getvalue() != binary:
                raise RuntimeError("helper upload mismatch")
        hashed = execute(f"/usr/bin/openssl dgst -sha256 {remote}/grab")
        if digest not in hashed:
            raise RuntimeError("camera helper hash mismatch")
        execute(f"chmod 500 {remote}/grab")
        # The progress log needs a directory of its own before the helper
        # starts: the helper creates the file, not the directory.
        execute(f"mkdir -p {shlex.quote(log_dir)}")

        command = build_command(remote, arguments.mode, arguments, log_path)
        record["command"] = command
        record["response"] = execute(command, timeout=int(arguments.timeout_ms) // 1000 + 90)
        (arguments.out_dir / "response.txt").write_text(record["response"])

        photos_after = photo_listing()
        record["photos_after"] = {k: len(v) for k, v in photos_after.items()}
        record["new_photos"] = new_photos(photos_before, photos_after)

        try:
            with FTP(CAMERA) as ftp:
                ftp.login("root", "")
                buffer = io.BytesIO()
                ftp.retrbinary(f"RETR {arguments.output}", buffer.write)
            data = buffer.getvalue()
            (arguments.out_dir / "sensor.raw").write_bytes(data)
            record["raw_bytes"] = len(data)
            record["raw_sha256"] = hashlib.sha256(data).hexdigest()
        except Exception as error:
            record["raw_fetch_error"] = repr(error)
    except Exception as error:
        record["error"] = repr(error)
    finally:
        # The on-card progress log is the record of how far the program got.
        # It is fetched before the directory is removed, and it survives a
        # camera that reboots in the middle of the run.
        try:
            log = execute(f"cat {shlex.quote(log_path)} 2>/dev/null")
            (arguments.out_dir / "progress.log").write_text(log)
            record["progress_log"] = log
        except Exception as error:
            record["progress_log_error"] = repr(error)
        # A helper that is still running keeps /dev/d5_ipcc open, and the
        # stock application cannot take its own pictures while that is true.
        try:
            record["leftover"] = execute(
                f"ps -w 2>&1 | grep {remote}/grab | grep -v grep; echo END")
            execute(f"kill $(ps -w 2>&1 | grep {remote}/grab | grep -v grep "
                    f"| awk '{{print $1}}') 2>/dev/null; echo KILLED")
        except Exception as error:
            record["leftover_error"] = repr(error)
        # The capture system's RAW switch is put back only if this run left
        # it on.  The helper restores it itself on every path it owns, so
        # this is the net for a run that died in between.
        try:
            state = last_ssif_state(record.get("response"), record.get("progress_log"))
            record["ssif_state_after_run"] = state
            if state == "on":
                record["ssif_restore"] = execute(
                    "/usr/bin/st cap capdtm setusr 124 0x7c0000")
            else:
                record["ssif_restore_skipped"] = "the run did not leave the switch on"
        except Exception as error:
            record["ssif_restore_error"] = repr(error)
        try:
            execute(f"rm -rf {shlex.quote(log_dir)}")
        except Exception as error:
            record["log_cleanup_error"] = repr(error)
        if created:
            try:
                execute(f"rm -rf {remote}")
                listing = execute(f"ls -d {remote} 2>&1; echo DONE")
                record["cleanup_verified"] = "No such file" in listing
            except Exception as error:
                record["cleanup_error"] = repr(error)
        record["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (arguments.out_dir / "run.json").write_text(json.dumps(record, indent=2))
    print(json.dumps({k: record.get(k) for k in
                      ("response", "new_photos", "raw_bytes", "raw_sha256", "error")},
                     indent=2))


if __name__ == "__main__":
    main()
