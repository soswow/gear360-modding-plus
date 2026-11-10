#!/usr/bin/env python3
"""Which shutter times describe the RAW brightness: the ones we set, or the
ones the firmware writes into Exif? (trial RANGE14)

Reads the DNGs of one exposure-run.py range14 step: 14 TVs evenly over the
usable range (41 to 218), then TV 41 again as a drift control. The scene must
have dark and bright parts, so that every pair of neighbouring frames shares
many pixels that are lit but not clipped.

Measurement. The 11 EV range is far wider than one frame holds, so no pixel
is usable in every frame. Instead each pair of neighbouring frames is compared:
the median per-pixel ratio of the black-subtracted RAW is that pair's
exposure ratio. The pixels are chosen on a third frame (the one before the
pair; for the first pair, the repeat), so that neither frame's own noise
decides which pixels count: scaled to the pair's exposures, that frame must
leave both lit (above --low, 6% of the range) and unclipped (below 85%). Chaining the 13 ratios gives each frame's exposure relative
to frame 1. One CFA site (every second row and column) is used, so colours
are not mixed. The repeat of TV 41 is compared with frame 1 directly.

Models, each scaled by one free constant (the absolute time is not measured):
  set       2^(-TV/16) s, the time we ask for
  exif      the firmware's ExposureTime (its third-stop name)
  physical  the time we ask for, capped at 166 ms and 5 us short (trials L1,
            ENDS1, ENDS2; fixed beforehand, not fitted here)

    python3 tools/experiments/exposure_range.py --out output/range14 output/speed-RANGE14/range14/*.dng

Writes range14.csv (per frame), pairs.csv (per pair and signal bin) and
range14.png (the chart) into --out.
"""
import argparse
import csv
import json
import math
import subprocess
import sys
from pathlib import Path

import numpy as np

TVS = [41, 55, 68, 82, 95, 109, 123, 136, 150, 164, 177, 191, 204, 218, 41]
CAP_S = 0.166
SHORT_S = 5e-6
COLOURS = {"set": "#2a78d6", "exif": "#eb6834", "physical": "#1baf7a", "measured": "#0b0b0b"}


