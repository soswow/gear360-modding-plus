#!/usr/bin/env python3
"""Offline tests for the init-probe orchestration path.

No camera and no network are used.  These tests drive the new path through a
fake camera that speaks the same five operations as the real one, and they
drive the observer directly so that each guard can be broken on its own.

They cover the failure modes the next trial has to survive:

  * the observer must be running before the probe is executed;
  * the CGI command must carry no literal newline and the response must carry
    the end marker;
  * the observer's bounded timeout must survive a camera that came from a
    different import of the runner file;
  * the helper's pid, start tick and executable path must agree before and
    after the sample, and the boot identity must not change;
  * the generated probe guard must refuse when the stock application is
    running again;
  * the owned directory must survive when the helper's termination is
    unknown;
  * a successful SDK return, an existing worker and a ready service are three
    different things and must be reported as three different things.
"""
from __future__ import annotations

import hashlib
import importlib.util
import contextlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import run_load_check as runner  # noqa: E402
import run_init_probe as orchestration  # noqa: E402
import observe_init_probe as observer  # noqa: E402
import prepare_init_probe as prepare  # noqa: E402


HELPER_BYTES = b"capture-shot-binary-for-the-tests\n"
HELPER_SHA = hashlib.sha256(HELPER_BYTES).hexdigest()
#: The boot identity as the camera reports it: the boot id and the btime on
#: two lines.
BOOT_RAW = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n1712345678"
#: The same identity as the runner composes it.
BOOT = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee:1712345678"
OWNER = "pid=1234\ntick=42\n"
HOLD = observer.HOLD_MARKER


def snapshot_text(pid: str = "1234", tick: str = "42",
                  helper: str = "/tmp/gear360-load-0123456789ab/capture-shot",
                  before_tick: str | None = None,
                  after_tick: str | None = None,
                  before_exe: str | None = None,
                  after_exe: str | None = None,
                  end: bool = True) -> str:
    before_tick = tick if before_tick is None else before_tick
    after_tick = tick if after_tick is None else after_tick
    before_exe = helper if before_exe is None else before_exe
    after_exe = helper if after_exe is None else after_exe
    text = (
        f"PID={pid}\n"
        f"BEFORE_TICK={before_tick}\nBEFORE_EXE={before_exe}\n"
        "FD_BEGIN\nFD|6|/dev/d5_lock\nFD|7|/dev/d5_ipcc\n"
        "FD|8|/dev/mem\nFD|9|/dev/mem\nFD_END\n"
        "TASK_BEGIN\nTHREAD|31|udd_ipcc_fmt_in|futex_wait\n"
        "THREAD|32|MCB_Receiver|futex_wait\n"
        "THREAD|33|MCB_Sender|futex_wait\nTASK_END\n"
        f"AFTER_TICK={after_tick}\nAFTER_EXE={after_exe}\n"
    )
    if end:
        text += "SNAPSHOT_END\n"
    return text


