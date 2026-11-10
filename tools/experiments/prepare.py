#!/usr/bin/env python3
"""Offline experiment plans, pinned target bundles, and timing summaries.

This module has no camera transport and never executes generated scripts.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shlex
import shutil
import statistics

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
FRAME_BYTES = 33732608
POLICIES = ("save", "discard", "skip-copy")
HDR_RUNNER = REPO / "hdr/web/hdr-capture.sh"
# The retired runner's two sequences (TV, ISO), for a checkout that does not have
# hdr/ (the public repository). Where the runner exists it is read instead, and a
# test requires the two to agree.
RETIRED_HDR_EXPOSURES = {
    "ev2": [(48, 1600), (48, 400), (48, 100), (80, 100), (112, 100), (144, 100),
            (176, 100), (208, 100)],
    "ev3": [(48, 1600), (48, 400), (64, 100), (112, 100), (160, 100), (208, 100)],
}


def hdr_exposures(preset, source=None):
    """Read the canonical legacy runner's TV/ISO words, not display labels.

    For example the UI calls TV 112 '1/125'; its requested TV is authoritative.
    Fail if its format changes so a future edit cannot silently use stale data.
    Without the runner file (and no `source` given) the recorded lists are used.
    """
    if preset not in ("ev2", "ev3"):
        raise ValueError("unknown HDR preset")
    if source is None and not HDR_RUNNER.exists():
        return list(RETIRED_HDR_EXPOSURES[preset])
    source = source if source is not None else HDR_RUNNER.read_text()
    match = re.search(r"^    " + preset + r"\|[^\n]*\n        preset=" + preset
                      + r"\n        exposures='([^']+)'\n        total=(\d+)", source, re.M)
    if not match:
        raise ValueError("canonical HDR sequence changed; review its format")
    values = [tuple(map(int, pair.split(":"))) for pair in match[1].split()]
    if len(values) != int(match[2]) or any(not -160 <= tv <= 240 or not 50 <= iso <= 25600
                                          for tv, iso in values):
        raise ValueError("invalid canonical HDR exposures")
    return values


def plans(wb_name="WB_DAYLIGHT"):
    if not re.fullmatch(r"WB_[A-Z0-9_]+", wb_name):
        raise ValueError("WB name must be the exact native symbol from usrlist")
    base = ["--frame", "--hold-ms", "5000", "--gap-ms", "2000"]
    result = []

    def add(name, step, count, extra=(), mode="reenter", requires=(), wb=""):
        argv = [*base, "--count", str(count), *extra]
        saving_frame = "--frame-output" not in extra or extra[extra.index("--frame-output") + 1] == "save"
        saving_jpeg = "--jpeg" not in extra or extra[extra.index("--jpeg") + 1] == "save"
        # 32 MiB allowance per still for all JPEGs and log overhead; reserve
        # is conservative, not a prediction of actual encoder output size.
        reserve = 128 * 1024 * 1024 + count * ((FRAME_BYTES if saving_frame else 0)
                                             + (32 * 1024 * 1024 if saving_jpeg else 0))
        result.append(dict(name=name, goal_step=step, status="prepared; camera not run",
                           start_mode=mode, requires=list(requires), argv=argv,
                           require_wb=wb, reserve_bytes=reserve))

    add("baseline", 2, 2, mode="fresh", requires=["normal stock shooting session"])
    add("reentry-1", 2, 1, requires=["baseline passed in this boot; no clients remain"])
    add("reentry-2", 2, 2, requires=["reentry-1 passed in this boot"])
    # ABCCBA balances drift; run each condition in a separate process only
    # once reentry works. Repeat the block at least three times for timing.
    for i, policy in enumerate((*POLICIES, *reversed(POLICIES)), 1):
        add(f"jpeg-{i}-{policy}", 1, 2, ("--jpeg", policy),
            requires=["reentry-2 passed", "fixed scene/exposure/WB; repeat balanced block >=3 times"])
    add("stress-10", 3, 10, ("--gap-ms", "0", "--frame-output", "discard", "--jpeg", "discard"),
        requires=["reentry-2 passed", "baseline saved-frame correctness passed"])
    add("stress-100", 3, 100, ("--gap-ms", "0", "--frame-output", "discard", "--jpeg", "discard"),
        requires=["stress-10 passed; inspect heat/battery/free space first"])
    add("after-stress", 3, 1, requires=["stress-100 passed; save and inspect a fresh frame/JPEG pair"])
    add("hdr-boundaries", 4, 3,
        ("--exposure", "208,100:48,100:48,1600", "--ltnr", "off", "--isonr", "off"),
        requires=["reentry-2 passed", "chosen WB and Smart Range off read back"], wb=wb_name)
    for preset in ("ev2", "ev3"):
        exposures = hdr_exposures(preset)
        add("hdr-" + preset, 4, len(exposures),
            ("--exposure", ":".join(f"{tv},{iso}" for tv, iso in exposures),
             "--ltnr", "off", "--isonr", "off"),
            requires=["hdr-boundaries passed", "chosen WB and Smart Range off read back"], wb=wb_name)
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bundle(destination, probe, wb_name, buttons, display):
    if destination.exists():
        raise ValueError("bundle destination exists; choose a new directory")
    data = probe.read_bytes()
    # EM_ARM == 40, ELF32 little endian. No native execution on the host.
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x01" or data[18:20] != b"\x28\x00" or b"metric,sequence=" not in data:
        raise ValueError("probe must be the ARM g360-shot build with experiment metrics")
    observer = buttons.read_bytes()
    if len(observer) < 52 or observer[:6] != b"\x7fELF\x01\x01" or observer[18:20] != b"\x28\x00":
        raise ValueError("button observer must be an ARM32 little-endian ELF")
    renderer = display.read_bytes()
    if len(renderer) < 52 or renderer[:6] != b"\x7fELF\x01\x01" or renderer[18:20] != b"\x28\x00" or b"--center" not in renderer:
        raise ValueError("display helper must be an ARM32 ELF supporting --center")
    cases = plans(wb_name)
    destination.mkdir(parents=True)
    shutil.copy2(probe, destination / "g360-shot")
    shutil.copy2(buttons, destination / "button-observer")
    shutil.copy2(display, destination / "gear360-display")
    for name in ("run-session.sh", "platform-snapshot.sh", "platform-compact.sh", "README.md"):
        shutil.copy2(HERE / name, destination / name)
    binary_hash = digest(probe)
    for case in cases:
        script = (
            "#!/bin/sh\nset -eu\n"
            "[ \"$#\" -eq 2 ] || { echo 'usage: sh CASE.sh EXPECTED_BTIME /opt/storage/sdcard/exp-UNIQUE' >&2; exit 2; }\n"
            'here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)\n'
            + f"export RESERVE_BYTES={case['reserve_bytes']} REQUIRE_WB={shlex.quote(case['require_wb'])}\n"
            + f"export DISPLAY_SHA256={digest(display)}\n"
            + f"exec sh \"$here/run-session.sh\" {case['start_mode']} \"$1\" \"$2\" {binary_hash} "
            + shlex.join(case["argv"]) + "\n")
        (destination / (case["name"] + ".sh")).write_text(script)
    manifest = dict(schema="gear360-experiments-v1", camera_executed=False, cases=cases,
                    files={p.name: digest(p) for p in sorted(destination.iterdir()) if p.is_file()},
                    sources={str(p.relative_to(REPO)): digest(p) for p in
                             [HERE / "prepare.py", HDR_RUNNER,
                              REPO / "reverse-engineering/facts/coordinates.yaml",
                              HERE / "button-observer.c", HERE / "build-buttons.sh",
                              HERE / "build-display.sh", REPO / "tools/experiments/gear360-display.c",
                              REPO / "src/lib/g360cap/g360-shot.c", REPO / "src/lib/g360cap/g360cap.c",
                              REPO / "src/lib/g360cap/g360cap.h", REPO / "src/lib/g360cap/g360cap_check.c",
                              REPO / "src/lib/g360cap/build-arm.sh"] if p.exists()})
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def percentile(values, p):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * p) - 1)]


def summarize(paths):
    groups = {}
    input_paths = set()
    for path in paths:
        if path.resolve() in input_paths:
            raise ValueError(f"duplicate input file: {path}")
        input_paths.add(path.resolve())
        # Select ONE stream per session (stdout OR probe.log). Duplicates
        # would bias timing; reject duplicate sequence numbers in one file.
        seen = set()
        for line in path.read_text().splitlines():
            if "metric," not in line:
                continue
            fields = dict(item.split("=", 1) for item in line.split("metric,", 1)[1].split(","))
            sequence = int(fields["sequence"])
            if sequence in seen:
                raise ValueError(f"duplicate metric sequence in {path}: {sequence}")
            seen.add(sequence)
            key = (fields["jpeg_policy"], fields["frame_output"], fields.get("library_log", "sync"))
            group = groups.setdefault(key, dict(rows=[], failed=0, frame_invalid=0, files=set()))
            group["files"].add(str(path))
            if int(fields["status"]) != 0:
                group["failed"] += 1
                continue
            flags = int(fields["frame_flags"])
            if int(fields["frame_bytes"]) != FRAME_BYTES or flags & 0x102 != 0x102 or flags & 0x600:
                group["frame_invalid"] += 1
                continue
            group["rows"].append(fields)
        if not seen:
            raise ValueError(f"no metrics in {path}")
    report = []
    for (jpeg, frame, library_log), group in sorted(groups.items()):
        rows = group["rows"]
        result = dict(jpeg_policy=jpeg, frame_output=frame, library_log=library_log, valid_frames=len(rows),
                      failures=group["failed"], invalid_frames=group["frame_invalid"],
                      sessions=len(group["files"]))
        for metric in ("capture_us", "save_us", "total_us", "cycle_us", "set_us", "ready_us", "frame_hold_us"):
            values = [int(row[metric]) for row in rows if metric != "cycle_us" or int(row[metric]) > 0]
            if values:
                result[metric] = dict(median=statistics.median(values), p95=percentile(values, .95),
                                      minimum=min(values), maximum=max(values), n=len(values))
        report.append(result)
    # Intentionally no "faster" verdict: matching settings, thermal state,
    # reboots and correct content require the retained observation records.
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    plan = sub.add_parser("plan")
    plan.add_argument("--wb-name", default="WB_DAYLIGHT")
    build = sub.add_parser("bundle")
    build.add_argument("--out", required=True, type=Path)
    build.add_argument("--probe", type=Path, default=REPO / "output/g360cap-build/g360-shot")
    build.add_argument("--buttons", type=Path, default=REPO / "output/experiment-buttons-build/button-observer")
    build.add_argument("--display", type=Path, default=REPO / "output/experiment-display-build/gear360-display")
    build.add_argument("--wb-name", default="WB_DAYLIGHT")
    summary = sub.add_parser("summarize")
    summary.add_argument("logs", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        if args.action == "plan":
            result = plans(args.wb_name)
        elif args.action == "bundle":
            result = bundle(args.out, args.probe, args.wb_name, args.buttons, args.display)
        else:
            result = summarize(args.logs)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
