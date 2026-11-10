#!/usr/bin/env python3
"""Exposure-length trials through the validated session wrapper (T28).

Reuses speed-run.py's preflight, app stop, staging and run-session.sh
(fresh/reenter chosen from the camera's state). Steps:

  ctl   three JPEG stills, ISO 100, LTNR off: 1/8 s, 1/4 s, 2 s
  l5    one JPEG still, ISO 100, LTNR off, 5 s (the long route stalled in E3
        with LTNR on and RAW; a stall holds the camera until a power cycle)

    python3 tools/experiments/exposure-run.py --tag L1 --steps ctl,l5

  fast  RAW+JPEG, LTNR off, ISO 100: 1/8192 (TV 208), then 1/16384 (224) and
        1/32768 (240), then 1/8192 again as a drift control (bright scene)

  alt   2 s / 5 s alternating three times (is 5 s really brighter than 2 s?)

  slow3 RAW+JPEG, LTNR off, ISO 100: the three longest usable times, 1 EV
        apart: TV 41 (1/5.9 s, at the ~1/6 s cap), 57 (1/11.8), 73 (1/23.6),
        then TV 41 again as a drift control
  fast3 the same for the three shortest: TV 186 (1/3158 s), 202 (1/6317),
        218 (1/12634, at the ~1/12,400 s floor), then TV 186 again

  range14 RAW+JPEG, LTNR off, ISO 100: 14 TVs evenly over the whole usable
        range, 41 (1/6 s) to 218 (1/12634 s), 13.6 TV (0.85 EV) apart, then
        TV 41 again as a drift control (15 stills)

  Analyse slow3/fast3 with tools/experiments/exposure_ends.py, range14 with
  tools/experiments/exposure_range.py.

Our app is stopped first and NOT restarted here.
"""
import importlib.util
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("speed_run", here / "speed-run.py")
sr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sr)
sr.STEPS["ctl"] = "--count 3 --gap-ms 500 --hold-ms 1000 --quality superfine --ltnr off --exposure 48,100:32,100:-16,100 --set-timeout-ms 30000"
sr.STEPS["l5"] = "--count 1 --hold-ms 1000 --quality superfine --ltnr off --exposure -37,100 --set-timeout-ms 60000"
sr.STEPS["alt"] = "--count 6 --gap-ms 500 --hold-ms 1000 --quality superfine --ltnr off --exposure -16,100:-37,100:-16,100:-37,100:-16,100:-37,100 --set-timeout-ms 60000"
sr.STEPS["fast"] = "--frame --count 4 --gap-ms 300 --hold-ms 1000 --frame-output dng --quality superfine --ltnr off --exposure 208,100:224,100:240,100:208,100 --set-timeout-ms 30000"
END = "--frame --count 4 --gap-ms 300 --hold-ms 1000 --frame-output dng --quality superfine --ltnr off --set-timeout-ms 30000"
sr.STEPS["slow3"] = END + " --exposure 41,100:57,100:73,100:41,100"
sr.STEPS["fast3"] = END + " --exposure 186,100:202,100:218,100:186,100"
RANGE14 = [41, 55, 68, 82, 95, 109, 123, 136, 150, 164, 177, 191, 204, 218, 41]
sr.STEPS["range14"] = END.replace("--count 4", f"--count {len(RANGE14)}") + " --exposure " + ":".join(f"{tv},100" for tv in RANGE14)
sys.exit(sr.main())