class FakeCamera(runner.Camera):
    """A camera that answers with what a test tells it to answer.

    It records the order of the operations, because the order is what the
    observer-before-probe requirement is about.
    """

    def __init__(self) -> None:
        self.log: list[tuple[str, str]] = []
        self.files: dict[str, bytes] = {}
        # What was uploaded, kept after the cleanup removed the directory.
        self.uploaded: dict[str, bytes] = {}
        self.directories: list[str] = []
        self.boot_before = BOOT_RAW
        self.boot_after: str | None = None
        self.hashes: dict[str, str] = {
            runner.PINNED_APP: runner.PINNED_APP_SHA256,
            runner.PINNED_LIBRARY: runner.PINNED_LIBRARY_SHA256,
            orchestration.PINNED_BRIDGE: orchestration.PINNED_BRIDGE_SHA256,
        }
        for path, digest in runner.PINNED_DEPENDENCIES.items():
            self.hashes[path] = digest
        self.status_content = "outcome=rc:0\nrc=0\nterminated=yes\n"
        self.stdout_content = b""
        self.stderr_content = b"capture-shot: client created; readiness unverified\n"
        self.progress_content = (HOLD + "\n").encode()
        self.recovery_reply = "OWNED-KILLED-VERIFIED\nNO-STATUS\n"
        self.execute_failure: BaseException | None = None
        self.remove_works = True
        # The stock application and the device channel, as the trial's
        # preconditions see them.
        self.app_present = True
        self.unit_active = True
        self.unit_mainpid_missing = False
        self.stock_state = "S"
        self.app_scan_complete = True
        self.holder_scan_complete = True
        self.holder_scan_error = False
        self.app_scan_error = False
        self.change_bridge_on_watchdog_put = False
        self.change_library_on_watchdog_run = False
        self.d5_holders: list[tuple[str, str, str, str]] = []
        # The /proc sample the observer gets.  It is built from the command
        # the observer sent, the way the camera answers it; a test that needs
        # a broken sample sets `snapshot_override`.
        self.snapshot_override: str | None = None
        self.snapshot_process_only = False
        self.snapshot_tick_mismatch = False
        # Set when the observer's sample has been answered, so the fake
        # watchdog can wait for it the way the camera waits for the hold.
        self.snapshot_done = threading.Event()
        self.hold_for_sample = True
        self.helper_ran = False

    # -- helpers ----------------------------------------------------------

    def _boot(self) -> str:
        if self.helper_ran and self.boot_after is not None:
            return self.boot_after
        return self.boot_before

    def removals(self) -> list[str]:
        return [arg for kind, arg in self.log if kind == "execute" and arg.startswith("rm -rf")]

    def snapshot_calls(self) -> list[str]:
        return [arg for kind, arg in self.log if kind == "execute" and "SNAPSHOT_END" in arg]

    def watchdog_calls(self) -> list[str]:
        return [arg for kind, arg in self.log
                if kind == "execute" and arg.startswith("/bin/sh ")
                and runner.WATCHDOG_NAME in arg]

    # -- the camera interface ---------------------------------------------

    def execute(self, command: str, timeout: float | None = None) -> str:
        self.log.append(("execute", command))
        if runner.RECOVERY_NAME in command and command.startswith("/bin/sh "):
            return self.recovery_reply
        if runner.WATCHDOG_NAME in command and command.startswith("/bin/sh "):
            if self.execute_failure is not None:
                raise self.execute_failure
            self.helper_ran = True
            if self.change_library_on_watchdog_run:
                self.hashes[runner.PINNED_LIBRARY] = "0" * 64
            match = re.search(r"/bin/sh '?([^'\s]+)'?", command)
            directory = match.group(1).rsplit("/", 1)[0] if match else ""
            self.files[f"{directory}/stdout"] = self.stdout_content
            self.files[f"{directory}/stderr"] = self.stderr_content
            self.files[f"{directory}/progress"] = self.progress_content
            self.files[f"{directory}/status"] = self.status_content.encode()
            self.files[f"{directory}/owner"] = OWNER.encode()
            if self.hold_for_sample:
                # The camera holds the probe while the observer samples it.
                self.snapshot_done.wait(timeout=10.0)
            return ""
        if "SNAPSHOT_END" in command:
            self.snapshot_done.set()
            if self.snapshot_override is not None:
                return self.snapshot_override
            pid = re.search(r"(?:^|; )pid=(\d+)", command)
            tick = re.search(r"expected_tick=(\d+)", command)
            # The command separates its parts with "; ", and a plain path is
            # not quoted by shlex, so the value ends at the separator.
            exe = re.search(r"expected_exe='([^']*)'", command)
            if exe is None:
                exe = re.search(r"expected_exe=([^;]*)", command)
            sample = snapshot_text(
                pid=pid.group(1) if pid else "1234",
                tick=tick.group(1) if tick else "42",
                after_tick=(str(int(tick.group(1)) + 1)
                            if self.snapshot_tick_mismatch and tick else None),
                helper=exe.group(1) if exe else "",
            )
            if self.snapshot_process_only:
                sample = re.sub(r"FD\|[^\n]*\n", "", sample)
            return sample
        if 'printf "A|' in command:
            # The camera echoes the command before its output, and the echo
            # contains the record marker itself.
            if not self.app_present:
                return "Executing:  " + command + "\nAPPS_END"
            error = "APP_SCAN_ERROR=comm:888\n" if self.app_scan_error else ""
            suffix = "APPS_END\n" if self.app_scan_complete else ""
            return "Executing:  " + command + "\n" + error + f"A|4321|98765|{self.stock_state}\n" + suffix
        if 'printf "H|' in command:
            rows = "".join("H|%s|%s|%s|%s\n" % row for row in self.d5_holders)
            error = "SCAN_ERROR=readlink:888:3\n" if self.holder_scan_error else ""
            return "Executing:  " + command + "\n" + error + rows + ("SCAN_END\n" if self.holder_scan_complete else "")
        if "systemctl show" in command:
            if self.unit_active:
                return "ActiveState=active\nMainPID=4321\n"
            return "ActiveState=inactive\n" + ("" if self.unit_mainpid_missing else "MainPID=0\n")
        if "systemctl --no-block stop" in command:
            self.app_present = False
            self.unit_active = False
            # The stock application goes away with the stop; a holder that is
            # not the stock application does not.
            self.d5_holders = [row for row in self.d5_holders
                               if row[3] != "di-camera-app"]
            return "STOP_RC=0\n"
        if 'kill -9' in command and "reset-failed" in command:
            self.app_present = False
            self.unit_active = False
            self.d5_holders = [row for row in self.d5_holders
                               if row[3] != "di-camera-app"]
            return "STOP_RC=0\nRESET_RC=0\n"
        if "/proc/[0-9]*" in command:
            # The runner's own application-identity command.
            if not self.app_present:
                return ""
            return "APP 4321 98765\n"
        if "btime" in command:
            return self._boot() + "\n"
        if "openssl dgst" in command:
            match = re.search(r"openssl dgst -sha256 '([^']+)'", command)
            path = match.group(1) if match else ""
            if path in self.hashes:
                return f"SHA256({path})= {self.hashes[path]}\n"
            data = self.files.get(path, b"")
            return f"SHA256({path})= {hashlib.sha256(data).hexdigest()}\n"
        if command.startswith("rm -rf"):
            if self.remove_works:
                self.files = {}
                self.directories = []
            return ""
        if "ls -d" in command:
            if self.remove_works:
                return "ls: cannot access 'x': No such file or directory\nDONE\n"
            match = re.search(r"ls -d '?([^'\s]+)'?", command)
            return f"{match.group(1) if match else 'x'}\nDONE\n"
        if command.startswith("chmod"):
            return ""
        return ""

    def remote_sha256(self, remote_path: str) -> str:
        text = self.execute(f"/usr/bin/openssl dgst -sha256 '{remote_path}'")
        match = re.search(r"\b([0-9a-f]{64})\b", text)
        if not match:
            raise runner.GuardError(f"no digest for {remote_path}")
        return match.group(1)

    def mkdir(self, remote_dir: str) -> None:
        self.directories.append(remote_dir)

    def put(self, remote_path: str, data: bytes) -> None:
        self.log.append(("put", remote_path))
        self.files[remote_path] = data
        self.uploaded[remote_path] = data
        if self.change_bridge_on_watchdog_put and remote_path.endswith("/watchdog.sh"):
            self.hashes[orchestration.PINNED_BRIDGE] = "0" * 64

    def get(self, remote_path: str) -> bytes:
        self.log.append(("get", remote_path))
        if remote_path not in self.files:
            raise OSError(f"no such file: {remote_path}")
        return self.files[remote_path]


