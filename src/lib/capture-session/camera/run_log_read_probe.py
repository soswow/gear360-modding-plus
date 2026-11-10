#!/usr/bin/env python3
"""Run a read-only shared-memory probe on the camera and collect what it read.

The probes staged by this runner only open the shared preference window
through libuser-spec's pref_rtos_get_long and report what they find.  They
never call into the capture library, the bridge, or the stock application,
so they need no application stop and they cannot end the camera's boot.

The run is deliberately small and self-contained rather than a mode of
run_init_probe.py: that runner exists to stop the stock application and to
run a capture, and neither is wanted here.

Every step is recorded in the output directory, and the remote directory is
removed again with its removal verified.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import sys
import uuid
from pathlib import Path
from typing import Any, Dict, List

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import run_load_check as runner  # noqa: E402
from run_load_check import (  # noqa: E402
    Camera,
    GuardError,
    HttpFtpCamera,
    Recorder,
    is_generated_remote_dir,
    now_iso,
)


def camera_state(camera: Camera) -> Dict[str, Any]:
    """The camera's own health markers: boot, application, load."""
    boot = camera.execute(
        "cat /proc/sys/kernel/random/boot_id 2>&1; "
        "uptime 2>&1; "
        "cat /proc/loadavg 2>&1",
        timeout=30.0,
    )
    app = camera.execute(
        "systemctl is-active di-camera-app 2>&1; "
        "for f in /proc/[0-9]*/comm; do c=$(cat $f 2>/dev/null); "
        "[ \"$c\" = di-camera-app ] && echo APP=$f; done",
        timeout=30.0,
    )
    return {"boot_and_load": boot, "stock_application": app}


def run_probe(camera: Camera, remote_dir: str, command: str,
              fetch_names: List[str], recorder: Recorder) -> Dict[str, Any]:
    """Run the staged probe once and collect the files it wrote."""
    recorder.event("execute.begin", command=command)
    result: Dict[str, Any] = {"command": command}
    try:
        response = camera.execute(command, timeout=120.0)
        result["response"] = response
        recorder.update(execute_response=response[:4000])
    except Exception as exc:  # a transport failure is not a probe result
        result["error"] = f"{type(exc).__name__}: {exc}"
        recorder.event("execute.failed", error=result["error"])
    for name in fetch_names:
        try:
            data = camera.get(f"{remote_dir}/{name}")
            recorder.save_bytes(name, data)
            result.setdefault("fetched", {})[name] = len(data)
        except Exception as exc:
            result.setdefault("fetch_errors", {})[name] = \
                f"{type(exc).__name__}: {exc}"
            recorder.event("fetch.failed", name=name, error=result["fetch_errors"][name])
    return result


def remove_remote_dir(camera: Camera, remote_dir: str, recorder: Recorder) -> Dict[str, Any]:
    """Remove the owned remote directory and verify that it is gone."""
    if not is_generated_remote_dir(remote_dir):
        raise GuardError(f"the remote path {remote_dir} is not one this runner owns")
    quoted = shlex.quote(remote_dir)
    text = camera.execute(f"rm -rf {quoted}; test -e {quoted} && echo PRESENT || echo GONE")
    recorder.event("cleanup", remote_dir=remote_dir, result=text.strip()[:200])
    return {"result": text.strip()[:200], "removed": "GONE" in text}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.43.1")
    parser.add_argument("--helper", type=Path, required=True,
                        help="the probe binary to stage")
    parser.add_argument("--helper-sha256", required=True,
                        help="the expected sha256 of the staged binary")
    parser.add_argument("--probe-name", default="log-read-probe",
                        help="the staged binary's name on the camera")
    parser.add_argument("--command", required=True,
                        help="the command to run; {dir} is the remote directory")
    parser.add_argument("--fetch", default="",
                        help="comma-separated names to fetch from the remote directory")
    parser.add_argument("--output", type=Path, required=True,
                        help="the local directory for the record")
    args = parser.parse_args()

    fetch_names = [name.strip() for name in args.fetch.split(",") if name.strip()]

    helper_bytes = args.helper.read_bytes()
    digest = hashlib.sha256(helper_bytes).hexdigest()
    if digest != args.helper_sha256.lower():
        raise SystemExit(
            f"the helper sha256 is {digest}, not the expected "
            f"{args.helper_sha256.lower()}"
        )

    out_dir = args.output
    recorder = Recorder(out_dir)
    camera = HttpFtpCamera(host=args.host)

    remote_dir = f"/tmp/{runner.REMOTE_DIR_PREFIX}{uuid.uuid4().hex[:12]}"
    if not is_generated_remote_dir(remote_dir):
        raise SystemExit(f"the generated remote path {remote_dir} is not safe")

    recorder.update(helper_sha256=digest, remote_dir=remote_dir,
                    host=args.host, started_local=now_iso())

    result: Dict[str, Any] = {"helper_sha256": digest, "remote_dir": remote_dir}
    try:
        recorder.update(state_before=camera_state(camera))

        camera.mkdir(remote_dir)
        recorder.event("mkdir", remote_dir=remote_dir)

        remote_path = f"{remote_dir}/{args.probe_name}"
        camera.put(remote_path, helper_bytes)
        fetched = camera.get(remote_path)
        if fetched != helper_bytes:
            raise GuardError(
                f"the staged probe is {len(fetched)} bytes on the camera, not the "
                f"{len(helper_bytes)} bytes of the local binary"
            )
        remote_digest = camera.remote_sha256(remote_path)
        if remote_digest != digest:
            raise GuardError(
                f"the staged probe has sha256 {remote_digest} on the camera, not "
                f"the expected {digest}"
            )
        camera.execute(f"chmod 500 {shlex.quote(remote_path)}")
        recorder.event("stage.done", path=remote_path, sha256=remote_digest)

        command = args.command.format(dir=remote_dir)
        result["probe"] = run_probe(camera, remote_dir, command, fetch_names, recorder)
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
        recorder.event("run.failed", error=result["error"])
    finally:
        try:
            result["cleanup"] = remove_remote_dir(camera, remote_dir, recorder)
        except Exception as exc:
            result["cleanup"] = {"error": f"{type(exc).__name__}: {exc}"}
            recorder.event("cleanup.failed", error=result["cleanup"]["error"])
        try:
            result["state_after"] = camera_state(camera)
        except Exception as exc:
            result["state_after"] = {"error": f"{type(exc).__name__}: {exc}"}

    recorder.update(result=result)
    print(json.dumps(result, indent=2, sort_keys=True)[:4000])
    return 0 if not result.get("error") else 1


if __name__ == "__main__":
    sys.exit(main())
