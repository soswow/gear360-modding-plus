"""Host tests for g360ctl and g360-ctl.

A fake /sys/class/leds tree (regular files: a write replaces the content,
which is what the library's parser must also accept), a recorded file of
ARM32 input_event records, and fake sound players.
"""
import os
import struct
import subprocess
import tempfile
from pathlib import Path

here = Path(__file__).resolve().parents[1]
re_root = here.parent
cc = os.environ.get("CC", "cc")

TRIGGERS = "[none] rfkill0 battery-charging timer gpio default-on"


def build(work: Path) -> Path:
    exe = work / "g360-ctl"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-g",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    f"-I{here}", f"-I{re_root / 'capture-session'}",
                    str(here / "g360ctl.c"), str(here / "g360-ctl.c"), "-o", str(exe)], check=True)
    return exe


def make_leds(root: Path) -> None:
    for i, (b, trig, delays) in {0: ("0", TRIGGERS, None), 1: ("1", "none [timer] gpio", ("200", "300")),
                                 3: ("1", "none battery-charging [battery-full] timer", None)}.items():
        d = root / f"led.{i}"
        d.mkdir(parents=True)
        (d / "brightness").write_text(b + "\n")
        (d / "max_brightness").write_text("255\n")
        (d / "trigger").write_text(trig + "\n")
        if delays:
            (d / "delay_on").write_text(delays[0] + "\n")
            (d / "delay_off").write_text(delays[1] + "\n")


def read(root: Path, i: int, name: str) -> str:
    return (root / f"led.{i}" / name).read_text().strip()


def event(sec, usec, type_, code, value) -> bytes:
    return struct.pack("<iiHHi", sec, usec, type_, code, value)


