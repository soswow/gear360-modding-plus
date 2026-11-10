#!/usr/bin/env python3
"""Run one fixed camera diagnostic, with the observer already watching.

This is the fixed-purpose orchestration path for the next receiver-verification
trial.  It reuses the guarded runner's guards, staging, watchdog, recovery and
cleanup code.  It does not add a mode to the runner and does not change the
runner's `--load-only` contract: the runner still runs a load-only helper and
nothing else.

What this path adds over running the pieces by hand:

  * the observer is started **before** the probe is executed, so the guarded
    /proc snapshot is taken while the helper is still alive;
  * the evidence - the probe's stdout, stderr, progress, status and owner
    files, the staged script bytes and the observer's own record - is kept in
    the run directory whether the trial succeeds or fails;
  * the generated directory is kept when the helper's termination could not be
    confirmed, because it still holds the owner file and the status file that
    are needed to finish the job;
  * the SDK return, guarded helper-process snapshot, and required local
    descriptor/thread metadata are reported separately. Remote service
    readiness is never established by this path.

Nothing in this module contacts the camera on import, and running the module
as a script requires an explicit host, helper, digest and output directory.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple

HERE = Path(__file__).resolve().parent

RUNNER_MODULE = "gear360_run_load_check"
OBSERVER_MODULE = "gear360_observe_init_probe"
PREPARE_MODULE = "gear360_prepare_init_probe"
PINNED_BRIDGE = "/usr/lib/libmulticore-bridge.so"
PINNED_BRIDGE_SHA256 = "9129761c6ee9f6b62968d3a8307a8b5fbf02e1493323658c42d9ebe587673a20"


def _load(name: str, filename: str) -> Any:
    """Import a sibling module under a stable name, once per process."""
    existing = sys.modules.get(name)
    if existing is not None:
        return existing
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {filename}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def runner_module() -> Any:
    return _load(RUNNER_MODULE, "run_load_check.py")


def observer_module() -> Any:
    return _load(OBSERVER_MODULE, "observe_init_probe.py")


def prepare_module() -> Any:
    return _load(PREPARE_MODULE, "prepare_init_probe.py")


def observer_camera_class() -> Any:
    """The observer's own ``HttpFtpCamera`` class.

    The observer applies its remaining-timeout logic by rebuilding the camera
    it was given, and it decides whether it may do that with an ``isinstance``
    check against the runner module **it** loaded.  A camera built from a
    different import of the same file is a different class object, so the
    check would fail and the observer would keep the unbounded timeout of the
    camera it was handed.  Taking the class from the observer's own runner
    makes the identity hold.
    """
    observer = observer_module()
    return observer._runner().HttpFtpCamera


@dataclass
class ProbeResult:
    """The trial's answer, with the three levels kept apart."""

    ok: bool = False
    interrupted: bool = False
    helper_termination: str = "not-started"
    diagnostic_mode: str = "init-only"
    still_completion: str = "not-established"
    request_association: str = "not-established"
    raw_ownership: str = "not-established"
    outcome: str = "not-run"
    rc: Optional[int] = None
    #: the probe process itself returned 0
    sdk_returned: bool = False
    #: a guarded /proc snapshot of that exact helper was taken
    worker_observed: bool = False
    #: the observer's own verdict: snapshot, partial-stall, deadline, ...
    worker_observation: str = "not-attempted"
    #: Process metadata includes the required local transport descriptors and threads.
    local_transport_observed: bool = False
    local_transport_missing: List[str] = field(default_factory=lambda: [
        "fd:d5_ipcc", "fd:d5_lock", "thread:udd_ipcc_fmt_in",
        "thread:MCB_Receiver", "thread:MCB_Sender",
    ])
    #: never established by this path
    remote_readiness: str = "not-established"
    remote_dir: str = ""
    cleanup_verified: bool = False
    residual_path: str = ""
    errors: List[str] = field(default_factory=list)
    uncertainty: List[str] = field(default_factory=list)

    def console(self) -> Dict[str, Any]:
        return {
            "ok": self.ok,
            "interrupted": self.interrupted,
            "helper_termination": self.helper_termination,
            "diagnostic_mode": self.diagnostic_mode,
            "still_completion": self.still_completion,
            "request_association": self.request_association,
            "raw_ownership": self.raw_ownership,
            "sdk_returned_meaning": "helper process returned zero; not still completion",
            "outcome": self.outcome,
            "rc": self.rc,
            "sdk_returned": self.sdk_returned,
            "worker_observed": self.worker_observed,
            "worker_observation": self.worker_observation,
            "local_transport_observed": self.local_transport_observed,
            "local_transport_missing": list(self.local_transport_missing),
            "remote_readiness": self.remote_readiness,
            "remote_dir": self.remote_dir,
            "cleanup_verified": self.cleanup_verified,
            "residual_path": self.residual_path,
            "errors": list(self.errors),
            "uncertainty": list(self.uncertainty),
        }


class _InterruptibleObserverCamera:
    """Do not start another observer camera call after interruption.

    An operation already in flight cannot be cancelled by this wrapper.
    """
    def __init__(self, camera: Any, interrupted: threading.Event) -> None:
        self._camera = camera
        self._interrupted = interrupted

    def with_timeout(self, seconds: float, runner: Any) -> "_InterruptibleObserverCamera":
        bounded = observer_module()._camera_with_timeout(self._camera, seconds, runner)
        return _InterruptibleObserverCamera(bounded, self._interrupted)

    def get(self, *args: Any, **kwargs: Any) -> Any:
        if self._interrupted.is_set():
            raise RuntimeError("observer cancelled after interruption")
        return self._camera.get(*args, **kwargs)

    def execute(self, *args: Any, **kwargs: Any) -> Any:
        if self._interrupted.is_set():
            raise RuntimeError("observer cancelled after interruption")
        return self._camera.execute(*args, **kwargs)


