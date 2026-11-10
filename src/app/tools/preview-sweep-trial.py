#!/usr/bin/env python3
"""Attended trial: does using the live preview make the next RAW sweep die?

On 7 November 2026 the camera switched itself off while saving frame 6 of a
RAW sweep (HDR 6, flash first) taken about a minute after the live preview
had been started and stopped (output/sweep-death-20261107/). This trial
drives the running g360-app the way the remote page does and records the
camera while it works:

  A. one sweep on a boot where the preview has not been used yet (baseline);
  B. ROUNDS times: preview on with one viewer for --preview-seconds, preview
     off, --rest-seconds of rest, then one sweep.

It stops at the first death. Throughout, it polls `cmd=state` every 0.7 s
(as the page does) and runs `rtoslog follow ... stats` on the camera, which
streams the RTOS log, kernel messages, a 100 ms Linux heartbeat and memory/
battery/CPU-clock readings every second over UDP to this Mac (nothing is
written to the card, so the last records before a power cut survive).

It needs the app at READY, the stock app stopped and the Mac on the camera's
network. It changes the app's settings for the trial (HDR, RAW on,
flash first, no delay) and puts them back afterwards. The sweeps' frames go
to the user's G360/photos like any sweep; --delete-frames removes the
trial's own sweeps (by their timestamps) at the end.

    python3 src/app/tools/preview-sweep-trial.py --tag 20261107a [--rounds 3]
        [--preview-seconds 60] [--rest-seconds 25] [--skip-baseline] [--sounds on|off]
        [--delete-frames]
    python3 src/app/tools/preview-sweep-trial.py --tag 20261107a --collect
        (after a death and the user's power-on: fetch the logs and summarise)

Output: output/preview-sweep-trial-TAG/ (net-rtoslog.txt, state.jsonl,
stats.csv, app/capture log tails, SUMMARY.md).
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
RTOSLOG = REPO / "output/observe-build/rtoslog"
NET_PORT = 5701
APP_LOG = "/opt/storage/sdcard/G360/state/app.log"
CAPTURE_LOG = "/opt/storage/sdcard/G360/state/capture.log"
PHOTOS = "/opt/storage/sdcard/G360/photos"
QUEUE = "/opt/usr/g360-app/queue"
POLL_S = 0.7          # the remote page's state poll
DEAD_AFTER_S = 15.0   # no answer for this long during a sweep: the camera died
SWEEP_TIMEOUT_S = 180.0
TRIAL_SETTINGS = {"mode": "hdr", "output": "raw", "save": "flash", "delay": "0"}
STAMP = re.compile(r"\d{8}-\d{6}")


# ---- Pure parsing (host-tested) --------------------------------------------

def preview_used_this_boot(app_log: str, btime: str) -> bool:
    """True when the app log has a preview start after this boot's first launch."""
    lines = app_log.splitlines()
    first = None
    for i, line in enumerate(lines):
        if line.startswith("launch ") and f"btime={btime} " in line + " ":
            first = i
            break
    if first is None:
        return True  # this boot's launch is not in the log: assume the worst
    return any("APP PREVIEW camera start" in line for line in lines[first:])


def sweep_lines(app_log: str, stamp: str) -> list:
    """The app log's lines of one sweep, from its start line to the next launch
    or the line after `SWEEP SAVED`/`SWEEP MOVED`."""
    out, inside = [], False
    for line in app_log.splitlines():
        if line.startswith(f"APP SWEEP {stamp}: "):
            inside = True
        elif inside and line.startswith("launch "):
            out.append(line)
            break
        if inside:
            out.append(line)
            if line.startswith(f"APP SWEEP SAVED {stamp}") or line.startswith(f"APP SWEEP MOVED {stamp}"):
                break
    return out


def frame_times(lines: list) -> list:
    """[(frame, ms since the first capture)] from `APP SWEEP FRAME` lines."""
    out = []
    for line in lines:
        m = re.match(r"APP SWEEP FRAME (\d+) .* at (\d+) ms$", line)
        if m:
            out.append((int(m.group(1)), int(m.group(2))))
    return out


