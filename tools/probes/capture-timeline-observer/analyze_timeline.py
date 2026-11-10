#!/usr/bin/env python3
"""Summarize a capture-timeline log into allocation runs and pixel-write activity.

Record layout observed on the live camera (one 16-byte record per 16 KiB block):
word0 = allocation tag (memory slot), word1 = blocks remaining in the run counted
from this block, word2/word3 = extra words, non-zero on the run's first block.
A run therefore starts at a record whose word1 equals the run length.
"""
import argparse
import collections
import json
import re
from pathlib import Path

BLOCK_COUNT = 26624
BLOCK_BYTES = 0x4000
POOL_BASE = 0xA6000000
SAMPLE_STRIDE_BLOCKS = 16  # one sample point every 256 KiB


def runs(state):
    """Return allocation runs as (first_index, tag, length, word2, word3)."""
    found = []
    index = 0
    while index < BLOCK_COUNT:
        tag, remaining, word2, word3 = state[index]
        if tag and remaining:
            found.append((index, tag, remaining, word2, word3))
            index += remaining
        else:
            index += 1
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", type=Path)
    arguments = parser.parse_args()

    state = [(0, 0, 0, 0)] * BLOCK_COUNT
    events = collections.defaultdict(list)
    pixels = collections.defaultdict(list)
    end = None
    for line in arguments.log.read_text().splitlines():
        if line.startswith("BASE "):
            parts = line.split()
            first, last = int(parts[1]), int(parts[2])
            words = tuple(int(x, 16) for x in parts[3:7])
            for index in range(first, last + 1):
                state[index] = words
        elif " REC " in line:
            match = re.match(r"T (\d+) REC (\d+) (\d+) (.+) -> (.+)$", line)
            time, first, last = int(match[1]), int(match[2]), int(match[3])
            new = tuple(int(x, 16) for x in match[5].split())
            events[time].append((first, last, new))
        elif " PIX " in line:
            parts = line.split()  # T <time_us> PIX <point> <checksum>
            pixels[int(parts[3])].append(int(parts[1]))
        elif line.startswith("END "):
            end = line

    timeline = []
    previous = set(runs(state))
    timeline.append({"time_ms": 0.0, "runs": sorted(previous)})
    summary = []
    for time in sorted(events):
        for first, last, new in events[time]:
            for index in range(first, last + 1):
                state[index] = new
        current = set(runs(state))
        added = sorted(current - previous)
        removed = sorted(previous - current)
        if added or removed:
            summary.append({"time_ms": time / 1000, "added": added, "removed": removed})
        previous = current

    print("Initial allocation runs (first_block, tag, length, word2, word3):")
    for run in sorted(timeline[0]["runs"]):
        first = run[0]
        print(f"  blocks {first}-{first + run[2] - 1} addr 0x{POOL_BASE + first * BLOCK_BYTES:08x} "
              f"tag {run[1]} len {run[2]} ({run[2] * 16} KiB) w2 0x{run[3]:x} w3 0x{run[4]:x}")
    print("\nAllocation changes:")
    for change in summary:
        for label in ("added", "removed"):
            for run in change[label]:
                first = run[0]
                print(f"  t={change['time_ms']:9.1f} ms {label:7s} blocks {first}-{first + run[2] - 1} "
                      f"addr 0x{POOL_BASE + first * BLOCK_BYTES:08x} tag {run[1]} len {run[2]} "
                      f"({run[2] * 16 / 1024:.2f} MiB) w2 0x{run[3]:x} w3 0x{run[4]:x}")

    print("\nPixel sample activity by 256 KiB point (first/last change time, count):")
    by_region = []
    for point, times in sorted(pixels.items()):
        by_region.append((point, min(times) / 1000, max(times) / 1000, len(times)))
    # Group consecutive points with similar first-change times.
    group = []
    for item in by_region:
        if group and item[0] == group[-1][0] + 1 and abs(item[1] - group[0][1]) < 400:
            group.append(item)
        else:
            if group:
                print_group(group)
            group = [item]
    if group:
        print_group(group)
    print("\n" + (end or "no END line"))
    if arguments.json:
        arguments.json.write_text(json.dumps({"changes": summary, "pixels": by_region}, indent=1))


def print_group(group):
    first_point, last_point = group[0][0], group[-1][0]
    first_block = first_point * SAMPLE_STRIDE_BLOCKS
    last_block = last_point * SAMPLE_STRIDE_BLOCKS + SAMPLE_STRIDE_BLOCKS - 1
    first_time = min(g[1] for g in group)
    last_time = max(g[2] for g in group)
    changes = sum(g[3] for g in group)
    print(f"  points {first_point}-{last_point} (blocks {first_block}-{last_block}, "
          f"0x{POOL_BASE + first_block * BLOCK_BYTES:08x}) first {first_time:8.1f} ms "
          f"last {last_time:8.1f} ms changes {changes}")


if __name__ == "__main__":
    main()