class ObserverTestCase(unittest.TestCase):
    """The observer on its own, so each of its guards can be broken alone."""

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.camera = FakeCamera()
        self.remote_dir = "/tmp/gear360-load-0123456789ab"
        self.output = self.root / "observer.json"
        self.camera.files[f"{self.remote_dir}/owner"] = OWNER.encode()
        self.camera.files[f"{self.remote_dir}/progress"] = (HOLD + "\n").encode()

    def observe(self, **kwargs):
        return observer.observe_init_probe(
            self.camera, BOOT, self.remote_dir, self.output, **kwargs
        )

    def test_a_complete_sample_is_a_snapshot(self):
        record = self.observe()
        self.assertEqual(record["status"], "snapshot")
        self.assertIsNone(record["reason"])
        self.assertEqual(record["owner"], {"pid": "1234", "start_tick": "42"})

    def test_the_command_carries_no_literal_newline(self):
        command = observer._snapshot_command("1234", "42", "/tmp/x/capture-shot")
        self.assertNotIn("\n", command)
        self.assertIn("; ", command)

    def test_a_response_without_the_end_marker_is_refused(self):
        self.camera.snapshot_override = snapshot_text(end=False)
        record = self.observe()
        self.assertEqual(record["status"], "refused")
        self.assertIn("incomplete", record["reason"])

    def test_a_changed_start_tick_is_refused(self):
        self.camera.snapshot_override = snapshot_text(after_tick="43")
        record = self.observe()
        self.assertEqual(record["status"], "refused")
        self.assertIn("start tick", record["reason"])

    def test_a_changed_executable_is_refused(self):
        self.camera.snapshot_override = snapshot_text(after_exe="/tmp/other/capture-shot")
        record = self.observe()
        self.assertEqual(record["status"], "refused")
        self.assertIn("executable path", record["reason"])

    def test_a_guard_failure_in_the_response_is_refused(self):
        self.camera.snapshot_override = "GUARD_FAIL=exe-before\n"
        record = self.observe()
        self.assertEqual(record["status"], "refused")
        self.assertEqual(record["reason"], "exe-before")

    def test_a_mismatched_pid_is_refused(self):
        self.camera.snapshot_override = snapshot_text(pid="9999")
        record = self.observe()
        self.assertEqual(record["status"], "refused")
        self.assertIn("PID", record["reason"])

    def test_a_changed_boot_is_refused_before_the_sample(self):
        self.camera.boot_after = "ffffffff-bbbb-cccc-dddd-eeeeeeeeeeee:1712345678"
        self.camera.helper_ran = True
        record = self.observe()
        self.assertEqual(record["status"], "refused")
        self.assertIn("boot identity", record["reason"])

    def test_the_stop_predicate_ends_the_observation(self):
        # Without the hold marker the only thing that can end the
        # observation early is the stop predicate.
        self.camera.files[f"{self.remote_dir}/progress"] = b"capture-shot: start\n"
        record = self.observe(should_stop=lambda: True)
        self.assertEqual(record["status"], "stopped")
        self.assertIn("probe finished", record["reason"])

    def test_the_timeout_survives_a_camera_from_another_import(self):
        # A second import of the same runner file is a different class object.
        spec = importlib.util.spec_from_file_location(
            "gear360_run_load_check_second", HERE / "run_load_check.py"
        )
        assert spec is not None and spec.loader is not None
        other = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = other
        spec.loader.exec_module(other)
        camera = other.HttpFtpCamera("192.168.43.1", timeout=120.0)
        bounded = observer._camera_with_timeout(camera, 7.5, observer._runner())
        self.assertLessEqual(bounded.timeout, 7.5)
        self.assertIsNot(bounded, camera)

    def test_timeout_clone_keeps_interrupt_guard_and_smaller_budget(self):
        event = threading.Event()
        camera = observer._runner().HttpFtpCamera("192.168.43.1", timeout=55.0)
        wrapped = orchestration._InterruptibleObserverCamera(camera, event)
        bounded = observer._camera_with_timeout(wrapped, 7.5, observer._runner())
        smaller = observer._camera_with_timeout(bounded, 2.0, observer._runner())
        self.assertIsInstance(smaller, orchestration._InterruptibleObserverCamera)
        self.assertEqual(bounded._camera.timeout, 7.5)
        self.assertEqual(smaller._camera.timeout, 2.0)
        self.assertIs(smaller._interrupted, event)
        event.set()
        with self.assertRaisesRegex(RuntimeError, "cancelled"):
            smaller.get("/not-accessed")
        with self.assertRaisesRegex(RuntimeError, "cancelled"):
            bounded.execute("not executed")

