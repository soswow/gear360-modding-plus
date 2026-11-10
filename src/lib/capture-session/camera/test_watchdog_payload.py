#!/usr/bin/env python3
"""Host checks for the SDK-free watchdog qualification payload.

These are the checks the packet asks for and nothing more: that the
payload refuses malformed or extra arguments and a log path that is not
the runner's generated one, and that it writes the success marker when
it is given exactly the runner's arguments.

The endless variant (WATCHDOG_TEST_HANG=1) is exercised only as a
bounded child of this script: the script owns the child, waits with a
deadline, terminates it and checks that the marker was written before
the child stopped.

Nothing here touches the camera, the dump or the network, and every
directory it writes is a temporary one under /tmp.
"""

import os
import shutil
import signal
import subprocess
import sys
import tempfile
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SOURCE = os.path.join(HERE, "watchdog-payload.c")
CLANG = "/opt/homebrew/opt/llvm/bin/clang"

LIBRARY = "/usr/lib/libcapture-fw-prod.so"
MARKER = "SDK-free watchdog qualification"

failures = []
checks = 0


def check(name, condition, detail=""):
    global checks
    checks += 1
    if condition:
        print("ok   %s" % name)
    else:
        print("FAIL %s %s" % (name, detail))
        failures.append(name)


def build(workdir, extra=()):
    out = os.path.join(workdir, "watchdog-payload" + "".join(extra))
    command = [
        CLANG, "-std=c99", "-Wall", "-Wextra", "-O2",
        "-I", ROOT, SOURCE, "-o", out,
    ] + list(extra)
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise SystemExit("the host build failed")
    return out


def generated_dir():
    return "/tmp/gear360-load-" + uuid.uuid4().hex[:12]


def run(binary, args):
    return subprocess.run(
        [binary] + args, capture_output=True, text=True, timeout=30
    )


def refuses(name, binary, args, path=None):
    """The payload must refuse and must not create the log path."""
    result = run(binary, args)
    check(name + ": refused", result.returncode != 0,
          "returncode %d" % result.returncode)
    check(name + ": nothing written", path is None or not os.path.exists(path),
          "the log path exists")
    check(name + ": said why", "refusing" in result.stdout,
          repr(result.stdout))


def main():
    workdir = tempfile.mkdtemp(prefix="watchdog-payload-host-")
    try:
        binary = build(workdir)

        # 1. The runner's exact arguments: the marker is printed and written.
        remote = generated_dir()
        os.makedirs(remote)
        progress = os.path.join(remote, "progress")
        try:
            result = run(binary, [
                "--load-only", "--library", LIBRARY, "--log", progress,
            ])
            check("success: exit 0", result.returncode == 0,
                  "returncode %d" % result.returncode)
            check("success: marker printed", MARKER in result.stdout,
                  repr(result.stdout))
            check("success: marker written",
                  os.path.exists(progress) and
                  MARKER in open(progress).read(), "the log path is wrong")
        finally:
            shutil.rmtree(remote, ignore_errors=True)

        # 2. Malformed and extra arguments.
        refuses("no arguments", binary, [])
        refuses("missing --log", binary, ["--load-only", "--library", LIBRARY])
        refuses("extra argument", binary, [
            "--load-only", "--library", LIBRARY, "--log", "/tmp/x", "extra",
        ])
        refuses("wrong mode", binary, [
            "--capture", "--library", LIBRARY, "--log", "/tmp/x/progress",
        ])
        refuses("wrong library", binary, [
            "--load-only", "--library", "/usr/lib/libother.so",
            "--log", "/tmp/x/progress",
        ])
        refuses("options swapped", binary, [
            "--library", LIBRARY, "--load-only", "--log", "/tmp/x/progress",
        ])

        # 3. Log paths that are not the runner's generated one.
        bad_paths = [
            "/tmp/gear360-load-0123456789ab/progressx",
            "/tmp/gear360-load-0123456789ab/progress/",
            "/tmp/gear360-load-0123456789a/progress",
            "/tmp/gear360-load-0123456789abc/progress",
            "/tmp/gear360-load-0123456789AB/progress",
            "/tmp/gear360-load-0123456789ab/prog",
            "/tmp/gear360-load-/progress",
            "/tmp/gear360-load-0123456789ab",
            "/var/tmp/gear360-load-0123456789ab/progress",
            "/tmp/gear360-load-0123456789ab/../progress",
            "/etc/passwd",
        ]
        for path in bad_paths:
            refuses("path %s" % path, binary, [
                "--load-only", "--library", LIBRARY, "--log", path,
            ])

        # 4. The endless variant, as a bounded child of this script.
        hang_binary = build(workdir, ["-DWATCHDOG_TEST_HANG=1"])
        remote = generated_dir()
        os.makedirs(remote)
        progress = os.path.join(remote, "progress")
        child = None
        try:
            child = subprocess.Popen(
                [hang_binary, "--load-only", "--library", LIBRARY,
                 "--log", progress],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            try:
                child.wait(timeout=3)
                check("hang: still running", False, "the child exited by itself")
            except subprocess.TimeoutExpired:
                check("hang: still running", True)
            check("hang: marker written before hanging",
                  os.path.exists(progress) and
                  MARKER in open(progress).read(),
                  "the log path is wrong")
            child.send_signal(signal.SIGTERM)
            try:
                child.wait(timeout=5)
                check("hang: terminated", True)
            except subprocess.TimeoutExpired:
                check("hang: terminated", False, "the child ignored SIGTERM")
        finally:
            if child is not None and child.poll() is None:
                child.kill()
                child.wait(timeout=5)
            shutil.rmtree(remote, ignore_errors=True)

        print("\n%d checks, %d failures" % (checks, len(failures)))
        return 1 if failures else 0
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
