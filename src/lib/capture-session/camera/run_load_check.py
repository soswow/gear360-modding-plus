#!/usr/bin/env python3
"""Bounded load-only runner for capture-shot on the Gear 360 camera.

What this runs on the camera, and nothing else:

    capture-shot --load-only --library /usr/lib/libcapture-fw-prod.so \
                --log /tmp/gear360-load-<uuid>/progress

That is the `--load-only` path of capture-shot (capture-shot.c): it loads the
capture library, resolves its entry points and stops.  This program makes no
explicit client, create or realise call, and never asks the camera for a
still.  What it does not establish is what loading the library itself did:
loading a shared object may run the constructors of the library and of the
libraries it depends on, and this runner makes no claim that those
constructors had no effect - in particular, it does not claim that no
capture module was initialised.

Everything else is refusal: the hashes of the pinned files - the capture
library, the camera application and the pinned native dependencies of the
capture library - the identity of the single camera application process, and
the boot identity are checked before anything is staged, checked again
immediately before the helper is executed, and checked again afterwards.  A
failed check stops the run before the helper can be executed.  The dependency
pins are a reviewed subset of the library's dependencies: they are not a
complete loader closure, and they are not an authorization to load the SDK.

The camera is reached only through the `Camera` interface below, so the
failure paths are exercised offline by injecting a fake camera.

Files written: the output directory given on the command line, and one
temporary directory on the camera under /tmp whose name is generated here.
Nothing is written to /opt or to any other persistent location.  The camera
directory is removed at the end and its absence verified - unless the
helper's termination could not be verified, in which case the directory is
kept, so that the identity needed to finish the job is not thrown away, and
the exact residual path is reported.

**How the camera is reached.**  The camera's execute endpoint takes its
command in the URL of a GET. The first multi-kilobyte script request
timed out; its exact transport failure was not established. The two generated scripts - the
watchdog and the recovery - are uploaded over FTP into the directory this
run already owns, read back, and compared byte for byte and by digest.
Only then is the endpoint asked to run one short fixed command:

    /bin/sh /tmp/gear360-load-<uuid>/watchdog.sh
    /bin/sh /tmp/gear360-load-<uuid>/recovery.sh

The script bodies are never put in a URL.  The bytes and the digests of
both scripts are kept in the local run record, so what was staged can be
read afterwards.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import posixpath
import re
import shlex
import socket
import sys
import uuid
import subprocess
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from urllib.parse import quote as url_quote

# --- the pinned world ------------------------------------------------------

#: The capture library capture-shot loads.  Fixed, not a command line option:
#: the runner never sends an arbitrary path to the camera.
PINNED_LIBRARY = "/usr/lib/libcapture-fw-prod.so"
PINNED_LIBRARY_SHA256 = "fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7"

#: The camera application whose single process is the identity anchor.
PINNED_APP = "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app"
PINNED_APP_SHA256 = "5937b3abe7630abc8ac265ce280cc45ee591562968296bc4b368cf7e15742789"
APP_COMMAND = "di-camera-app"

#: The native dependencies of the capture library whose load effects are
#: under review, and the sha256 each one must have.  They are pinned files
#: like the library itself: fixed, not a command line option, and there is
#: no way to switch a pin off.
#:
#: The capture library's DT_NEEDED literally names `libdevice-node.so.0.1`;
#: the plugin's path is the one its dlopen call names.  This is a reviewed
#: subset of the library's dependencies, not a complete loader closure, and
#: pinning them says nothing about whether loading them is allowed.
PINNED_DEPENDENCIES = {
    "/usr/lib/libdevice-node.so.0.1":
        "ff7a00ff2982f8a39ac2cd2256d025406b3cadea2ededfe4ee56b1e81e5a4db4",
    "/usr/lib/libslp_devman_plugin.so":
        "973e5e096c9fb07d2b108d7f336fec254132d31fe4d7ed009440c055b0862b14",
}

#: The only place this runner writes on the camera.
REMOTE_ROOT = "/tmp"
REMOTE_DIR_PREFIX = "gear360-load-"
HELPER_NAME = "capture-shot"

#: The generated scripts, staged over FTP and then run by name.
#:
#: The camera's execute endpoint takes its command in the URL of a GET,
#: so scripts go over FTP into the directory this run already owns, and
#: the endpoint is only ever asked to run one short fixed command.
WATCHDOG_NAME = "watchdog.sh"
RECOVERY_NAME = "recovery.sh"

#: The owner file says which process the run started.  One key per line, so
#: that each value can be read on its own and checked on its own.
OWNER_KEYS = ("pid", "tick")

#: The status file says what the watchdog did.  Exactly these three keys.
STATUS_KEYS = ("outcome", "rc", "terminated")

#: The outcomes the watchdog may report, apart from `rc:<n>`.
#:
#: `state-unknown` is the one that matters most: the child could not be
#: read, so nothing about it is known and nothing may be removed.
KNOWN_OUTCOMES = (
    "timeout",
    "kill-unconfirmed",
    "identity-lost",
    "identity-unknown",
    "image-changed",
    "state-unknown",
    "clock-unavailable",
)

#: The outcomes that mean the child was not shown to have stopped.
UNPROVEN_OUTCOMES = (
    "kill-unconfirmed",
    "identity-lost",
    "identity-unknown",
    "image-changed",
    "state-unknown",
    "clock-unavailable",
)

#: The outcomes the run reports as uncertain.  A timeout is in this list
#: because the helper did not finish in time, even though its termination
#: was confirmed; the others are in it because nothing about the child was
#: established at all.
UNCERTAIN_OUTCOMES = ("timeout",) + UNPROVEN_OUTCOMES

#: The shell that finds the camera application.  Taken from the reference
#: observer runner: one line per matching process, with its start tick.
APP_IDENTITY_COMMAND = (
    'for d in /proc/[0-9]*; do c=$(cat $d/comm 2>/dev/null); '
    '[ "$c" = ' + shlex.quote(APP_COMMAND) + ' ] '
    '&& echo APP $(basename $d) $(cut -d" " -f22 $d/stat); done'
)

#: The boot identity: the kernel's boot id when it is there, and the boot
#: time from /proc/stat.  A reboot changes both.
BOOT_IDENTITY_COMMAND = (
    "cat /proc/sys/kernel/random/boot_id 2>/dev/null; "
    "awk '/^btime/{print $2}' /proc/stat"
)


class LoadCheckError(RuntimeError):
    """A refusal or a failure that the run reports as its own outcome."""


class GuardError(LoadCheckError):
    """A pinned expectation did not hold; the helper must not run."""


class CameraRefused(LoadCheckError):
    """The camera answered that it will not do what was asked.

    A refusal is not the same as a lost connection: a refused `mkd` means
    the directory was not created and this run does not own it.
    """


class UncertainOwnership(LoadCheckError):
    """The connection was lost while the directory was being created.

    Whether the camera created it is not known, so it is not removed.
    """


@dataclass
class Config:
    """The bounded inputs of one run.  There is no mode, no client, no
    probe, no still and no setting: `--load-only` is the whole contract."""

    helper: Path
    helper_sha256: str
    deadline_seconds: int = 60
    http_timeout: float = 120.0
    remote_root: str = REMOTE_ROOT


@dataclass
class Outcome:
    """What the helper did, as far as it could be established."""

    outcome: str = "not-run"
    rc: Optional[int] = None
    terminated: Optional[bool] = None
    uncertainty: List[str] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)
    files: Dict[str, bytes] = field(default_factory=dict)


@dataclass
class Result:
    """The run's answer.  `errors` keeps the first failure first, so a
    later cleanup or identity failure never hides the original error."""

    ok: bool = False
    outcome: str = "not-run"
    rc: Optional[int] = None
    remote_dir: str = ""
    cleanup_verified: bool = False
    residual_path: str = ""
    errors: List[str] = field(default_factory=list)
    uncertainty: List[str] = field(default_factory=list)

    def console(self) -> dict:
        """The console summary.  It carries no boot identity and no
        credential; the full record stays in the output directory."""
        return {
            "ok": self.ok,
            "outcome": self.outcome,
            "rc": self.rc,
            "remote_dir": self.remote_dir,
            "cleanup_verified": self.cleanup_verified,
            "residual_path": self.residual_path,
            "errors": list(self.errors),
            "uncertainty": list(self.uncertainty),
        }


def exit_code(result: Result) -> int:
    """0 the run succeeded, 1 it failed, 2 it could not be decided."""
    if result.ok:
        return 0
    if result.uncertainty:
        return 2
    return 1


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


# --- the strict formats ----------------------------------------------------


def parse_owner(text: str) -> Tuple[str, str]:
    """Read the owner file: the pid and the start tick, one key per line.

    The format is exact: two lines, `pid=<digits>` and `tick=<digits>`,
    one of each, and nothing else.  Anything else is refused, because a
    half-read or mangled owner file must not be turned into a signal.
    """
    values: Dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        key, separator, value = line.partition("=")
        if not separator or key not in OWNER_KEYS or key in values:
            raise LoadCheckError(f"the owner file is not in the expected format: {line!r}")
        if not value.isdigit():
            raise LoadCheckError(f"the owner file holds a non-numeric {key}: {line!r}")
        values[key] = value
    missing = [key for key in OWNER_KEYS if key not in values]
    if missing:
        raise LoadCheckError(f"the owner file does not hold {', '.join(missing)}")
    return values["pid"], values["tick"]


def parse_status(text: str) -> Tuple[str, Optional[int], str]:
    """Read the watchdog's status file.

    The format is exact: `outcome=`, `rc=` and `terminated=`, one of each,
    and nothing else.  The outcome must be one the watchdog may report, and
    an `rc:<n>` outcome must agree with the `rc` value.  A malformed or
    inconsistent status is refused, so a status that cannot be read fails
    closed rather than passing for a clean run.
    """
    values: Dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        key, separator, value = line.partition("=")
        if not separator or key not in STATUS_KEYS or key in values:
            raise LoadCheckError(f"the status file is not in the expected format: {line!r}")
        values[key] = value
    missing = [key for key in STATUS_KEYS if key not in values]
    if missing:
        raise LoadCheckError(f"the status file does not hold {', '.join(missing)}")

    outcome = values["outcome"]
    terminated = values["terminated"]
    if terminated not in ("yes", "no"):
        raise LoadCheckError(f"the status file holds an unknown terminated: {terminated!r}")

    raw_rc = values["rc"]
    if raw_rc == "unknown":
        rc: Optional[int] = None
    elif raw_rc.isdigit():
        rc = int(raw_rc)
    else:
        raise LoadCheckError(f"the status file holds a non-numeric rc: {raw_rc!r}")

    if outcome.startswith("rc:"):
        number = outcome[3:]
        if not number.isdigit():
            raise LoadCheckError(f"the status file holds a malformed outcome: {outcome!r}")
        if rc is None or int(number) != rc:
            raise LoadCheckError(
                f"the status file holds outcome {outcome!r} and rc {raw_rc!r}, "
                "which do not agree"
            )
        if terminated != "yes":
            raise LoadCheckError(
                f"the status file holds outcome {outcome!r} and terminated "
                f"{terminated!r}, which do not agree"
            )
    elif outcome in KNOWN_OUTCOMES:
        if outcome == "timeout" and terminated != "yes":
            raise LoadCheckError(
                f"the status file holds outcome {outcome!r} and terminated "
                f"{terminated!r}, which do not agree"
            )
        if outcome in UNPROVEN_OUTCOMES and terminated != "no":
            raise LoadCheckError(
                f"the status file holds outcome {outcome!r} and terminated "
                f"{terminated!r}, which do not agree"
            )
        if outcome == "clock-unavailable" and rc is not None:
            raise LoadCheckError(
                "the status file holds a clock-unavailable outcome and an exit code"
            )
        if outcome in UNPROVEN_OUTCOMES and rc is not None:
            raise LoadCheckError(
                f"the status file holds outcome {outcome!r} and an exit code, "
                "which do not agree"
            )
    else:
        raise LoadCheckError(f"the status file holds an unknown outcome: {outcome!r}")

    return outcome, rc, terminated


# --- the camera ------------------------------------------------------------


class Camera:
    """Everything this runner does to the camera, in five operations.

    The real implementation speaks the camera's HTTP execute endpoint and
    its FTP server, as the reference observer runner documents them.  The
    offline tests inject a different implementation of the same five calls.
    """

    def execute(self, command: str, timeout: Optional[float] = None) -> str:
        raise NotImplementedError

    def remote_sha256(self, remote_path: str) -> str:
        raise NotImplementedError

    def mkdir(self, remote_dir: str) -> None:
        raise NotImplementedError

    def put(self, remote_path: str, data: bytes) -> None:
        raise NotImplementedError

    def get(self, remote_path: str) -> bytes:
        raise NotImplementedError


class HttpFtpCamera(Camera):
    """The camera as the reference observer runner documents it.

    The transport is the host's ``curl`` for both the CGI execute endpoint
    and FTP, because this process's own sockets cannot route to the camera's
    access point (the harness shell lacks the local-network routing grant,
    while Apple-signed curl succeeds). The command semantics, replies and
    error classes are unchanged from the urllib/ftplib implementation:
    ``execute`` returns the CGI's ``<pre>`` body, a connection-level curl
    failure raises OSError, and an FTP refusal raises CameraRefused.
    """

    #: curl exit codes treated as a lost connection rather than a refusal.
    _CURL_TRANSPORT_CODES = (7, 28)

    def __init__(self, host: str = "192.168.43.1", timeout: float = 120.0) -> None:
        self.host = host
        self.timeout = timeout

    def _curl(self, arguments: List[str], stdin_data: Optional[bytes] = None,
              timeout: Optional[float] = None) -> subprocess.CompletedProcess:
        timeout_value = timeout or self.timeout
        result = subprocess.run(
            ["curl", "-s", "-m", f"{timeout_value:g}"] + arguments,
            input=stdin_data,
            capture_output=True,
            timeout=timeout_value + 10.0,
        )
        return result

    def _transport_error(self, result: subprocess.CompletedProcess) -> OSError:
        return OSError(
            f"curl rc={result.returncode}: "
            f"{result.stderr.decode('utf-8', 'replace')[:300]}"
        )

    def execute(self, command: str, timeout: Optional[float] = None) -> str:
        url = (
            f"http://{self.host}:8888/cgi-bin/execute?rawcmd="
            + url_quote(command)
        )
        try:
            result = self._curl([url], timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            raise OSError(f"curl timed out: {exc}") from exc
        if result.returncode != 0:
            raise self._transport_error(result)
        text = result.stdout.decode("utf-8", "replace")
        match = re.search(r"<pre>\n?(.*)</pre>", text, re.S)
        return match.group(1) if match else text

    def remote_sha256(self, remote_path: str) -> str:
        quoted = shlex.quote(remote_path)
        for command in (
            f"/usr/bin/openssl dgst -sha256 {quoted}",
            f"sha256sum {quoted}",
        ):
            text = self.execute(command)
            match = re.search(r"\b([0-9a-f]{64})\b", text)
            if match:
                return match.group(1)
        raise GuardError(f"the camera reported no sha256 digest for {remote_path}")

    def mkdir(self, remote_dir: str) -> None:
        # The camera's FTP server refuses MKD (550) but accepts STOR, so the
        # directory is created through the CGI shell, which is the same root
        # the runner's cleanup commands already use.  A transport failure
        # raises OSError from execute(); a verified absence raises
        # CameraRefused, preserving the runner's ownership semantics.
        quoted = shlex.quote(remote_dir)
        text = self.execute(
            f"mkdir -p {quoted} && test -d {quoted} && echo CREATED || echo REFUSED"
        )
        if "CREATED" not in text:
            raise CameraRefused(
                f"the camera refused to create {remote_dir}: {text[:200]!r}"
            )

    def put(self, remote_path: str, data: bytes) -> None:
        try:
            result = self._curl(
                ["--ftp-create-dirs", "-T", "-",
                 f"ftp://root:@{self.host}{remote_path}"],
                stdin_data=data,
            )
        except subprocess.TimeoutExpired as exc:
            raise OSError(f"curl timed out: {exc}") from exc
        if result.returncode != 0:
            raise self._transport_error(result)

    def get(self, remote_path: str) -> bytes:
        try:
            result = self._curl([f"ftp://root:@{self.host}{remote_path}"])
        except subprocess.TimeoutExpired as exc:
            raise OSError(f"curl timed out: {exc}") from exc
        if result.returncode != 0:
            raise self._transport_error(result)
        return result.stdout


# --- the record ------------------------------------------------------------


class Recorder:
    """Writes the full record as the run proceeds.

    The record is written after every step, so a run that dies half way
    still leaves the commands and the results that were reached.
    """

    def __init__(self, out_dir: Path) -> None:
        self.out_dir = out_dir
        out_dir.mkdir(parents=True, exist_ok=False)
        self.record: Dict[str, object] = {"started_utc": now_iso()}

    def update(self, **fields: object) -> None:
        self.record.update(fields)
        self.record["updated_utc"] = now_iso()
        self._write()

    def event(self, event_name: str, **fields: object) -> None:
        entry = {"event": event_name, "utc": now_iso()}
        entry.update(fields)
        with (self.out_dir / "events.jsonl").open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(entry, sort_keys=True) + "\n")

    def save_bytes(self, name: str, data: bytes) -> None:
        (self.out_dir / name).write_bytes(data)
        artifacts = self.record.setdefault("artifacts", {})
        artifacts[name] = len(data)  # type: ignore[union-attr]

    def _write(self) -> None:
        target = self.out_dir / "run.json"
        temporary = self.out_dir / "run.json.tmp"
        temporary.write_text(json.dumps(self.record, indent=2, sort_keys=True))
        os.replace(temporary, target)


# --- the guards ------------------------------------------------------------


def app_identity(camera: Camera) -> Tuple[str, str]:
    """The pid and start tick of the single camera application process."""
    try:
        text = camera.execute(APP_IDENTITY_COMMAND)
    except Exception as exc:  # transport failure is a refusal, not a pass
        raise GuardError(
            f"the camera application identity could not be read: "
            f"{type(exc).__name__}: {exc}"
        ) from exc
    apps = re.findall(r"^APP (\d+) (\d+)$", text, re.M)
    if len(apps) != 1:
        raise GuardError(
            f"expected exactly one {APP_COMMAND} process, saw {len(apps)}"
        )
    return apps[0]


def boot_identity(camera: Camera) -> str:
    """An identifier of the boot session; it changes when the camera reboots."""
    try:
        text = camera.execute(BOOT_IDENTITY_COMMAND)
    except Exception as exc:
        raise GuardError(
            f"the boot identity could not be read: {type(exc).__name__}: {exc}"
        ) from exc
    boot_id = ""
    btime = ""
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.isdigit():
            btime = line
        elif "-" in line:
            boot_id = line
    if not btime and not boot_id:
        raise GuardError("the camera did not report a boot identity")
    return f"{boot_id or 'no-boot-id'}:{btime or 'no-btime'}"


def dependency_sha256(camera: Camera, path: str) -> str:
    """The digest of one pinned dependency, read from the camera.

    A file that is not there and a digest that cannot be read are the same
    thing here: an unknown dependency is not a matching dependency, so both
    are a refusal rather than a pass.  The digest is also checked for shape,
    because a reply that is not a digest says nothing about the file.
    """
    try:
        digest = camera.remote_sha256(path)
    except Exception as exc:
        raise GuardError(
            f"the sha256 of {path} could not be read: {type(exc).__name__}: {exc}"
        ) from exc
    if not re.fullmatch(r"[0-9a-f]{64}", digest or ""):
        raise GuardError(
            f"the camera reported no usable sha256 digest for {path}"
        )
    return digest


def collect_guards(
    camera: Camera, cfg: Config, recorder: Recorder
) -> Tuple[Dict[str, object], bytes]:
    """Check every pinned expectation before anything is staged."""
    helper_bytes = cfg.helper.read_bytes()
    digest = sha256_bytes(helper_bytes)
    if digest != cfg.helper_sha256.lower():
        raise GuardError(
            f"the helper sha256 is {digest}, not the expected "
            f"{cfg.helper_sha256.lower()}"
        )
    recorder.event("guard.helper", helper_sha256=digest)

    app = app_identity(camera)
    recorder.event("guard.app_identity", pid=app[0], start_tick=app[1])

    boot = boot_identity(camera)
    recorder.event("guard.boot_identity")

    app_hash = camera.remote_sha256(PINNED_APP)
    if app_hash != PINNED_APP_SHA256:
        raise GuardError(
            f"{PINNED_APP} has sha256 {app_hash}, not the expected "
            f"{PINNED_APP_SHA256}"
        )
    library_hash = camera.remote_sha256(PINNED_LIBRARY)
    if library_hash != PINNED_LIBRARY_SHA256:
        raise GuardError(
            f"{PINNED_LIBRARY} has sha256 {library_hash}, not the expected "
            f"{PINNED_LIBRARY_SHA256}"
        )

    # The pinned dependencies are checked here, before anything is staged:
    # a dependency that is missing, that changed or whose digest cannot be
    # read refuses the run before the helper could be executed at all.
    dependency_hashes: Dict[str, str] = {}
    for path, expected in PINNED_DEPENDENCIES.items():
        dependency_hash = dependency_sha256(camera, path)
        if dependency_hash != expected:
            raise GuardError(
                f"{path} has sha256 {dependency_hash}, not the expected "
                f"{expected}"
            )
        dependency_hashes[path] = dependency_hash
        recorder.event("guard.dependency", path=path, sha256=dependency_hash)

    guards = {
        "app_pid": app[0],
        "app_start_tick": app[1],
        "boot_identity": boot,
        "app_sha256": app_hash,
        "library_sha256": library_hash,
        "helper_sha256": digest,
        "dependency_sha256": dependency_hashes,
    }
    recorder.update(
        guards_before=guards,
        dependency_sha256=dependency_hashes,
        pinned_dependencies=dict(PINNED_DEPENDENCIES),
    )
    return guards, helper_bytes


def verify_guards(
    camera: Camera, cfg: Config, guards: Dict[str, object], recorder: Recorder, label: str
) -> None:
    """Re-check the pinned expectations; they must be unchanged."""
    app = app_identity(camera)
    if list(app) != [guards["app_pid"], guards["app_start_tick"]]:
        raise GuardError(
            f"the camera application identity changed {label}: it was "
            f"{guards['app_pid']}/{guards['app_start_tick']}, it is "
            f"{app[0]}/{app[1]}"
        )
    boot = boot_identity(camera)
    if boot != guards["boot_identity"]:
        raise GuardError(f"the boot identity changed {label}")
    app_hash = camera.remote_sha256(PINNED_APP)
    if app_hash != guards["app_sha256"]:
        raise GuardError(f"{PINNED_APP} changed {label}")
    library_hash = camera.remote_sha256(PINNED_LIBRARY)
    if library_hash != guards["library_sha256"]:
        raise GuardError(f"{PINNED_LIBRARY} changed {label}")
    # The dependencies are re-checked at the same two points as the other
    # pins.  A dependency that is gone, that changed, or whose digest can
    # no longer be read stops the run here; after the run, the same
    # failure is reported as an error and an uncertainty.
    recorded = guards.get("dependency_sha256")
    if not isinstance(recorded, dict):
        recorded = {}
    for path, expected in PINNED_DEPENDENCIES.items():
        dependency_hash = dependency_sha256(camera, path)
        if dependency_hash != recorded.get(path, expected):
            raise GuardError(f"{path} changed {label}")
    recorder.event("guard.verified", label=label)


# --- staging ---------------------------------------------------------------


def is_generated_remote_dir(path: str, root: str = REMOTE_ROOT) -> bool:
    """True only for a directory this runner generated."""
    return bool(
        re.fullmatch(re.escape(root) + "/" + REMOTE_DIR_PREFIX + r"[0-9a-f]{12}", path)
    )


def new_remote_dir(cfg: Config) -> str:
    """The name of the directory this run will create under /tmp."""
    remote_dir = f"{cfg.remote_root}/{REMOTE_DIR_PREFIX}{uuid.uuid4().hex[:12]}"
    if not is_generated_remote_dir(remote_dir, cfg.remote_root):
        raise LoadCheckError(f"the generated remote path {remote_dir} is not safe")
    return remote_dir


def stage_helper(
    camera: Camera, cfg: Config, recorder: Recorder, remote_dir: str, helper_bytes: bytes
) -> None:
    """Put the helper in the generated directory and prove it is there."""
    recorder.event("stage.begin", remote_dir=remote_dir)
    helper_path = f"{remote_dir}/{HELPER_NAME}"
    camera.put(helper_path, helper_bytes)

    fetched = camera.get(helper_path)
    if fetched != helper_bytes:
        raise GuardError(
            f"the uploaded helper is {len(fetched)} bytes on the camera, not the "
            f"{len(helper_bytes)} bytes of the local helper"
        )
    remote_digest = camera.remote_sha256(helper_path)
    if remote_digest != cfg.helper_sha256.lower():
        raise GuardError(
            f"the uploaded helper has sha256 {remote_digest} on the camera, not "
            f"the expected {cfg.helper_sha256.lower()}"
        )
    camera.execute(f"chmod 500 {shlex.quote(helper_path)}")
    recorder.event("stage.done", helper_sha256=remote_digest)


def stage_scripts(
    camera: Camera, cfg: Config, recorder: Recorder, remote_dir: str,
    scripts: Optional[Dict[str, str]] = None,
) -> Dict[str, str]:
    """Stage the watchdog and the recovery scripts, and prove they are there.

    The scripts are uploaded over FTP rather than sent through the execute
    endpoint.  The endpoint takes its command in the URL of a GET, and a
    first request carrying a large script timed out. Its execution state
    was uncertain until separate short inspection and cleanup succeeded.

    Both scripts are staged before anything is executed, and both are read
    back and compared byte for byte and by digest.  A script that cannot
    be read back is not run: the run stops before the helper is executed.

    The bytes and the digests are kept in the local run record, so what
    was staged can be read afterwards.

    `scripts` replaces the bodies to stage.  It exists so another fixed-purpose
    path can reuse this staging and its verification verbatim.  The default is
    the runner's own load-only watchdog and recovery pair, and the runner's
    CLI still stages only those.
    """
    if scripts is None:
        scripts = {
            WATCHDOG_NAME: build_watchdog_script(cfg, remote_dir),
            RECOVERY_NAME: build_recovery_script(remote_dir),
        }
    staged: Dict[str, str] = {}
    for name, text in scripts.items():
        data = text.encode("utf-8")
        digest = sha256_bytes(data)
        path = f"{remote_dir}/{name}"
        camera.put(path, data)

        fetched = camera.get(path)
        if fetched != data:
            raise GuardError(
                f"the staged {name} is {len(fetched)} bytes on the camera, not "
                f"the {len(data)} bytes that were sent"
            )
        remote_digest = camera.remote_sha256(path)
        if remote_digest != digest:
            raise GuardError(
                f"the staged {name} has sha256 {remote_digest} on the camera, "
                f"not the expected {digest}"
            )
        camera.execute(f"chmod 500 {shlex.quote(path)}")
        recorder.save_bytes(name, data)
        staged[name] = digest
        recorder.event("stage.script", name=name, sha256=digest, bytes=len(data))
    recorder.update(staged_scripts=staged)
    return staged


def script_command(remote_dir: str, name: str) -> str:
    """The short fixed command that runs a staged script.

    The path comes from the directory this run generated and the script's
    name is fixed, so there is no way to send another path or another
    argument.
    """
    if name not in (WATCHDOG_NAME, RECOVERY_NAME):
        raise LoadCheckError(f"{name} is not a script this runner stages")
    return f"/bin/sh {shlex.quote(f'{remote_dir}/{name}')}"


# --- the identity guard ----------------------------------------------------


def build_identity_guard(proc_root: str = "/proc") -> str:
    """The identity check, shared by the watchdog and the recovery.

    A process is the one this run started only when three things agree at
    the same moment: the pid is there, its start tick is the one recorded
    when it was started, and it is running the exact path of the helper.
    The path is compared whole - the command line's first field or the
    image the process is running - and never by a base name, so a
    different program that happens to be called `capture-shot` does not
    pass.

    This is a positive identification, not a proof against every possible
    reuse of a pid: it says the process is the same one, with the same
    start, running the same image.  It cannot say anything about what the
    process does.

    Beside it, `proc_state` says whether a process is running, is known to
    have stopped, or cannot be read.  The third answer is the one that
    keeps a live child alive and keeps the recovery files: a process whose
    `stat` cannot be read has not been shown to have stopped, and an
    absent `/proc` tree is no evidence of anything.
    """
    return (
        'PROC_ROOT=${PROC_ROOT:-' + proc_root + "}\n"
        # The state of a process is three-valued, and the third value is
        # the one that matters here: a process that cannot be read is not
        # the same as a process that is gone.  Only a probe that works can
        # say that a child has stopped.
        "proc_state() {\n"
        "  pid=$1\n"
        '  if [ ! -d "$PROC_ROOT" ]; then echo unknown; return; fi\n'
        '  if [ -z "$(cat "$PROC_ROOT/uptime" 2>/dev/null)" ]; then'
        " echo unknown; return; fi\n"
        '  if [ ! -d "$PROC_ROOT/$pid" ]; then echo stopped; return; fi\n'
        "  state=$(awk '{print $3}' \"$PROC_ROOT/$pid/stat\" 2>/dev/null)\n"
        '  if [ -z "$state" ]; then echo unknown; return; fi\n'
        '  if [ "$state" = "Z" ]; then echo stopped; return; fi\n'
        "  echo running\n"
        "}\n"
        "own_child() {\n"
        "  pid=$1; tick=$2; path=$3\n"
        '  [ -n "$pid" ] || return 1\n'
        '  [ -n "$tick" ] || return 1\n'
        '  [ -d "$PROC_ROOT/$pid" ] || return 1\n'
        "  now=$(awk '{print $22}' \"$PROC_ROOT/$pid/stat\" 2>/dev/null)\n"
        '  [ -n "$now" ] || return 1\n'
        '  [ "$now" = "$tick" ] || return 1\n'
        '  exe=$(readlink "$PROC_ROOT/$pid/exe" 2>/dev/null)\n'
        "  argv0=$(tr '\\0' '\\n' < \"$PROC_ROOT/$pid/cmdline\" 2>/dev/null | sed -n '1p')\n"
        '  [ "$exe" = "$path" ] || [ "$argv0" = "$path" ]\n'
        "}\n"
    )


def build_watchdog_script(cfg: Config, remote_dir: str) -> str:
    """The one command the camera runs.

    The helper is started in the background so that a watchdog in the same
    shell can hold the deadline.  Every signal the watchdog sends is
    preceded, immediately before the signal, by the identity check: the
    same start tick and the exact helper path.  When the identity cannot
    be established - no tick, a changed tick, a different image - the
    watchdog signals nothing and says that it did not.
    """
    q = shlex.quote
    helper_path = f"{remote_dir}/{HELPER_NAME}"
    lines = [
        build_identity_guard(),
        f"cd {q(remote_dir)} || exit 90",
        "rm -f " + " ".join(
            q(f"{remote_dir}/{name}") for name in ("stdout", "stderr", "progress", "status", "owner")
        ),
        f"helper_path={q(helper_path)}",
        f"library_path={q(PINNED_LIBRARY)}",
        f"progress_path={q(f'{remote_dir}/progress')}",
        f"stdout_path={q(f'{remote_dir}/stdout')}",
        f"stderr_path={q(f'{remote_dir}/stderr')}",
        f"status_path={q(f'{remote_dir}/status')}",
        f"owner_path={q(f'{remote_dir}/owner')}",
        # The fixed argv.  There is no mode, no client, no probe, no still
        # and no setting on it, and the log stays inside the generated
        # directory instead of capture-shot's own /opt default.
        '"$helper_path" --load-only --library "$library_path" --log "$progress_path"'
        ' > "$stdout_path" 2> "$stderr_path" &',
        "child=$!",
        # The pid is written first, so that the identity is not lost if the
        # tick cannot be read.
        "printf 'pid=%s\\n' \"$child\" > \"$owner_path\"",
        # The start tick is what makes the child identifiable later.  It is
        # read with a few attempts, because an entry that cannot be read on
        # the first try is not the same as an entry that does not exist.
        # A tick that never comes leaves the run without an identity, and
        # the watchdog then signals nothing at all.
        "tick=",
        "attempt=0",
        "while [ $attempt -lt 5 ]; do",
        "  tick=$(awk '{print $22}' \"$PROC_ROOT/$child/stat\" 2>/dev/null)",
        '  [ -n "$tick" ] && break',
        "  attempt=$((attempt + 1))",
        "  sleep 1",
        "done",
        "printf 'tick=%s\\n' \"$tick\" >> \"$owner_path\"",
        "start=$(awk '{print int($1)}' \"$PROC_ROOT/uptime\" 2>/dev/null)",
        "outcome=exited",
        "terminated=no",
        "if [ -z \"$start\" ]; then",
        "  outcome=clock-unavailable",
        "else",
        "  while :; do",
        # The state of the child is three-valued.  Only a state that is
        # known to be stopped may lead to the blocking wait below; an
        # unreadable child is reported and left alone.
        '    state=$(proc_state "$child")',
        '    case "$state" in',
        "      running) ;;",
        "      stopped) outcome=exited; break ;;",
        "      *) outcome=state-unknown; break ;;",
        "    esac",
        '    if [ -z "$tick" ]; then outcome=identity-unknown; break; fi',
        "    nowtick=$(awk '{print $22}' \"$PROC_ROOT/$child/stat\" 2>/dev/null)",
        '    if [ -n "$nowtick" ] && [ "$nowtick" != "$tick" ]; then'
        " outcome=identity-lost; break; fi",
        "    cur=$(awk '{print int($1)}' \"$PROC_ROOT/uptime\" 2>/dev/null)",
        '    if [ -z "$cur" ]; then outcome=clock-unavailable; break; fi',
        f"    if [ $((cur - start)) -ge {cfg.deadline_seconds} ]; then",
        '      if own_child "$child" "$tick" "$helper_path"; then',
        '        kill -TERM "$child" 2>/dev/null',
        "        sleep 2",
        # The identity is checked again immediately before the KILL: a
        # process that took the pid in the meantime is not signalled.
        '        if own_child "$child" "$tick" "$helper_path"; then',
        '          kill -KILL "$child" 2>/dev/null',
        "          sleep 1",
        "        fi",
        "      else",
        # The child is not positively identified.  Which of the two it is
        # decides whether the old child is provably gone or still there.
        "        nowtick=$(awk '{print $22}' \"$PROC_ROOT/$child/stat\" 2>/dev/null)",
        '        if [ -z "$nowtick" ]; then',
        "          outcome=identity-unknown",
        '        elif [ "$nowtick" != "$tick" ]; then',
        "          outcome=identity-lost",
        "        else",
        "          outcome=image-changed",
        "        fi",
        "        break",
        "      fi",
        # The exit check after the signals uses the same three-valued rule:
        # only a known stopped child certifies the termination.
        '      case "$(proc_state "$child")" in',
        "        stopped) outcome=timeout; terminated=yes ;;",
        "        running) outcome=kill-unconfirmed ;;",
        "        *) outcome=state-unknown ;;",
        "      esac",
        "      break",
        "    fi",
        "    sleep 1",
        "  done",
        "fi",
        "rc=unknown",
        'case "$outcome" in',
        '  exited) wait "$child" 2>/dev/null; rc=$?; outcome="rc:$rc"; terminated=yes ;;',
        '  timeout) wait "$child" 2>/dev/null; rc=$? ;;',
        "esac",
        "printf 'outcome=%s\\nrc=%s\\nterminated=%s\\n' \"$outcome\" \"$rc\" \"$terminated\""
        ' > "$status_path"',
        "exit 0",
    ]
    return "\n".join(lines)


def build_recovery_script(remote_dir: str) -> str:
    """The second line of defence, used when the execute call itself fails.

    It signals only a process that the identity check accepts, and it
    checks the identity again immediately before every signal.  It says
    what it found, and it never says that the child has gone unless it can
    show it: a child that is signalled and confirmed gone, a child that is
    signalled and is still there, a child that is known not to be running,
    a pid whose start tick shows it was reused, a pid that is running
    another image, a state that cannot be read, or an owner file that
    cannot be read.
    """
    q = shlex.quote
    lines = [
        build_identity_guard(),
        f"owner_path={q(f'{remote_dir}/owner')}",
        f"status_path={q(f'{remote_dir}/status')}",
        f"helper_path={q(f'{remote_dir}/{HELPER_NAME}')}",
        'if [ ! -f "$owner_path" ]; then',
        "  echo OWNER-UNUSABLE",
        "else",
        # The owner file is read strictly: one pid line, one tick line,
        # both numeric.  Anything else is unusable and nothing is signalled.
        "  pid=$(sed -n 's/^pid=\\([0-9][0-9]*\\)$/\\1/p' \"$owner_path\")",
        "  tick=$(sed -n 's/^tick=\\([0-9][0-9]*\\)$/\\1/p' \"$owner_path\")",
        "  pid_lines=$(grep -c '^pid=' \"$owner_path\" 2>/dev/null)",
        "  tick_lines=$(grep -c '^tick=' \"$owner_path\" 2>/dev/null)",
        '  if [ "$pid_lines" != "1" ] || [ "$tick_lines" != "1" ] || [ -z "$pid" ]'
        ' || [ -z "$tick" ]; then',
        "    echo OWNER-UNUSABLE",
        "  else",
        # The state is three-valued.  A child that cannot be read is not a
        # child that has gone, and nothing is signalled on a state that is
        # not known.
        '    state=$(proc_state "$pid")',
        '    case "$state" in',
        # The probe works and the entry is gone: the child has stopped.
        "      stopped) echo NOT-RUNNING ;;",
        # The probe cannot answer: nothing is known, nothing is signalled.
        "      unknown) echo STATE-UNKNOWN ;;",
        "      running)",
        '        if own_child "$pid" "$tick" "$helper_path"; then',
        '          kill -TERM "$pid" 2>/dev/null',
        "          sleep 2",
        # The identity is checked again immediately before the KILL.
        '          if own_child "$pid" "$tick" "$helper_path"; then',
        '            kill -KILL "$pid" 2>/dev/null',
        "            sleep 1",
        "          fi",
        # The exit check after the signals uses the same three-valued rule.
        '          case "$(proc_state "$pid")" in',
        "            stopped) echo OWNED-KILLED-VERIFIED ;;",
        "            running) echo OWNED-KILL-UNCERTAIN ;;",
        "            *) echo OWNED-KILL-STATE-UNKNOWN ;;",
        "          esac",
        "        else",
        # The child is not positively identified.  Why it is not decides
        # whether the old child is provably gone or is still there.
        "          nowtick=$(awk '{print $22}' \"$PROC_ROOT/$pid/stat\" 2>/dev/null)",
        '          if [ -z "$nowtick" ]; then',
        # The current identity cannot be read: nothing is proved.
        "            echo IDENTITY-UNKNOWN",
        '          elif [ "$nowtick" != "$tick" ]; then',
        # A different, readable start tick: the pid was reused, so the
        # old child was reaped and is gone.
        "            echo IDENTITY-LOST",
        "          else",
        # The same start, another image: the process is alive.
        "            echo IMAGE-CHANGED",
        "          fi",
        "        fi",
        "        ;;",
        "    esac",
        "  fi",
        "fi",
        'cat "$status_path" 2>/dev/null || echo NO-STATUS',
    ]
    return "\n".join(lines)


#: What each recovery verdict says about the run's own child.
#:
#: True means the child's termination was proved.  False and None mean it
#: was not: False is a child that is still there, None is a child whose
#: state could not be read.  Neither may lead to the removal of the
#: generated directory, because that directory holds the owner file, the
#: status file and the helper that are needed to finish the job.
RECOVERY_VERDICTS = {
    # the child was signalled and is confirmed gone
    "OWNED-KILLED-VERIFIED": (True, "the run's own child was identified, signalled and is confirmed gone"),
    # the probe works and the entry is gone: the child has stopped
    "NOT-RUNNING": (True, "the run's own child is not running"),
    # the pid carries a different, readable start tick: it was reused, so
    # the old child was reaped and is gone
    "IDENTITY-LOST": (True, "the pid now has a different start tick, so the run's own child is gone"),
    # the child was signalled and is still there
    "OWNED-KILL-UNCERTAIN": (False, "the run's own child was signalled but is still there"),
    # the child was signalled and its state cannot be read
    "OWNED-KILL-STATE-UNKNOWN": (None, "the run's own child was signalled and its state could not be read"),
    # the same process, but it is running another image: it is alive
    "IMAGE-CHANGED": (False, "the pid is the same process but it is running another image, so it is still alive"),
    # the current identity cannot be read: nothing is proved
    "IDENTITY-UNKNOWN": (None, "the child's current identity could not be read, so nothing was signalled"),
    # the state of the process cannot be read at all
    "STATE-UNKNOWN": (None, "the child's state could not be read, so nothing was signalled"),
    # the identity is not known, so nothing was signalled
    "OWNER-UNUSABLE": (None, "the owner file could not be read, so nothing was signalled"),
}


def parse_recovery(text: str) -> Tuple[Optional[bool], str]:
    """Read the recovery's verdict: is the child gone, and what did it do."""
    for line in text.splitlines():
        line = line.strip()
        if line in RECOVERY_VERDICTS:
            stopped, note = RECOVERY_VERDICTS[line]
            return stopped, note
    return None, "the recovery call did not say what it found"