class OrchestrationTestCase(unittest.TestCase):
    """The whole path, against the fake camera."""

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.helper = self.root / "capture-shot"
        self.helper.write_bytes(HELPER_BYTES)
        self.camera = FakeCamera()
        self.camera.app_present = False
        self.camera.unit_active = False
        self.cfg = runner.Config(
            helper=self.helper,
            helper_sha256=HELPER_SHA,
            deadline_seconds=5,
            http_timeout=30.0,
        )

    def run_it(self, **kwargs):
        out = self.root / f"run-{len(list(self.root.iterdir()))}"
        recorder = runner.Recorder(out)
        return orchestration.run_init_probe(self.camera, self.cfg, recorder, **kwargs)

    def test_still_mode_validation_happens_before_camera_io(self):
        cases = [
            dict(diagnostic_still=True, local_bootstrap=True, notify=True),
            dict(diagnostic_still=True, local_bootstrap=False, notify=True, free_bytes="1"),
            dict(diagnostic_still=True, local_bootstrap=True, notify=False, free_bytes="1"),
            dict(diagnostic_still=False, free_bytes="1"),
        ]
        for value in ("", "-1", "+1", "1;touch x", "١", "18446744073709551616", 1):
            cases.append(dict(diagnostic_still=True, local_bootstrap=True, notify=True, free_bytes=value))
        for kwargs in cases:
            with self.subTest(kwargs=kwargs):
                self.camera.log.clear()
                result = self.run_it(**kwargs)
                self.assertFalse(result.ok)
                self.assertTrue(result.errors)
                self.assertEqual(self.camera.log, [])
                self.assertEqual(self.camera.directories, [])

    def test_still_mode_fixed_argv_and_bounded_result(self):
        result = self.run_it(diagnostic_still=True, local_bootstrap=True,
                             notify=True, free_bytes="00018446744073709551615")
        self.assertTrue(result.ok)
        self.assertEqual(result.diagnostic_mode, "one-still")
        self.assertEqual(result.still_completion, "not-established")
        self.assertEqual(result.request_association, "not-established")
        self.assertEqual(result.raw_ownership, "not-established")
        watchdog = self.camera.uploaded[result.remote_dir + "/watchdog.sh"].decode()
        command = ('"$helper_path" --diagnostic-one-still --library "$library_path" '
                   '--log "$progress_path" --free-bytes 18446744073709551615')
        self.assertEqual(watchdog.count(command), 1)
        self.assertNotIn(" --probe ", watchdog)
        self.assertNotIn(" --probe-notify", watchdog)
        self.assertTrue(result.cleanup_verified)

    def test_still_mode_unknown_termination_preserves_directory(self):
        self.camera.execute_failure = OSError("connection lost")
        self.camera.recovery_reply = "NO-OWNED-CHILD\n"
        result = self.run_it(diagnostic_still=True, local_bootstrap=True,
                             notify=True, free_bytes="0")
        self.assertFalse(result.ok)
        self.assertEqual(result.residual_path, result.remote_dir)
        self.assertEqual(self.camera.removals(), [])

    def test_payloads_reject_injection_and_keep_default_argv(self):
        remote = "/tmp/gear360-load-0123456789ab"
        normal, recovery = prepare.build_payloads(remote, BOOT)
        self.assertIn(prepare.PROBE_ARGV, normal)
        self.assertNotIn("--diagnostic-one-still", normal)
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, True, "1;id")
        still, recovery2 = prepare.build_payloads(remote, BOOT, True, True, True, "0")
        self.assertEqual(recovery, recovery2)
        self.assertEqual(still.count('--diagnostic-one-still'), 1)

    def test_liveview_payload_is_staged_and_never_carries_a_still_command(self):
        remote = "/tmp/gear360-load-0123456789ab"
        normal, recovery = prepare.build_payloads(remote, BOOT)
        self.assertNotIn("--diagnostic-liveview-only", normal)
        liveview, recovery2 = prepare.build_payloads(
            remote, BOOT, True, True, False, "4294967297", True)
        self.assertEqual(recovery, recovery2)
        self.assertEqual(liveview.count("--diagnostic-liveview-only"), 1)
        # The liveview helper must never be given a still command.
        self.assertNotIn("--diagnostic-one-still", liveview)
        # It still takes --free-bytes because its own CLI is strict about it.
        self.assertIn("--free-bytes 4294967297", liveview)

    def test_liveview_and_still_modes_are_exclusive(self):
        remote = "/tmp/gear360-load-0123456789ab"
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, True, "1", True)
        # A liveview run without a free-bytes value is refused before staging.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, None, True)

    def test_liveview_latch_payload_is_staged_and_carries_no_still_command(self):
        remote = "/tmp/gear360-load-0123456789ab"
        normal, recovery = prepare.build_payloads(remote, BOOT)
        # The plain payload must never carry the latch mode token.
        self.assertNotIn("--diagnostic-liveview-latch-only", normal)
        latch, recovery2 = prepare.build_payloads(
            remote, BOOT, True, True, False, "4294967297", False, True)
        self.assertEqual(recovery, recovery2)
        self.assertEqual(latch.count("--diagnostic-liveview-latch-only"), 1)
        # The latch helper must never be given a still command, and it must
        # not be confused with the liveview-only helper.
        self.assertNotIn("--diagnostic-one-still", latch)
        self.assertNotIn("--diagnostic-liveview-only", latch)
        self.assertIn("--free-bytes 4294967297", latch)

    def test_liveview_latch_mode_is_exclusive_with_the_other_diagnostics(self):
        remote = "/tmp/gear360-load-0123456789ab"
        # latch and still together are refused.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, True, "1", False, True)
        # latch and liveview-only together are refused.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, "1", True, True)
        # A latch run without a free-bytes value is refused before staging.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, None, False, True)

    def test_liveview_latch_cli_refuses_invalid_before_creating_output(self):
        output = self.root / "no-output-latch"
        args = ["--helper", "unused", "--helper-sha256", HELPER_SHA,
                "--output", str(output), "--diagnostic-liveview-latch",
                "--free-bytes", "18446744073709551616"]
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                orchestration.main(args)
        self.assertEqual(raised.exception.code, 2)
        self.assertFalse(output.exists())

    def test_liveview_admit_payload_is_staged_and_carries_no_still_command(self):
        remote = "/tmp/gear360-load-0123456789ab"
        normal, recovery = prepare.build_payloads(remote, BOOT)
        # The plain payload must never carry the admit mode token.
        self.assertNotIn("--diagnostic-liveview-admit-only", normal)
        admit, recovery2 = prepare.build_payloads(
            remote, BOOT, True, True, False, "4294967297", False, False, True)
        self.assertEqual(recovery, recovery2)
        self.assertEqual(admit.count("--diagnostic-liveview-admit-only"), 1)
        # The admit helper must never be given a still command, and it must
        # not be confused with the other liveview helpers.
        self.assertNotIn("--diagnostic-one-still", admit)
        self.assertNotIn("--diagnostic-liveview-only", admit)
        self.assertNotIn("--diagnostic-liveview-latch-only", admit)
        self.assertIn("--free-bytes 4294967297", admit)

    def test_liveview_admit_mode_is_exclusive_with_the_other_diagnostics(self):
        remote = "/tmp/gear360-load-0123456789ab"
        # admit and still together are refused.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, True, "1", False, False, True)
        # admit and liveview-only together are refused.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, "1", True, False, True)
        # admit and liveview-latch together are refused.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, "1", False, True, True)
        # An admit run without a free-bytes value is refused before staging.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, None, False, False, True)

    def test_liveview_admit_cli_refuses_invalid_before_creating_output(self):
        output = self.root / "no-output-admit"
        args = ["--helper", "unused", "--helper-sha256", HELPER_SHA,
                "--output", str(output), "--diagnostic-liveview-admit",
                "--free-bytes", "18446744073709551616"]
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                orchestration.main(args)
        self.assertEqual(raised.exception.code, 2)
        self.assertFalse(output.exists())

    def test_still_liveview_payload_is_staged_and_carries_no_other_mode(self):
        remote = "/tmp/gear360-load-0123456789ab"
        normal, recovery = prepare.build_payloads(remote, BOOT)
        # The plain payload must never carry the still-liveview mode token.
        self.assertNotIn("--diagnostic-one-still-liveview", normal)
        still_liveview, recovery2 = prepare.build_payloads(
            remote, BOOT, True, True, False, "4294967297", False, False, False, True)
        self.assertEqual(recovery, recovery2)
        self.assertEqual(still_liveview.count("--diagnostic-one-still-liveview"), 1)
        # It must not be confused with the plain still mode or the liveview modes.
        self.assertNotIn("--diagnostic-one-still ", still_liveview)
        self.assertNotIn("--diagnostic-liveview-only", still_liveview)
        self.assertNotIn("--diagnostic-liveview-latch-only", still_liveview)
        self.assertNotIn("--diagnostic-liveview-admit-only", still_liveview)
        self.assertIn("--free-bytes 4294967297", still_liveview)

    def test_still_liveview_mode_is_exclusive_with_the_other_diagnostics(self):
        remote = "/tmp/gear360-load-0123456789ab"
        # still-liveview and still together are refused.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, True, "1", False, False, False, True)
        # still-liveview and the liveview modes are refused.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, "1", True, False, False, True)
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, "1", False, True, False, True)
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, "1", False, False, True, True)
        # The still modes still require local_bootstrap and notify.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, False, False, False, "1", False, False, False, True)
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, False, False, "1", False, False, False, True)
        # A still-liveview run without a free-bytes value is refused before staging.
        with self.assertRaises(ValueError):
            prepare.build_payloads(remote, BOOT, True, True, False, None, False, False, False, True)

    def test_still_liveview_cli_refuses_invalid_before_creating_output(self):
        output = self.root / "no-output-still-liveview"
        args = ["--helper", "unused", "--helper-sha256", HELPER_SHA,
                "--output", str(output), "--diagnostic-still-liveview",
                "--local-bootstrap", "--notify", "--free-bytes", "18446744073709551616"]
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                orchestration.main(args)
        self.assertEqual(raised.exception.code, 2)
        self.assertFalse(output.exists())

    def test_still_cli_refuses_invalid_before_creating_output(self):
        output = self.root / "no-output"
        args = ["--helper", "unused", "--helper-sha256", HELPER_SHA,
                "--output", str(output), "--diagnostic-still", "--local-bootstrap", "--notify",
                "--free-bytes", "18446744073709551616"]
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                orchestration.main(args)
        self.assertEqual(raised.exception.code, 2)
        self.assertFalse(output.exists())

    def assert_interrupted_saved(self, recorder, result):
        self.assertFalse(result.ok)
        self.assertTrue(result.interrupted)
        self.assertEqual(result.outcome, "interrupted")
        self.assertEqual(result.helper_termination, "unknown")
        self.assertEqual(result.residual_path, result.remote_dir)
        self.assertFalse(result.cleanup_verified)
        self.assertTrue(result.uncertainty)
        record = json.loads((recorder.out_dir / "run.json").read_text())
        self.assertTrue(record["interrupted"])
        self.assertEqual(record["outcome"], "interrupted")
        self.assertEqual(record["helper_termination"], "unknown")
        self.assertEqual(record["residual_path"], result.remote_dir)
        self.assertFalse(record["ok"])
        index = self.camera.log.index(("interrupt", "here"))
        self.assertEqual(self.camera.log[index + 1:], [])
        self.assertEqual(self.camera.removals(), [])
        self.assertFalse(any(text.startswith("/bin/sh ") and runner.RECOVERY_NAME in text
                             for kind, text in self.camera.log if kind == "execute"))

    def test_interrupt_during_execution_preserves_unknown_child_without_calls(self):
        original = self.camera.execute
        def execute(command, timeout=None):
            if runner.WATCHDOG_NAME in command and command.startswith("/bin/sh "):
                self.camera.log.append(("interrupt", "here"))
                raise KeyboardInterrupt()
            return original(command, timeout=timeout)
        self.camera.execute = execute
        recorder = runner.Recorder(self.root / "interrupted-execute")
        result = orchestration.run_init_probe(self.camera, self.cfg, recorder)
        self.assert_interrupted_saved(recorder, result)

    def test_interrupt_during_evidence_fetch_stops_remaining_fetches(self):
        original = self.camera.get
        def get(path):
            if self.camera.helper_ran and path.endswith("/stdout"):
                self.camera.log.append(("interrupt", "here"))
                raise KeyboardInterrupt()
            return original(path)
        self.camera.get = get
        recorder = runner.Recorder(self.root / "interrupted-evidence")
        result = orchestration.run_init_probe(self.camera, self.cfg, recorder)
        self.assert_interrupted_saved(recorder, result)
        self.assertEqual(len(self.camera.watchdog_calls()), 1)

    def test_interrupt_during_stop_records_possible_service_effect(self):
        original = orchestration.stop_stock_application
        def interrupted_stop(*args, **kwargs):
            self.camera.log.append(("interrupt", "here"))
            raise KeyboardInterrupt()
        orchestration.stop_stock_application = interrupted_stop
        try:
            recorder = runner.Recorder(self.root / "interrupted-stop")
            result = orchestration.run_init_probe(self.camera, self.cfg, recorder, stop_stock=True)
        finally:
            orchestration.stop_stock_application = original
        self.assertTrue(result.interrupted)
        self.assertEqual(result.helper_termination, "not-started")
        self.assertTrue(any("stop may have been requested" in x for x in result.uncertainty))
        self.assertTrue(result.residual_path)
        index = self.camera.log.index(("interrupt", "here"))
        self.assertEqual(self.camera.log[index + 1:], [])
        self.assertFalse(result.cleanup_verified)

    def test_interrupt_before_remote_ownership_makes_no_camera_followup(self):
        def execute(command, timeout=None):
            self.camera.log.append(("interrupt", "here"))
            raise KeyboardInterrupt()
        self.camera.execute = execute
        recorder = runner.Recorder(self.root / "interrupted-before")
        result = orchestration.run_init_probe(self.camera, self.cfg, recorder)
        self.assertTrue(result.interrupted)
        self.assertEqual(result.helper_termination, "not-started")
        self.assertEqual(result.residual_path, "")
        self.assertEqual(self.camera.log, [("interrupt", "here")])
        self.assertEqual(json.loads((recorder.out_dir / "run.json").read_text())["outcome"], "interrupted")

    def test_the_observer_starts_before_the_probe_is_executed(self):
        result = self.run_it()
        kinds = [kind for kind, _ in self.camera.log]
        first_get = kinds.index("get") if "get" in kinds else -1
        first_watchdog = kinds.index("execute", self.camera.log.index(
            ("execute", self.camera.watchdog_calls()[0])
        )) if self.camera.watchdog_calls() else -1
        # The observer's first read of the owner file must precede the
        # watchdog command that starts the probe.
        self.assertGreaterEqual(first_get, 0)
        self.assertGreaterEqual(first_watchdog, 0)
        self.assertLess(first_get, first_watchdog)

    def test_a_clean_trial_reports_the_three_levels_apart(self):
        recorder = runner.Recorder(self.root / "clean-run")
        result = orchestration.run_init_probe(self.camera, self.cfg, recorder)
        self.assertTrue(result.sdk_returned)
        self.assertTrue(result.worker_observed)
        self.assertEqual(result.worker_observation, "snapshot")
        self.assertTrue(result.local_transport_observed)
        self.assertEqual(result.local_transport_missing, [])
        raw = json.loads((recorder.out_dir / "observer.json").read_text())["proc_raw"]
        self.assertIn("FD|6|/dev/d5_lock", raw)
        self.assertIn("FD|7|/dev/d5_ipcc", raw)
        self.assertIn("FD|8|/dev/mem", raw)
        self.assertIn("FD|9|/dev/mem", raw)
        self.assertIn("THREAD|31|udd_ipcc_fmt_in", raw)
        self.assertIn("THREAD|32|MCB_Receiver", raw)
        self.assertIn("THREAD|33|MCB_Sender", raw)
        self.assertEqual(recorder.record.get("bridge_sha256"),
                         orchestration.PINNED_BRIDGE_SHA256)
        self.assertEqual(result.remote_readiness, "not-established")
        self.assertTrue(result.cleanup_verified)
        self.assertTrue(result.ok)

    def test_the_remote_service_is_never_reported_as_ready(self):
        result = self.run_it()
        self.assertNotEqual(result.remote_readiness, "ready")
        self.assertNotIn("ready", result.console()["remote_readiness"])

    def test_a_missing_sample_does_not_pass(self):
        # The observer never gets a chance to sample the helper.
        self.camera.progress_content = b"capture-shot: start\n"
        self.camera.hold_for_sample = False
        result = self.run_it()
        self.assertTrue(result.sdk_returned)
        self.assertFalse(result.worker_observed)
        self.assertFalse(result.ok)

    def test_the_owned_directory_is_kept_when_termination_is_unknown(self):
        self.camera.execute_failure = OSError("the connection was lost")
        self.camera.recovery_reply = "NO-OWNED-CHILD\n"
        result = self.run_it()
        self.assertFalse(result.cleanup_verified)
        self.assertEqual(result.residual_path, result.remote_dir)
        self.assertEqual(self.camera.removals(), [])
        self.assertTrue(result.uncertainty)

    def test_the_owned_directory_is_removed_when_the_helper_stopped(self):
        result = self.run_it()
        self.assertTrue(result.cleanup_verified)
        self.assertEqual(self.camera.removals(), ["rm -rf " + result.remote_dir])

    def test_a_repeated_boot_identity_is_refused_before_anything_is_staged(self):
        result = self.run_it(previous_boot=BOOT)
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.directories, [])
        self.assertEqual(self.camera.watchdog_calls(), [])
        self.assertTrue(any("fresh boot" in note for note in result.errors))

    def test_a_different_boot_identity_is_allowed(self):
        result = self.run_it(previous_boot="ffffffff-bbbb-cccc-dddd-eeeeeeeeeeee:1")
        self.assertTrue(result.sdk_returned)

    def test_the_evidence_survives_a_failed_trial(self):
        self.camera.execute_failure = OSError("the connection was lost")
        self.camera.recovery_reply = "NO-OWNED-CHILD\n"
        out = self.root / "failed-run"
        recorder = runner.Recorder(out)
        orchestration.run_init_probe(self.camera, self.cfg, recorder)
        self.assertTrue((out / "stdout.txt").exists())
        self.assertTrue((out / "stderr.txt").exists())
        self.assertTrue((out / "progress.txt").exists())
        self.assertTrue((out / "status.txt").exists())
        self.assertTrue((out / "owner.txt").exists())
        self.assertTrue((out / "run.json").exists())

    def test_the_observer_record_is_kept(self):
        out = self.root / "observer-run"
        recorder = runner.Recorder(out)
        orchestration.run_init_probe(self.camera, self.cfg, recorder)
        self.assertTrue((out / "observer.json").exists())
        record = json.loads((out / "observer.json").read_text())
        self.assertEqual(record["status"], "snapshot")

    def test_the_staged_scripts_are_the_probe_ones(self):
        self.run_it()
        staged = [path for kind, path in self.camera.log
                  if kind == "put" and path.endswith(".sh")]
        self.assertEqual(len(staged), 2)
        bodies = [self.camera.uploaded[path].decode() for path in staged
                  if path in self.camera.uploaded]
        self.assertTrue(any("--probe" in body for body in bodies))
        # The load-only command must be gone: the staged watchdog runs the
        # probe, not a load.
        self.assertFalse(any("--load-only" in body for body in bodies))


