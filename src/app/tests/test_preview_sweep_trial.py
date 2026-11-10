#!/usr/bin/env python3
"""Host tests for the preview-then-sweep trial's log parsing (no camera)."""
import importlib.util
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "tools" / "preview-sweep-trial.py"
spec = importlib.util.spec_from_file_location("preview_sweep_trial", TOOL)
t = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t)  # camsh is only imported by main()

APP_LOG = """launch mode=fresh btime=100 executable=aa
APP PREVIEW camera start: ok
launch mode=fresh btime=200 executable=aa
APP UI READY
launch mode=reenter btime=200 executable=bb
APP SWEEP 20261107-160837: 6 frames, flash first
APP SWEEP FRAME 1 TV 48 ISO 1600 exposure 1/8 ISO 1600 at 3307 ms
APP POSE 330 samples pitch -8.2 roll 0.3 up -0.005 0.140 -0.971 gyro 3.0 dps
APP SWEEP FRAME 2 TV 48 ISO 400 exposure 1/8 ISO 400 at 6996 ms
launch mode=fresh btime=300 executable=bb
APP SWEEP 20261107-170000: 6 frames, flash first
APP SWEEP FRAME 1 TV 48 ISO 1600 exposure 1/8 ISO 1600 at 2500 ms
APP SWEEP CAPTURED 20261107-170000: 1 frame(s), held still 2000 ms, last frame saved 2500 ms
APP SWEEP SAVED 20261107-170000: moved to the card in 3000 ms
APP FLASH LIFE A 1 B 1 pre-EOL 1
"""


def test_preview_used_this_boot():
    assert t.preview_used_this_boot(APP_LOG, "100")
    assert not t.preview_used_this_boot(APP_LOG, "200")
    assert t.preview_used_this_boot(APP_LOG, "999")  # boot not in the log
    assert t.preview_used_this_boot(APP_LOG + "APP PREVIEW camera start: ok\n", "300")


def test_sweep_lines_end_at_death_or_saved():
    died = t.sweep_lines(APP_LOG, "20261107-160837")
    assert died[0].startswith("APP SWEEP 20261107-160837:")
    assert died[-1].startswith("launch mode=fresh btime=300")
    assert t.frame_times(died) == [(1, 3307), (2, 6996)]
    ok = t.sweep_lines(APP_LOG, "20261107-170000")
    assert ok[-1].startswith("APP SWEEP SAVED 20261107-170000")
    assert t.frame_times(ok) == [(1, 2500)]


CAPTURE_LOG = """g360cap: monotonic=10.0 open: loading the capture library
g360cap: monotonic=20.000 user attribute: mm_camera_set_usr_attributes (attribute, value, result) a=0x0 b=0x3 c=0x0 d=0x0
g360cap: monotonic=20.500 frame: armed (physical, virtual, size) a=0xa6000000 b=0x94d1d000 c=0x202b800 d=0x0
g360cap: monotonic=20.600 capture: still request (sequence, type, remaining, max sets) a=0x5 b=0x15 c=0x988 d=0x1
g360cap: monotonic=21.000 ReleaseCaptureBuffer (type, address, result) a=0x5 b=0x9cd21000 c=0x0 d=0x0
g360cap: monotonic=21.500 ReleaseCaptureBuffer (type, address, result) a=0x7 b=0x9913d000 c=0x0 d=0x0
g360cap: monotonic=22.000 frame: result (physical, flags, hold us, mode) a=0xa6000000 b=0x103 c=0xabc70 d=0x3
g360cap: monotonic=23.000 user attribute: mm_camera_set_usr_attributes (attribute, value, result) a=0x0 b=0x3 c=0x0 d=0x0
g360cap: monotonic=23.010 frame: armed (physical, virtual, size) a=0xa6000000 b=0x94d1d000 c=0x202b800 d=0x0
g360cap: monotonic=23.100 capture: still request (sequence, type, remaining, max sets) a=0x6 b=0x15 c=0x988 d=0x1
g360cap: monotonic=24.600 frame: result (physical, flags, hold us, mode) a=0xa6000000 b=0x103 c=0xabc70 d=0x3
"""


def test_capture_frames():
    rows = t.capture_frames(CAPTURE_LOG)
    assert len(rows) == 2
    assert rows[0]["save_gap"] is None and abs(rows[0]["arm"] - 0.5) < 1e-6
    assert abs(rows[0]["capture"] - 1.4) < 1e-6 and abs(rows[0]["release_span"] - 0.5) < 1e-6
    assert abs(rows[1]["save_gap"] - 1.0) < 1e-6 and abs(rows[1]["capture"] - 1.5) < 1e-6


NET = (b"\n@@S 0\n/proc/meminfo MemTotal:         378700 kB\nMemFree:            4544 kB\n"
       b"Cached:            64420 kB\nDirty:                12 kB\n/proc/loadavg 0.44 0.36 0.15 1/90 2433\n"
       b"/sys/class/power_supply/battery/temp 400\n/sys/class/power_supply/battery/voltage_now 4117000\n"
       b"/sys/class/power_supply/battery/current_now -368\n"
       b"/sys/devices/platform/drime5-clock/acpu/rate 800000000\n"
       b"/sys/devices/platform/drime5-clock/mbus/rate 200000000\n"
       b"/sys/devices/platform/drime5-ddrfreq/ch_a 700000000\n"
       b"\n@@HB 100000\n\n@@T 120000 W 5 R 0\nrtos text\n\n@@HB 200000\n"
       b"\n@@K 250000 6,123,456,-;kernel line\n\n@@HB 900000\n")


def test_parse_net():
    n = t.parse_net(NET)
    assert n["beats"] == [100000, 200000, 900000]
    assert n["max_gap_us"] == 700000 and n["last_beat_us"] == 900000
    s = n["stats"][0]
    assert s["MemFree"] == 4544 and s["Cached"] == 64420 and s["Dirty"] == 12
    assert s["load"] == 0.44 and s["temp"] == 400 and s["voltage"] == 4117000
    assert s["current"] == -368 and s["cpu_hz"] == 800000000
    assert s["mbus_hz"] == 200000000 and s["ddr_hz"] == 700000000 and "ep_hz" not in s
    assert n["kernel"] == [(250000, "6,123,456,-;kernel line")]
    csv = t.stats_csv(n["stats"]).splitlines()
    assert csv[0].startswith("us,MemFree") and csv[1].startswith("0,4544,64420,12,")


if __name__ == "__main__":
    for name, f in list(globals().items()):
        if name.startswith("test_"):
            f()
    print("ok")
