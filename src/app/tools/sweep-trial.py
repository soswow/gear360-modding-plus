#!/usr/bin/env python3
"""Unattended sweep trial: one `g360-app --sweep-once` (or, with --still, one
`--once` still) on the camera's current boot.

The build is staged in /tmp and run with the launcher's locks, against a
trial root on the card (/opt/storage/sdcard/exp-sweep-TAG/G360) and a trial
queue on the built-in flash (/opt/usr/g360-exp-sweep-TAG/queue), so the
user's photos and counter are not touched. Needs the stock app stopped and no
other capture/UI client; it never starts or stops anything itself.

Afterwards it lists the trial's files, fetches frames 1, 4 and 7 to
output/sweep-trial-TAG/ for a local check (sizes, DNG header, mean level;
for JPEGs the markers and, with exiftool, the Exif), then deletes the
trial's DNGs and JPEGs from the card (the records stay there), the empty
flash queue and the staged build.

    python3 src/app/tools/sweep-trial.py --tag A [--still] [--save flash|card]
        [--output raw|jpeg|raw+jpeg] [--sounds on|off] [--keep] [--no-fetch]

--save, --output and --sounds are passed as the app's --sweep-save, --output
and --sounds (this run only; the trial root has no stored settings, so the
defaults are flash, RAW+JPEG and sounds on). A sweep is always 7 frames. Use
--sounds off when nobody should hear the camera.
"""
import argparse
import re
import struct
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tools" / "observe"))
import run_still_trial as rst  # noqa: E402

APP = REPO / "output/camera-app-build/g360-app"
HEADER = 40960  # the DNG header with the shot's metadata (g360_dng_header_full)
FRAME_FILE = HEADER + 33732608
WIDTH, HEIGHT = 5824, 2896


def remote_script(tag: str, btime: str, run: str) -> str:
    stage, card = f"/tmp/g360-sweep-{tag}", f"/opt/storage/sdcard/exp-sweep-{tag}"
    root, flash = f"{card}/G360", f"/opt/usr/g360-exp-sweep-{tag}"
    return f"""set -u
die() {{ echo "REFUSED: $*"; exit 1; }}
exec 9>/tmp/gear360-controls.lock; flock -n 9 || die 'controls lock'
exec 8>/tmp/gear360-screen.lock; flock -n 8 || die 'screen lock'
exec 7>/tmp/gear360-experiments.lock; flock -n 7 || die 'experiments lock'
[ "$(awk '$1 == "btime" {{print $2}}' /proc/stat)" = {btime} ] || die 'boot changed'
s=$(systemctl show -p ActiveState -p MainPID di-camera-app.service)
echo "$s" | grep -qx ActiveState=inactive || die 'stock app is not inactive'
echo "$s" | grep -qx MainPID=0 || die 'stock MainPID is nonzero'
for p in /proc/[0-9]*/comm; do
    case "$(cat "$p" 2>/dev/null)" in g360-app|g360-shot|capture-shot|capture-idle|capture-session*|gear360-screen|gear360-control*|di-camera-app)
        die "client present: $p";;
    esac
done
[ ! -e {card} ] && [ ! -e {flash} ] || die 'trial folders exist'
mkdir -p {root} {flash} || die 'mkdir'
free=$(df -B1 /opt/storage/sdcard | awk 'NR == 2 {{print $4}}')
qfree=$(df -B1 /opt/usr | awk 'NR == 2 {{print $4}}')
echo "free=$free flash_free=$qfree"
echo "START $(cut -d' ' -f1 /proc/uptime)"
{{ {stage}/g360-app --root {root} --free-bytes $((free - 5242880)) --log {root}/state/capture.log \\
    --queue {flash}/queue --queue-free-bytes "$qfree" {run} 2>&1; echo "app_exit=$?"; }} |
    while IFS= read -r line; do echo "$(cut -d' ' -f1 /proc/uptime) $line"; done
echo "END $(cut -d' ' -f1 /proc/uptime) btime=$(awk '$1 == "btime" {{print $2}}' /proc/stat)"
"""


def dng_check(path: Path) -> str:
    data = path.read_bytes()
    assert len(data) == FRAME_FILE, (path, len(data))
    assert data[:4] == b"II*\0", path
    samples = memoryview(data)[HEADER:].cast("H")
    # Mean of a sparse grid over the whole frame (both lenses).
    total = count = 0
    for y in range(8, HEIGHT, 64):
        row = y * WIDTH
        for x in range(8, WIDTH, 64):
            total += samples[row + x]
            count += 1
    return f"{path.name}: {len(data)} bytes, mean {total / count:.1f} (16-bit words, {count} samples)"


