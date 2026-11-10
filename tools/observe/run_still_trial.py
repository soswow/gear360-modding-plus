#!/usr/bin/env python3
"""One-command still trial with full recording (ACTIVE_PLAN step 4f onward).

    python3 run_still_trial.py run --tag step4f --probe output/stock-like-gyro-build/capture-shot
    python3 run_still_trial.py collect --tag step4f     # after a power cycle
    python3 run_still_trial.py run --tag step4g --strace  # also trace our probe
    python3 run_still_trial.py run --tag step4h --keep-app  # stock app keeps running
    python3 run_still_trial.py run --tag step4i --busy      # CPU kept busy, app stopped
    python3 run_still_trial.py run --tag t --keep-app --pagewatch  # also watch shared RTOS memory
    python3 run_still_trial.py run --tag t --ddrwatch               # sample the DDR rate
    python3 run_still_trial.py run --tag t --keep-app --freeze-ipc  # stock app runs, its IPC threads stopped
    python3 run_still_trial.py run --tag t --keep-app --freeze-threads MCB_Receiver,MCB_Sender
    python3 run_still_trial.py run --tag t --keep-app --freeze-ipc --freeze-mode after-gyro
        # stock library answers the gyro request, then its IPC threads stop
    python3 run_still_trial.py run --tag t --keep-app --freeze-ipc --freeze-mode until-gyro
    python3 run_still_trial.py run --tag t --keep-app --freeze-ipc --freeze-mode after-gyro-adjust
        # stock IPC threads stopped until our library has answered, then released
    python3 run_still_trial.py run --tag t --keep-app --focused-watch   # still-expansion ring watch
    python3 run_still_trial.py run --tag t --irqwatch           # image-engine interrupts on Linux
    python3 run_still_trial.py run --tag t --irqwatch --regwatch  # plus image-engine top registers
    python3 run_still_trial.py run --tag t --parity   # probe started like the stock app (cwd /, its env, setsid)
    python3 run_still_trial.py run --tag t --second-client output/stock-like-suppress-build/capture-shot
        # a second library client (no-still build) is initialized before our still
    python3 run_still_trial.py run --tag step4z1 --probe output/g360cap-build/g360-shot --g360 1 --net only
        # the g360cap library: N stills, images saved on the card and fetched
    python3 run_still_trial.py run --tag step5a --probe output/g360cap-build/g360-shot --g360 1 \
        --g360-quality raw-superfine --net only
        # plan step 5: RAW quality set by our library, set back to superfine after
    python3 run_still_trial.py run --tag step5b --probe output/g360cap-build/g360-shot --g360 1 \
        --g360-quality raw-superfine --g360-drive wbbrk --g360-sets 4 --net only
        # white-balance bracket at a RAW quality; up to 4 image sets copied
    python3 run_still_trial.py run --tag step5c --probe output/g360cap-build/g360-shot --g360 1 \
        --g360-quality raw-superfine --g360-capture-type wbb --g360-sets 4 --net only
        # the bracket requested as its own capture type (step 5b: the request sets the drive)
    python3 run_still_trial.py run --tag step5d --probe output/g360cap-build/g360-shot --g360 1 \
        --g360-quality raw-superfine --g360-capture-type wbb --g360-bracket-words 0,0,0x1a0000,1 \
        --g360-sets 4 --net only
        # with the WB bracket words libcapture requires (step 5c: AB3)
    python3 run_still_trial.py run --tag step6a --probe output/g360cap-build/g360-shot --g360 2 \
        --g360-exposure 96,100 --net only
        # manual exposure (TV 96 = 1/64 s, ISO 100), two stills; automatic exposure set back after
    python3 run_still_trial.py run --tag expoE3 --probe output/g360cap-build/g360-shot --g360 1 \
        --g360-quality raw-superfine --g360-exposure -37,100 --g360-ltnr on --g360-set-timeout-ms 90000 --net only
        # 5 s at RAW quality with a 90 s wait (step E2 stalled or was slower than 15 s)
    python3 run_still_trial.py run --tag rawR1 --probe output/g360cap-build/g360-shot --g360 1 --g360-raw --net only
        # a normal still plus its RAW frame copied from capture memory (0xa6000000; R1 build)
    python3 run_still_trial.py run --tag rawQ1 --probe output/g360cap-build/g360-shot --g360 1 --g360-raw \
        --g360-quickview holdzoom --net only
        # quick view HOLDZOOM (live view should stay off) and both per-lens RAW images;
        # the camera's previous quick-view value is read first and set back after
    python3 run_still_trial.py run --tag rawQ2 --probe output/g360cap-build/g360-shot --g360 1 --g360-raw \
        --g360-raw-layout 0xa6000000,5824,2896,11648,16 --g360-raw-layout 0xad134000,2896,2896,5824,16 \
        --g360-quickview holdzoom --net only
        # the same, copying the 0xa6000000 region (full RAW frame or a picture?) and lens 2

`run` does, in order:
  1. preflight: uptime/btime, stock-app state, card free space, library
     hashes, the device-plugin sysfs switches, the gyro history properties;
  2. stages rtoslog, ipccrec, fsynctee, accelprops and the probe in
     /tmp/obs-<tag>/ and checks their sha256 on the camera;
  3. starts the recorders on the SD card (/opt/storage/sdcard/obs-<tag>/):
     RTOS log + Linux heartbeat + /dev/kmsg, ring A/B messages, and the Linux
     dlog through fsynctee, all synced after every write;
  4. stops the stock app gracefully (never restarts it: a restarted app
     enters boot-snapshot creation);
  5. runs the probe with its log on the SD card;
  6. watches the camera for 45 s. Alive: fetches and summarises. Dead: tells
     you to power-cycle and run `collect`.

Records land in output/decompile/rea/pass-<tag>/ (ignored except Markdown);
summary.md is written there. This script needs the user present: the still
can power the camera off.
"""

import argparse
import hashlib
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import camsh  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
BUILD = REPO / "output/observe-build"
HOST = camsh.HOST  # the access point or the home network, whichever answers (camsh.py)
TOOLS = ["rtoslog", "ipccrec", "fsynctee", "accelprops", "cpuhog", "pagewatch", "ptstop", "irqwatch", "regwatch"]
# The stock app's library IPC threads (step 4c thread list). Stopping them
# makes our library answer every RTOS request while the rest of the app runs.
IPC_THREADS = ["udd_ipcc_fmt_in", "MCB_Receiver", "MCB_Sender"]
# Shared windows the stock app maps (step 4i2; from libprefman) plus the RTOS
# pages our library maps while answering adjust and gyro requests (4g strace).
# Fast scanner: small prefman windows and the RTOS request pages (~5 ms per
# pass). Slow scanner: RTOSPREF and the 1 MB prefman window (~150 ms).
PAGEWATCH_FAST = ["0x85480000:0x5000", "0x854e0000:0xc000", "0x85580000:0x1000",
                  "0x85600000:0x1000", "0x85c20000:0x1000", "0x85c40000:0x1000",
                  "0x85c60000:0x1000", "0x81475000:0x1000", "0x8068b000:0x1000",
                  "0x8078d000:0x1000", "0x82066000:0x1000", "0xaa000000:0x1000"]