with tempfile.TemporaryDirectory(prefix="g360ctl-") as td:
    work = Path(td)
    exe = build(work)

    def run(*args):
        return subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=30)

    # LEDs: list reads every present LED, its active trigger and delays.
    root = work / "leds"
    make_leds(root)
    r = run("--root", str(root), "leds", "list")
    assert r.returncode == 0, r.stderr
    lines = r.stdout.splitlines()
    assert lines == ["led=0 brightness=0 max=255 trigger=none name=side-orange",
                     "led=1 brightness=1 max=255 trigger=timer name=side-green delay_on=200 delay_off=300",
                     "led=3 brightness=1 max=255 trigger=battery-full name=front-green",
                     "leds present=3"], lines

    # Save, change, restore: every LED returns to its saved state.
    saved = work / "saved.txt"
    r = run("--root", str(root), "leds", "save", str(saved))
    assert r.returncode == 0 and "leds saved=3" in r.stdout, r.stdout
    assert saved.read_text().splitlines()[1] == \
        "led=1 brightness=1 max=255 trigger=timer delays=1 delay_on=200 delay_off=300", saved.read_text()
    r = run("--root", str(root), "leds", "set", "front-green", "255")
    assert r.returncode == 0 and read(root, 3, "trigger") == "none" and read(root, 3, "brightness") == "255"
    # The kernel creates delay_on/delay_off when the timer trigger is chosen;
    # the fake has them ready (the library never creates attributes).
    for name in ("delay_on", "delay_off"):
        (root / "led.0" / name).write_text("500\n")
    r = run("--root", str(root), "leds", "blink", "0", "128", "100", "900")
    assert r.returncode == 0, r.stderr
    assert (read(root, 0, "trigger"), read(root, 0, "delay_on"), read(root, 0, "delay_off"),
            read(root, 0, "brightness")) == ("timer", "100", "900", "128")
    r = run("--root", str(root), "leds", "set", "1", "0")
    assert r.returncode == 0 and read(root, 1, "trigger") == "none"
    r = run("--root", str(root), "leds", "restore", str(saved))
    assert r.returncode == 0 and "leds restored=3" in r.stdout, (r.stdout, r.stderr)
    assert (read(root, 0, "trigger"), read(root, 0, "brightness")) == ("none", "0")
    assert (read(root, 1, "trigger"), read(root, 1, "delay_on"), read(root, 1, "delay_off"),
            read(root, 1, "brightness")) == ("timer", "200", "300", "1")
    # A trigger-driven LED gets its trigger back; its brightness is the kernel's.
    assert (read(root, 3, "trigger"), read(root, 3, "brightness")) == ("battery-full", "255")

    # Refusals: bad index, above max, missing LED, zero blink, bad numbers.
    before = {i: (read(root, i, "trigger"), read(root, i, "brightness")) for i in (0, 1, 3)}
    for args, code in ((["set", "side-purple", "1"], 2), (["set", "2", "1"], 1), (["set", "0", "256"], 1), (["set", "16", "1"], 2),
                       (["blink", "0", "0", "100", "100"], 1), (["set", "0", "x"], 2),
                       (["set", "0"], 2), (["frobnicate"], 2)):
        r = run("--root", str(root), "leds", *args)
        assert r.returncode == code, (args, r.returncode, r.stderr)
    assert before == {i: (read(root, i, "trigger"), read(root, i, "brightness")) for i in (0, 1, 3)}
    # A blink whose delay attributes never appear fails after switching the
    # trigger; a later restore puts the LED back.
    r = run("--root", str(root), "leds", "blink", "3", "1", "100", "100")
    assert r.returncode == 1 and read(root, 3, "trigger") == "timer", r.stdout
    r = run("--root", str(root), "leds", "restore", str(saved))
    assert r.returncode == 0 and read(root, 3, "trigger") == "battery-full"
    # A damaged saved file restores nothing.
    bad = work / "bad.txt"
    bad.write_text("led=0 brightness=9 max=5 trigger=none delays=0 delay_on=0 delay_off=0\n")
    r = run("--root", str(root), "leds", "restore", str(bad))
    assert r.returncode == 1 and read(root, 0, "brightness") == "0", r.stdout
    r = run("--root", str(work / "nothing"), "leds", "save", str(work / "none.txt"))
    assert r.returncode == 1 and not (work / "none.txt").exists()

    # Buttons: key events named, other events skipped, SYN_DROPPED reported.
    events = work / "events.bin"
    events.write_bytes(
        event(10, 5, 1, 126, 1) + event(10, 5, 0, 0, 0) +       # Capture press, SYN_REPORT
        event(10, 900000, 1, 126, 0) +                          # Capture release
        event(11, 1, 0, 3, 0) +                                 # SYN_DROPPED
        event(11, 2, 1, 127, 1) + event(11, 2, 1, 127, 0) +     # Menu pulse
        event(12, 0, 1, 73, 1) + event(12, 400000, 1, 73, 2) +  # Power press, repeat
        event(13, 0, 1, 215, 1) + event(13, 360000, 1, 215, 0) +  # Menu hold
        event(14, 0, 1, 116, 1) +                               # Power hold
        event(15, 0, 1, 30, 1) + event(15, 0, 4, 4, 7))         # unknown key, EV_MSC
    r = run("buttons", str(events), "5")
    assert r.returncode == 0, r.stderr
    got = [l for l in r.stdout.splitlines()]
    assert got == ["buttons device=%s seconds=5" % events,
                   "button t=10.000005 code=126 key=CAPTURE value=1",
                   "button t=10.900000 code=126 key=CAPTURE value=0",
                   "dropped",
                   "button t=11.000002 code=127 key=MENU value=1",
                   "button t=11.000002 code=127 key=MENU value=0",
                   "button t=12.000000 code=73 key=POWER value=1",
                   "button t=12.400000 code=73 key=POWER value=2",
                   "button t=13.000000 code=215 key=MENU_HOLD value=1",
                   "button t=13.360000 code=215 key=MENU_HOLD value=0",
                   "button t=14.000000 code=116 key=POWER_HOLD value=1",
                   "button t=15.000000 code=30 key=OTHER value=1",
                   "buttons end (read error or end of input)",
                   "buttons events=10"], got
    # A truncated record ends the read cleanly.
    events.write_bytes(event(1, 0, 1, 126, 1) + b"\x01\x02\x03")
    r = run("buttons", str(events), "5")
    assert r.returncode == 0 and "buttons events=1" in r.stdout, r.stdout
    assert run("buttons", str(work / "missing"), "1").returncode == 1
    assert run("buttons", str(events), "0").returncode == 2

    # Sound: the player gets the file; its exit status is reported.
    trace = work / "player-trace"
    good = work / "good-player"
    good.write_text(f"#!/bin/sh\necho \"$@\" > '{trace}'\nexit 0\n")
    failing = work / "failing-player"
    failing.write_text("#!/bin/sh\nexit 3\n")
    for p in (good, failing):
        p.chmod(0o755)
    r = run("sound", str(good), "/sounds/shutter.wav")
    assert r.returncode == 0 and "status=0" in r.stdout and trace.read_text().strip() == "/sounds/shutter.wav", r.stdout
    r = run("sound", str(failing), "x.wav")
    assert r.returncode == 1 and "status=3" in r.stdout, r.stdout
    r = run("sound", str(work / "no-such-player"), "x.wav")
    assert r.returncode == 1 and "status=-1" in r.stdout, r.stdout
    # --root applies only to LEDs.
    assert run("--root", str(root), "sound", str(good), "x").returncode == 2

print("g360ctl host tests passed")
