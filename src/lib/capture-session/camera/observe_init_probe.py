#!/usr/bin/env python3
"""Read-only, bounded observer for a running init-only capture-shot probe."""
from __future__ import annotations

import importlib.util
import json
import re
import shlex
import sys
import time
from pathlib import Path
from typing import Any


RUNNER = Path(__file__).with_name("run_load_check.py")
MAX_SECONDS = 55.0
PARTIAL_AFTER_SECONDS = 8.0
POLL_SECONDS = 0.25
HOLD_MARKER = "capture-shot: before probe hold"


def _runner():
    existing = sys.modules.get("gear360_observe_run_load_check")
    if existing is not None:
        return existing
    spec = importlib.util.spec_from_file_location("gear360_observe_run_load_check", RUNNER)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {RUNNER}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _save(path: Path, record: dict[str, Any]) -> dict[str, Any]:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return record


def _camera_with_timeout(camera: Any, seconds: float, runner: Any) -> Any:
    """Rebuild the camera with the time that is left, when that is possible.

    The check must not depend on which import of the runner file the camera's
    class came from.  ``isinstance`` against the runner module *this* observer
    loaded fails for a camera built from a second import of the same file, and
    the observer would then keep the camera's own unbounded timeout instead
    of the remaining budget - which is how a slow camera turns into an
    observer that overruns its own deadline.  The class name and the
    constructor's own attributes are therefore checked as well.
    """
    with_timeout = getattr(camera, "with_timeout", None)
    if callable(with_timeout):
        return with_timeout(seconds, runner)
    cls = type(camera)
    is_http_camera = isinstance(camera, runner.HttpFtpCamera) or (
        cls.__name__ == "HttpFtpCamera"
        and hasattr(camera, "host")
        and hasattr(camera, "timeout")
    )
    if is_http_camera:
        return cls(camera.host, timeout=max(0.1, seconds))
    return camera


def _snapshot_command(pid: str, tick: str, helper: str) -> str:
    q = shlex.quote
    # The camera CGI strips literal newlines; every separator is explicit.
    return "; ".join([
        f"pid={pid}", f"expected_tick={tick}", f"expected_exe={q(helper)}",
        "printf 'PID=%s\\n' \"$pid\"",
        "before_tick=$(awk '{print $22}' \"/proc/$pid/stat\" 2>/dev/null)",
        'before_exe=$(readlink "/proc/$pid/exe" 2>/dev/null)',
        "printf 'BEFORE_TICK=%s\\nBEFORE_EXE=%s\\n' \"$before_tick\" \"$before_exe\"",
        '[ "$before_tick" = "$expected_tick" ] || { echo GUARD_FAIL=pid-tick-before; exit 0; }',
        '[ "$before_exe" = "$expected_exe" ] || { echo GUARD_FAIL=exe-before; exit 0; }',
        'echo FD_BEGIN',
        'for fd in "/proc/$pid/fd"/*; do [ -L "$fd" ] || continue; '
        'printf \'FD|%s|%s\\n\' "${fd##*/}" "$(readlink "$fd" 2>/dev/null)"; done',
        'echo TASK_BEGIN',
        'for task in "/proc/$pid/task"/*; do [ -d "$task" ] || continue; '
        'tid=${task##*/}; comm=$(cat "$task/comm" 2>/dev/null) || continue; '
        'wchan=$(cat "$task/wchan" 2>/dev/null); '
        'printf \'THREAD|%s|%s|%s\\n\' "$tid" "$comm" "$wchan"; '
        'printf \'STATUS_BEGIN|%s\\n\' "$tid"; cat "$task/status" 2>/dev/null; echo STATUS_END; done',
        'echo TASK_END', 'echo FD_END',
        "after_tick=$(awk '{print $22}' \"/proc/$pid/stat\" 2>/dev/null)",
        'after_exe=$(readlink "/proc/$pid/exe" 2>/dev/null)',
        "printf 'AFTER_TICK=%s\\nAFTER_EXE=%s\\n' \"$after_tick\" \"$after_exe\"",
        '[ "$after_tick" = "$expected_tick" ] || { echo GUARD_FAIL=pid-tick-after; exit 0; }',
        '[ "$after_exe" = "$expected_exe" ] || { echo GUARD_FAIL=exe-after; exit 0; }',
        'echo SNAPSHOT_END',
    ])


def _field(text: str, name: str) -> str | None:
    values = re.findall(rf"^{re.escape(name)}=(.*)$", text, re.M)
    return values[0] if len(values) == 1 else None