PAGEWATCH_SLOW = ["0x85400000:0x81000", "0x85c61000:0xff000"]
# Focused RTOSPREF watch (3 Nov): still-expansion ring slots (576 bytes from
# 0x85430e0c), its position bytes at 0x85401c50, flags 0x85401c4c and
# 0x854342f8, the 0x854002xx block, image-set descriptors and the prefman
# counter 0x85580200. Small enough for sub-millisecond passes.
# Linux's drime5_ep lines (46 core A, 47 core B, 48 DMA, 50 acc by resource
# order), the IPCC interrupt (142) and d5_cmdq (159); and the kernel page that
# holds the drime5_ep interrupt masks (VA 0xc084ece4-0xc084ecf4).
EP_IRQS = ["46", "47", "48", "50", "142", "159"]
EP_MASK_PAGE = ["0x8684e000:0x1000"]
# Image-engine top registers, read only (no JPEG-block register): clock gating
# 0x20821020/24, acc interrupt enable 0x20821080, acc status 0x208210d8,
# read/write DMA muxes 0x20821108/0c.
JPEG_REGS = ["0x2082a000", "0x2082a280", "0x2082a800"]
EP_TOP_REGS = ["0x20821020", "0x20821024", "0x20821080", "0x208210d8", "0x20821108", "0x2082110c"]
FOCUSED_WATCH = ["0x85400000:0x1000", "0x85401000:0x1000", "0x85430000:0x2000",
                 "0x85434000:0x1000", "0x8541b000:0x1000", "0x85580000:0x1000"]
PLUGIN_NODES = [
    "/sys/devices/platform/drime5-ispfreq/ep_lock",
    "/sys/devices/platform/drime5-ispfreq/bus_boost",
    "/sys/devices/platform/drime5-ispfreq/dvfs_use",
    "/sys/devices/platform/drime5-ispfreq/scen",
    "/sys/devices/platform/drime5-clock/ep_a/rate",
    "/sys/devices/system/cpu/cpufreq/pmqos/cpufreq_min",
    "/sys/devices/system/cpu/cpufreq/pmqos/cpufreq_max",
    "/sys/class/accelerometer/accel/stream_status",
    "/sys/devices/platform/drime5-ddrfreq/ch_a",
]
DDR_RATE = "/sys/devices/platform/drime5-ddrfreq/ch_a"
# Text only the SUPPRESS_THE_STILL probe build contains; the second client must
# never be able to send a still itself.
SUPPRESS_MARK = b"still command suppressed by build variant"
# Text only the g360cap command-line program contains.
G360_MARK = b"g360-shot: open: %s"


def valid_raw_layout(text: str) -> bool:
    """Match g360-shot's RAW limits before staging or stopping the app."""
    if not re.fullmatch(r"(0x[0-9a-fA-F]+|\d+)(,(0x[0-9a-fA-F]+|\d+)){4}", text):
        return False
    physical, width, height, stride, bits = [
        int(x, 16 if x.startswith("0x") else 10) for x in text.split(",")]
    size = stride * height
    return (all(0 < x <= 0xffffffff for x in (physical, width, height, stride, bits))
            and bits <= 16 and width <= stride // 2
            and size <= 96 * 1024 * 1024 and physical + size <= 0xffffffff)


def sh(out_dir: Path, name: str, command: str, timeout: float = 60.0) -> str:
    boot, text, elapsed = camsh.run(command, timeout)
    (out_dir / name).write_text(f"# command: {command}\n# elapsed_s: {elapsed:.2f}\n# boot_id: {boot}\n{text}\n")
    return text


def ftp_put(local: Path, remote: str) -> None:
    subprocess.run(["curl", "-s", "-m", "60", "--ftp-create-dirs", "-T", str(local),
                    f"ftp://root:@{HOST}{remote}"], check=True)


def ftp_get(remote: str, local: Path) -> bool:
    # Long enough for a 34 MB RAW frame over the camera's Wi-Fi.
    r = subprocess.run(["curl", "-s", "-m", "300", "-o", str(local), f"ftp://root:@{HOST}{remote}"])
    return r.returncode == 0 and local.exists()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def alive(timeout: float = 6.0) -> bool:
    try:
        camsh.run("true", timeout, boot_id=False)
        return True
    except Exception:
        return False


def collect(tag: str, out_dir: Path) -> None:
    sd = f"/opt/storage/sdcard/obs-{tag}"
    listing = sh(out_dir, "90-sd-listing.txt", f"ls -l {sd}; echo SHOTS; ls -l {sd}/shots 2>/dev/null")
    records, _, shots = listing.partition("SHOTS")
    for name in re.findall(r"\s(\S+\.txt|\S+\.log)$", records, re.M):
        ftp_get(f"{sd}/{name}", out_dir / f"rec-{name}")
    names = re.findall(r"\s(\S+\.(?:jpg|srw|bin|txt))$", shots, re.M)
    if names:
        (out_dir / "shots").mkdir(exist_ok=True)
        for name in names:
            ftp_get(f"{sd}/shots/{name}", out_dir / "shots" / name)
    summarise(out_dir)


def jpeg_segments(data: bytes) -> str:
    """The JPEG's header segments up to the scan: APPn kinds and frame size."""
    out, i = [], 2
    while i + 4 <= len(data) and data[i] == 0xFF:
        marker = data[i + 1]
        if marker == 0xD8 or 0xD0 <= marker <= 0xD7:
            i += 2
            continue
        length = int.from_bytes(data[i + 2:i + 4], "big")
        body = data[i + 4:i + 2 + length]
        if 0xE0 <= marker <= 0xEF:
            tag = body[:5].split(b"\0")[0].decode("ascii", "replace")
            out.append(f"APP{marker - 0xE0}({tag or '?'},{length})")
        elif marker in (0xC0, 0xC1, 0xC2):
            h, w = int.from_bytes(body[1:3], "big"), int.from_bytes(body[3:5], "big")
            out.append(f"SOF{marker - 0xC0} {w}x{h}")
        elif marker == 0xDA:
            out.append("SOS")
            break
        else:
            out.append(f"{marker:02X}")
        i += 2 + length
    return " ".join(out)


def mean_level(jpeg: Path) -> str:
    """Mean 8-bit level of a JPEG (converted to a small BMP with Apple's sips)."""
    tmp = jpeg.with_suffix(".level.bmp")
    try:
        subprocess.run(["sips", "-s", "format", "bmp", "-Z", "256", str(jpeg), "--out", str(tmp)],
                       capture_output=True, check=True)
        data = tmp.read_bytes()
        offset = int.from_bytes(data[10:14], "little")
        pixels = data[offset:]
        return f"mean level {sum(pixels) / max(len(pixels), 1):.1f}"
    except Exception as exc:  # measurement only; never fail the summary
        return f"mean level unavailable ({exc.__class__.__name__})"
    finally:
        tmp.unlink(missing_ok=True)


def jpeg_luma(jpeg: Path, width: int, height: int) -> list:
    """Rows of 8-bit luma of a JPEG scaled to width x height (Apple's sips), or []."""
    tmp = jpeg.with_suffix(f".{width}x{height}.bmp")
    try:
        subprocess.run(["sips", "-s", "format", "bmp", "-z", str(height), str(width), str(jpeg),
                        "--out", str(tmp)], capture_output=True, check=True)
        data = tmp.read_bytes()
        offset = int.from_bytes(data[10:14], "little")
        w = int.from_bytes(data[18:22], "little", signed=True)
        h = int.from_bytes(data[22:26], "little", signed=True)
        bpp = int.from_bytes(data[28:30], "little") // 8
        if w != width or abs(h) != height or bpp < 3:
            return []
        row_bytes = (w * bpp + 3) // 4 * 4
        rows = []
        for j in range(abs(h)):
            r = j if h < 0 else abs(h) - 1 - j
            p = offset + r * row_bytes
            rows.append([0.114 * data[p + i * bpp] + 0.587 * data[p + i * bpp + 1] + 0.299 * data[p + i * bpp + 2]
                         for i in range(w)])
        return rows
    except Exception:  # measurement only; never fail the summary
        return []
    finally:
        tmp.unlink(missing_ok=True)