class _ObserverThread(threading.Thread):
    """Runs the observer once and keeps whatever it produced.

    It is a daemon thread: a stuck observer must never hold the trial open
    or keep the process alive.  The trial bounds the join instead and records
    an uncertainty when the observer did not finish.
    """

    def __init__(self, observer: Any, camera: Any, expected_boot: str,
                 remote_dir: str, output_path: Path,
                 should_stop: Callable[[], bool]) -> None:
        super().__init__(name="init-probe-observer", daemon=True)
        self._observer = observer
        self._camera = camera
        self._expected_boot = expected_boot
        self._remote_dir = remote_dir
        self._output_path = output_path
        self._should_stop = should_stop
        self.record: Optional[Dict[str, Any]] = None
        self.error: Optional[str] = None

    def run(self) -> None:
        try:
            self.record = self._observer.observe_init_probe(
                self._camera, self._expected_boot, self._remote_dir,
                self._output_path, should_stop=self._should_stop,
            )
        except Exception as exc:  # a failed observer is evidence, not a crash
            self.error = f"{type(exc).__name__}: {exc}"


def _read_observer_record(path: Path) -> Dict[str, Any]:
    """Read the observer's record; a missing or broken one is a stated fact."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return {"status": "no-record", "reason": f"the observer record is unreadable: {exc}"}
    try:
        record = json.loads(text)
    except ValueError as exc:
        return {"status": "no-record", "reason": f"the observer record is not JSON: {exc}"}
    if not isinstance(record, dict):
        return {"status": "no-record", "reason": "the observer record is not an object"}
    return record


def _strip_echo(text: str) -> str:
    """Drop the camera's echo of the command.

    The execute endpoint answers with the command it ran, followed by that
    command's output.  The echo contains the record markers themselves, so it
    has to go before anything is parsed out of the reply - otherwise the
    parser reads its own command back as a record.
    """
    if "\n" in text:
        return text.split("\n", 1)[1]
    return ""


def scan_d5_holders(camera: Any) -> List[Dict[str, str]]:
    """Every process holding a descriptor on a d5_* device, from a full scan.

    The command is one line with explicit semicolon separators: the camera's
    CGI endpoint strips literal newlines, so a multi-line command would be
    concatenated into one statement.  The records are marked with a leading
    "H|" so the parse does not depend on the reply keeping its line breaks.
    """
    command = (
        'for d in /proc/[0-9]*; do [ -d "$d" ] || continue; p=${d##*/}; '
        'c=$(cat "$d"/comm 2>/dev/null) || { [ -d "$d" ] && echo SCAN_ERROR=comm:$p; continue; }; '
        '[ -n "$c" ] || { [ -d "$d" ] && echo SCAN_ERROR=comm:$p; continue; }; '
        'for f in "$d"/fd/*; do [ -e "$f" ] || [ -L "$f" ] || continue; '
        't=$(readlink "$f" 2>/dev/null) || { if [ -e "$f" ] || [ -L "$f" ]; then '
        'echo SCAN_ERROR=readlink:$p:${f##*/}; fi; continue; }; '
        '[ -n "$t" ] || { if [ -e "$f" ] || [ -L "$f" ]; then '
        'echo SCAN_ERROR=readlink:$p:${f##*/}; fi; continue; }; '
        'case "$t" in *d5_*) printf "H|%s|%s|%s|%s\\n" "$p" "${f##*/}" "$t" "$c";; '
        'esac; done; done; echo SCAN_END'
    )
    text = _strip_echo(camera.execute(command, timeout=30.0))
    if (len(re.findall(r"^SCAN_END$", text, re.M)) != 1
            or text.strip().splitlines()[-1:] != ["SCAN_END"]
            or re.search(r"^SCAN_ERROR=", text, re.M)):
        raise runner_module().GuardError("device-holder scan was incomplete")
    holders: List[Dict[str, str]] = []
    for line in text.splitlines():
        if not line.startswith("H|"):
            continue
        parts = line.split("|")
        if (len(parts) != 5 or not parts[1].isdigit() or not parts[2].isdigit()
                or not parts[3] or not parts[4]):
            raise runner_module().GuardError("device-holder scan had a malformed row")
        holders.append({"pid": parts[1], "fd": parts[2],
                        "device": parts[3], "comm": parts[4]})
    return holders


def collect_guards_absent(camera: Any, cfg: Any, recorder: Any) -> Dict[str, Any]:
    """The runner's guards, minus the camera application's live identity.

    The application identity - its pid and start tick - exists so that the
    stop request can only reach that exact process.  When the application is
    already stopped there is no stop target to pin, so the identity does not
    apply; the boot identity and the file digests still pin the trial, and
    the application's own file digest is checked, so the binary that *would*
    have been running is still the pinned one.

    This is the runner's own check with the one step that needs a live
    process removed, and it is only used when the runner's check failed
    *because* there is no process.
    """
    runner = runner_module()
    helper_bytes = cfg.helper.read_bytes()
    digest = hashlib.sha256(helper_bytes).hexdigest()
    if digest != cfg.helper_sha256.lower():
        raise runner.GuardError(
            f"the helper sha256 is {digest}, not the expected "
            f"{cfg.helper_sha256.lower()}"
        )
    recorder.event("guard.helper", helper_sha256=digest)
    boot = runner.boot_identity(camera)
    recorder.event("guard.boot_identity")
    app_hash = camera.remote_sha256(runner.PINNED_APP)
    if app_hash != runner.PINNED_APP_SHA256:
        raise runner.GuardError(
            f"{runner.PINNED_APP} has sha256 {app_hash}, not the expected "
            f"{runner.PINNED_APP_SHA256}"
        )
    library_hash = camera.remote_sha256(runner.PINNED_LIBRARY)
    if library_hash != runner.PINNED_LIBRARY_SHA256:
        raise runner.GuardError(
            f"{runner.PINNED_LIBRARY} has sha256 {library_hash}, not the "
            f"expected {runner.PINNED_LIBRARY_SHA256}"
        )
    dependency_hashes: Dict[str, str] = {}
    for path, expected in runner.PINNED_DEPENDENCIES.items():
        dependency_hash = runner.dependency_sha256(camera, path)
        if dependency_hash != expected:
            raise runner.GuardError(
                f"{path} has sha256 {dependency_hash}, not the expected {expected}"
            )
        dependency_hashes[path] = dependency_hash
        recorder.event("guard.dependency", path=path, sha256=dependency_hash)
    bridge_hash = collect_bridge_guard(camera, recorder)
    guards = {
        "app_pid": None,
        "app_start_tick": None,
        "app_identity_note": "the camera application was already stopped, so "
                             "there was no live identity to pin",
        "boot_identity": boot,
        "app_sha256": app_hash,
        "library_sha256": library_hash,
        "helper_sha256": digest,
        "dependency_sha256": dependency_hashes,
        "bridge_sha256": bridge_hash,
    }
    recorder.update(guards_before=guards,
                    dependency_sha256=dependency_hashes,
                    pinned_dependencies=dict(runner.PINNED_DEPENDENCIES))
    return guards, helper_bytes


def collect_bridge_guard(camera: Any, recorder: Any) -> str:
    """Pin the bridge ABI dependency used by the notify diagnostic path."""
    digest = camera.remote_sha256(PINNED_BRIDGE)
    if not re.fullmatch(r"[0-9a-f]{64}", digest or ""):
        raise runner_module().GuardError(f"{PINNED_BRIDGE} did not yield a sha256")
    recorder.event("guard.bridge", path=PINNED_BRIDGE, sha256=digest)
    recorder.update(bridge_sha256=digest, pinned_bridge_sha256=PINNED_BRIDGE_SHA256)
    if digest != PINNED_BRIDGE_SHA256:
        raise runner_module().GuardError(
            f"{PINNED_BRIDGE} has sha256 {digest}, not the expected {PINNED_BRIDGE_SHA256}")
    return digest


def verify_bridge_guard(camera: Any, guards: Dict[str, Any],
                        recorder: Any, label: str) -> None:
    digest = camera.remote_sha256(PINNED_BRIDGE)
    recorder.update(bridge_sha256=digest, pinned_bridge_sha256=PINNED_BRIDGE_SHA256)
    if (not re.fullmatch(r"[0-9a-f]{64}", digest or "")
            or digest != PINNED_BRIDGE_SHA256
            or digest != guards.get("bridge_sha256")):
        raise runner_module().GuardError(f"{PINNED_BRIDGE} changed or mismatched {label}")
    recorder.event("guard.bridge.verified", path=PINNED_BRIDGE,
                    sha256=digest, label=label)


def stock_application_processes(camera: Any) -> List[Dict[str, str]]:
    """Every di-camera-app process, with its pid, start tick and run state."""
    command = ('for d in /proc/[0-9]*; do [ -d "$d" ] || continue; p=${d##*/}; '
               'c=$(cat "$d"/comm 2>/dev/null) || { [ -d "$d" ] && echo APP_SCAN_ERROR=comm:$p; continue; }; '
               '[ -n "$c" ] || { [ -d "$d" ] && echo APP_SCAN_ERROR=comm:$p; continue; }; '
               '[ "$c" = "di-camera-app" ] || continue; '
               's=$(cat "$d"/stat 2>/dev/null) || { [ -d "$d" ] && echo BAD_APP_ROW; continue; }; '
               '[ -n "$s" ] || { [ -d "$d" ] && echo BAD_APP_ROW; continue; }; '
               'r=${s##*) }; set -- $r; [ "$#" -ge 20 ] || { [ -d "$d" ] && echo BAD_APP_ROW; continue; }; '
               'tick=${20}; printf "A|%s|%s|%s\\n" "$p" "$tick" "$1"; done; echo APPS_END')
    text = _strip_echo(camera.execute(command, timeout=30.0))
    if (len(re.findall(r"^APPS_END$", text, re.M)) != 1
            or text.strip().splitlines()[-1:] != ["APPS_END"]
            or "BAD_APP_ROW" in text
            or re.search(r"^APP_SCAN_ERROR=", text, re.M)):
        raise runner_module().GuardError("stock-process scan was incomplete or malformed")
    found: List[Dict[str, str]] = []
    for line in text.splitlines():
        if not line.startswith("A|"):
            continue
        parts = line.split("|")
        if (len(parts) != 4 or not parts[1].isdigit() or not parts[2].isdigit()
                or not re.fullmatch(r"[RSDZTtXxKWPI]", parts[3])):
            raise runner_module().GuardError("stock-process scan had a malformed row")
        found.append({"pid": parts[1], "start_tick": parts[2], "state": parts[3]})
    if len({p["pid"] for p in found}) != len(found):
        raise runner_module().GuardError("stock-process scan had duplicate process rows")
    return found


def unit_state(camera: Any) -> Dict[str, Optional[str]]:
    """The effective state of the camera application unit."""
    text = camera.execute(
        "systemctl show --property=ActiveState --property=MainPID "
        "di-camera-app.service 2>/dev/null", timeout=20.0
    )
    state: Dict[str, Optional[str]] = {}
    for line in text.splitlines():
        line = line.strip()
        for key in ("ActiveState", "MainPID"):
            if line.startswith(key + "="):
                state[key] = line.split("=", 1)[1]
    if any(len([line for line in text.splitlines()
                if line.strip().startswith(key + "=")]) != 1
           for key in ("ActiveState", "MainPID")):
        raise runner_module().GuardError("camera unit state is missing or duplicated")
    if not state["MainPID"].isdigit():
        raise runner_module().GuardError("camera unit MainPID is not numeric")
    return state


def stop_stock_application(camera: Any, recorder: Any,
                           expected_pid: str, expected_tick: str,
                           hard: bool = False) -> Dict[str, Any]:
    """Send exactly one no-block stop request to the pinned application unit.

    This is the one state-changing step the trial needs, and it is
    deliberately not automatic: the caller must ask for it.  A stop may
    power the camera off or leave RTOS state uncleared, so the caller must
    also be able to recover with a power cycle.  The request is sent once and
    never repeated - if the unit is already inactive with no process, no
    request is sent at all.

    The default stop is systemd's SIGTERM stop, which lets the application
    run its own shutdown sequence.  The hard variant SIGKILLs the unit's
    processes without any application cleanup: the stock shutdown path
    (PowerSave category 22 -> RTOS power state 3) cannot run, and the RTOS
    capture framework stays in its boot-time power-on state.  After the kill
    systemd marks the unit failed, so the hard variant also resets the
    failed state so the absence guard can see the unit inactive.
    """
    state = unit_state(camera)
    processes = stock_application_processes(camera)
    if state.get("ActiveState") == "inactive" and state.get("MainPID") == "0" and not processes:
        recorder.event("stock.stop.skipped", reason="the unit is already inactive")
        return {"stop_sent": False, "already_absent": True, "unit_before": state}
    matches = [p for p in processes if p["pid"] == expected_pid
               and p["start_tick"] == expected_tick]
    if (state.get("ActiveState") != "active" or state.get("MainPID") != expected_pid
            or len(matches) != 1 or len(processes) != 1):
        raise runner_module().GuardError(
            "camera unit/process identity no longer matches the pinned stop target")
    recorder.event("stock.stop.begin", mode="hard" if hard else "graceful")
    outcome: Dict[str, Any] = {"stop_sent": True, "already_absent": False,
                               "unit_before": state, "mode": "hard" if hard else "graceful"}
    if hard:
        # systemd 208 on the camera has no `systemctl kill`, so the hard
        # stop SIGKILLs every process whose comm is exactly di-camera-app
        # (the same predicate the absence scan uses) and then resets the
        # unit's failed state so the absence guard can see it inactive.
        command = (
            'pids=""; for d in /proc/[0-9]*; do p=${d##*/}; '
            'c=$(cat "$d/comm" 2>/dev/null) || continue; '
            '[ "$c" = "di-camera-app" ] || continue; pids="$pids $p"; done; '
            'rc=0; [ -z "$pids" ] && rc=1; '
            '[ -n "$pids" ] && kill -9 $pids 2>/dev/null || rc=1; echo STOP_RC=$rc; '
            'sleep 2; systemctl --no-block reset-failed di-camera-app.service 2>&1; '
            'rc2=$?; echo RESET_RC=$rc2'
        )
    else:
        command = "systemctl --no-block stop di-camera-app.service 2>&1; rc=$?; echo STOP_RC=$rc"
    try:
        text = camera.execute(command, timeout=12.0)
    except Exception as exc:
        # The request may have reached systemd before the transport timed out.
        # Never retry it; report an uncertain send and let absence polling decide.
        outcome["request_uncertain"] = True
        outcome["error"] = f"{type(exc).__name__}: {exc}"
        recorder.event("stock.stop.uncertain", error=outcome["error"])
        return outcome
    codes = re.findall(r"^STOP_RC=(\d+)$", text, re.M)
    if len(codes) != 1:
        outcome["request_uncertain"] = True
        outcome["stop_rc"] = None
    else:
        outcome["stop_rc"] = int(codes[0])
        if outcome["stop_rc"] != 0:
            outcome["error"] = "systemctl rejected the stop request"
    if hard:
        resets = re.findall(r"^RESET_RC=(\d+)$", text, re.M)
        if len(resets) != 1:
            outcome["reset_uncertain"] = True
        else:
            outcome["reset_rc"] = int(resets[0])
            if outcome["reset_rc"] != 0:
                outcome["error"] = outcome.get("error") or "the failed-state reset was rejected"
    recorder.event("stock.stop.done", rc=outcome["stop_rc"], mode=outcome["mode"])
    return outcome


def local_transport_items(proc_raw: Any) -> Tuple[bool, List[str]]:
    """Validate typed descriptor/thread metadata from one guarded snapshot."""
    required_fds = {"d5_lock", "d5_ipcc"}
    required_threads = {"udd_ipcc_fmt_in", "MCB_Receiver", "MCB_Sender"}
    missing = [f"fd:{name}" for name in sorted(required_fds)]
    missing += [f"thread:{name}" for name in sorted(required_threads)]
    if not isinstance(proc_raw, str):
        return False, missing
    for marker in ("FD_BEGIN", "FD_END", "TASK_BEGIN", "TASK_END", "SNAPSHOT_END"):
        if not re.search(rf"^{marker}$", proc_raw, re.M):
            return False, missing
    fds: set[str] = set()
    threads: set[str] = set()
    for line in proc_raw.splitlines():
        if line.startswith("FD|"):
            parts = line.split("|", 2)
            if len(parts) != 3 or not parts[1].isdigit() or not parts[2]:
                return False, missing
            fds.add(parts[2].rsplit("/", 1)[-1])
        elif line.startswith("THREAD|"):
            parts = line.split("|", 3)
            if (len(parts) != 4 or not parts[1].isdigit()
                    or not parts[2] or not parts[3]):
                return False, missing
            threads.add(parts[2])
    missing = [f"fd:{name}" for name in sorted(required_fds - fds)]
    missing += [f"thread:{name}" for name in sorted(required_threads - threads)]
    return not missing, missing


def verify_stock_absent(camera: Any, recorder: Any, timeout: float = 120.0,
                        interval: float = 1.0) -> Dict[str, Any]:
    """Check the three preconditions the trial needs before it may start.

    The unit must be inactive with MainPID 0, no di-camera-app process may
    exist, and a complete /proc scan must show no process holding a
    d5_ipcc or d5_lock descriptor.  Any failure or ambiguity refuses the
    start: stock absence is a precondition, not a readiness proof.

    The check waits, because the unit's stop is not instantaneous: it has a
    ninety-second stop timeout and the process may take a while to go.  The
    wait is bounded, and a unit that never goes away fails the check rather
    than hanging the trial.
    """
    started = time.monotonic()
    deadline = started + timeout
    record: Dict[str, Any] = {}
    while True:
        state = unit_state(camera)
        processes = stock_application_processes(camera)
        holders = scan_d5_holders(camera)
        competing = [h for h in holders
                     if h["device"].endswith("d5_ipcc")
                     or h["device"].endswith("d5_lock")]
        problems: List[str] = []
        if state.get("ActiveState") != "inactive":
            problems.append(f"the unit is {state.get('ActiveState')!r}, not inactive")
        if state.get("MainPID") != "0":
            problems.append(f"the unit still has MainPID {state.get('MainPID')!r}")
        if competing:
            problems.append("a competing d5_ipcc/d5_lock descriptor owner is present")
        # A process that is still listed but holds no device descriptor cannot
        # interfere with the channel - it is a remnant of the exit, not a
        # running application.  It is recorded, and reported as an
        # uncertainty, so that it is never hidden.
        remnants = []
        if processes:
            problems.append(f"{len(processes)} di-camera-app process(es) still exist")
        record = {
            "unit": state,
            "stock_processes": processes,
            "stock_remnants": remnants,
            "d5_holders": holders,
            "competing_holders": competing,
            "problems": problems,
            "waited_seconds": round(time.monotonic() - started, 1),
        }
        if not problems:
            record["absent"] = True
            recorder.event("stock.absent", absent=True,
                           waited_seconds=record["waited_seconds"],
                           remnants=remnants, holders=holders)
            return record
        if time.monotonic() >= deadline:
            record["absent"] = False
            recorder.event("stock.absent.failed", problems=problems,
                           waited_seconds=record["waited_seconds"])
            return record
        time.sleep(interval)


def verify_guards_after_stop(camera: Any, cfg: Any, guards: Dict[str, Any],
                              recorder: Any, label: str) -> None:
    """The runner's after-run checks, minus the application identity.

    The runner expects the camera application to still be running with the
    same pid and start tick.  When this trial stopped it on purpose that
    expectation is wrong - the application must be *gone* - so the identity
    check is replaced by the absence check, and the boot identity and the
    file digests are still checked because they must not have changed.
    """
    runner = runner_module()
    boot = runner.boot_identity(camera)
    if boot != guards["boot_identity"]:
        raise runner.GuardError(f"the boot identity changed {label}")
    app_hash = camera.remote_sha256(runner.PINNED_APP)
    if app_hash != guards["app_sha256"]:
        raise runner.GuardError(f"{runner.PINNED_APP} changed {label}")
    library_hash = camera.remote_sha256(runner.PINNED_LIBRARY)
    if library_hash != guards["library_sha256"]:
        raise runner.GuardError(f"{runner.PINNED_LIBRARY} changed {label}")
    recorded = guards.get("dependency_sha256") or {}
    for path, expected in runner.PINNED_DEPENDENCIES.items():
        digest = runner.dependency_sha256(camera, path)
        if digest != recorded.get(path, expected):
            raise runner.GuardError(f"{path} changed {label}")
    recorder.event("guard.verified", label=label, application="stopped")


def run_init_probe(
    camera: Any,
    cfg: Any,
    recorder: Any,
    *,
    local_bootstrap: bool = False,
    notify: bool = False,
    diagnostic_still: bool = False,
    free_bytes: Optional[str] = None,
    diagnostic_liveview: bool = False,
    diagnostic_liveview_latch: bool = False,
    diagnostic_liveview_admit: bool = False,
    diagnostic_still_liveview: bool = False,
    previous_boot: Optional[List[str]] = None,
    stop_stock: bool = False,
    stop_stock_hard: bool = False,
    stock_absence_timeout: float = 300.0,
) -> ProbeResult:
    """Run one fixed diagnostic helper and return its answer. It never raises for a
    trial failure: a failed trial is a result, not an exception.

    `previous_boot`, when given, is the boot identity of the last completed
    trial.  The current boot identity must differ from it: the trial needs a
    fresh boot, and a run against the same boot is refused before anything is
    staged.
    """
    runner = runner_module()
    observer = observer_module()
    prepare = prepare_module()

    result = ProbeResult(diagnostic_mode="one-still" if diagnostic_still else
                         ("one-still-liveview" if diagnostic_still_liveview else
                          ("liveview-admit" if diagnostic_liveview_admit else
                           ("liveview-latch" if diagnostic_liveview_latch else
                            ("liveview-only" if diagnostic_liveview else "init-only")))))
    try:
        canonical_free = prepare.validate_diagnostic_mode(
            diagnostic_still, free_bytes, local_bootstrap, notify, diagnostic_liveview,
            diagnostic_liveview_latch, diagnostic_liveview_admit,
            diagnostic_still_liveview)
    except ValueError as exc:
        result.errors.append(str(exc))
        recorder.update(**result.console())
        return result
    recorder.update(diagnostic_mode=result.diagnostic_mode,
                    still_completion=result.still_completion,
                    request_association=result.request_association,
                    raw_ownership=result.raw_ownership,
                    sdk_returned_meaning="helper process returned zero; not still completion")
    guards: Optional[Dict[str, Any]] = None
    stop_requested = stop_stock or stop_stock_hard
    remote_dir: Optional[str] = None
    # "not-created", "unknown" or "created": what this trial owns.
    ownership = "not-created"
    child_stopped: Optional[bool] = True  # no helper has run yet
    execution_requested = False
    stop_attempted = False
    interrupted_event = threading.Event()
    final_guards_verified = False
    first_error: Optional[str] = None
    probe_done = threading.Event()
    watcher: Optional[_ObserverThread] = None
    observer_path = recorder.out_dir / "observer.json"

    def mark_interrupted() -> None:
        nonlocal first_error
        interrupted_event.set()
        probe_done.set()
        result.interrupted = True
        result.outcome = "interrupted"
        result.ok = False
        result.helper_termination = ("confirmed" if execution_requested and child_stopped is True
                                     else "unknown" if execution_requested else "not-started")
        result.cleanup_verified = False
        if remote_dir:
            result.residual_path = remote_dir
        note = "interrupted; no further camera calls or cleanup will be attempted"
        if note not in result.uncertainty:
            result.uncertainty.append(note)
        if stop_attempted:
            result.uncertainty.append("a stock stop may have been requested; its effects are not established")
        if execution_requested and child_stopped is not True:
            result.uncertainty.append("helper termination remains unknown")
        first_error = note
        recorder.update(**result.console())

    try:
        initial_absence: Optional[Dict[str, Any]] = None
        if not stop_requested:
            # Do not stage anything while a stock or competing owner remains.
            initial_absence = verify_stock_absent(
                camera, recorder, timeout=0.0, interval=0.0)
            if not initial_absence["absent"]:
                raise runner.GuardError(
                    "stock/device absence is required; refusing before staging: "
                    + "; ".join(initial_absence["problems"]))
            recorder.update(stock_absence=initial_absence)
        running = stock_application_processes(camera)
        if not running:
            recorder.event("guard.app_identity.absent", reason="no stock process is present")
            if stop_requested:
                already_absent = verify_stock_absent(
                    camera, recorder, timeout=0.0, interval=0.0)
                recorder.update(stock_absence=already_absent)
                if not already_absent["absent"]:
                    raise runner.GuardError(
                        "the stock app is absent but the unit/device channel is not: "
                        + "; ".join(already_absent["problems"]))
            guards, helper_bytes = collect_guards_absent(camera, cfg, recorder)
        else:
            if not stop_requested:
                raise runner.GuardError("stock app is present and stop was not requested")
            guards, helper_bytes = runner.collect_guards(camera, cfg, recorder)
            guards["bridge_sha256"] = collect_bridge_guard(camera, recorder)
        expected_boot = guards["boot_identity"]
        recorder.update(boot_identity=expected_boot)
        if previous_boot:
            wanted = ([previous_boot] if isinstance(previous_boot, str)
                      else list(previous_boot))
            stale = [boot for boot in wanted if boot == expected_boot]
            if stale:
                raise runner.GuardError(
                    "the boot identity is the same as a previous trial "
                    f"({expected_boot}); this trial needs a fresh boot, so "
                    "nothing was staged"
                )
        remote_dir = runner.new_remote_dir(cfg)
        result.remote_dir = remote_dir
        recorder.update(remote_dir=remote_dir)
        # Until mkdir returns, the remote directory may already exist.
        ownership = "unknown"
        try:
            camera.mkdir(remote_dir)
        except runner.CameraRefused as exc:
            ownership = "not-created"
            # The camera said no: the directory was not created, so this trial
            # does not own it and must not remove it.
            raise runner.LoadCheckError(str(exc)) from exc
        except Exception as exc:
            # The connection was lost: the camera may have created it.
            ownership = "unknown"
            raise runner.UncertainOwnership(
                f"the connection was lost while {remote_dir} was being created, "
                f"so whether it exists is not known: {type(exc).__name__}: {exc}"
            ) from exc
        ownership = "created"
        runner.stage_helper(camera, cfg, recorder, remote_dir, helper_bytes)
        # The probe payloads are the runner's watchdog and recovery scripts
        # with the probe command, the boot guard and the owner-file anchors
        # substituted in.  They are staged and read back with the runner's own
        # verification, so a script that cannot be read back is not run.
        watchdog, recovery = prepare.build_payloads(
            remote_dir, expected_boot, local_bootstrap, notify,
            diagnostic_still, canonical_free, diagnostic_liveview,
            diagnostic_liveview_latch, diagnostic_liveview_admit,
            diagnostic_still_liveview
        )
        runner.stage_scripts(
            camera, cfg, recorder, remote_dir,
            scripts={
                runner.WATCHDOG_NAME: watchdog,
                runner.RECOVERY_NAME: recovery,
            },
        )
        # Immediately before the execution, the pinned expectations are checked
        # again.  A failure here stops the trial before the helper can run.
        # When the camera application was already stopped there is no live
        # identity to re-check, so the application-free variant is used.
        if guards.get("app_pid") is None:
            verify_guards_after_stop(camera, cfg, guards, recorder,
                                     "before execution")
        else:
            runner.verify_guards(camera, cfg, guards, recorder,
                                 "before execution")
        if stop_stock or stop_stock_hard:
            # The one state-changing step the trial needs, and the one the
            # caller has to ask for.  It is sent once and never repeated, and
            # the trial refuses to start unless the camera application is
            # verifiably gone and no process holds the device channel.  The
            # hard variant bypasses the application's own shutdown sequence
            # (see stop_stock_application).
            stop_attempted = True
            if guards.get("app_pid") is None:
                stopped = stop_stock_application(camera, recorder, "", "",
                                                 hard=stop_stock_hard)
            else:
                stopped = stop_stock_application(
                    camera, recorder, str(guards["app_pid"]),
                    str(guards["app_start_tick"]), hard=stop_stock_hard)
            if ((not stopped.get("stop_sent") and not stopped.get("already_absent"))
                    or stopped.get("request_uncertain")
                    or stopped.get("error")):
                if stopped.get("request_uncertain"):
                    result.uncertainty.append(
                        "the no-block stop request may have reached systemd; it was not retried")
                raise runner.LoadCheckError(
                    "the one no-block stop request was rejected or uncertain; "
                    "it will not be retried: %s" % stopped.get("error", "unknown result")
                )
        absent = verify_stock_absent(camera, recorder,
                                     timeout=stock_absence_timeout if stop_requested else 0.0,
                                     interval=0.0 if not stop_requested else 1.0)
        recorder.update(stock_absence=absent)
        if not absent["absent"]:
            raise runner.GuardError(
                "the camera application/device channel is not verifiably absent, "
                "so the trial refuses to start: " + "; ".join(absent["problems"]))
        verify_bridge_guard(camera, guards, recorder, "before execution")
        # From here on the helper may be running, so its state is unknown
        # until the status file or the recovery says otherwise.  If anything
        # goes wrong in between, the directory is kept.
        child_stopped = None
        # The observer starts BEFORE the probe is executed.  If it started
        # after, the helper would already be inside its hold and the guarded
        # snapshot would race the probe's own exit.
        bounded_camera = observer._camera_with_timeout(camera, observer.MAX_SECONDS, observer._runner())
        watcher = _ObserverThread(
            observer, _InterruptibleObserverCamera(bounded_camera, interrupted_event),
            expected_boot, remote_dir, observer_path,
            should_stop=probe_done.is_set,
        )
        watcher.start()
        recorder.event("observer.started", remote_dir=remote_dir)
        execution_requested = True
        outcome = runner.execute_helper(camera, cfg, recorder, remote_dir)
        probe_done.set()
        result.outcome = outcome.outcome
        result.rc = outcome.rc
        result.errors.extend(outcome.errors)
        result.uncertainty.extend(outcome.uncertainty)
        child_stopped = outcome.terminated
        result.helper_termination = "confirmed" if child_stopped is True else "unknown"
        # Legacy field: only the helper process returned zero. It establishes
        # neither the worker's existence nor remote readiness or completion.
        result.sdk_returned = outcome.outcome == "rc:0" and outcome.rc == 0
    except KeyboardInterrupt:
        mark_interrupted()
    except runner.LoadCheckError as exc:
        first_error = str(exc)
        result.errors.append(first_error)
    except Exception as exc:  # a broken transport is a failure, not a pass
        first_error = f"{type(exc).__name__}: {exc}"
        result.errors.append(first_error)
    finally:
        # The probe is over whether it succeeded or not: let the observer stop
        # polling instead of waiting out its own deadline.
        probe_done.set()

    if watcher is not None:
        try:
            watcher.join(timeout=10.0)
        except KeyboardInterrupt:
            mark_interrupted()
        if watcher.is_alive():
            result.uncertainty.append(
                "the observer did not finish within the join bound, so its "
                "record may be incomplete"
            )
        elif watcher.error is not None:
            result.uncertainty.append(f"the observer failed: {watcher.error}")
        record = _read_observer_record(observer_path)
        recorder.save_bytes("observer.json", observer_path.read_bytes()) \
            if observer_path.exists() else None
        result.worker_observation = str(record.get("status", "no-record"))
        # The worker was observed only when a guarded snapshot was taken of
        # the exact helper - same pid, same start tick, same image, before and
        # after the read.
        result.worker_observed = result.worker_observation == "snapshot"
        if result.worker_observed:
            result.local_transport_observed, result.local_transport_missing = local_transport_items(
                record.get("proc_raw"))
        else:
            result.local_transport_observed, result.local_transport_missing = local_transport_items(None)
        recorder.update(worker_observation=result.worker_observation)
        recorder.update(local_transport_observed=result.local_transport_observed,
                        local_transport_missing=result.local_transport_missing)

    # Verify the pinned files and boot in every path, including a no-stop run
    # that began with the app already absent. The execution-requested flag is
    # separate from child_stopped's initial sentinel so early refusals never
    # spend the post-run absence wait.
    if guards is not None and not result.interrupted:
        try:
            if stop_requested or guards.get("app_pid") is None:
                verify_guards_after_stop(camera, cfg, guards, recorder,
                                         "after execution")
            else:
                runner.verify_guards(camera, cfg, guards, recorder, "after execution")
            verify_bridge_guard(camera, guards, recorder, "after execution")
            final_guards_verified = True
            if execution_requested:
                still_absent = verify_stock_absent(camera, recorder,
                                                   timeout=30.0)
                recorder.update(stock_absence_after=still_absent)
                if not still_absent["absent"]:
                    raise runner.GuardError(
                        "the camera application/device owner appeared during the trial: "
                        + "; ".join(still_absent["problems"])
                    )
        except KeyboardInterrupt:
            mark_interrupted()
        except runner.LoadCheckError as exc:
            note = str(exc)
            result.errors.append(note)
            result.uncertainty.append("the after-run identity check failed")
            if first_error is None:
                first_error = note
        except Exception as exc:
            note = (
                "the after-run identity check could not be completed: "
                f"{type(exc).__name__}: {exc}"
            )
            result.uncertainty.append(note)
            if first_error is None:
                result.errors.append(note)

    # Finally the generated directory is removed and its absence verified -
    # but only when this trial owns it and the helper's termination is
    # confirmed.  A directory that may still hold a running helper is kept,
    # so the identity needed to finish the job is not thrown away.
    if result.interrupted:
        # No recovery, guards, evidence fetch or deletion after interruption.
        pass
    elif ownership == "not-created":
        result.cleanup_verified = True
    elif ownership == "unknown" and remote_dir:
        result.cleanup_verified = False
        result.residual_path = remote_dir
        note = (
            f"the connection was lost while {remote_dir} was being created, so "
            "it may still be on the camera; it was not removed"
        )
        result.uncertainty.append(note)
        if first_error is None:
            result.errors.append(note)
    elif remote_dir:
        if watcher is not None and watcher.is_alive():
            result.cleanup_verified = False
            result.residual_path = remote_dir
            note = (f"the observer thread is still active, so {remote_dir} was kept "
                    "until it can no longer read its evidence files")
            result.uncertainty.append(note)
            if first_error is None:
                result.errors.append(note)
        elif not final_guards_verified:
            result.cleanup_verified = False
            result.residual_path = remote_dir
            note = (f"the final boot/file guards were not verified, so {remote_dir} "
                    "was kept for diagnosis")
            result.uncertainty.append(note)
            if first_error is None:
                result.errors.append(note)
        elif child_stopped is not True:
            result.cleanup_verified = False
            result.residual_path = remote_dir
            note = (
                f"the helper's termination could not be confirmed, so {remote_dir} "
                "was kept: it still holds the owner file, the status file and the "
                "helper that are needed to finish the job"
            )
            result.uncertainty.append(note)
            if first_error is None:
                result.errors.append(note)
        else:
            try:
                result.cleanup_verified = runner.cleanup_remote(
                    camera, remote_dir, recorder
                )
            except KeyboardInterrupt:
                mark_interrupted()
            except runner.LoadCheckError as exc:
                note = str(exc)
                result.residual_path = remote_dir
                result.uncertainty.append(note)
                if first_error is None:
                    result.errors.append(note)
            except Exception as exc:
                note = f"the cleanup could not be completed: {type(exc).__name__}: {exc}"
                result.residual_path = remote_dir
                result.uncertainty.append(note)
                if first_error is None:
                    result.errors.append(note)

    # The remote service was never shown to be ready by this path.  The probe
    # hosts the library and creates a client; its own log says that a
    # successful return does not certify readiness, and nothing here changes
    # that.
    result.remote_readiness = "not-established"
    result.ok = (
        first_error is None
        and not result.errors
        and not result.uncertainty
        and result.sdk_returned
        and result.worker_observed
        and result.local_transport_observed
    )
    recorder.update(
        ok=result.ok,
        interrupted=result.interrupted,
        helper_termination=result.helper_termination,
        outcome=result.outcome,
        rc=result.rc,
        sdk_returned=result.sdk_returned,
        worker_observed=result.worker_observed,
        worker_observation=result.worker_observation,
        remote_readiness=result.remote_readiness,
        cleanup_verified=result.cleanup_verified,
        residual_path=result.residual_path,
        errors=result.errors,
        uncertainty=result.uncertainty,
        finished_utc=runner.now_iso(),
    )
    return result


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Run one fixed diagnostic helper, with the observer "
                    "already watching.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--host", default="192.168.43.1")
    parser.add_argument("--helper", type=Path, required=True,
                        help="the local capture-shot helper to stage")
    parser.add_argument("--helper-sha256", required=True,
                        help="the expected sha256 of the helper")
    parser.add_argument("--output", type=Path, required=True,
                        help="the run directory; it must not already exist")
    parser.add_argument("--deadline-seconds", type=int, choices=(45,), default=45,
                        help="fixed watchdog deadline (45 seconds)")
    parser.add_argument("--http-timeout", type=float, default=120.0)
    parser.add_argument("--local-bootstrap", action="store_true",
                        help="include the explicit local-bootstrap diagnostic step")
    parser.add_argument("--notify", action="store_true",
                        help="add the one zero-length category 0x10 request to "
                             "the probe; any response remains untagged")
    parser.add_argument("--diagnostic-still", action="store_true",
                        help="run the pinned standalone one-still diagnostic helper")
    parser.add_argument("--diagnostic-liveview", action="store_true",
                        help="run the pinned standalone liveview-only diagnostic "
                             "helper; it issues one liveview start edge and never "
                             "issues a still command")
    parser.add_argument("--diagnostic-liveview-latch", action="store_true",
                        help="run the standalone liveview diagnostic helper that "
                             "also reads the transport's destination-alive byte "
                             "immediately before and after the one liveview "
                             "request; it never issues a still command")
    parser.add_argument("--diagnostic-still-liveview", action="store_true",
                        help="run the standalone one-still diagnostic helper that "
                             "reproduces the stock ordering: the transport "
                             "admission handshake, then one liveview start edge, "
                             "then the one still request")
    parser.add_argument("--diagnostic-liveview-admit", action="store_true",
                        help="run the standalone liveview diagnostic helper that "
                             "first performs the transport's own admission "
                             "handshake, one explicit empty category 0x10 send, "
                             "and then issues the one liveview request; it "
                             "never issues a still command")
    parser.add_argument("--free-bytes", help="required ASCII u64 for --diagnostic-still")
    parser.add_argument("--stop-stock-app", action="store_true",
                        help="send the single stop request for the camera "
                             "application unit before the trial and refuse to "
                             "start unless it is verifiably gone")
    parser.add_argument("--stop-stock-app-hard", action="store_true",
                        help="like --stop-stock-app, but SIGKILL the unit "
                             "processes so the application's own shutdown "
                             "sequence (and its RTOS power-save) cannot run; "
                             "the failed unit state is reset afterwards")
    parser.add_argument("--previous-boot", action="append", default=None,
                        help="a boot identity of an earlier trial; the current "
                             "one must differ from every one given. May be "
                             "repeated.")
    parser.add_argument("--stock-absence-timeout", type=float, default=300.0,
                        help="how long to wait for the camera application to "
                             "go away, in seconds. The unit has a ninety "
                             "second stop timeout and the application has been "
                             "observed to outlive it.")
    args = parser.parse_args(argv)
    try:
        prepare_module().validate_diagnostic_mode(args.diagnostic_still, args.free_bytes,
                                                  args.local_bootstrap, args.notify,
                                                  args.diagnostic_liveview,
                                                  args.diagnostic_liveview_latch,
                                                  args.diagnostic_liveview_admit,
                                                  args.diagnostic_still_liveview)
    except ValueError as exc:
        parser.error(str(exc))

    runner = runner_module()
    cfg = runner.Config(
        helper=args.helper,
        helper_sha256=args.helper_sha256.lower(),
        deadline_seconds=args.deadline_seconds,
        http_timeout=args.http_timeout,
    )
    recorder = runner.Recorder(args.output)
    # The camera is built from the observer's own class so that the observer's
    # remaining-timeout logic applies to it.
    camera = observer_camera_class()(args.host, timeout=args.http_timeout)
    result = run_init_probe(
        camera, cfg, recorder,
        local_bootstrap=args.local_bootstrap,
        notify=args.notify,
        diagnostic_still=args.diagnostic_still,
        free_bytes=args.free_bytes,
        diagnostic_liveview=args.diagnostic_liveview,
        diagnostic_liveview_latch=args.diagnostic_liveview_latch,
        diagnostic_liveview_admit=args.diagnostic_liveview_admit,
        diagnostic_still_liveview=args.diagnostic_still_liveview,
        previous_boot=args.previous_boot,
        stop_stock=args.stop_stock_app,
        stop_stock_hard=args.stop_stock_app_hard,
        stock_absence_timeout=args.stock_absence_timeout,
    )
    print(json.dumps(result.console(), indent=2, sort_keys=True))
    return 0 if result.ok else (2 if result.uncertainty else 1)


if __name__ == "__main__":
    raise SystemExit(main())