def observe_init_probe(camera: Any, expected_boot: str, remote_dir: str,
                       output_path: str | Path,
                       should_stop: Any = None) -> dict[str, Any]:
    """Poll the owned run and save a guarded /proc metadata snapshot.

    Intended for a parent-owned background thread while the probe execute call
    is active. This function never stages, signals, cleans up, reads memory, or
    interacts with an SDK. All failures are saved as an explicit non-snapshot.

    `should_stop`, when given, is called between polls.  When it returns true
    the probe is over, there is nothing left to snapshot, and the observation
    ends with the status `stopped` instead of waiting out the deadline.
    """
    runner = _runner()
    output = Path(output_path)
    if not runner.is_generated_remote_dir(remote_dir):
        raise ValueError("remote_dir must be an owned /tmp/gear360-load-<12 lowercase hex> path")
    if not isinstance(expected_boot, str) or re.fullmatch(
            r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}:[0-9]+", expected_boot) is None:
        raise ValueError("expected_boot must be a lowercase UUID followed by ':' and decimal btime")

    started = time.monotonic()
    deadline = started + MAX_SECONDS
    record: dict[str, Any] = {
        "expected_boot": expected_boot,
        "remote_dir": remote_dir,
        "status": "waiting",
        "owner": None,
        "progress": "",
        "proc_raw": None,
        "reason": None,
    }
    owner: tuple[str, str] | None = None
    progress = ""
    snapshot_now = False

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        bounded = _camera_with_timeout(camera, remaining, runner)
        try:
            owner_bytes = bounded.get(f"{remote_dir}/owner")
            owner = runner.parse_owner(owner_bytes.decode("utf-8", "strict"))
        except Exception:
            owner = None
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        bounded = _camera_with_timeout(camera, remaining, runner)
        try:
            progress = bounded.get(f"{remote_dir}/progress").decode("utf-8", "replace")
        except Exception:
            progress = ""
        elapsed = time.monotonic() - started
        if HOLD_MARKER in progress:
            snapshot_now = True
            record["status"] = "hold-marker"
            record["trigger"] = "hold-marker"
            break
        if elapsed >= PARTIAL_AFTER_SECONDS:
            snapshot_now = True
            record["status"] = "partial-stall"
            record["trigger"] = "partial-stall"
            break
        if should_stop is not None:
            try:
                stopped = bool(should_stop())
            except Exception:
                stopped = False
            if stopped:
                # The probe is over: there is no live helper left to read.
                record["status"] = "stopped"
                record["trigger"] = "probe-finished"
                break
        time.sleep(min(POLL_SECONDS, max(0.0, deadline - time.monotonic())))

    record["owner"] = {"pid": owner[0], "start_tick": owner[1]} if owner else None
    record["progress"] = progress
    if record["status"] == "stopped":
        record["reason"] = "the probe finished before a snapshot could be taken"
        record["elapsed_seconds"] = time.monotonic() - started
        return _save(output, record)
    if not snapshot_now or owner is None:
        record["status"] = "partial-stall" if snapshot_now else "deadline"
        record["reason"] = "owner file unavailable or invalid before bounded observation point"
        record["elapsed_seconds"] = time.monotonic() - started
        return _save(output, record)

    pid, tick = owner
    if not pid.isdigit() or not tick.isdigit() or int(pid) <= 0:
        record.update(status="refused", reason="invalid numeric owner identity")
        return _save(output, record)

    remaining = deadline - time.monotonic()
    if remaining <= 0:
        record.update(status="deadline", reason="deadline reached before snapshot")
        return _save(output, record)
    bounded = _camera_with_timeout(camera, remaining, runner)
    try:
        boot_before = runner.boot_identity(bounded)
    except Exception as exc:
        record.update(status="refused", reason=f"boot identity read failed: {type(exc).__name__}")
        return _save(output, record)
    record["boot_before"] = boot_before
    if boot_before != expected_boot:
        record.update(status="refused", reason="boot identity changed before snapshot")
        return _save(output, record)

    helper = f"{remote_dir}/capture-shot"
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        record.update(status="deadline", reason="deadline reached before /proc metadata read")
        return _save(output, record)
    bounded = _camera_with_timeout(camera, remaining, runner)
    try:
        proc_raw = bounded.execute(_snapshot_command(pid, tick, helper), timeout=remaining)
    except Exception as exc:
        record.update(status="refused", reason=f"/proc metadata request failed: {type(exc).__name__}")
        return _save(output, record)
    record["proc_raw"] = proc_raw
    fail_reason = _field(proc_raw, "GUARD_FAIL")
    before_tick, before_exe = _field(proc_raw, "BEFORE_TICK"), _field(proc_raw, "BEFORE_EXE")
    after_tick, after_exe = _field(proc_raw, "AFTER_TICK"), _field(proc_raw, "AFTER_EXE")
    if fail_reason:
        record.update(status="refused", reason=fail_reason)
        return _save(output, record)
    if before_tick != tick or after_tick != tick:
        record.update(status="refused", reason="PID start tick did not match owner before and after read")
        return _save(output, record)
    if before_exe != helper or after_exe != helper:
        record.update(status="refused", reason="executable path did not match owned helper before and after read")
        return _save(output, record)
    if re.search(r"^SNAPSHOT_END$", proc_raw, re.M) is None:
        record.update(status="refused", reason="/proc response incomplete")
        return _save(output, record)
    if _field(proc_raw, "PID") != pid:
        record.update(status="refused", reason="/proc response PID did not match owner")
        return _save(output, record)

    remaining = deadline - time.monotonic()
    if remaining <= 0:
        record.update(status="deadline", reason="deadline reached before final boot check")
        return _save(output, record)
    bounded = _camera_with_timeout(camera, remaining, runner)
    try:
        boot_after = runner.boot_identity(bounded)
    except Exception as exc:
        record.update(status="refused", reason=f"final boot identity read failed: {type(exc).__name__}")
        return _save(output, record)
    record["boot_after"] = boot_after
    if boot_after != expected_boot or boot_after != boot_before:
        record.update(status="refused", reason="boot identity changed during snapshot")
        return _save(output, record)

    record.update(status="snapshot", reason=None, elapsed_seconds=time.monotonic() - started)
    return _save(output, record)
