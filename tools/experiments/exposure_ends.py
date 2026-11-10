#!/usr/bin/env python3
"""Do the RAW values scale with the shutter at the ends of its range?

Reads the DNGs of one exposure-run.py slow3 or fast3 step (four frames:
three TVs 16 apart, i.e. 1 EV, then the first TV again) and compares the
black-subtracted RAW signal of each frame with the first one, on pixels that
are well above black and below clipping in all four frames. The Gear's
RAW holds 10-bit codes scaled by 64 (white 65472 = 1023 x 64). A real exposure
gives 1, 0.5, 0.25 and 1 (the repeat shows drift or flicker).

    python3 tools/experiments/exposure_ends.py --tv 41,57,73,41 output/ends-slow/*.dng

The fast end is also compared with whole sensor lines (13.1 us each, at
least 6), the floor that trial F1 matched.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

LINE_US = 13.1
MIN_LINES = 6


def load(path: Path):
    tags = ["-ImageWidth", "-ImageHeight", "-StripOffsets", "-BlackLevel", "-WhiteLevel", "-ExposureTime", "-ISO"]
    info = json.loads(subprocess.check_output(["exiftool", "-j", "-n", *tags, str(path)], text=True))[0]
    raster = np.memmap(path, mode="r", dtype="<u2", shape=(int(info["ImageHeight"]), int(info["ImageWidth"])),
                       offset=int(info["StripOffsets"]))
    black = float(np.atleast_1d(info["BlackLevel"])[0]) if not isinstance(info["BlackLevel"], str) else \
        float(info["BlackLevel"].split()[0])
    return raster, black, float(info["WhiteLevel"]), info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tv", required=True, help="the requested TVs, in frame order")
    parser.add_argument("--low", type=float, default=0.05,
                        help="lowest signal used, as a fraction of white - black (default 0.05: about 50 of the 1023 codes)")
    parser.add_argument("--step", type=int, default=4, help="sample every Nth row and column (default 4)")
    parser.add_argument("dngs", nargs="+", type=Path)
    args = parser.parse_args()
    tvs = [int(t) for t in args.tv.split(",")]
    if len(tvs) != len(args.dngs):
        parser.error("one TV per DNG")
    frames = [load(p) for p in sorted(args.dngs)]
    blacks = {b for _, b, _, _ in frames}
    whites = {w for _, _, w, _ in frames}
    if len(blacks) != 1 or len(whites) != 1:
        sys.exit(f"black/white levels differ between frames: {blacks} {whites}")
    black, white = blacks.pop(), whites.pop()
    span = white - black
    # Every 4th row and column (even offsets): one CFA colour, the same site
    # in every frame, so the ratios are not mixed across colours.
    data = np.stack([np.asarray(r[::args.step, ::args.step], dtype=np.float64) - black for r, _, _, _ in frames])
    ok = np.all((data > args.low * span) & (data < 0.85 * span), axis=0)
    n = int(ok.sum())
    print(f"black {black:.0f}, white {white:.0f}; {n} sampled pixels between {args.low:.0%} and 85% in every frame")
    if n < 1000:
        print("too few usable pixels: the scene is too dark or too bright for this end")
        return 1
    ref = data[0][ok]
    print(f"{'frame':>5} {'TV':>4} {'requested':>12} {'Exif':>9} {'ISO':>4} {'mean':>9} {'vs 1st':>7} "
          f"{'median px':>9} {'expected':>8} {'lines':>8}")
    for k, ((_, _, _, info), tv, frame) in enumerate(zip(frames, tvs, data), 1):
        seconds = 2 ** (-tv / 16)
        expected = 2 ** (-(tv - tvs[0]) / 16)
        lines = max(MIN_LINES, round(seconds * 1e6 / LINE_US))
        ref_lines = max(MIN_LINES, round(2 ** (-tvs[0] / 16) * 1e6 / LINE_US))
        values = frame[ok]
        exif = info.get("ExposureTime")
        exif = f"1/{1 / exif:.0f}" if isinstance(exif, (int, float)) and exif else "none"
        print(f"{k:>5} {tv:>4} {f'1/{1 / seconds:.1f} s':>12} {exif:>9} {info.get('ISO', '?'):>4} "
              f"{values.mean():>9.1f} {values.mean() / ref.mean():>7.3f} {np.median(values / ref):>9.3f} "
              f"{expected:>8.3f} {lines / ref_lines:>8.3f}")
    print("'expected' is 2^(-dTV/16); 'lines' is the ratio if the time is whole 13.1 us lines, 6 at least.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