def run_recovery(camera: Camera, remote_dir: str) -> Tuple[Optional[bool], str]:
    """Try to end the run's own child after a failed execute call.

    The recovery script was staged and verified before the helper ran, so
    it is on the camera even when the connection is the thing that failed.
    It is invoked by the same short fixed command as the watchdog.
    """
    command = script_command(remote_dir, RECOVERY_NAME)
    try:
        text = camera.execute(command, timeout=30.0)
    except Exception as exc:
        return None, f"the recovery call failed: {type(exc).__name__}: {exc}"
    return parse_recovery(text)


def execute_helper(
    camera: Camera, cfg: Config, recorder: Recorder, remote_dir: str
) -> Outcome:
    """Run the staged watchdog and collect what the helper left behind."""
    outcome = Outcome()
    paths = {
        "stdout": f"{remote_dir}/stdout",
        "stderr": f"{remote_dir}/stderr",
        "progress": f"{remote_dir}/progress",
        "status": f"{remote_dir}/status",
        "owner": f"{remote_dir}/owner",
    }
    command = script_command(remote_dir, WATCHDOG_NAME)
    recorder.update(remote_command=command)
    recorder.event("execute.begin", remote_dir=remote_dir, command=command)

    transport_failed = False
    timed_out = False
    try:
        response = camera.execute(command, timeout=cfg.http_timeout)
        recorder.update(execute_response=response[:2000])
    except (socket.timeout, TimeoutError) as exc:
        outcome.uncertainty.append(f"the execute call timed out: {exc}")
        transport_failed = True
        timed_out = True
    except OSError as exc:
        outcome.uncertainty.append(f"the execute call failed: {exc}")
        transport_failed = True

    if transport_failed:
        # The remote shell may still be running.  The recovery call ends
        # the run's own child if it can identify it, and says what it found.
        stopped, note = run_recovery(camera, remote_dir)
        recorder.update(recovery=note, recovery_child_stopped=stopped)
        outcome.terminated = stopped
        outcome.outcome = "http-timeout" if timed_out else "connection-lost"
        if stopped is not True:
            outcome.uncertainty.append(note)

    for name, path in paths.items():
        try:
            outcome.files[name] = camera.get(path)
        except Exception as exc:
            outcome.files[name] = b""
            outcome.errors.append(
                f"the {name} file could not be fetched: {type(exc).__name__}: {exc}"
            )
        recorder.save_bytes(f"{name}.txt", outcome.files[name])

    status_text = outcome.files.get("status", b"").decode("utf-8", "replace")
    parsed = None
    status_error = None
    try:
        parsed = parse_status(status_text)
    except LoadCheckError as exc:
        status_error = str(exc)

    if parsed is not None:
        if not transport_failed:
            outcome.outcome = parsed[0]
            outcome.rc = parsed[1]
            outcome.terminated = parsed[2] == "yes"
        elif outcome.terminated is not True:
            # The status file says more than the recovery call did.
            outcome.terminated = parsed[2] == "yes"
    elif status_error is not None:
        if transport_failed:
            outcome.errors.append(
                f"the helper's status file could not be read: {status_error}"
            )
        else:
            # A status that cannot be read fails closed: the run is not a
            # success and what the helper did is not known.
            outcome.outcome = "status-invalid"
            outcome.errors.append(status_error)
            outcome.uncertainty.append(
                "the helper's status file could not be read, so what it did is not known"
            )
            recorder.event("execute.end", outcome=outcome.outcome, rc=outcome.rc)
            return outcome

    if outcome.outcome.startswith("rc:") and outcome.rc not in (0, None):
        outcome.errors.append(f"the helper exited with rc={outcome.rc}")
    if outcome.outcome == "timeout":
        outcome.uncertainty.append(
            "the helper reached the deadline and was killed by the watchdog"
        )
    elif outcome.outcome in UNPROVEN_OUTCOMES:
        outcome.uncertainty.append(
            f"the watchdog reported {outcome.outcome}, so the helper's "
            "termination could not be confirmed"
        )
    recorder.event("execute.end", outcome=outcome.outcome, rc=outcome.rc)
    return outcome