def jpeg_check(path: Path) -> str:
    data = path.read_bytes()
    assert data[:4] == b"\xff\xd8\xff\xe1" and data[-2:] == b"\xff\xd9", path  # SOI, our Exif APP1 first, EOI
    try:
        import subprocess
        tags = subprocess.run(["exiftool", "-s3", "-Make", "-Model", "-ImageWidth", "-ImageHeight", "-ExposureTime",
                               "-ISO", "-DateTimeOriginal", str(path)], capture_output=True, text=True).stdout.split("\n")
        exif = ", ".join(t for t in tags if t)
    except OSError:
        exif = "exiftool not available"
    return f"{path.name}: {len(data)} bytes, {exif}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--keep", action="store_true", help="keep the trial photos on the card")
    parser.add_argument("--no-fetch", action="store_true", help="don't fetch frames 1, 4 and 7")
    parser.add_argument("--still", action="store_true", help="one still (--once) instead of a sweep")
    parser.add_argument("--save", choices=("flash", "card"))
    parser.add_argument("--output", choices=("raw", "jpeg", "raw+jpeg"))
    parser.add_argument("--raw", choices=("on", "off"), help=argparse.SUPPRESS)
    parser.add_argument("--sounds", choices=("on", "off"))
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,16}", args.tag):
        parser.error("--tag takes letters, digits, _ and -")
    if args.output and args.raw:
        parser.error("choose --output or the legacy --raw override, not both")
    tag = args.tag
    out = REPO / "output" / f"sweep-trial-{tag}"
    out.mkdir(parents=True, exist_ok=True)
    stage, card, flash = f"/tmp/g360-sweep-{tag}", f"/opt/storage/sdcard/exp-sweep-{tag}", f"/opt/usr/g360-exp-sweep-{tag}"
    pre = rst.sh(out, "00-preflight.txt", "grep btime /proc/stat; systemctl show -p ActiveState -p MainPID "
                 "di-camera-app.service; df -k /opt/usr /opt/storage/sdcard; free; echo CHECKED")
    btime = re.search(r"^btime (\d+)", pre, re.M)
    if not btime or "CHECKED" not in pre or "ActiveState=inactive" not in pre:
        sys.exit("the stock app must be stopped (and the camera reachable); nothing run")
    btime = btime.group(1)
    sha = rst.sha256(APP)
    script = out / "10-run.sh"
    run = ("--once" if args.still else "--sweep-once") + (f" --sweep-save {args.save}" if args.save else "") + (f" --output {args.output}" if args.output else f" --raw {args.raw}" if args.raw else "") + \
        (f" --sounds {args.sounds}" if args.sounds else "")
    script.write_text(remote_script(tag, btime, run))
    rst.sh(out, "01-stage-dir.txt", f"mkdir {stage} && echo MADE")
    rst.ftp_put(APP, f"{stage}/g360-app")
    rst.ftp_put(script, f"{stage}/run.sh")
    staged = rst.sh(out, "02-staged.txt", f"chmod 755 {stage}/g360-app && /usr/bin/openssl dgst -sha256 {stage}/g360-app")
    if f"{stage}/g360-app)= {sha}" not in staged:
        sys.exit("staged build does not match; nothing run")
    rst.sh(out, "11-launch.txt", f"(sh {stage}/run.sh > {stage}/run.out 2>&1; echo DONE >> {stage}/run.out) "
                                 f"> /dev/null 2>&1 & sleep 1; cat {stage}/run.out")
    print(f"{'still' if args.still else 'sweep'} trial {tag}: g360-app {run} (build {sha[:12]})")
    text = ""
    for _ in range(120):
        time.sleep(2)
        try:
            _, text, _ = rst.camsh.run(f"cat {stage}/run.out", 20, boot_id=False)
        except Exception:
            continue
        if re.search(r"^DONE$", text, re.M):
            break
    (out / "12-run.out").write_text(text)
    print(text)
    listing = rst.sh(out, "13-files.txt",
                     f"ls -l {card}/G360/photos {flash}/queue; cat {card}/G360/photos/*-sweep.csv; "
                     f"tail -5 {card}/G360/state/capture.log; df -k /opt/usr")
    print(listing)
    for ext, check in (("dng", dng_check), ("jpg", jpeg_check)):
        names = sorted(set(re.findall(r"(\d{8}-\d{6}-\d{5}\.%s)" % ext, listing)))
        for k in () if args.no_fetch else (0, 3, 6):
            if k < len(names):
                local = out / names[k]
                if rst.ftp_get(f"{card}/G360/photos/{names[k]}", local):
                    print(check(local))
    if not args.keep:
        clean = rst.sh(out, "20-clean.txt", f"rm -f {card}/G360/photos/*.dng {card}/G360/photos/*.jpg && "
                                            f"rmdir {flash}/queue {flash} && "
                                            f"rm -r {stage} && sync && echo CLEANED; df -k /opt/usr")
        print(clean)
    return 0 if "app_exit=0" in text else 1


if __name__ == "__main__":
    sys.exit(main())