class ProbeGuardTestCase(unittest.TestCase):
    """The generated guard, run for real against a fabricated /proc."""

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.proc = self.root / "proc"
        self.proc.mkdir()
        (self.proc / "sys" / "kernel" / "random").mkdir(parents=True)
        (self.proc / "sys" / "kernel" / "random" / "boot_id").write_text(
            "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n"
        )
        (self.proc / "stat").write_text("btime 1712345678\n")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        # A systemctl that answers the way a stopped camera's unit does.
        (self.bin / "systemctl").write_text(
            "#!/bin/sh\n"
            "echo ActiveState=inactive\n"
            "echo MainPID=0\n"
        )
        (self.bin / "systemctl").chmod(0o755)

    def add_process(self, pid: int, comm: str) -> None:
        directory = self.proc / str(pid)
        directory.mkdir()
        (directory / "comm").write_text(comm + "\n")

    def guard_script(self) -> str:
        watchdog, _ = prepare.build_payloads(
            "/tmp/gear360-load-0123456789ab", BOOT
        )
        match = re.search(r"^probe_guard\(\) \{.*?^\}", watchdog, re.S | re.M)
        assert match is not None, "the generated watchdog has no probe_guard"
        return match.group(0)

    def run_guard(self) -> subprocess.CompletedProcess:
        script = (
            f"PROC_ROOT={self.proc}\n"
            f"PATH={self.bin}:$PATH\n"
            + self.guard_script()
            + "\nprobe_guard\n"
            "echo \"rc=$?\"\n"
            "echo \"reason=$GUARD_REASON\"\n"
        )
        return subprocess.run(
            ["sh", "-c", script], capture_output=True, text=True, timeout=60
        )

    def test_the_guard_refuses_when_the_stock_application_is_running(self):
        self.add_process(4321, "di-camera-app")
        result = self.run_guard()
        self.assertIn("rc=1", result.stdout)
        self.assertIn("reason=stock-seen", result.stdout)

    def test_the_guard_accepts_a_clean_proc(self):
        self.add_process(4321, "something-else")
        result = self.run_guard()
        self.assertIn("rc=0", result.stdout)
        self.assertNotIn("GUARD_REASON=stock-seen", result.stdout)

    def test_the_guard_refuses_when_the_unit_is_running(self):
        (self.bin / "systemctl").write_text(
            "#!/bin/sh\n"
            "echo ActiveState=active\n"
            "echo MainPID=1234\n"
        )
        (self.bin / "systemctl").chmod(0o755)
        result = self.run_guard()
        self.assertIn("rc=1", result.stdout)
        self.assertIn("reason=unit-state", result.stdout)