# --- cleanup ---------------------------------------------------------------


def cleanup_remote(camera: Camera, remote_dir: str, recorder: Recorder) -> bool:
    """Remove the generated directory and verify that it is gone."""
    if not is_generated_remote_dir(remote_dir):
        raise LoadCheckError(
            f"refusing to remove {remote_dir}: it is not the generated directory"
        )
    camera.execute(f"rm -rf {shlex.quote(remote_dir)}")
    listing = camera.execute(f"ls -d {shlex.quote(remote_dir)} 2>&1; echo DONE")
    removed = "No such file" in listing
    recorder.event("cleanup", remote_dir=remote_dir, removed=removed)
    if not removed:
        raise LoadCheckError(
            f"the generated directory {remote_dir} is still on the camera"
        )
    return removed


# --- the run ---------------------------------------------------------------


def run_load_check(camera: Camera, cfg: Config, recorder: Recorder) -> Result:
    """Do the run and return its answer.  It never raises for a run failure."""
    result = Result()
    guards: Optional[Dict[str, object]] = None
    remote_dir: Optional[str] = None
    # "not-created", "unknown" or "created": what this run owns.
    ownership = "not-created"
    child_stopped: Optional[bool] = True  # no helper has run yet
    first_error: Optional[str] = None

    try:
        guards, helper_bytes = collect_guards(camera, cfg, recorder)
        remote_dir = new_remote_dir(cfg)
        result.remote_dir = remote_dir
        recorder.update(remote_dir=remote_dir)
        try:
            camera.mkdir(remote_dir)
        except CameraRefused as exc:
            # The camera said no: the directory was not created, so this
            # run does not own it and must not remove it.
            raise LoadCheckError(str(exc)) from exc
        except Exception as exc:
            # The connection was lost: the camera may have created it.
            ownership = "unknown"
            raise UncertainOwnership(
                f"the connection was lost while {remote_dir} was being created, "
                f"so whether it exists is not known: {type(exc).__name__}: {exc}"
            ) from exc
        ownership = "created"
        # From here the directory exists and this run owns it, so it has
        # to be cleaned up whatever the next step does.
        stage_helper(camera, cfg, recorder, remote_dir, helper_bytes)
        # The watchdog and the recovery scripts are staged and verified
        # before anything is executed.  A script that cannot be read back
        # is not run, and the run stops here rather than starting a child
        # it would not be able to end.
        stage_scripts(camera, cfg, recorder, remote_dir)
        # Immediately before the execution, the pinned expectations are
        # checked again.  A failure here stops the run before the helper
        # can be executed at all.
        verify_guards(camera, cfg, guards, recorder, "before execution")
        # From here on the helper may be running, so its state is unknown
        # until the status file or the recovery says otherwise.  If
        # anything goes wrong in between, the directory is kept.
        child_stopped = None
        outcome = execute_helper(camera, cfg, recorder, remote_dir)
        result.outcome = outcome.outcome
        result.rc = outcome.rc
        result.errors.extend(outcome.errors)
        result.uncertainty.extend(outcome.uncertainty)
        child_stopped = outcome.terminated
    except LoadCheckError as exc:
        first_error = str(exc)
        result.errors.append(first_error)
    except Exception as exc:  # a broken transport is a failure, not a pass
        first_error = f"{type(exc).__name__}: {exc}"
        result.errors.append(first_error)

    # After the run, the pinned expectations must still hold.
    if guards is not None:
        try:
            verify_guards(camera, cfg, guards, recorder, "after execution")
        except LoadCheckError as exc:
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
    # but only when this run owns it and the helper's termination is
    # confirmed.  A directory that may still hold a running helper is kept,
    # so the identity needed to finish the job is not thrown away.
    if ownership == "not-created":
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
        if child_stopped is not True:
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
                result.cleanup_verified = cleanup_remote(camera, remote_dir, recorder)
            except LoadCheckError as exc:
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

    result.ok = (
        first_error is None
        and not result.errors
        and not result.uncertainty
        and result.rc == 0
        and result.outcome == "rc:0"
    )
    recorder.update(
        ok=result.ok,
        outcome=result.outcome,
        rc=result.rc,
        cleanup_verified=result.cleanup_verified,
        residual_path=result.residual_path,
        errors=result.errors,
        uncertainty=result.uncertainty,
        finished_utc=now_iso(),
    )
    return result


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Run capture-shot --load-only on the camera, once, under guards.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--helper", type=Path, required=True,
                        help="the capture-shot binary to upload")
    parser.add_argument("--helper-sha256", required=True,
                        help="the sha256 the helper must have")
    parser.add_argument("--out-dir", type=Path, required=True,
                        help="where the full record is written")
    parser.add_argument("--camera", default="192.168.43.1",
                        help="the camera's address")
    parser.add_argument("--deadline", type=int, default=60,
                        help="the helper's deadline in seconds")
    parser.add_argument("--http-timeout", type=float, default=120.0,
                        help="the execute call's timeout in seconds")
    arguments = parser.parse_args(argv)

    cfg = Config(
        helper=arguments.helper,
        helper_sha256=arguments.helper_sha256.lower(),
        deadline_seconds=arguments.deadline,
        http_timeout=arguments.http_timeout,
    )
    try:
        recorder = Recorder(arguments.out_dir)
    except FileExistsError:
        print(f"run_load_check: {arguments.out_dir} already exists", file=sys.stderr)
        return 1

    camera = HttpFtpCamera(host=arguments.camera, timeout=arguments.http_timeout)
    result = run_load_check(camera, cfg, recorder)
    summary = result.console()
    summary["helper_sha256"] = cfg.helper_sha256
    summary["library_sha256"] = PINNED_LIBRARY_SHA256
    summary["app_sha256"] = PINNED_APP_SHA256
    summary["dependency_sha256"] = dict(PINNED_DEPENDENCIES)
    summary["out_dir"] = str(arguments.out_dir)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return exit_code(result)


if __name__ == "__main__":
    sys.exit(main())