def capture_frames(capture_log: str) -> list:
    """Per-capture timing from g360cap's log: start, arming, capture and the
    save gap before it (seconds), and the span of the buffer releases."""
    rows, cur, prev_done = [], None, None
    for line in capture_log.splitlines():
        m = re.match(r"g360cap: monotonic=([\d.]+) (.*)", line)
        if not m:
            continue
        t, s = float(m.group(1)), m.group(2)
        if s.startswith("open: loading"):
            prev_done = None
        if s.startswith("user attribute") and "a=0x0 b=0x3" in s:
            cur = {"start": t, "gap": (t - prev_done) if prev_done is not None else None, "rel": []}
        elif cur is not None and s.startswith("frame: armed"):
            cur["armed"] = t
        elif cur is not None and s.startswith("capture: still request"):
            cur["req"] = t
        elif cur is not None and s.startswith("ReleaseCaptureBuffer"):
            cur["rel"].append(t)
        elif cur is not None and s.startswith("frame: result"):
            rel = cur["rel"]
            rows.append({"start": cur["start"], "save_gap": cur["gap"],
                         "arm": cur.get("armed", cur["start"]) - cur["start"],
                         "capture": t - cur.get("req", t),
                         "release_span": rel[-1] - rel[0] if len(rel) > 1 else 0.0})
            prev_done, cur = t, None
    return rows


def parse_net(data: bytes) -> dict:
    """Heartbeats, stats records and kernel records from rtoslog's stream."""
    text = data.decode("utf-8", "replace")
    beats = [int(x) for x in re.findall(r"\n@@HB (\d+)\n", text)]
    gaps = [b - a for a, b in zip(beats, beats[1:])]
    stats = []
    for m in re.finditer(r"\n@@S (\d+)\n(.*?)(?=\n@@|\Z)", text, re.S):
        body, row = m.group(2), {"us": int(m.group(1))}
        for key in ("MemFree", "Cached", "Dirty", "Writeback", "AnonPages", "Slab"):
            k = re.search(rf"^{key}:\s+(\d+) kB", body, re.M)
            if k:
                row[key] = int(k.group(1))
        for name, key in (("loadavg", "load"), ("battery/temp", "temp"), ("voltage_now", "voltage"),
                          ("current_now", "current"), ("capacity", "capacity"), ("acpu/rate", "cpu_hz"),
                          ("mbus/rate", "mbus_hz"), ("ep_a/rate", "ep_hz"), ("ddrfreq/ch_a", "ddr_hz")):
            k = re.search(rf"{re.escape(name)} (-?[\d.]+)", body)
            if k:
                row[key] = float(k.group(1)) if "." in k.group(1) else int(k.group(1))
        stats.append(row)
    kernel = re.findall(r"\n@@K (\d+) ([^\n]*)", text)
    return {"beats": beats, "max_gap_us": max(gaps) if gaps else 0,
            "last_beat_us": beats[-1] if beats else None, "stats": stats,
            "kernel": [(int(t), r) for t, r in kernel], "rtos_bytes": len(text)}


STATS_COLUMNS = ["us", "MemFree", "Cached", "Dirty", "Writeback", "AnonPages", "Slab",
                 "load", "temp", "voltage", "current", "capacity", "cpu_hz", "mbus_hz", "ep_hz", "ddr_hz"]


def stats_csv(stats: list) -> str:
    lines = [",".join(STATS_COLUMNS)]
    for row in stats:
        lines.append(",".join(str(row.get(c, "")) for c in STATS_COLUMNS))
    return "\n".join(lines) + "\n"


# ---- Camera access ------------------------------------------------------------

def camsh():
    sys.path.insert(0, str(REPO / "tools" / "observe"))
    import camsh as module  # finds the camera's address on import
    return module


def ask(host: str, query: str, timeout: float = 5.0):
    url = f"http://{host}:8888/cgi-bin/g360?{query}&_={int(time.time() * 1000)}"
    r = subprocess.run(["curl", "-s", "-m", f"{timeout:g}", url], capture_output=True)
    if r.returncode != 0:
        return None
    try:
        return json.loads(r.stdout.decode("utf-8", "replace"))
    except ValueError:
        return None


class Poller(threading.Thread):
    """Polls cmd=state like the page and remembers when the camera last answered."""

    def __init__(self, host: str, out: Path):
        super().__init__(daemon=True)
        self.host, self.file = host, open(out, "a")
        self.latest, self.last_ok, self.stop = None, time.monotonic(), threading.Event()

    def run(self):
        while not self.stop.is_set():
            s = ask(self.host, "cmd=state", timeout=3.0)
            now = time.monotonic()
            if s is not None and s.get("app") == "on":
                self.latest, self.last_ok = s, now
            self.file.write(json.dumps({"t": time.time(), "state": s}) + "\n")
            self.file.flush()
            self.stop.wait(POLL_S)

    def silent_for(self) -> float:
        return time.monotonic() - self.last_ok


