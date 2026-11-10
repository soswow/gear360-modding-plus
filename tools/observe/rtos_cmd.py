#!/usr/bin/env python3
"""Run an RTOS shell command and return only the lines it added to the RTOS log.

Forwarded `st cap ...` commands print into the RTOS console log, which
`st cap log` dumps. This snapshots the log, runs the command, waits, snapshots
again and keeps the lines that are new in the second snapshot.

    python3 rtos_cmd.py [--wait 1.0] [--out FILE] -- st cap capmm show
    python3 rtos_cmd.py --dump-only --out FILE      # just a cleaned st cap log
"""

import argparse
import difflib
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import camsh  # noqa: E402

SEP = "@@OBS_SEPARATOR@@"
PROMPT = re.compile(r"T2EX >>")


def clean(text: str) -> list[str]:
    text = re.sub(r"\x1b\[[0-9;]*m", "", text).replace("\r", "")
    out = []
    for line in text.split("\n"):
        # Drop shell prompts and the filler bytes that come with them.
        line = PROMPT.sub("", line)
        line = "".join(ch for ch in line if ch == "\t" or (" " <= ch and ch != "�"))
        if line.strip():
            out.append(line.rstrip())
    return out


def new_lines(before: list[str], after: list[str]) -> list[str]:
    matcher = difflib.SequenceMatcher(a=before, b=after, autojunk=False)
    added = []
    for tag, _, _, j1, j2 in matcher.get_opcodes():
        if tag in ("insert", "replace"):
            added.extend(after[j1:j2])
    return added


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wait", type=float, default=1.0)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--dump-only", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    words = args.command[1:] if args.command[:1] == ["--"] else args.command
    command = " ".join(words)
    if not args.dump_only and not command:
        parser.error("no command")

    if args.dump_only:
        remote = "st cap log 2>&1"
    else:
        remote = (f"st cap log > /tmp/obs_a.txt 2>&1; {command} > /tmp/obs_cmd.txt 2>&1; "
                  f"echo rc=$? >> /tmp/obs_cmd.txt; sleep {args.wait:g}; "
                  f"st cap log > /tmp/obs_b.txt 2>&1; cat /tmp/obs_a.txt; echo {SEP}; "
                  f"cat /tmp/obs_cmd.txt; echo {SEP}; cat /tmp/obs_b.txt; "
                  f"rm -f /tmp/obs_a.txt /tmp/obs_cmd.txt /tmp/obs_b.txt")
    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    boot, body, elapsed = camsh.run(remote, args.timeout)

    if args.dump_only:
        result = clean(body)
        stdout_part = []
    else:
        parts = body.split(SEP)
        if len(parts) != 3:
            print(f"unexpected reply ({len(parts)} parts)", file=sys.stderr)
            print(body[-2000:], file=sys.stderr)
            return 2
        before, stdout_part, after = clean(parts[0]), clean(parts[1]), clean(parts[2])
        result = new_lines(before, after)

    lines = []
    if stdout_part:
        lines.append("## command stdout")
        lines.extend(stdout_part)
        lines.append("## new RTOS log lines")
    lines.extend(result)
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        header = (f"# command: {command or 'st cap log'}\n# host_time: {started}\n"
                  f"# elapsed_s: {elapsed:.2f}\n# boot_id: {boot}\n")
        args.out.write_text(header + text)
    print(f"[elapsed {elapsed:.2f}s, boot {boot or '?'}, {len(result)} lines]", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