class StockAbsenceTests(unittest.TestCase):
    """The trial's preconditions: the stock application must be verifiably
    gone and the device channel free before the helper may run."""

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.helper = self.root / "capture-shot"
        self.helper.write_bytes(HELPER_BYTES)
        self.camera = FakeCamera()
        self.cfg = runner.Config(
            helper=self.helper,
            helper_sha256=HELPER_SHA,
            deadline_seconds=5,
            http_timeout=30.0,
        )

    def test_the_stop_request_is_sent_once_and_absence_is_verified(self):
        out = self.root / "run-stop"
        recorder = runner.Recorder(out)
        result = orchestration.run_init_probe(
            self.camera, self.cfg, recorder, stop_stock=True,
            stock_absence_timeout=2.0)
        stops = [arg for kind, arg in self.camera.log
                 if kind == "execute" and "systemctl --no-block stop" in arg]
        self.assertEqual(len(stops), 1, "the stop request must be sent exactly once")
        self.assertTrue(result.sdk_returned)
        self.assertTrue(result.ok, result.errors)

    def test_the_hard_stop_kills_and_resets_the_unit(self):
        out = self.root / "run-stop-hard"
        recorder = runner.Recorder(out)
        result = orchestration.run_init_probe(
            self.camera, self.cfg, recorder, stop_stock_hard=True,
            stock_absence_timeout=2.0)
        kills = [arg for kind, arg in self.camera.log
                 if kind == "execute" and "kill -9" in arg]
        self.assertEqual(len(kills), 1, "the hard kill must be sent exactly once")
        resets = [arg for kind, arg in self.camera.log
                  if kind == "execute" and "reset-failed" in arg]
        self.assertEqual(len(resets), 1, "the failed state must be reset once")
        self.assertTrue(result.sdk_returned)
        self.assertTrue(result.ok, result.errors)

    def test_a_hard_stop_never_issues_the_graceful_stop(self):
        out = self.root / "run-stop-hard-only"
        recorder = runner.Recorder(out)
        result = orchestration.run_init_probe(
            self.camera, self.cfg, recorder, stop_stock_hard=True,
            stock_absence_timeout=2.0)
        stops = [arg for kind, arg in self.camera.log
                 if kind == "execute" and "systemctl --no-block stop" in arg]
        self.assertEqual(stops, [], "the hard variant must not send the graceful stop")
        self.assertTrue(result.ok, result.errors)

    def test_a_competing_descriptor_holder_refuses_the_start(self):
        # The unit is stopped, but a process still holds the device channel.
        self.camera.d5_holders = [("999", "3", "/dev/d5_ipcc", "someone-else")]
        out = self.root / "run-refuse"
        recorder = runner.Recorder(out)
        result = orchestration.run_init_probe(
            self.camera, self.cfg, recorder, stop_stock=True,
            stock_absence_timeout=2.0)
        self.assertFalse(result.ok)
        self.assertFalse(result.sdk_returned, "the helper must not run")
        self.assertTrue(any("competing" in note for note in result.errors),
                        result.errors)

    def test_a_running_stock_application_refuses_the_start(self):
        # The stop request is not sent, so the application is still there.
        self.camera.app_present = True
        self.camera.unit_active = True
        out = self.root / "run-still-running"
        recorder = runner.Recorder(out)
        original = orchestration.stop_stock_application
        try:
            orchestration.stop_stock_application = (
                lambda *args, **kwargs: {"stop_sent": False, "error": "refused"})
            result = orchestration.run_init_probe(
                self.camera, self.cfg, recorder, stop_stock=True,
                stock_absence_timeout=2.0)
        finally:
            orchestration.stop_stock_application = original
        self.assertFalse(result.sdk_returned)
        self.assertTrue(any("rejected or uncertain" in note for note in result.errors),
                        result.errors)

    def test_an_already_stopped_application_is_tolerated_and_recorded(self):
        # There is no stop target to pin, so the live identity does not
        # apply - but the trial must say so, and it must still run.
        self.camera.app_present = False
        self.camera.unit_active = False
        out = self.root / "run-already-stopped"
        recorder = runner.Recorder(out)
        result = orchestration.run_init_probe(
            self.camera, self.cfg, recorder, stop_stock=True,
            stock_absence_timeout=2.0)
        self.assertTrue(result.sdk_returned)
        guards = recorder.record.get("guards_before") or {}
        self.assertIsNone(guards.get("app_pid"))
        self.assertIn("already stopped", guards.get("app_identity_note", ""))
        stops = [arg for kind, arg in self.camera.log
                 if kind == "execute" and "systemctl --no-block stop" in arg]
        self.assertEqual(stops, [], "no stop is sent when there is nothing to stop")

    def test_without_the_flag_the_stock_application_is_never_stopped(self):
        out = self.root / "run-nostop"
        recorder = runner.Recorder(out)
        started = time.monotonic()
        result = orchestration.run_init_probe(self.camera, self.cfg, recorder)
        elapsed = time.monotonic() - started
        stops = [arg for kind, arg in self.camera.log
                 if kind == "execute" and "systemctl --no-block stop" in arg]
        self.assertEqual(stops, [], "the stop must only happen when asked for")
        self.assertFalse(result.sdk_returned)
        self.assertEqual(self.camera.directories, [], "refusal must happen before staging")
        self.assertLess(elapsed, 5.0, "early refusal must not wait for stock-app exit")

    def test_no_stop_flag_allows_a_previously_absent_stock_app(self):
        self.camera.app_present = False
        self.camera.unit_active = False
        out = self.root / "run-absent-no-stop"
        result = orchestration.run_init_probe(
            self.camera, self.cfg, runner.Recorder(out))
        self.assertTrue(result.sdk_returned)

    def test_missing_mainpid_refuses_stock_absence(self):
        self.camera.app_present = False
        self.camera.unit_active = False
        self.camera.unit_mainpid_missing = True
        out = self.root / "run-missing-mainpid"
        result = orchestration.run_init_probe(
            self.camera, self.cfg, runner.Recorder(out))
        self.assertFalse(result.sdk_returned)
        self.assertEqual(self.camera.directories, [])

    def test_truncated_stock_process_scan_refuses(self):
        self.camera.app_present = False
        self.camera.unit_active = False
        self.camera.app_scan_complete = False
        original = self.camera.execute
        def truncated(command, timeout=None):
            text = original(command, timeout)
            if 'printf "A|' in command:
                return text.replace("APPS_END", "")
            return text
        self.camera.execute = truncated
        with self.assertRaises(orchestration.runner_module().GuardError):
            orchestration.stock_application_processes(self.camera)

    def test_truncated_device_scan_refuses(self):
        self.camera.holder_scan_complete = False
        with self.assertRaises(orchestration.runner_module().GuardError):
            orchestration.scan_d5_holders(self.camera)

    def test_unreadable_live_descriptor_refuses_even_with_scan_marker(self):
        self.camera.holder_scan_error = True
        with self.assertRaises(orchestration.runner_module().GuardError):
            orchestration.scan_d5_holders(self.camera)

    def test_unreadable_live_process_refuses_even_with_scan_marker(self):
        self.camera.app_scan_error = True
        with self.assertRaises(orchestration.runner_module().GuardError):
            orchestration.stock_application_processes(self.camera)

    def test_stopped_and_zombie_processes_do_not_count_as_absent(self):
        self.camera.app_present = True
        self.camera.unit_active = False
        for state in ("T", "Z"):
            self.camera.stock_state = state
            record = orchestration.verify_stock_absent(
                self.camera, runner.Recorder(self.root / f"run-{state}"),
                timeout=0.0, interval=0.0)
            self.assertFalse(record["absent"])

    def test_process_snapshot_alone_does_not_satisfy_local_transport(self):
        out = self.root / "run-process-only"
        recorder = runner.Recorder(out)
        self.camera.app_present = False
        self.camera.unit_active = False
        self.camera.snapshot_process_only = True
        result = orchestration.run_init_probe(
            self.camera, self.cfg, recorder)
        self.assertTrue(result.worker_observed)
        self.assertFalse(result.local_transport_observed)
        self.assertIn("fd:d5_lock", result.local_transport_missing)
        self.assertFalse(result.ok)

    def test_udd_fd_cannot_substitute_for_missing_udd_thread(self):
        sample = snapshot_text().replace(
            "THREAD|31|udd_ipcc_fmt_in|futex_wait\n", "")
        sample = sample.replace("FD|9|/dev/mem\n", "FD|9|/dev/mem\nFD|10|/dev/udd_ipcc_fmt_in\n")
        observed, missing = orchestration.local_transport_items(sample)
        self.assertFalse(observed)
        self.assertEqual(missing, ["thread:udd_ipcc_fmt_in"])

    def test_local_transport_requires_a_successful_guarded_snapshot(self):
        self.camera.app_present = False
        self.camera.unit_active = False
        self.camera.snapshot_tick_mismatch = True
        result = orchestration.run_init_probe(
            self.camera, self.cfg, runner.Recorder(self.root / "run-bad-tick"))
        self.assertFalse(result.worker_observed)
        self.assertFalse(result.local_transport_observed)

    def test_bridge_mismatch_refuses_before_staging(self):
        self.camera.app_present = False
        self.camera.unit_active = False
        self.camera.hashes[orchestration.PINNED_BRIDGE] = "0" * 64
        out = self.root / "run-bridge-before-stage"
        result = orchestration.run_init_probe(
            self.camera, self.cfg, runner.Recorder(out))
        self.assertFalse(result.sdk_returned)
        self.assertEqual(self.camera.directories, [])

    def test_bridge_change_after_staging_refuses_before_probe_execute(self):
        self.camera.app_present = False
        self.camera.unit_active = False
        self.camera.change_bridge_on_watchdog_put = True
        out = self.root / "run-bridge-before-execute"
        result = orchestration.run_init_probe(
            self.camera, self.cfg, runner.Recorder(out))
        self.assertFalse(result.sdk_returned)
        self.assertEqual(self.camera.watchdog_calls(), [])
        self.assertTrue(any("changed or mismatched before execution" in error
                            for error in result.errors), result.errors)

    def test_no_stop_final_boot_or_library_change_is_detected_and_preserved(self):
        for changed in ("boot", "library"):
            with self.subTest(changed=changed):
                camera = FakeCamera()
                camera.app_present = False
                camera.unit_active = False
                if changed == "boot":
                    camera.boot_after = "ffffffff-bbbb-cccc-dddd-eeeeeeeeeeee\n1712345678"
                else:
                    camera.change_library_on_watchdog_run = True
                out = self.root / f"run-final-{changed}"
                result = orchestration.run_init_probe(
                    camera, self.cfg, runner.Recorder(out))
                self.assertFalse(result.ok)
                self.assertTrue(result.residual_path)
                self.assertEqual(camera.removals(), [])
                self.assertTrue(any("changed after execution" in error
                                    or "identity check failed" in error
                                    for error in result.errors + result.uncertainty))

    def test_cli_rejects_nonfixed_probe_deadline(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                orchestration.main([
                    "--helper", "unused", "--helper-sha256", HELPER_SHA,
                    "--output", "unused-output", "--deadline-seconds", "44",
                ])
        self.assertEqual(raised.exception.code, 2)

if __name__ == "__main__":
    unittest.main()