def load(path: Path):
    tags = ["-ImageWidth", "-ImageHeight", "-StripOffsets", "-BlackLevel", "-WhiteLevel", "-ExposureTime", "-ISO"]
    info = json.loads(subprocess.check_output(["exiftool", "-j", "-n", *tags, str(path)], text=True))[0]
    raster = np.memmap(path, mode="r", dtype="<u2", shape=(int(info["ImageHeight"]), int(info["ImageWidth"])),
                       offset=int(info["StripOffsets"]))
    black = info["BlackLevel"]
    black = float(str(black).split()[0])
    return raster, black, float(info["WhiteLevel"]), info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--low", type=float, default=0.06,
                        help="lowest signal of the brighter frame used, fraction of white - black (default 0.06)")
    parser.add_argument("dngs", nargs="+", type=Path)
    args = parser.parse_args()
    paths = sorted(args.dngs)
    if len(paths) != len(TVS):
        parser.error(f"{len(TVS)} DNGs expected, got {len(paths)}")
    args.out.mkdir(parents=True, exist_ok=True)
    frames = [load(p) for p in paths]
    black, white = frames[0][1], frames[0][2]
    if any(f[1] != black or f[2] != white for f in frames):
        sys.exit("black/white levels differ between frames")
    span = white - black

    planes = {}

    def plane(k):
        if k not in planes:
            planes[k] = np.asarray(frames[k][0][::2, ::2], dtype=np.float32) - black
        return planes[k]

    def ratio(i, j, s, rows=None):
        """Median per-pixel ratio frame j / frame i. The pixels are chosen on a
        third frame S, whose noise is independent of both: S scaled to each
        frame's exposure must leave both lit (above --low) and unclipped
        (below 85%). Choosing them on i or j themselves keeps their noisy-high
        or noisy-low pixels near a threshold and biases the ratio. Also per
        signal bin (the brighter frame's predicted level)."""
        sel = plane(s) / span
        to_i, to_j = 2 ** (-(TVS[i] - TVS[s]) / 16), 2 ** (-(TVS[j] - TVS[s]) / 16)
        hi_level = sel * max(to_i, to_j)
        ok = (sel * min(to_i, to_j) > args.low) & (hi_level < 0.85) & (sel < 0.95)
        r = plane(j)[ok] / plane(i)[ok]
        level = hi_level[ok]
        if rows is not None:
            edges = [args.low, 0.1, 0.15, 0.2, 0.3, 0.45, 0.6, 0.85]
            for lo, hi in zip(edges, edges[1:]):
                pick = (level >= lo) & (level < hi)
                if pick.sum() >= 200:
                    rows.append((lo, hi, int(pick.sum()), float(np.median(r[pick]))))
        return float(np.median(r)), int(ok.sum())

    last = len(TVS) - 1  # the repeat of frame 1
    pair_rows, measured, counts = [], [0.0], []
    for k in range(1, last):
        selector = k - 2 if k >= 2 else last  # the frame before the pair; for the first pair, the repeat
        bins = []
        r, n = ratio(k - 1, k, selector, bins)
        counts.append(n)
        measured.append(measured[-1] + math.log2(r))
        for lo, hi, m, rb in bins:
            pair_rows.append(dict(pair=f"{k}-{k + 1}", tv_a=TVS[k - 1], tv_b=TVS[k], selector=selector + 1,
                                  bin_low=lo, bin_high=hi, pixels=m, ratio=rb, ratio_of_pair=r))
        planes.pop(k - 2, None)
    drift, drift_n = ratio(0, last, 1)

    set_s = [2 ** (-tv / 16) for tv in TVS[:-1]]
    exif_s = [float(f[3]["ExposureTime"]) for f in frames[:-1]]
    phys_s = [min(t, CAP_S) - SHORT_S for t in set_s]
    m = np.array(measured)
    models = {}
    for name, times in (("set", set_s), ("exif", exif_s), ("physical", phys_s)):
        lg = np.log2(np.array(times))
        offset = float(np.mean(m - lg))  # the one free constant
        models[name] = lg + offset - m      # residual in EV: model minus measured
    with open(args.out / "range14.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["frame", "file", "tv", "set_s", "exif_s", "physical_s", "measured_ev_vs_frame1",
                    "pixels_vs_previous", "resid_set_ev", "resid_exif_ev", "resid_physical_ev"])
        for k in range(len(TVS) - 1):
            w.writerow([k + 1, paths[k].name, TVS[k], f"{set_s[k]:.8g}", f"{exif_s[k]:.8g}", f"{phys_s[k]:.8g}",
                        f"{m[k]:.4f}", counts[k - 1] if k else "", f"{models['set'][k]:+.4f}",
                        f"{models['exif'][k]:+.4f}", f"{models['physical'][k]:+.4f}"])
    with open(args.out / "pairs.csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(pair_rows[0]))
        w.writeheader()
        w.writerows(pair_rows)

    print(f"black {black:.0f}, white {white:.0f}; drift control (frame 15 / frame 1): {drift:.4f} on {drift_n} px")
    print(f"{'fr':>2} {'TV':>3} {'set':>10} {'Exif':>8} {'meas EV':>8} {'px':>8} {'set':>7} {'exif':>7} {'phys':>7}")
    for k in range(len(TVS) - 1):
        print(f"{k + 1:>2} {TVS[k]:>3} {f'1/{1 / set_s[k]:.1f}':>10} {f'1/{1 / exif_s[k]:.0f}':>8} {m[k]:>8.3f} "
              f"{counts[k - 1] if k else '':>8} {models['set'][k]:>+7.3f} {models['exif'][k]:>+7.3f} "
              f"{models['physical'][k]:>+7.3f}")
    for name, res in models.items():
        print(f"{name:>9}: RMS {math.sqrt(float(np.mean(res ** 2))):.3f} EV, worst {float(np.max(np.abs(res))):.3f} EV")
    # Step by step: each neighbouring pair's measured step against each model's.
    steps = np.diff(m)
    for name, times in (("set", set_s), ("exif", exif_s), ("physical", phys_s)):
        model_steps = np.diff(np.log2(np.array(times)))
        print(f"{name:>9}: step error RMS {math.sqrt(float(np.mean((model_steps - steps) ** 2))):.3f} EV, "
              f"worst {float(np.max(np.abs(model_steps - steps))):.3f} EV")

    chart(args.out / "range14.png", m, set_s, exif_s, phys_s, models, pair_rows, drift)
    return 0


def chart(path, m, set_s, exif_s, phys_s, models, pair_rows, drift):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.size": 10, "axes.edgecolor": "#c3c2b7", "axes.labelcolor": "#52514e",
                         "xtick.color": "#52514e", "ytick.color": "#52514e", "axes.spines.top": False,
                         "axes.spines.right": False})
    tvs = np.array(TVS[:-1])
    fig, axes = plt.subplots(3, 1, figsize=(9, 12.5), gridspec_kw={"height_ratios": [1.2, 1, 0.9]})
    fig.patch.set_facecolor("#fcfcfb")
    names = {"set": "Set by us: 2^(−TV/16) s", "exif": "Firmware Exif ExposureTime",
             "physical": "Set, with 166 ms cap and 5 µs short (earlier trials)"}

    ax = axes[0]
    ax.plot(tvs, m, "o", color=COLOURS["measured"], ms=8, label="Measured (RAW, chained pairs)", zorder=3)
    for name, times in (("set", set_s), ("exif", exif_s)):
        lg = np.log2(np.array(times))
        ax.plot(tvs, lg + np.mean(m - lg), "-", color=COLOURS[name], lw=2, label=names[name])
    ax.set_ylabel("Exposure vs frame 1 (EV, log2)")
    ax.set_title("RAW brightness follows the shutter over the whole 11 EV range\n"
                 "(the two lines differ by under 0.2 EV here; the panel below separates them)",
                 loc="left", color="#0b0b0b")
    ax.legend(frameon=False, loc="lower left")
    ax.grid(color="#e8e7e2", lw=0.8)
    labels = [f"1/{1 / t:.0f}" if t < 0.1 else f"1/{1 / t:.1f}" for t in set_s]
    top = ax.secondary_xaxis("top")
    top.set_xticks(tvs[::2], labels[::2])
    top.tick_params(colors="#52514e", labelsize=8)

    ax = axes[1]
    for name in ("set", "exif", "physical"):
        ax.plot(tvs, models[name], "-o", color=COLOURS[name], lw=2, ms=6, label=names[name])
    ax.axhline(0, color="#8a8984", lw=1)
    ax.set_ylabel("Model − measured (EV)")
    rms = {k: math.sqrt(float(np.mean(v ** 2))) for k, v in models.items()}
    ax.set_title("Which times describe the brightness? (closer to 0 is better)\n"
                 f"RMS error: set {rms['set']:.3f} EV, Exif {rms['exif']:.3f} EV, set with cap and offset "
                 f"{rms['physical']:.3f} EV", loc="left", color="#0b0b0b")
    ax.set_ylim(-0.26, 0.17)
    ax.legend(frameon=False, loc="lower left", fontsize=9)
    ax.grid(color="#e8e7e2", lw=0.8)

    ax = axes[2]
    pairs = sorted({r["pair"] for r in pair_rows}, key=lambda p: int(p.split("-")[0]))
    for i, p in enumerate(pairs):
        rows = [r for r in pair_rows if r["pair"] == p]
        x = [math.sqrt(r["bin_low"] * r["bin_high"]) for r in rows]
        y = [r["ratio"] / r["ratio_of_pair"] for r in rows]
        ax.plot(x, y, "-", color="#2a78d6", alpha=0.25 + 0.6 * i / max(1, len(pairs) - 1), lw=1.5)
    ax.axhline(1, color="#8a8984", lw=1)
    ax.set_xscale("log")
    from matplotlib.ticker import FixedLocator, FuncFormatter, NullLocator
    ax.xaxis.set_major_locator(FixedLocator([0.06, 0.1, 0.15, 0.2, 0.3, 0.45, 0.6, 0.85]))
    ax.xaxis.set_minor_locator(NullLocator())
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:.0%}"))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v - 1:+.1%}"))
    ax.set_xlabel("Signal of the brighter frame of the pair (share of the RAW range)")
    ax.set_ylabel("Pair ratio at that level vs the whole pair")
    ax.set_title(f"Each pair's ratio holds within ±1% from dim to bright pixels (13 pairs; lighter = slower)\n"
                 f"repeat of 1/6 s vs frame 1: {drift:.4f}",
                 loc="left", color="#0b0b0b", fontsize=10)
    ax.grid(color="#e8e7e2", lw=0.8)
    for a in axes[:2]:
        a.set_xlabel("TV (exposure 2^(−TV/16) s)")
    for a in axes:
        a.set_facecolor("#fcfcfb")
    fig.tight_layout()
    fig.savefig(path, dpi=130, facecolor=fig.get_facecolor())
    print(f"chart: {path}")


if __name__ == "__main__":
    sys.exit(main())