def correlation(a: list, b: list) -> float:
    n = len(a)
    if n == 0 or n != len(b):
        return float("nan")
    ma, mb = sum(a) / n, sum(b) / n
    cov = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    return cov / (va * vb) ** 0.5 if va > 0 and vb > 0 else float("nan")


def raw_report(out_dir: Path) -> list:
    """Statistics, a grey preview and (for lens images) a JPEG comparison of each fetched RAW image."""
    import struct
    lines = []
    shots = out_dir / "shots"
    # The per-lens RAW images and the full frame copied during the gyro
    # hand-off share the description format.
    candidates = sorted(shots.glob("*-raw*.bin")) + sorted(shots.glob("*-frame.bin")) if shots.exists() else []
    for f in candidates:
        desc = f.with_suffix(".txt").read_text() if f.with_suffix(".txt").exists() else ""
        fields = dict(re.findall(r"^(\w+): (.+)$", desc, re.M))
        try:
            width, height = int(fields["width"]), int(fields["height"])
            stride, bits = int(fields["stride_bytes"]), int(fields["bits"])
        except (KeyError, ValueError):
            lines.append(f"{f.name}: no usable description")
            continue
        data = f.read_bytes()
        if len(data) < stride * height:
            lines.append(f"{f.name}: {len(data)} bytes, shorter than {stride * height}")
            continue
        top = (1 << bits) - 1
        lo, hi, total, count, at_top, at_zero = top, 0, 0, 0, 0, 0
        phase = [0, 0, 0, 0]
        phase_n = [0, 0, 0, 0]
        low_zero = [0] * 9   # samples whose lowest k bits are zero
        step = 8
        rows = []
        for y0 in range(0, height - 1, step):
            for y in (y0, y0 + 1):  # a row pair, so both colour-phase rows are seen
                row = struct.unpack_from(f"<{width}H", data, y * stride)
                if y == y0:
                    rows.append(row[::step])
                for x in range(0, width - 1, 2):
                    for dx in (0, 1):
                        v = row[x + dx]
                        lo, hi = min(lo, v), max(hi, v)
                        total += v
                        count += 1
                        at_top += v >= top
                        at_zero += v == 0
                        k = (y % 2) * 2 + dx
                        phase[k] += v
                        phase_n[k] += 1
                        for b in range(9):
                            low_zero[b] += (v & ((1 << b) - 1)) == 0
        means = [phase[k] / max(phase_n[k], 1) for k in range(4)]
        zero_bits = max(b for b in range(9) if low_zero[b] >= 0.999 * count)
        lines.append(f"{f.name}: {fields.get('layout', '?')} at {fields.get('physical', '?')}, {width}x{height}, "
                     f"values_ok {fields.get('values_ok', '?')}, changed_during_copy "
                     f"{fields.get('changed_during_copy', '?')}")
        lines.append(f"   sampled every {step}th row pair: min {lo}, max {hi}, mean {total / max(count, 1):.1f}, "
                     f"at {top} {100 * at_top / max(count, 1):.2f}%, zero {100 * at_zero / max(count, 1):.2f}%; "
                     f"lowest {zero_bits} bit(s) always zero; significant bits up to {hi.bit_length()}")
        lines.append(f"   2x2 phase means (row even/odd x col even/odd): "
                     + ", ".join(f"{m:.1f}" for m in means))
        # Grey preview: every 8th sample (one colour phase), square-root scaled
        # to the 99.5th percentile, written as PGM then JPEG.
        flat = sorted(v for r in rows for v in r)
        scale = max(flat[int(0.995 * (len(flat) - 1))], 1) if flat else 1
        pgm = f.with_suffix(".preview.pgm")
        pw, ph = len(rows[0]), len(rows)
        body = bytes(min(255, int(min(v / scale, 1.0) ** 0.5 * 255)) for r in rows for v in r)
        pgm.write_bytes(f"P5 {pw} {ph} 255\n".encode() + body)
        jpg = f.with_suffix(".preview.jpg")
        subprocess.run(["sips", "-s", "format", "jpeg", str(pgm), "--out", str(jpg)], capture_output=True)
        pgm.unlink(missing_ok=True)
        lines.append(f"   preview: {jpg.name}" if jpg.exists() else "   preview: not written")
        # A lens image against its half of the main JPEG (lens 1 left, lens 2 right),
        # in four orientations: the structure should match in one of them.
        m = re.match(r"lens (\d)", fields.get("layout", ""))
        main = shots / re.sub(r"-raw\d+\.bin$", "-main.jpg", f.name)
        if not m and main.name.endswith("-main.jpg") and main.exists() and abs(pw - 2 * ph) <= 8:
            # A full-width region (both lenses side by side) against the whole JPEG.
            luma = jpeg_luma(main, pw, ph)
            if luma:
                score = correlation([v ** 0.5 for r in rows for v in r], [v for r in luma for v in r])
                lines.append(f"   correlation with the whole JPEG (as is): {score:.3f}")
        if m and main.name.endswith("-main.jpg") and main.exists() and pw == ph:
            luma = jpeg_luma(main, 2 * pw, ph)
            if luma:
                half = int(m.group(1)) - 1
                ref = [r[half * pw:(half + 1) * pw] for r in luma]
                raw_rows = [list(r) for r in rows]
                views = {"as is": raw_rows, "mirrored": [r[::-1] for r in raw_rows],
                         "upside down": raw_rows[::-1], "rotated 180": [r[::-1] for r in raw_rows[::-1]]}
                flat_ref = [v for r in ref for v in r]
                scores = {k: correlation([v ** 0.5 for r in view for v in r], flat_ref) for k, view in views.items()}
                lines.append("   correlation with the JPEG's " + ("left" if half == 0 else "right") + " half: "
                             + ", ".join(f"{k} {v:.3f}" for k, v in scores.items()))
    return lines


def shot_report(out_dir: Path) -> list:
    """One line per fetched image: size, JPEG start/end markers, header segments."""
    lines = []
    for f in sorted((out_dir / "shots").glob("*")) if (out_dir / "shots").exists() else []:
        data = f.read_bytes()
        if f.suffix == ".jpg":
            ok = data[:2] == b"\xff\xd8" and data[-2:] == b"\xff\xd9"
            lines.append(f"{f.name}: {len(data)} bytes, {'markers ok' if ok else 'BAD JPEG MARKERS'}; "
                         f"{jpeg_segments(data)}; {mean_level(f) if ok else ''}")
        elif f.suffix in (".srw",):
            lines.append(f"{f.name}: {len(data)} bytes; first bytes {data[:16].hex()}")
    return lines


def buffer_returns(out_dir: Path) -> list:
    """Linux->RTOS give-back frames (cat 0x83, attr 0x020c) from the ring record."""
    rec = out_dir / "rec-ipcc.txt"
    if not rec.exists():
        return ["(no ring record)"]
    text = subprocess.run([sys.executable, str(Path(__file__).with_name("decode_ipcc.py")), str(rec)],
                          capture_output=True, text=True).stdout
    return [l for l in text.splitlines() if "cat 0x0083 attr 0x020c" in l] or ["(no 0x020c frame)"]


def clean(path: Path) -> list:
    if not path.exists():
        return []
    text = path.read_bytes().decode("utf-8", "replace").replace("\r", "")
    text = re.sub(r"\x1b\[[0-9;]*m", "", text)
    return [re.sub(r"T2EX >>", "", l).rstrip() for l in text.split("\n") if l.strip()]


