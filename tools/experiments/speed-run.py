#!/usr/bin/env python3
"""Where does a shot's time go? Card speed and back-to-back capture timing.

Unattended, on the camera's current boot. Steps:
  card   dd writes with fsync to a scratch folder on the card (DNG-sized,
         JPEG-sized, 10-bit-packed-DNG-sized, small files), the shutter
         sound's play time; the scratch files are deleted afterwards
  s1     8 back-to-back stills, manual TV96/ISO100, RAW copied and discarded,
         JPEGs copied and discarded: the capture floor
  s2     s1 while dd streams 270 MB to the card in the background
  s2n    s2 with the background writer at nice 19
  s3     4 back-to-back auto stills saving DNG and Exif JPEGs (as the app does)
  emmc   the same dd tests on the internal flash's data partition (/opt/usr):
         8 DNG-sized writes, then each copied to the card; CPU use from
         /proc/stat for one card write and one flash write; all deleted
  s2e    s1 while dd streams 270 MB to /opt/usr in the background, starting
         once g360-shot is running
  s4c    8 back-to-back stills, manual TV96/ISO100, each saved as DNG to the
         card before the next (JPEGs copied and discarded)
  s4e    s4c with the DNGs saved to /opt/usr, then copied to the card
  s5     the HDR sweep's 8 exposures (hdr/web/hdr-capture.sh), RAW and JPEGs
         copied and discarded
  s5e    s5 with each DNG saved to /opt/usr before the next, then copied

Our camera app, if running, is stopped first (SIGTERM after boot, PID and
executable checks). The stock app is never started; if it is running, the
first session stops it once (fresh), the rest reenter.

    python3 tools/experiments/speed-run.py --tag A [--steps card,s1,s2,s3]

Records go to output/speed-<tag>/ (ignored by git). Scratch files on the card
and on /opt/usr are deleted; the session records stay on the card.
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
APP = "/opt/storage/sdcard/mods/g360-app-v1-20261105/g360-app"
SOUND = "/usr/apps/com.samsung.di-camera-app/res/sounds/camera/shutter.wav"
STEPS = {
    "s1": "--frame --count 8 --gap-ms 0 --hold-ms 1000 --frame-output discard --jpeg discard --exposure 96,100",
    "s2": "--frame --count 8 --gap-ms 0 --hold-ms 1000 --frame-output discard --jpeg discard --exposure 96,100",
    "s3": "--frame --count 4 --gap-ms 0 --hold-ms 1000 --frame-output dng --exif on",
}
STEPS["s2n"] = STEPS["s2"]  # s2 with the background writer at nice 19
STEPS["s2e"] = STEPS["s2"]  # s2 writing to /opt/usr
STEPS["s4c"] = "--frame --count 8 --gap-ms 0 --hold-ms 1000 --frame-output dng --jpeg discard --exposure 96,100"
STEPS["s4e"] = STEPS["s4c"]  # s4c saving to /opt/usr
SWEEP = "48,1600:48,400:48,100:80,100:112,100:144,100:176,100:208,100"
STEPS["s5"] = f"--frame --count 8 --gap-ms 0 --hold-ms 1000 --frame-output discard --jpeg discard --exposure {SWEEP}"
STEPS["s5e"] = f"--frame --count 8 --gap-ms 0 --hold-ms 1000 --frame-output dng --jpeg discard --exposure {SWEEP}"
ON_FLASH = ("s4e", "s5e")  # shots saved to /opt/usr
FLASH = "/opt/usr"
STAMP = "$(date +%s.%N) $(head -1 /proc/stat)"  # time and the cpu line
CARD = (  # name, bytes, repeats
    ("dng", 33733196, 3),
    ("jpeg", 4800000, 3),
    ("packed10", 21083468, 2),
)


def boot_state(out: Path, name: str) -> tuple[str, str]:
    pre = rst.sh(out, name, "grep btime /proc/stat; systemctl show -p ActiveState -p MainPID di-camera-app.service; "
                            "for p in /proc/[0-9]*; do [ \"$(cat $p/comm 2>/dev/null)\" = g360-app ] && "
                            "echo APP ${p#/proc/} $(readlink $p/exe); done; echo CHECKED")
    btime = re.search(r"^btime (\d+)", pre, re.M)
    if not btime or "CHECKED" not in pre:
        sys.exit("could not read the camera's state")
    return btime.group(1), pre


def stop_app(out: Path, btime: str, pre: str) -> None:
    for pid, exe in re.findall(r"^APP (\d+) (\S+)$", pre, re.M):
        if exe != APP:
            sys.exit(f"g360-app {pid} runs {exe}, not {APP}; not touched")
        text = rst.sh(out, f"01-stop-app-{pid}.txt",
                      f"[ \"$(awk '$1==\"btime\"{{print $2}}' /proc/stat)\" = {btime} ] && "
                      f"[ \"$(cat /proc/{pid}/comm)\" = g360-app ] && [ \"$(readlink /proc/{pid}/exe)\" = {APP} ] && "
                      f"kill -TERM {pid} && echo SENT")
        if "SENT" not in text:
            sys.exit(f"could not stop g360-app {pid}")
        for _ in range(30):
            time.sleep(1)
            if "GONE" in rst.camsh.run(f"[ -e /proc/{pid} ] && echo ALIVE || echo GONE", 20, boot_id=False)[1]:
                break
        else:
            sys.exit(f"g360-app {pid} did not exit")
        print(f"stopped our app (PID {pid})")


def card(out: Path, tag: str) -> None:
    d = f"/opt/storage/sdcard/exp-speed-{tag}-card"
    lines = [f"mkdir {d} || exit 1", "echo SOUND_START $(date +%s.%N)", f"paplay {SOUND}",
             "echo SOUND_END $(date +%s.%N)"]
    for name, size, repeats in CARD:
        count = -(-size // 65536)  # whole 64 KiB blocks, within 64 KiB of the size
        for i in range(repeats):
            f = f"{d}/{name}-{i}.bin"
            lines += [f"echo WRITE {name} {i} {count * 65536} START $(date +%s.%N)",
                      f"dd if=/dev/zero of={f} bs=65536 count={count} conv=fsync 2>&1 | tail -1",
                      f"echo WRITE {name} {i} END $(date +%s.%N)"]
    for i in range(5):
        lines += [f"echo SMALL {i} START $(date +%s.%N)",
                  f"dd if=/dev/zero of={d}/small-{i}.bin bs=20 count=1 conv=fsync 2>/dev/null",
                  f"echo SMALL {i} END $(date +%s.%N)"]
    lines += [f"ls -l {d}", f"rm -r {d} && echo CLEANED", "sync"]
    script = out / "10-card.sh"
    script.write_text("\n".join(lines) + "\n")
    rst.ftp_put(script, f"/tmp/speed-{tag}-card.sh")
    text = rst.sh(out, "10-card.txt", f"sh /tmp/speed-{tag}-card.sh 2>&1; rm -f /tmp/speed-{tag}-card.sh", timeout=300)
    print(text)


def emmc(out: Path, tag: str) -> None:
    d = f"{FLASH}/g360-exp-speed-{tag}-emmc"
    c = f"/opt/storage/sdcard/exp-speed-{tag}-copy"
    name, size, _ = CARD[0]
    count = -(-size // 65536)
    lines = [f"[ ! -e {d} ] && [ ! -e {c} ] || exit 1", f"mkdir {d} {c} || exit 1", "df -k /opt/usr", "sync"]
    for i in range(8):
        lines += [f"echo FLASH {i} {count * 65536} START {STAMP}",
                  f"dd if=/dev/zero of={d}/{name}-{i}.bin bs=65536 count={count} conv=fsync 2>&1 | tail -1",
                  f"echo FLASH {i} END {STAMP}"]
    lines += [f"echo CARD 0 {count * 65536} START {STAMP}",
              f"dd if=/dev/zero of={c}/zero-0.bin bs=65536 count={count} conv=fsync 2>&1 | tail -1",
              f"echo CARD 0 END {STAMP}", f"rm {c}/zero-0.bin", "sync"]
    for i in range(8):
        lines += [f"echo COPY {i} START {STAMP}",
                  f"dd if={d}/{name}-{i}.bin of={c}/{name}-{i}.bin bs=65536 conv=fsync 2>&1 | tail -1",
                  f"echo COPY {i} END {STAMP}"]
    for i in range(5):
        lines += [f"echo SMALL {i} START $(date +%s.%N)",
                  f"dd if=/dev/zero of={d}/small-{i}.bin bs=20 count=1 conv=fsync 2>/dev/null",
                  f"echo SMALL {i} END $(date +%s.%N)"]
    lines += [f"ls -l {d} {c}", f"rm -r {d} {c} && echo CLEANED", "sync", "df -k /opt/usr"]
    script = out / "30-emmc.sh"
    script.write_text("\n".join(lines) + "\n")
    rst.ftp_put(script, f"/tmp/speed-{tag}-emmc.sh")
    text = rst.sh(out, "30-emmc.txt", f"sh /tmp/speed-{tag}-emmc.sh 2>&1; rm -f /tmp/speed-{tag}-emmc.sh", timeout=600)
    print(text)


def session(out: Path, tag: str, step: str, btime: str, mode: str, hashes: dict, stage: str) -> None:
    run_dir = f"/opt/storage/sdcard/exp-speed-{tag}-{step}"
    bg = f"{FLASH}/g360-exp-speed-{tag}-bg" if step == "s2e" else f"/opt/storage/sdcard/exp-speed-{tag}-bg"
    shots = f"{FLASH}/g360-exp-speed-{tag}-{step}" if step in ON_FLASH else f"{run_dir}/shots"
    env = f"SHOTS_DIR={shots} " if step in ON_FLASH else ""
    background = ""
    if step in ("s2", "s2n", "s2e"):
        nice = "nice -n 19 " if step == "s2n" else ""
        # The flash writer is fast enough to finish before the session's
        # checks are done, so s2e starts writing only once g360-shot runs.
        wait = ("i=0; while ! grep -qx g360-shot /proc/[0-9]*/comm 2>/dev/null && [ $i -lt 120 ]; do "
                "sleep 1; i=$((i + 1)); done; " if step == "s2e" else "")
        background = (f"mkdir {bg} && ({wait}echo BG_START $(date +%s.%N) $(cat /proc/uptime) > {stage}/bg.txt; "
                      f"{nice}dd if=/dev/zero of={bg}/bg.bin bs=1048576 count=270 conv=fsync >> {stage}/bg.txt 2>&1; "
                      f"echo BG_END $(date +%s.%N) $(cat /proc/uptime) >> {stage}/bg.txt) > /dev/null 2>&1 & sleep 1; ")
    rst.sh(out, f"20-{step}-launch.txt",
           f"cd {stage}; rm -f {stage}/launch-{step}.out; {background}"
           f"({env}DISPLAY_SHA256={hashes['gear360-display']} DISPLAY_TEXT='SPEED TEST' "
           f"sh ./run-session.sh {mode} {btime} {run_dir} {hashes['g360-shot']} {STEPS[step]} "
           f"> {stage}/launch-{step}.out 2>&1; echo exit=$? >> {stage}/launch-{step}.out) > /dev/null 2>&1 & "
           f"sleep 1; cat {stage}/launch-{step}.out")
    print(f"{step}: running")
    for _ in range(150):
        time.sleep(2)
        try:
            _, text, _ = rst.camsh.run(f"cat {stage}/launch-{step}.out", 20, boot_id=False)
        except Exception:
            continue
        if re.search(r"^exit=\d+", text, re.M):
            break
    if step in ("s2", "s2n", "s2e"):
        for _ in range(60):
            _, text, _ = rst.camsh.run(f"cat {stage}/bg.txt", 20, boot_id=False)
            if "BG_END" in text:
                break
            time.sleep(2)
    text = rst.sh(out, f"21-{step}-result.txt",
                  f"cat {stage}/launch-{step}.out; echo ---; cat {run_dir}/session.txt; echo ---; "
                  f"cat {stage}/bg.txt 2>/dev/null; rm -f {stage}/bg.txt; ls -l {shots} | head -20; "
                  f"[ -d {bg} ] && rm -r {bg} && echo BG_CLEANED")
    if step in ON_FLASH:
        copy = f"/opt/storage/sdcard/exp-speed-{tag}-{step}-copy"
        text += rst.sh(out, f"22-{step}-copy.txt",
                       f"mkdir {copy} && for f in {shots}/*.dng; do echo COPY $f START {STAMP}; "
                       f"dd if=$f of={copy}/${{f##*/}} bs=65536 conv=fsync 2>&1 | tail -1; echo COPY END {STAMP}; done; "
                       f"ls -l {copy}; rm -r {copy} && echo COPY_CLEANED", timeout=600)
    if step in ("s3", "s4c", *ON_FLASH):
        text += rst.sh(out, f"23-{step}-clean.txt", f"rm -r {shots} && echo SHOTS_CLEANED; sync")
    for line in text.splitlines():
        if line.startswith("metric,") or re.match(r"(REFUSED|probe_exit|session_exit|BG_|shots=|COPY|SHOTS_)", line):
            print("  " + line)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--steps", default="card,s1,s2,s3")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,16}", args.tag):
        parser.error("--tag takes letters, digits, _ and -")
    steps = args.steps.split(",")
    if any(s not in ("card", "emmc", *STEPS) for s in steps):
        parser.error("--steps takes card, emmc, " + ", ".join(STEPS))
    out = REPO / "output" / f"speed-{args.tag}"
    out.mkdir(parents=True, exist_ok=True)
    btime, pre = boot_state(out, "00-preflight.txt")
    stop_app(out, btime, pre)
    if "card" in steps:
        card(out, args.tag)
    if "emmc" in steps:
        emmc(out, args.tag)
    captures = [s for s in steps if s in STEPS]
    if not captures:
        return 0
    hashes = {f.name: rst.sha256(f) for f in (PROBE, DISPLAY, SESSION)}
    stage = f"/tmp/speed-{args.tag}"
    for f in (PROBE, DISPLAY, SESSION):
        rst.ftp_put(f, f"{stage}/{f.name}")
    staged = rst.sh(out, "02-staged.txt",
                    f"chmod 755 {stage}/g360-shot {stage}/gear360-display {stage}/run-session.sh; "
                    f"/usr/bin/openssl dgst -sha256 {stage}/g360-shot {stage}/gear360-display {stage}/run-session.sh")
    for name, h in hashes.items():
        if f"{stage}/{name})= {h}" not in staged:
            sys.exit(f"staged {name} does not match its local hash; nothing run")
    for step in captures:
        now, state = boot_state(out, f"03-before-{step}.txt")
        if now != btime:
            sys.exit("the camera rebooted; stopping")
        mode = "fresh" if "ActiveState=active" in state else "reenter"
        session(out, args.tag, step, btime, mode, hashes, stage)
    rst.sh(out, "90-cleanup.txt", f"rm -r {stage} && echo STAGE_CLEANED; grep btime /proc/stat")
    return 0


if __name__ == "__main__":
    sys.exit(main())
