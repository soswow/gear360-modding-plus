#!/usr/bin/env python3
"""Run one shell command on the camera through the SD mod's CGI and keep the output.

Uses the host's curl (the harness process cannot route to the camera's access
point, Apple-signed curl can). Prints the command's output; with --out it also
writes a file whose header records the command, the host time, the elapsed
time and the camera's boot id read in the same request.

    python3 camsh.py [--out FILE] [--timeout SECONDS] [--no-boot-id] -- <command>
"""

import argparse
import html
import re
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path

# The camera is on its own access point (192.168.43.1, fixed by its firmware) or,
# after g360wifi join / the app's WIFI: HOME setting, on a network of yours at an
# address your router leased (it can change). Those addresses are not kept in
# the repository: list them, one per line, in <repo>/camera-hosts (git-ignored;
# copy camera-hosts.example). GEAR360_HOST overrides everything; otherwise the
# first candidate whose port 8888 answers wins.
ACCESS_POINT = "192.168.43.1"
HOSTS_FILE = Path(__file__).resolve().parents[2] / "camera-hosts"


def candidate_hosts() -> list[str]:
    """Addresses from camera-hosts in file order, then the access point."""
    hosts: list[str] = []
    try:
        lines = HOSTS_FILE.read_text().splitlines()
    except OSError:
        lines = []
    for line in lines:
        host = line.split("#", 1)[0].strip()
        if host and host not in hosts:
            hosts.append(host)
    if ACCESS_POINT not in hosts:
        hosts.append(ACCESS_POINT)
    return hosts


def find_host() -> str:
    import os
    import socket
    if os.environ.get("GEAR360_HOST"):
        return os.environ["GEAR360_HOST"]
    candidates = candidate_hosts()
    for host in candidates:
        try:
            socket.create_connection((host, 8888), timeout=1.5).close()
            return host
        except OSError:
            continue
    raise OSError("camera not reachable at " + " or ".join(candidates)
                  + ": join the computer to the camera's AP or your network, add the "
                  "camera's address to camera-hosts, or set GEAR360_HOST")


HOST = find_host()
BOOT_MARK = "@@BOOT_ID@@"


def run(command: str, timeout: float = 60.0, boot_id: bool = True) -> tuple[str, str, float]:
    """Return (boot id or "", command output, elapsed seconds)."""
    remote = command
    if boot_id:
        remote = f"echo {BOOT_MARK}$(cat /proc/sys/kernel/random/boot_id); {command}"
    url = f"http://{HOST}:8888/cgi-bin/execute?rawcmd=" + urllib.parse.quote(remote)
    start = time.monotonic()
    result = subprocess.run(["curl", "-s", "-m", f"{timeout:g}", url],
                            capture_output=True, timeout=timeout + 10)
    elapsed = time.monotonic() - start
    if result.returncode != 0:
        raise OSError(f"curl rc={result.returncode} after {elapsed:.1f}s: "
                      f"{result.stderr.decode('utf-8', 'replace')[:300]}")
    text = result.stdout.decode("utf-8", "replace")
    match = re.search(r"<pre>\n?(.*)</pre>", text, re.S)
    body = html.unescape(match.group(1) if match else text)
    # The CGI echoes "Executing:  <command>" as its first line.
    lines = body.split("\n")
    if lines and lines[0].startswith("Executing:"):
        lines = lines[1:]
    found = ""
    if boot_id and lines and lines[0].startswith(BOOT_MARK):
        found = lines[0][len(BOOT_MARK):].strip()
        lines = lines[1:]
    return found, "\n".join(lines), elapsed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--no-boot-id", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    words = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not words:
        parser.error("no command")
    command = " ".join(words)
    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    boot, output, elapsed = run(command, args.timeout, not args.no_boot_id)
    sys.stdout.write(output)
    if not output.endswith("\n"):
        sys.stdout.write("\n")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        header = (f"# command: {command}\n# host_time: {started}\n"
                  f"# elapsed_s: {elapsed:.2f}\n# boot_id: {boot}\n")
        args.out.write_text(header + output + ("" if output.endswith("\n") else "\n"))
    print(f"[elapsed {elapsed:.2f}s, boot {boot or '?'}]", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