def summarise(out_dir: Path) -> None:
    # --net only streams the RTOS log to the Mac instead of the card.
    rtos = clean(out_dir / "rec-rtoslog.txt") or clean(out_dir / "net-rtoslog.txt")
    probe = clean(out_dir / "rec-probe.log")
    second = clean(out_dir / "rec-probe-idle.log")
    dlog = clean(out_dir / "rec-dlog.txt")
    hb = [int(x) for l in rtos for x in re.findall(r"^@@HB (\d+)", l)]
    lines = ["# Still trial summary", ""]
    def pick(src, pattern, limit=12):
        return [l for l in src if re.search(pattern, l)][:limit]
    lines += ["## Probe", "```"] + pick(probe, r"gyro gate|capture_still|callback|diagnostic end|abort|"
                                        r"ReleaseCaptureBuffer|capture: |saved |still \d|exit:|open: ready", 60) + ["```", ""]
    shots = shot_report(out_dir)
    if shots:
        lines += ["## Saved images", "```"] + shots + ["```", ""]
    raws = raw_report(out_dir)
    if raws:
        lines += ["## RAW frames", "```"] + raws + ["```", ""]
    if any("ReleaseCaptureBuffer" in l for l in probe):
        lines += ["## Buffer give-backs on ring B (0x83/0x020c)", "```"] + buffer_returns(out_dir) + ["```", ""]
    if second:
        lines += ["## Second client (no-still build)", "```"] + pick(
            second, r"gyro gate|suppressed|callback|diagnostic end|abort", 12) + ["```", ""]
    lines += ["## RTOS milestones", "```"] + pick(
        rtos, r"roll/pitch/yaw|GET A9 SENSOR|restartLiveview|DisableStream|EnableStream|VideoFrameReadout|SetUserData|"
              r"JPEG IMAGE SET|ENC_SET|LIVEVIEW_START_COMPLETION|Start Power Off|movieWork|Quickview|QUICKVIEW|"
              r"NORMAL_LIVEVIEW|Liveview NOT|ID_LIVEVIEW_START|PERMIT_LIVEVIEW|EXPANSION|PP\] WDMA Done|"
              r"rdma0 addr=0xa6000000|image copy", 60) + ["```", ""]
    lines += ["## Linux log (gyro responder, devman plugin)", "```"] + pick(
        dlog, r"sensor buf phy|ACGY|module_init|captureProcessA9|exec tiem|EX_YCC_SET|STL_EXPANSION", 30) + ["```", ""]
    lines += [f"Last Linux heartbeat: {hb[-1] if hb else 'none'} us after recorder start; "
              f"kernel records: {sum(1 for l in rtos if l.startswith('@@K'))}", ""]
    (out_dir / "summary.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


# UDP streaming of the recorders to this Mac (Apple-signed /usr/bin/nc as the
# receiver), so the last milliseconds before a power cut are not lost to SD
# card write lag. One port per recorder.
NET_PORTS = {"rtoslog": 5701, "regwatch": 5702, "irqwatch": 5703, "heartbeat": 5704}


def mac_address_toward_camera() -> str:
    route = subprocess.run(["route", "-n", "get", HOST], capture_output=True, text=True).stdout
    m = re.search(r"interface:\s*(\S+)", route)
    if not m:
        return ""
    ip = subprocess.run(["ipconfig", "getifaddr", m.group(1)], capture_output=True, text=True).stdout.strip()
    return ip if re.fullmatch(r"\d+\.\d+\.\d+\.\d+", ip) else ""


def start_net_listeners(out_dir: Path) -> list:
    procs = []
    for name, port in NET_PORTS.items():
        f = open(out_dir / f"net-{name}.txt", "wb")
        procs.append((subprocess.Popen(["/usr/bin/nc", "-u", "-l", str(port)], stdout=f,
                                       stderr=subprocess.DEVNULL), f))
    return procs


def stop_net_listeners(procs: list) -> None:
    time.sleep(1)
    for proc, f in procs:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        f.close()



def run(tag: str, probe: Path, out_dir: Path, trace: bool = False, keep_app: bool = False,
        busy: bool = False, pagewatch: bool = False, ddrwatch: bool = False,
        freeze: list = None, second_client: Path = None, freeze_mode: str = "now",
        watch: list = None, parity: bool = False, irqwatch: bool = False,
        regwatch: bool = False, epmask: bool = False, probe_sched: str = "",
        net: str = "", jpegregs: bool = False, g360: int = 0, g360_quality: str = "",
        g360_drive: str = "", g360_sets: int = 1, g360_capture_type: str = "",
        g360_bracket_words: str = "", g360_exposure: str = "", g360_ltnr: str = "",
        g360_isonr: str = "", g360_set_timeout_ms: int = 0, g360_raw: bool = False,
        g360_quickview: str = "", g360_raw_layouts: list = None,
        g360_frame: bool = False, g360_frame_layouts: list = None) -> int:
    remote = f"/tmp/obs-{tag}"
    sd = f"/opt/storage/sdcard/obs-{tag}"
    files = {name: BUILD / name for name in TOOLS}
    files["capture-shot"] = probe
    if g360 and G360_MARK not in probe.read_bytes():
        print(f"{probe} is not a g360-shot build; refusing", file=sys.stderr)
        return 2
    if not g360 and G360_MARK in probe.read_bytes():
        print(f"{probe} is a g360-shot build: pass --g360 N", file=sys.stderr)
        return 2
    if second_client:
        if not second_client.exists() or SUPPRESS_MARK not in second_client.read_bytes():
            print(f"{second_client} is not a SUPPRESS_THE_STILL probe build; refusing", file=sys.stderr)
            return 2
        if SUPPRESS_MARK in probe.read_bytes():
            print("the main probe is a no-still build; nothing would send the still", file=sys.stderr)
            return 2
        files["capture-idle"] = second_client
    for name, path in files.items():
        if not path.exists():
            print(f"missing local file {path}; build first", file=sys.stderr)
            return 2
    hashes = {name: sha256(path) for name, path in files.items()}
    (out_dir / "00-local-hashes.txt").write_text("".join(f"{v}  {k}\n" for k, v in hashes.items()))

    pre = sh(out_dir, "01-preflight.txt",
             "cut -d' ' -f1 /proc/uptime; grep btime /proc/stat; "
             "systemctl show --property=ActiveState --property=MainPID di-camera-app.service; "
             "df -B1 /opt/storage/sdcard | tail -1; "
             "/usr/bin/openssl dgst -sha256 /usr/lib/libcapture-fw-prod.so /usr/lib/libmulticore-bridge.so; "
             "echo STOCK_ENV_BEGIN; tr '\\0' '\\n' < /proc/$(systemctl show -p MainPID di-camera-app | cut -d= -f2)/environ; "
             "echo STOCK_ENV_END; "
             + "".join(f"echo {n}=$(cat {n} 2>&1); " for n in PLUGIN_NODES))
    if "ActiveState=active" not in pre:
        print("stock app is not active: power-cycle the camera first (never restart the app)", file=sys.stderr)
        return 2
    m = re.search(r"/dev/mmcblk1p1\s+\d+\s+\d+\s+(\d+)", pre)
    if not m:
        print("could not read the card's free space", file=sys.stderr)
        return 2
    free_bytes = max(int(m.group(1)) - 5 * 1024 * 1024, 1)

    for name, path in files.items():
        ftp_put(path, f"{remote}/{name}")
    check = sh(out_dir, "02-staged.txt",
               f"cd {remote} && chmod +x * && /usr/bin/openssl dgst -sha256 " + " ".join(files) +
               f"; ./accelprops 1; mkdir -p {sd}/shots")
    for name, digest in hashes.items():
        if digest not in check:
            print(f"hash mismatch on the camera for {name}", file=sys.stderr)
            return 2

    stop = ("systemctl --no-block stop di-camera-app.service; echo STOP_RC=$?; "
            "for i in $(seq 1 30); do s=$(systemctl show --property=ActiveState di-camera-app.service); n=0; "
            "for d in /proc/[0-9]*; do [ \"$(cat $d/comm 2>/dev/null)\" = di-camera-app ] && n=$((n+1)); done; "
            "echo \"t=$(cut -d' ' -f1 /proc/uptime) $s procs=$n\"; "
            "[ \"$s\" = ActiveState=inactive ] && [ $n -eq 0 ] && break; sleep 1; done")
    if freeze and not keep_app:
        print("--freeze-ipc/--freeze-threads need --keep-app", file=sys.stderr)
        return 2
    if keep_app:
        stop = "echo KEEP_APP: stock app left running; grep State /proc/254/status"
    if freeze:
        pid = re.search(r"MainPID=(\d+)", pre)
        if not pid or pid.group(1) == "0":
            print("could not read the stock app's pid", file=sys.stderr)
            return 2
        # Hold 45 s: covers the probe (about 25 s) and the still. ptstop
        # releases the threads by itself; if it dies, the kernel does.
        flag = {"now": "", "after-gyro": "--after-gyro-reply ", "until-gyro": "--until-gyro-reply ",
                "after-gyro-adjust": "--after-gyro-adjust "}[freeze_mode]
        ready_word = "STOPPED" if freeze_mode == "now" else "ARMED"
        stop += (f"; (./ptstop {flag}{sd}/ptstop.txt {pid.group(1)} 45 {' '.join(freeze)} > pt.rc 2>&1 &); "
                 f"for i in $(seq 1 80); do grep -q -E '{ready_word}|NOT_ALL|NO_THREADS|ERROR' {sd}/ptstop.txt "
                 f"2>/dev/null && break; sleep 0.1; done; cat {sd}/ptstop.txt")
    if busy:
        # Keep the Linux CPU busy through the still; cpuhog exits by itself
        # after 90 s. (The camera has no `yes`: the first 4i attempt never
        # ran its hog.) Refuse to continue unless the hog is running.
        stop = ("(./cpuhog 90 > /dev/null 2>&1 &); sleep 1; "
                "if ps -eo pid,comm | grep -qw cpuhog; then echo BUSY_OK $(ps -eo pid,pcpu,comm | grep -w cpuhog); "
                "else echo BUSY_FAILED; exit 0; fi; " + stop)
    # With net ("both" or "only"), recorders also stream to this Mac; "only"
    # drops their SD files ("-") to lighten the card's write load.
    mac = mac_address_toward_camera() if net else ""
    if net and not mac:
        print("could not find this Mac's address toward the camera; no still sent", file=sys.stderr)
        return 2
    def dest(name: str) -> str:
        return f"{mac}:{NET_PORTS[name]}" if net else ""
    def card(path: str) -> str:
        return "-" if net == "only" else path
    listeners = start_net_listeners(out_dir) if net else []
    rtos_args = f"{card(sd + '/rtoslog.txt')} 180 10" + (f" {dest('rtoslog')}" if net else "")
    reg_net = f"-n {dest('regwatch')} " if net else ""
    hb_net = f"-n {dest('heartbeat')} " if net else ""
    irq_net = f"-n {dest('irqwatch')} " if net else ""
    stopped = sh(out_dir, "03-recorders-and-stop.txt",
       f"cd {remote}; "
       f"(./ipccrec record {sd}/ipcc.txt 180 100 A:4c:ec:395 B:54:277:395 > ipcc.rc 2>&1 &); "
       f"(./rtoslog follow {rtos_args} > rtos.rc 2>&1 &); "
       f"(dlogutil -v time 2>/dev/null | ./fsynctee {sd}/dlog.txt 180 > dlog.rc 2>&1 &); "
       + (f"(./pagewatch -a {sd}/pw-fast.txt 90 5 {' '.join(PAGEWATCH_FAST)} > pwf.rc 2>&1 &); "
          f"(./pagewatch -a {sd}/pw-slow.txt 90 20 {' '.join(PAGEWATCH_SLOW)} > pws.rc 2>&1 &); "
          if pagewatch else "")
       # DDR channel-A rate every 50 ms (uptime, Hz), synced to the card.
       + (f"((while :; do echo \"$(cut -d' ' -f1 /proc/uptime) $(cat {DDR_RATE})\"; sleep 0.05; done) 2>/dev/null "
          f"| ./fsynctee {sd}/ddr.txt 180 > ddr.rc 2>&1 &); "
          if ddrwatch else "")
       # One fast microsecond scanner over small ranges (no other scanner).
       + (f"(./pagewatch -a -u {sd}/watch.txt 120 500 {' '.join(watch)} > pwu.rc 2>&1 &); "
          if watch else "")
       # Image-engine interrupt counts + ring cursors, synced on every change,
       # and the kernel's drime5_ep mask page.
       + (f"(./irqwatch -r -s {irq_net}{card(sd + '/irq.txt')} 120 1000 {' '.join(EP_IRQS)} > irq.rc 2>&1 &); "
          if irqwatch else "")
       + (f"(./pagewatch -a -u {sd}/epmask.txt 120 500 {' '.join(EP_MASK_PAGE)} > epm.rc 2>&1 &); "
          if epmask else "")
       # Image-engine top registers every 1 ms, 10 ms heartbeat, synced per change;
       # and a second regwatch that reads no device register (pure heartbeat),
       # to tell an image-engine read hang from a whole-Linux stop.
       + (f"(./regwatch -r -s -b 10 {reg_net}{card(sd + '/reg.txt')} 120 1000 {' '.join(EP_TOP_REGS)} > reg.rc 2>&1 &); "
          f"(./regwatch -s -b 10 {hb_net}{card(sd + '/hb.txt')} 120 1000 > hb.rc 2>&1 &); "
          if regwatch else "")
       # JPEG-block registers in their own process (a hung read stops only it):
       # core control 0x2082a000, stream count 0x2082a280, read DMA 0x2082a800.
       + (f"(./regwatch -s -b 10 {card(sd + '/jpeg.txt')} 120 1000 {' '.join(JPEG_REGS)} > jpeg.rc 2>&1 &); "
          if jpegregs else "") +
       "sleep 2; echo recorders_at=$(cut -d' ' -f1 /proc/uptime); " + stop, timeout=90)
    if freeze and freeze_mode in ("now", "until-gyro") and not re.search(
            rf"^ALL_STOPPED \d+ {len(freeze)}$", stopped, re.M):
        print("the chosen stock-app threads did not all stop; no still sent", file=sys.stderr)
        return 2
    if freeze and freeze_mode != "now" and not re.search(r"^ARMED \d+$", stopped, re.M):
        print("the gyro-reply trigger is not armed; no still sent", file=sys.stderr)
        return 2
    if busy and "BUSY_OK" not in stopped:
        print("the CPU hog did not start; stock app not stopped, no still sent", file=sys.stderr)
        return 2
    if second_client:
        # Start the second library client and wait until it has finished
        # initializing (its log says the still was suppressed). It then holds
        # for 20 s with its library threads running, covering our still.
        idle_cmd = (f"./capture-idle --diagnostic-one-still --library /usr/lib/libcapture-fw-prod.so "
                    f"--log {sd}/probe-idle.log --free-bytes {free_bytes}")
        ready = sh(out_dir, "04a-second-client.txt",
                   f"cd {remote}; echo idle_at=$(cut -d' ' -f1 /proc/uptime); "
                   f"({idle_cmd} > idle.out 2>&1; echo exit=$? >> idle.out) > /dev/null 2>&1 & "
                   f"for i in $(seq 1 150); do grep -q 'still command suppressed' {sd}/probe-idle.log 2>/dev/null "
                   f"&& break; sleep 0.1; done; echo idle_ready_at=$(cut -d' ' -f1 /proc/uptime); "
                   f"grep -q 'still command suppressed' {sd}/probe-idle.log 2>/dev/null && echo IDLE_READY; "
                   f"tail -3 {sd}/probe-idle.log; ps -eo pid,comm | grep -E 'capture'", timeout=60)
        if not re.search(r"^IDLE_READY$", ready, re.M):
            print("the second client did not finish initializing; no still sent", file=sys.stderr)
            return 2
    probe_cmd = (f"./capture-shot --diagnostic-one-still --library /usr/lib/libcapture-fw-prod.so "
                 f"--log {sd}/probe.log --free-bytes {free_bytes}")
    if g360:
        probe_cmd = (f"./capture-shot --out {sd}/shots --log {sd}/probe.log --free-bytes {free_bytes} "
                     f"--count {g360}")
        if g360_drive:
            probe_cmd += f" --drive {g360_drive}"
        if g360_capture_type:
            probe_cmd += f" --capture-type {g360_capture_type}"
        if g360_bracket_words:
            probe_cmd += f" --bracket-words {g360_bracket_words}"
        if g360_exposure:
            probe_cmd += f" --exposure {g360_exposure}"
        if g360_ltnr:
            probe_cmd += f" --ltnr {g360_ltnr}"
        if g360_isonr:
            probe_cmd += f" --isonr {g360_isonr}"
        if g360_set_timeout_ms:
            probe_cmd += f" --set-timeout-ms {g360_set_timeout_ms}"
        if g360_raw:
            probe_cmd += " --raw"
            for layout in g360_raw_layouts or []:
                probe_cmd += f" --raw-layout {layout}"
        if g360_frame:
            # The full RAW frame, copied during the camera's gyro hand-off
            # (the expansion callback). The library registers before the
            # capture library's Beauty object, so its callback runs first.
            probe_cmd += " --frame"
            for layout in g360_frame_layouts or []:
                probe_cmd += f" --frame-layout {layout}"
        if g360_sets > 1:
            probe_cmd += f" --sets {g360_sets}"
        if g360_quality:
            probe_cmd += f" --quality {g360_quality}"
        if g360_quality or g360_drive or g360_exposure or g360_ltnr or g360_isonr or g360_quickview:
            # The camera's own settings list, before (read-only; step 1 used it).
            before = sh(out_dir, "01b-usrlist-before.txt",
                        "st cap capdtm usrlist 2>&1 | grep -a -i -E 'QUALITY|RAW|DRIVE|BRK|MODE|ISO|LTNR|QUICKVIEW'")
            if g360_quickview:
                # Set back afterwards to the value the camera has now.
                m = re.search(r"USERDATA_QUICKVIEWTIME\s*\|[^|]*\|\s*0x0033000([0-5])\s*\|", before)
                if not m:
                    print("could not read the camera's quick-view setting; no still sent", file=sys.stderr)
                    return 2
                restore = ["off", "1s", "3s", "5s", "hold", "holdzoom"][int(m.group(1))]
                print(f"quick view now: {restore}; it is set back to that after the still")
                probe_cmd += f" --quickview {g360_quickview} --restore-quickview {restore}"
    if probe_sched:
        # Start the probe (and every thread that inherits its policy) in a
        # real-time class, to test whether thread scheduling separates the
        # stock library's handling from ours.
        pol, prio = probe_sched.split(":")
        probe_cmd = f"chrt -{pol} {prio} {probe_cmd}"
    if parity:
        # Start the probe the way systemd starts the stock app: working
        # directory /, exactly the stock app's environment (read live in the
        # preflight), its own session. Paths are absolute.
        env_block = pre.split("STOCK_ENV_BEGIN", 1)[1].split("STOCK_ENV_END", 1)[0]
        env_vars = [l for l in env_block.splitlines() if re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", l)]
        if not any(v.startswith("LD_LIBRARY_PATH=") for v in env_vars):
            print("could not read the stock app's environment; no still sent", file=sys.stderr)
            return 2
        quoted = " ".join("'" + v.replace("'", "'\\''") + "'" for v in env_vars)
        (out_dir / "00-stock-env.txt").write_text("\n".join(env_vars) + "\n")
        probe_cmd = probe_cmd.replace("./capture-shot", f"{remote}/capture-shot")
        launch = (f"(cd / && env -i {quoted} setsid {probe_cmd} > {remote}/probe.out 2>&1; "
                  f"echo exit=$? >> {remote}/probe.out) > /dev/null 2>&1 &")
    elif trace:
        # strace writes to stderr unbuffered; fsynctee syncs every chunk to
        # the SD card, so the last system calls before a power cut survive.
        launch = (f"(strace -f -tt -s 64 {probe_cmd} 3>&1 1>/dev/null 2>&3 "
                  f"| ./fsynctee {sd}/probe-strace.txt 120; echo done >> probe.out) > /dev/null 2>&1 &")
    else:
        launch = f"({probe_cmd} > probe.out 2>&1; echo exit=$? >> probe.out) > /dev/null 2>&1 &"
    sh(out_dir, "04-probe-launch.txt",
       f"cd {remote}; echo probe_at=$(cut -d' ' -f1 /proc/uptime); {launch} "
       "sleep 1; ps -eo pid,comm | grep -E 'capture-shot|strace'")

    misses = 0
    # Watch at least 44 s, longer when the still may take longer (long exposures).
    watch_rounds = max(22, (g360_set_timeout_ms // 1000 + 30) // 2)
    for _ in range(watch_rounds):
        time.sleep(2)
        if alive():
            misses = 0
        else:
            misses += 1
            if misses >= 3:
                stop_net_listeners(listeners)
                print("\nThe camera stopped answering. Power-cycle it, rejoin its Wi-Fi, then run:\n"
                      f"  python3 {Path(__file__).relative_to(REPO)} collect --tag {tag}")
                return 1
    stop_net_listeners(listeners)
    print("camera alive after the still window; collecting")
    collect(tag, out_dir)
    after = sh(out_dir, "91-after.txt", f"echo UPTIME $(cut -d' ' -f1 /proc/uptime); grep btime /proc/stat; "
               f"cat {remote}/probe.out; cat {remote}/idle.out 2>/dev/null; "
               "ls -t /opt/storage/sdcard/DCIM/*/ | head -3")
    # The boot id is restored from the hibernation snapshot on every boot;
    # the boot time in /proc/stat is what changes.
    before_bt = re.search(r"^btime (\d+)", pre, re.M)
    after_bt = re.search(r"^btime (\d+)", after, re.M)
    if before_bt and after_bt:
        same = before_bt.group(1) == after_bt.group(1)
        print(f"boot time before {before_bt.group(1)}, after {after_bt.group(1)}: "
              f"{'no reboot' if same else 'THE CAMERA REBOOTED'}")
    if g360_quality or g360_drive or g360_exposure or g360_ltnr or g360_isonr or g360_quickview:
        after = sh(out_dir, "92-usrlist-after.txt",
                   "st cap capdtm usrlist 2>&1 | grep -a -i -E 'QUALITY|DRIVE|DIALMODE|SHOOTINGMODE|USERDATA_ISO |ISONR|LTNR|QUICKVIEW'")
        print("settings after the run (should be SUPERFINE, SINGLE, PROGRAM, ISO_AUTO, ISONR_MID, LTNR_ON,"
              " and the quick view as before):")
        print(after)
    return 0


# Per-thread scheduling of one process, read from /proc (fields after the
# comm: $16 priority, $17 nice, $38 rt_priority, $39 policy; 0 OTHER, 1 FIFO,
# 2 RR), plus each thread's allowed CPUs.
def sched_dump_cmd(pid_expr: str, label: str) -> str:
    return (f"P={pid_expr}; echo SCHED_BEGIN {label} $P; for t in /proc/$P/task/*; do "
            "s=$(cat $t/stat 2>/dev/null) || continue; rest=${s##*) }; set -- $rest; "
            "echo \"$(basename $t) $(tr ' ' '_' < $t/comm) state=$1 prio=${16} nice=${17} rt=${38} "
            "policy=${39} cpus=$(grep Cpus_allowed_list $t/status | cut -f2)\"; done; "
            f"echo SCHED_END {label}; ")


def sched_check(tag: str, out_dir: Path, idle: Path) -> int:
    """Non-lethal: start a no-still probe next to the stock app and record
    every thread's scheduling class and priority in both processes."""
    if not idle.exists() or SUPPRESS_MARK not in idle.read_bytes():
        print(f"{idle} is not a SUPPRESS_THE_STILL probe build; refusing", file=sys.stderr)
        return 2
    remote = f"/tmp/obs-{tag}"
    sd = f"/opt/storage/sdcard/obs-{tag}"
    (out_dir / "00-local-hashes.txt").write_text(f"{sha256(idle)}  capture-idle\n")
    pre = sh(out_dir, "01-preflight.txt",
             "cut -d' ' -f1 /proc/uptime; systemctl show --property=ActiveState --property=MainPID "
             "di-camera-app.service; df -B1 /opt/storage/sdcard | tail -1")
    if "ActiveState=active" not in pre:
        print("stock app is not active: power-cycle the camera first", file=sys.stderr)
        return 2
    m = re.search(r"/dev/mmcblk1p1\s+\d+\s+\d+\s+(\d+)", pre)
    free_bytes = max(int(m.group(1)) - 5 * 1024 * 1024, 1) if m else 1
    ftp_put(idle, f"{remote}/capture-idle")
    idle_cmd = (f"./capture-idle --diagnostic-one-still --library /usr/lib/libcapture-fw-prod.so "
                f"--log {sd}/probe-idle.log --free-bytes {free_bytes}")
    out = sh(out_dir, "02-sched.txt",
             f"mkdir -p {sd}; cd {remote} && chmod +x capture-idle && /usr/bin/openssl dgst -sha256 capture-idle; "
             f"({idle_cmd} > idle.out 2>&1; echo exit=$? >> idle.out) > /dev/null 2>&1 & "
             f"for i in $(seq 1 150); do grep -q 'still command suppressed' {sd}/probe-idle.log 2>/dev/null "
             f"&& break; sleep 0.1; done; "
             f"grep -q 'still command suppressed' {sd}/probe-idle.log 2>/dev/null && echo IDLE_READY; "
             + sched_dump_cmd("$(systemctl show -p MainPID di-camera-app | cut -d= -f2)", "stock")
             + sched_dump_cmd("$(pgrep -x capture-idle | head -1)", "probe")
             + "chrt -p $(systemctl show -p MainPID di-camera-app | cut -d= -f2); "
             "chrt -p $(pgrep -x capture-idle | head -1); chrt -m 2>&1 | head -3; "
             f"echo IDLE_LOG_BEGIN; tail -25 {sd}/probe-idle.log; echo IDLE_LOG_END", timeout=60)
    print(out)
    return 0 if "IDLE_READY" in out else 2



def net_check(tag: str, out_dir: Path) -> int:
    """Non-lethal: check that UDP from the camera reaches this Mac's listeners."""
    mac = mac_address_toward_camera()
    if not mac:
        print("could not find this Mac's address toward the camera", file=sys.stderr)
        return 2
    remote = f"/tmp/obs-{tag}"
    for name in ("regwatch", "rtoslog"):
        ftp_put(BUILD / name, f"{remote}/{name}")
    listeners = start_net_listeners(out_dir)
    time.sleep(0.5)
    out = sh(out_dir, "01-netcheck.txt",
             f"cd {remote} && chmod +x regwatch rtoslog && /usr/bin/openssl dgst -sha256 regwatch rtoslog; "
             f"./regwatch -b 10 -n {mac}:{NET_PORTS['heartbeat']} - 2 1000; echo reg_rc=$?; "
             f"./rtoslog follow - 2 10 {mac}:{NET_PORTS['rtoslog']}; echo rtos_rc=$?", timeout=30)
    stop_net_listeners(listeners)
    hb = (out_dir / "net-heartbeat.txt").read_bytes()
    rt = (out_dir / "net-rtoslog.txt").read_bytes()
    print(out)
    print(f"Mac {mac}: heartbeat bytes {len(hb)} ({hb.count(b'B ')} beats), rtoslog bytes {len(rt)}")
    return 0 if hb.count(b"B ") > 50 else 2


def jpeg_check(tag: str, out_dir: Path) -> int:
    """Small risk: read the JPEG-block registers for one second at idle. If the
    block's clock is gated, a read can hang the bus (the camera then needs a
    power cycle). Run only with the user's agreement."""
    remote = f"/tmp/obs-{tag}"
    ftp_put(BUILD / "regwatch", f"{remote}/regwatch")
    out = sh(out_dir, "01-jpegcheck.txt",
             f"cd {remote} && chmod +x regwatch && /usr/bin/openssl dgst -sha256 regwatch; "
             f"./regwatch -b 100 {remote}/jpeg.txt 1 1000 {' '.join(JPEG_REGS)}; echo rc=$?; cat {remote}/jpeg.txt",
             timeout=30)
    print(out)
    return 0 if "rc=0" in out else 2



def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["run", "collect", "sched", "netcheck", "jpegcheck"])
    parser.add_argument("--tag", required=True)
    parser.add_argument("--probe", type=Path, default=REPO / "output/stock-like-gyro-build/capture-shot")
    parser.add_argument("--strace", action="store_true",
                        help="run the probe under strace -f, streamed to the SD card")
    parser.add_argument("--keep-app", action="store_true",
                        help="leave the stock app running (coexistence still)")
    parser.add_argument("--busy", action="store_true",
                        help="keep the Linux CPU busy (cpuhog) through the still")
    parser.add_argument("--freeze-ipc", action="store_true",
                        help="with --keep-app: ptrace-stop the stock app's IPC threads during the still")
    parser.add_argument("--freeze-threads", default="",
                        help="with --keep-app: comma-separated stock-app thread names to ptrace-stop")
    parser.add_argument("--parity", action="store_true",
                        help="start the probe like the stock app: cwd /, its exact environment, setsid")
    parser.add_argument("--focused-watch", action="store_true",
                        help="microsecond watch of the still-expansion ring and nearby RTOSPREF words")
    parser.add_argument("--freeze-mode", choices=["now", "after-gyro", "until-gyro", "after-gyro-adjust"],
                        default="now",
                        help="when to stop/release the frozen threads relative to the gyro reply")
    parser.add_argument("--second-client", type=Path, default=None,
                        help="also run this SUPPRESS_THE_STILL probe build as a second library client")
    parser.add_argument("--ddrwatch", action="store_true",
                        help="sample the DDR channel-A rate every 50 ms")
    parser.add_argument("--irqwatch", action="store_true",
                        help="watch Linux's drime5_ep interrupt lines (synced per change)")
    parser.add_argument("--epmask", action="store_true",
                        help="also watch the kernel page holding Linux's drime5_ep interrupt masks")
    parser.add_argument("--idle", type=Path, default=REPO / "output/stock-like-suppress-build/capture-shot",
                        help="sched mode: the SUPPRESS_THE_STILL build to start next to the stock app")
    parser.add_argument("--probe-sched", default="",
                        help="run: start the probe under chrt, e.g. f:40 (SCHED_FIFO 40) or r:20 (SCHED_RR 20)")
    parser.add_argument("--net", choices=["both", "only"], default="",
                        help="also stream rtoslog/regwatch/irqwatch over UDP to this Mac; 'only' skips their SD files")
    parser.add_argument("--jpegregs", action="store_true",
                        help="also read the JPEG-block registers in a separate regwatch (do the idle test first)")
    parser.add_argument("--regwatch", action="store_true",
                        help="read the image-engine top registers every 1 ms (read-only), synced per change")
    parser.add_argument("--g360", type=int, default=0,
                        help="run: the probe is a g360-shot build; take this many stills (1-5) and fetch the images")
    parser.add_argument("--g360-quality", default="",
                        choices=["", "superfine", "fine", "normal", "raw", "raw-superfine", "raw-fine",
                                 "raw-normal", "sraw"],
                        help="with --g360: set this image quality before the stills (set back to superfine after)")
    parser.add_argument("--g360-drive", default="",
                        choices=["", "single", "contimid", "contilow", "burst", "aebrk", "wbbrk", "pwbrk",
                                 "depthbrk"],
                        help="with --g360: set this drive mode before the stills (set back to single after)")
    parser.add_argument("--g360-capture-type", default="",
                        choices=["", "lock3a", "single", "aeb", "wbb", "pwb", "depthbrk"],
                        help="with --g360: the still request's capture type (default lock3a)")
    parser.add_argument("--g360-bracket-words", default="",
                        help="with --g360: the request's words 11-14, e.g. 0,0,0x1a0000,1 (WB bracket AB3)")
    parser.add_argument("--g360-exposure", default="",
                        help="with --g360: manual exposure TV,ISO before each still (TV = 16*log2(1/s))")
    parser.add_argument("--g360-ltnr", default="", choices=["", "on", "off"],
                        help="with --g360: long-exposure noise reduction (set back to on after)")
    parser.add_argument("--g360-isonr", default="", choices=["", "off", "low", "mid", "high"],
                        help="with --g360: ISO noise reduction (set back to mid after)")
    parser.add_argument("--g360-set-timeout-ms", type=int, default=0,
                        help="with --g360: how long to wait for the image set (default 15000); the watch window grows with it")
    parser.add_argument("--g360-raw", action="store_true",
                        help="with --g360: also copy and save each still's two per-lens RAW images (fetched and summarised)")
    parser.add_argument("--g360-raw-layout", action="append", default=[],
                        help="with --g360-raw: PHYS,WIDTH,HEIGHT,STRIDE,BITS of a region to copy instead of the lens defaults (up to twice)")
    parser.add_argument("--g360-frame", action="store_true",
                        help="with --g360: also copy the still's full RAW frame during the camera's gyro hand-off")
    parser.add_argument("--g360-frame-layout", action="append", default=[],
                        help="with --g360-frame: PHYS,WIDTH,HEIGHT,STRIDE,BITS of the frame to copy (default: 0xa6000000,5824,2896,11648,16)")
    parser.add_argument("--g360-quickview", default="", choices=["", "off", "1s", "3s", "5s", "hold", "holdzoom"],
                        help="with --g360: quick-view time for the still (the camera's previous value is read first and set back)")
    parser.add_argument("--g360-sets", type=int, default=1,
                        help="with --g360: image sets to copy per still (brackets send several), 1-8")
    parser.add_argument("--pagewatch", action="store_true",
                        help="log every changed word in the shared RTOS windows (fast + slow scanner)")
    args = parser.parse_args()
    if args.freeze_mode != "now" and not (args.freeze_ipc or args.freeze_threads):
        parser.error("--freeze-mode needs --freeze-ipc or --freeze-threads")
    if (args.freeze_ipc or args.freeze_threads) and not args.keep_app:
        parser.error("--freeze-ipc/--freeze-threads need --keep-app")
    out_dir = REPO / f"output/decompile/rea/pass-{args.tag}"
    out_dir.mkdir(parents=True, exist_ok=True)
    if args.mode == "collect":
        collect(args.tag, out_dir)
        return 0
    if args.mode == "sched":
        return sched_check(args.tag, out_dir, args.idle.resolve())
    if args.mode == "netcheck":
        return net_check(args.tag, out_dir)
    if args.mode == "jpegcheck":
        return jpeg_check(args.tag, out_dir)
    if not 0 <= args.g360 <= 5:
        parser.error("--g360 takes 1 to 5 stills")
    if args.g360_bracket_words and not re.fullmatch(r"(0x[0-9a-fA-F]+|\d+)(,(0x[0-9a-fA-F]+|\d+)){3}",
                                                    args.g360_bracket_words):
        parser.error("--g360-bracket-words takes four comma-separated numbers")
    if args.g360_exposure and not re.fullmatch(r"-?\d+,\d+(:-?\d+,\d+)*", args.g360_exposure):
        parser.error("--g360-exposure takes TV,ISO[:TV,ISO...], e.g. 96,100 or 160,100:32,100")
    if not 0 <= args.g360_set_timeout_ms <= 600000:
        parser.error("--g360-set-timeout-ms takes 1 to 600000")
    if (args.g360_quality or args.g360_drive or args.g360_capture_type or args.g360_bracket_words
            or args.g360_exposure or args.g360_ltnr or args.g360_isonr or args.g360_set_timeout_ms
            or args.g360_raw or args.g360_quickview or args.g360_frame
            or args.g360_sets != 1) and not args.g360:
        parser.error("--g360-quality/--g360-drive/--g360-sets need --g360")
    if args.g360_raw_layout and not args.g360_raw:
        parser.error("--g360-raw-layout needs --g360-raw")
    if len(args.g360_raw_layout) > 2 or not all(
            valid_raw_layout(x) for x in args.g360_raw_layout):
        parser.error("--g360-raw-layout needs a bounded 16-bit-word layout <=96 MiB, at most twice")
    if args.g360_frame_layout and not args.g360_frame:
        parser.error("--g360-frame-layout needs --g360-frame")
    if len(args.g360_frame_layout) > 1 or not all(
            valid_raw_layout(x) for x in args.g360_frame_layout):
        parser.error("--g360-frame-layout needs a bounded 16-bit-word layout <=96 MiB, at most once")
    if not 1 <= args.g360_sets <= 8:
        parser.error("--g360-sets takes 1 to 8")
    if args.probe_sched and not re.fullmatch(r"[fr]:\d{1,2}", args.probe_sched):
        parser.error("--probe-sched takes f:N or r:N (N 1-99)")
    return run(args.tag, args.probe, out_dir, args.strace, args.keep_app, args.busy, args.pagewatch, args.ddrwatch,
               IPC_THREADS if args.freeze_ipc else [t for t in args.freeze_threads.split(",") if t],
               args.second_client.resolve() if args.second_client else None, args.freeze_mode,
               FOCUSED_WATCH if args.focused_watch else None, args.parity, args.irqwatch, args.regwatch, args.epmask, args.probe_sched,
               args.net, args.jpegregs, args.g360, args.g360_quality, args.g360_drive, args.g360_sets,
               args.g360_capture_type, args.g360_bracket_words, args.g360_exposure, args.g360_ltnr,
               args.g360_isonr, args.g360_set_timeout_ms, args.g360_raw, args.g360_quickview,
               args.g360_raw_layout, args.g360_frame, args.g360_frame_layout)


if __name__ == "__main__":
    sys.exit(main())