def mac_address_toward(host: str) -> str:
    route = subprocess.run(["route", "-n", "get", host], capture_output=True, text=True).stdout
    m = re.search(r"interface:\s*(\S+)", route)
    if not m:
        return ""
    ip = subprocess.run(["ipconfig", "getifaddr", m.group(1)], capture_output=True, text=True).stdout.strip()
    return ip if re.fullmatch(r"\d+\.\d+\.\d+\.\d+", ip) else ""


def log(out_dir: Path, text: str) -> None:
    line = f"{time.strftime('%H:%M:%S')} {text}"
    print(line, flush=True)
    with open(out_dir / "trial.log", "a") as f:
        f.write(line + "\n")


def send(host: str, query: str):
    s = ask(host, query)
    if s is None or not s.get("ok", False):
        raise RuntimeError(f"{query}: {s}")
    return s


def wait_state(poller: Poller, want, timeout: float, dead_after: float = DEAD_AFTER_S) -> str:
    """Waits until the state is one of WANT; returns it, or 'dead'/'timeout'."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        s = poller.latest
        if s and s.get("state") in want:
            return s["state"]
        if poller.silent_for() > dead_after:
            return "dead"
        time.sleep(0.2)
    return "timeout"


def sweep(host: str, poller: Poller, out_dir: Path, label: str) -> str:
    """One sweep by remote Capture (HDR mode). Returns 'ok', 'dead' or a failure."""
    log(out_dir, f"{label}: sweep starts")
    poller.latest = None
    send(host, "cmd=capture")
    seen = wait_state(poller, ("sweeping", "saving", "error"), 30.0)
    if seen in ("dead", "timeout", "error"):
        return seen
    done = wait_state(poller, ("ready", "error"), SWEEP_TIMEOUT_S)
    log(out_dir, f"{label}: sweep ended: {done}")
    return "ok" if done == "ready" else done


def preview_cycle(host: str, poller: Poller, out_dir: Path, label: str, seconds: int) -> str:
    log(out_dir, f"{label}: preview on, one viewer for {seconds} s")
    send(host, "cmd=preview&on=1")
    if wait_state_preview(poller, ("on",), 15.0) != "on":
        return "preview did not start"
    mjpg = out_dir / f"{label}-preview.mjpg"
    subprocess.run(["curl", "-s", "-m", str(seconds), "-o", str(mjpg),
                    f"http://{host}:8090/preview.mjpg"])
    size = mjpg.stat().st_size if mjpg.exists() else 0
    mjpg.unlink(missing_ok=True)  # only its size matters
    log(out_dir, f"{label}: viewer got {size} bytes; preview off")
    send(host, "cmd=preview&on=0")
    if wait_state_preview(poller, ("off",), 15.0) != "off":
        return "preview did not stop"
    return "ok"


def wait_state_preview(poller: Poller, want, timeout: float) -> str:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        s = poller.latest
        if s and s.get("preview", {}).get("state") in want:
            return s["preview"]["state"]
        if poller.silent_for() > DEAD_AFTER_S:
            return "dead"
        time.sleep(0.2)
    return "timeout"


def start_listener(out_dir: Path):
    f = open(out_dir / "net-rtoslog.txt", "ab")
    return subprocess.Popen(["/usr/bin/nc", "-u", "-l", str(NET_PORT)], stdout=f,
                            stderr=subprocess.DEVNULL), f


def stop_listener(listener) -> None:
    proc, f = listener
    time.sleep(1)
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
    f.close()


def collect(cs, out_dir: Path, stamps: list, died: bool) -> None:
    """Fetches the log tails and writes SUMMARY.md."""
    try:
        _, app_log, _ = cs.run(f"tail -n 600 {APP_LOG}", 30, boot_id=False)
        _, cap_log, _ = cs.run(f"tail -n 1500 {CAPTURE_LOG}", 30, boot_id=False)
    except Exception as e:  # camera still off
        log(out_dir, f"logs not fetched ({e}); run again with --collect after the camera is on")
        app_log = cap_log = ""
    if app_log:
        (out_dir / "app-log-tail.txt").write_text(app_log)
        (out_dir / "capture-log-tail.txt").write_text(cap_log)
    stamps = stamps or sorted(set(re.findall(r"^APP SWEEP (\d{8}-\d{6}): ", app_log, re.M)))
    net = parse_net((out_dir / "net-rtoslog.txt").read_bytes() if (out_dir / "net-rtoslog.txt").exists() else b"")
    (out_dir / "stats.csv").write_text(stats_csv(net["stats"]))
    lines = [f"# Preview-then-sweep trial {out_dir.name}", "",
             f"Camera died: {'yes' if died else 'no'}.", "",
             f"rtoslog stream: {net['rtos_bytes']} bytes, {len(net['beats'])} heartbeats, "
             f"longest heartbeat gap {net['max_gap_us'] / 1000:.0f} ms, last heartbeat at "
             f"{(net['last_beat_us'] or 0) / 1e6:.1f} s, {len(net['stats'])} stats records.", ""]
    if net["stats"]:
        last = net["stats"][-1]
        lines += ["Last stats record: " + ", ".join(f"{k} {v}" for k, v in last.items()), ""]
    for stamp in stamps:
        sl = sweep_lines(app_log, stamp)
        times = frame_times(sl)
        steps = [b - a for (_, a), (_, b) in zip([(0, 0)] + times, times)]
        lines += [f"## Sweep {stamp}", "", "Frame times (ms since the first capture): "
                  + ", ".join(f"{f}: {t}" for f, t in times),
                  "Per frame (ms): " + ", ".join(str(s) for s in steps), ""]
        end = [line for line in sl if "SAVED" in line or "MOVED" in line or line.startswith("launch ")]
        lines += [f"    {line}" for line in end] + [""]
    rows = capture_frames(cap_log)
    if rows:
        lines += ["## Capture library timing (last 20 captures)", "",
                  "| start | save gap s | arm s | capture s | release span s |", "|---|---|---|---|---|"]
        for r in rows[-20:]:
            gap = f"{r['save_gap']:.2f}" if r["save_gap"] is not None else "-"
            lines.append(f"| {r['start']:.1f} | {gap} | {r['arm']:.2f} | {r['capture']:.2f} | {r['release_span']:.2f} |")
        lines.append("")
    (out_dir / "SUMMARY.md").write_text("\n".join(lines))
    log(out_dir, f"summary: {out_dir / 'SUMMARY.md'}")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--tag", required=True)
    p.add_argument("--rounds", type=int, default=3)
    p.add_argument("--preview-seconds", type=int, default=60)
    p.add_argument("--rest-seconds", type=int, default=25)
    p.add_argument("--skip-baseline", action="store_true")
    p.add_argument("--sounds", choices=("on", "off"),
                   help="the app's sounds during the trial (default: leave as set)")
    p.add_argument("--delete-frames", action="store_true")
    p.add_argument("--collect", action="store_true")
    a = p.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", a.tag):
        p.error("bad tag")
    if a.sounds:
        TRIAL_SETTINGS["sounds"] = a.sounds
    out_dir = REPO / f"output/preview-sweep-trial-{a.tag}"
    out_dir.mkdir(parents=True, exist_ok=True)
    cs = camsh()
    host = cs.HOST
    if a.collect:
        collect(cs, out_dir, [], died=(out_dir / "DIED").exists())
        before = out_dir / "settings-before.json"
        s = ask(host, "cmd=state")
        if before.exists() and s and s.get("app") == "on" and s.get("state") == "ready":
            old = json.loads(before.read_text())
            for key in ("mode", "output", "raw", "save", "delay", "sounds"):
                if key in old and str(s["settings"].get(key)) != str(old[key]):
                    send(host, f"cmd=set&{key}={old[key]}")
            log(out_dir, "settings restored")
        return 0

    # Preconditions.
    mac = mac_address_toward(host)
    if not mac:
        print("no address of this Mac toward the camera", file=sys.stderr)
        return 2
    s = ask(host, "cmd=state")
    if not s or s.get("app") != "on" or s.get("state") != "ready":
        print(f"the app is not at READY: {s}", file=sys.stderr)
        return 2
    _, pre, _ = cs.run("awk '$1==\"btime\"{print $2}' /proc/stat; "
                       "systemctl show -p ActiveState di-camera-app.service; "
                       f"ls {QUEUE} | wc -l; cat /proc/uptime", 20, boot_id=False)
    btime, active, queued, uptime = pre.split("\n")[:4]
    if active.strip() != "ActiveState=inactive":
        print(f"stock app not stopped: {active}", file=sys.stderr)
        return 2
    if queued.strip() != "0":
        print("the flash queue is not empty", file=sys.stderr)
        return 2
    _, app_log, _ = cs.run(f"cat {APP_LOG}", 30, boot_id=False)
    used = preview_used_this_boot(app_log, btime.strip())
    if used and not a.skip_baseline:
        print("the preview was already used on this boot: the baseline needs a boot without it "
              "(or pass --skip-baseline)", file=sys.stderr)
        return 2
    saved = {k: str(v) for k, v in s["settings"].items() if k in TRIAL_SETTINGS}
    log(out_dir, f"camera {host} btime {btime.strip()} uptime {uptime.split()[0]} s, Mac {mac}; "
                 f"settings before: {saved}")
    (out_dir / "settings-before.json").write_text(json.dumps(s["settings"]))

    # The recorder.
    digest = hashlib.sha256(RTOSLOG.read_bytes()).hexdigest()
    stage = f"/tmp/g360-pst-{a.tag}"
    subprocess.run(["curl", "-s", "-m", "60", "--ftp-create-dirs", "-T", str(RTOSLOG),
                    f"ftp://root:@{host}{stage}/rtoslog"], check=True)
    listener = start_listener(out_dir)
    time.sleep(0.5)
    _, started, _ = cs.run(
        f"cd {stage} && chmod +x rtoslog && /usr/bin/openssl dgst -sha256 rtoslog | grep -q {digest} "
        f"|| {{ echo HASH_MISMATCH; exit 1; }}; cat /proc/uptime; "
        f"(./rtoslog follow - 3600 10 {mac}:{NET_PORT} stats > rc.txt 2>&1 & echo $! > pid.txt); "
        f"sleep 1; cat pid.txt", 30, boot_id=False)
    if "HASH_MISMATCH" in started:
        stop_listener(listener)
        print("rtoslog hash mismatch on the camera", file=sys.stderr)
        return 2
    log(out_dir, f"rtoslog running (camera uptime at start, pid): {' '.join(started.split())}")

    poller = Poller(host, out_dir / "state.jsonl")
    poller.start()
    result, died, before_stamps = "ok", False, set(re.findall(r"^APP SWEEP (\d{8}-\d{6}): ", app_log, re.M))
    try:
        for key, value in TRIAL_SETTINGS.items():
            if saved.get(key) != value:
                send(host, f"cmd=set&{key}={value}")
        steps = ([] if a.skip_baseline else [("A", False)]) + [(f"B{i + 1}", True) for i in range(a.rounds)]
        for label, with_preview in steps:
            if with_preview:
                result = preview_cycle(host, poller, out_dir, label, a.preview_seconds)
                if result != "ok":
                    break
                log(out_dir, f"{label}: rest {a.rest_seconds} s")
                time.sleep(a.rest_seconds)
            result = sweep(host, poller, out_dir, label)
            if result != "ok":
                break
            time.sleep(5)
        died = result == "dead"
        log(out_dir, f"trial ended: {result}")
    finally:
        poller.stop.set()
        if died:
            (out_dir / "DIED").write_text(time.strftime("%Y-%m-%d %H:%M:%S\n"))
            time.sleep(10)  # the stream's last datagrams
            stop_listener(listener)
            log(out_dir, "the camera stopped answering. Turn it on with Power, wait for READY, then run "
                         "this script again with --collect")
        else:
            try:
                cs.run(f"kill $(cat {stage}/pid.txt) 2>/dev/null; sleep 0.5; cat {stage}/rc.txt; rm -rf {stage}",
                       20, boot_id=False)
            finally:
                stop_listener(listener)
            for key, value in saved.items():
                if TRIAL_SETTINGS.get(key) != value:
                    send(host, f"cmd=set&{key}={value}")
            log(out_dir, "settings restored")
    if died:
        return 3
    _, app_log, _ = cs.run(f"cat {APP_LOG}", 30, boot_id=False)
    stamps = sorted(set(re.findall(r"^APP SWEEP (\d{8}-\d{6}): ", app_log, re.M)) - before_stamps)
    (out_dir / "stamps.txt").write_text("\n".join(stamps) + "\n")
    collect(cs, out_dir, stamps, died=False)
    if a.delete_frames and stamps:
        assert all(STAMP.fullmatch(x) for x in stamps)
        cs.run("; ".join(f"rm -f {PHOTOS}/{x}-*" for x in stamps) + "; sync", 60, boot_id=False)
        log(out_dir, f"deleted the trial's sweeps: {' '.join(stamps)}")
    return 0 if result == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
