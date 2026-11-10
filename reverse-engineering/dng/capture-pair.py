#!/usr/bin/env python3
"""Take one normal still with its full RAW frame and save both, for DNG work.

Runs the validated experiment wrapper (tools/experiments/
run-session.sh) with g360-shot --frame --count 1. The camera stays on its
boot: with the stock app stopped (the usual state) the mode is reenter; with
it running, fresh stops it once. The stock app is never started.

    python3 reverse-engineering/dng/capture-pair.py --tag P1 [--hold-ms 5000]

Records go to output/dng-<tag>/ (ignored by git): the session log, the
library log, the JPEGs, the frame (.bin, 33,732,608 bytes) and its
description. Each fetched file is checked against the camera's SHA-256.
"""
import argparse
import re
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "observe"))
import run_still_trial as rst  # noqa: E402

PROBE = REPO / "output/g360cap-build/g360-shot"
DISPLAY = REPO / "output/experiment-display-build/gear360-display"
SESSION = REPO / "tools/experiments/run-session.sh"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--hold-ms", type=int, default=5000,
                        help="live view before the still, so AE and AWB settle (default 5000)")
    parser.add_argument("--frame-output", choices=("save", "dng"), default="save",
                        help="save unchanged frame.bin or write the DNG on camera")
    parser.add_argument("--count", type=int, default=1, help="1 to 10 consecutive stills (default 1)")
    parser.add_argument("--exif", choices=("on", "off"), default="on",
                        help="insert Exif in JPEGs (default on)")
    parser.add_argument("--exposure", help="manual TV,ISO pair (otherwise automatic exposure)")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,24}", args.tag):
        parser.error("--tag takes letters, digits, _ and -")
    if not 0 <= args.hold_ms <= 60000:
        parser.error("--hold-ms takes 0 to 60000")
    if not 1 <= args.count <= 10:
        parser.error("--count takes 1 to 10")
    if args.exposure is not None:
        match = re.fullmatch(r"(-?\d+),(\d+)", args.exposure)
        if not match or not -160 <= int(match[1]) <= 240 or not 50 <= int(match[2]) <= 25600:
            parser.error("--exposure takes TV (-160 to 240),ISO (50 to 25600)")
    out = REPO / "output" / f"dng-{args.tag}"
    if out.exists():
        parser.error(f"{out} exists; choose another tag")
    for f in (PROBE, DISPLAY, SESSION):
        if not f.exists():
            parser.error(f"missing {f}; build it first")
    out.mkdir(parents=True)
    hashes = {f.name: rst.sha256(f) for f in (PROBE, DISPLAY, SESSION)}
    (out / "00-local-hashes.txt").write_text("".join(f"{v}  {k}\n" for k, v in hashes.items()))

    stage = f"/tmp/dng-{args.tag}"
    run_dir = f"/opt/storage/sdcard/exp-dng-{args.tag}"
    pre = rst.sh(out, "01-preflight.txt",
                 "grep btime /proc/stat; systemctl show -p ActiveState -p MainPID di-camera-app.service; "
                 f"ls -d {stage} {run_dir} 2>&1")
    m = re.search(r"^btime (\d+)", pre, re.M)
    if not m:
        print("could not read the camera's boot time; nothing staged", file=sys.stderr)
        return 2
    btime = m.group(1)
    mode = "fresh" if "ActiveState=active" in pre else "reenter"
    print(f"boot {btime}, stock app {'running' if mode == 'fresh' else 'stopped'}: mode {mode}")
    if pre.count("No such file") < 2:
        print(f"{stage} or {run_dir} already exists on the camera; choose another tag", file=sys.stderr)
        return 2

    for f in (PROBE, DISPLAY, SESSION):
        rst.ftp_put(f, f"{stage}/{f.name}")
    staged = rst.sh(out, "02-staged.txt",
                    f"chmod 755 {stage}/g360-shot {stage}/gear360-display {stage}/run-session.sh; "
                    f"/usr/bin/openssl dgst -sha256 {stage}/g360-shot {stage}/gear360-display {stage}/run-session.sh")
    for name, h in hashes.items():
        if f"{stage}/{name})= {h}" not in staged:
            print(f"staged {name} does not match its local hash; not started", file=sys.stderr)
            return 2

    argv = (f"--frame --count {args.count} --gap-ms 0 --hold-ms {args.hold_ms} "
            f"--frame-output {args.frame_output} --exif {args.exif}")
    if args.exposure is not None:
        argv += f" --exposure {args.exposure}"
    rst.sh(out, "03-launch.txt",
           f"cd {stage}; (DISPLAY_SHA256={hashes['gear360-display']} DISPLAY_TEXT='DNG PAIR' "
           f"sh ./run-session.sh {mode} {btime} {run_dir} {hashes['g360-shot']} {argv} "
           f"> {stage}/launch.out 2>&1; echo exit=$? >> {stage}/launch.out) > /dev/null 2>&1 & "
           f"sleep 1; cat {stage}/launch.out")
    print("capturing (the screen shows DNG PAIR)")
    done = None
    for _ in range(120):
        time.sleep(2)
        try:
            _, text, _ = rst.camsh.run(f"cat {stage}/launch.out", 20, boot_id=False)
        except Exception:
            continue
        found = re.search(r"^exit=(\d+)", text, re.M)
        if found:
            done = int(found.group(1))
            break
    listing = rst.sh(out, "90-listing.txt",
                     f"cat {stage}/launch.out; ls -l {run_dir} {run_dir}/shots; grep btime /proc/stat; "
                     f"cd {run_dir} && /usr/bin/openssl dgst -sha256 *.txt *.log shots/* 2>/dev/null")
    if done is None:
        print("the session did not finish in 4 minutes; see 90-listing.txt", file=sys.stderr)
        return 1
    remote = dict((p, h) for p, h in re.findall(r"^SHA256\(([^)]+)\)= ([0-9a-f]{64})$", listing, re.M))
    ok = True
    for path, h in sorted(remote.items()):
        local = out / Path(path).name
        if not rst.ftp_get(f"{run_dir}/{path}", local) or rst.sha256(local) != h:
            print(f"fetch failed or hash differs: {path}", file=sys.stderr)
            ok = False
    after = re.search(r"^btime (\d+)", listing, re.M)
    print(f"session exit {done}; {len(remote)} files fetched to {out}")
    for line in (out / "session.txt").read_text(errors="replace").splitlines() if (out / "session.txt").exists() else []:
        if re.match(r"(REFUSED|probe_exit|session_exit|mode=)", line):
            print("  " + line)
    if after:
        print("boot time", btime, "->", after.group(1), "(no reboot)" if after.group(1) == btime else "(THE CAMERA REBOOTED)")
    return 0 if done == 0 and ok else 1


if __name__ == "__main__":
    sys.exit(main())
