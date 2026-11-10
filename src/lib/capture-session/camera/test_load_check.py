#!/usr/bin/env python3
"""Offline tests for the load-only runner.

No camera and no network are used.  Two kinds of test live here:

* runner tests, which drive the runner through a fake camera that speaks
  the same five operations as the real one;
* shell tests, which run the *generated* shell - the watchdog and the
  recovery, as they are really sent to the camera - against a fabricated
  /proc and a real dummy process, so the identity guard and the signals
  are exercised and not merely grepped for.

The shell tests are the ones that matter for the failure paths: a guard
that is present but wrong still kills the wrong thing, and only running it
shows that.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_load_check as runner  # noqa: E402


HELPER_BYTES = b"capture-shot-binary-for-the-tests\n"
HELPER_SHA = hashlib.sha256(HELPER_BYTES).hexdigest()


# --- the fake camera -------------------------------------------------------


class FakeCamera(runner.Camera):
    """A camera that answers with what a test tells it to answer.

    It does not model the camera; it models the answers the runner reads:
    the application identity, the boot identity, the digests, and the files
    the helper leaves behind.

    It also models the limit that made the camera qualification fail: the
    execute endpoint takes its command in the URL of a GET, and a command
    over `MAX_COMMAND` characters does not get through.  Anything longer
    is refused here, so a runner that sends a script body in a URL fails
    the tests instead of failing on the camera.
    """

    #: The bound the fake camera imposes on an execute command.
    MAX_COMMAND = 1000

    def __init__(self) -> None:
        self.commands: list[str] = []
        self.files: dict[str, bytes] = {}
        # What the camera holds for each uploaded file, kept after the
        # cleanup has removed the directory.
        self.uploaded: dict[str, bytes] = {}
        self.directories: list[str] = []
        self.apps_before = [("4321", "98765")]
        self.apps_after: list[tuple[str, str]] | None = None
        self.boot_before = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n1712345678"
        self.boot_after: str | None = None
        self.hashes: dict[str, str] = {
            runner.PINNED_APP: runner.PINNED_APP_SHA256,
            runner.PINNED_LIBRARY: runner.PINNED_LIBRARY_SHA256,
        }
        for path, digest in runner.PINNED_DEPENDENCIES.items():
            self.hashes[path] = digest
        # Paths the camera does not hold: the digest read finds no file.
        self.absent_paths: set[str] = set()
        # Paths whose digest cannot be read at all: the read fails.
        self.unreadable_paths: set[str] = set()
        # Paths whose digest changes after they have been read once: the
        # first read (the initial guard) is the pinned value, every later
        # read is the changed one.
        self.changed_after_first_read: dict[str, str] = {}
        # Digests the camera reports once the helper has run: the
        # after-execution guard sees these.
        self.hashes_after: dict[str, str] = {}
        self.hash_reads: dict[str, int] = {}
        self.corrupt_uploaded_bytes = False
        self.corrupt_uploaded_hash = False
        self.corrupt_script_bytes = False
        self.status_content = "outcome=rc:0\nrc=0\nterminated=yes\n"
        self.stdout_content = b""
        self.stderr_content = b"capture-shot: the capture library was loaded\n"
        self.progress_content = b"capture-shot: start\n"
        self.remove_works = True
        self.recovery_reply = "OWNED-KILLED-VERIFIED\nNO-STATUS\n"
        self.execute_failure: BaseException | None = None
        self.helper_ran = False
        # failures that can be injected at the staging steps
        self.mkdir_failure: str | None = None
        self.put_failure: BaseException | None = None
        self.fetch_failure: BaseException | None = None
        self.chmod_failure: BaseException | None = None

    # -- helpers used by the tests ----------------------------------------

    def _apps(self) -> list[tuple[str, str]]:
        if self.helper_ran and self.apps_after is not None:
            return self.apps_after
        return self.apps_before

    def _boot(self) -> str:
        if self.helper_ran and self.boot_after is not None:
            return self.boot_after
        return self.boot_before

    def executed_scripts(self) -> list[str]:
        return [c for c in self.commands
                if c.startswith("/bin/sh ") and runner.WATCHDOG_NAME in c]

    def recovery_commands(self) -> list[str]:
        return [c for c in self.commands
                if c.startswith("/bin/sh ") and runner.RECOVERY_NAME in c]

    def removals(self) -> list[str]:
        return [c for c in self.commands if c.startswith("rm -rf")]

    def longest_command(self) -> int:
        return max((len(c) for c in self.commands), default=0)

    # -- the camera interface ----------------------------------------------

    def execute(self, command: str, timeout: float | None = None) -> str:
        self.commands.append(command)
        if len(command) > self.MAX_COMMAND:
            # The camera's CGI endpoint cannot carry a command this long.
            raise RuntimeError(
                f"the camera refused a command of {len(command)} characters; "
                f"the limit is {self.MAX_COMMAND}"
            )
        if runner.RECOVERY_NAME in command and command.startswith("/bin/sh "):
            return self.recovery_reply
        if runner.WATCHDOG_NAME in command and command.startswith("/bin/sh "):
            if self.execute_failure is not None:
                raise self.execute_failure
            self.helper_ran = True
            match = re.search(r"/bin/sh '?([^'\s]+)'?", command)
            directory = match.group(1).rsplit("/", 1)[0] if match else ""
            self.files[f"{directory}/stdout"] = self.stdout_content
            self.files[f"{directory}/stderr"] = self.stderr_content
            self.files[f"{directory}/progress"] = self.progress_content
            self.files[f"{directory}/status"] = self.status_content.encode()
            self.files[f"{directory}/owner"] = b"pid=1234\ntick=42\n"
            return ""
        if "/proc/[0-9]*" in command:
            return "".join(f"APP {pid} {tick}\n" for pid, tick in self._apps())
        if "btime" in command:
            return self._boot() + "\n"
        if "openssl dgst" in command:
            match = re.search(r"openssl dgst -sha256 '([^']+)'", command)
            path = match.group(1) if match else ""
            self.hash_reads[path] = self.hash_reads.get(path, 0) + 1
            reads = self.hash_reads[path]
            if path in self.absent_paths:
                # The file is not there: the camera reports no digest.
                return f"openssl: could not read {path}: No such file or directory\n"
            if path in self.unreadable_paths:
                # The read itself fails; the digest is unknown.
                raise OSError("the connection was lost while the digest was read")
            if path in self.changed_after_first_read and reads > 1:
                return f"SHA256({path})= {self.changed_after_first_read[path]}\n"
            if self.helper_ran and path in self.hashes_after:
                return f"SHA256({path})= {self.hashes_after[path]}\n"
            if path in self.hashes:
                return f"SHA256({path})= {self.hashes[path]}\n"
            if self.corrupt_uploaded_hash and path.endswith("/capture-shot"):
                return f"SHA256({path})= {'f' * 64}\n"
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
            if self.chmod_failure is not None:
                raise self.chmod_failure
            return ""
        return ""

    def remote_sha256(self, remote_path: str) -> str:
        text = self.execute(f"/usr/bin/openssl dgst -sha256 '{remote_path}'")
        match = re.search(r"\b([0-9a-f]{64})\b", text)
        if not match:
            raise runner.GuardError(f"no digest for {remote_path}")
        return match.group(1)

    def mkdir(self, remote_dir: str) -> None:
        if self.mkdir_failure == "refused":
            raise runner.CameraRefused(f"the camera refused to create {remote_dir}")
        if self.mkdir_failure == "transport":
            raise OSError("the connection was lost")
        self.directories.append(remote_dir)

    def put(self, remote_path: str, data: bytes) -> None:
        if self.put_failure is not None:
            raise self.put_failure
        if self.corrupt_uploaded_bytes:
            data = data[:-1]
        if self.corrupt_script_bytes and remote_path.endswith(".sh"):
            # The camera stores something other than what was sent.
            data = data.replace(b"own_child", b"own_ch1ld")
        self.files[remote_path] = data
        self.uploaded[remote_path] = data

    def get(self, remote_path: str) -> bytes:
        if remote_path.endswith("/capture-shot") and self.fetch_failure is not None:
            raise self.fetch_failure
        if remote_path not in self.files:
            raise OSError(f"no such file: {remote_path}")
        return self.files[remote_path]


class RunnerTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.helper = self.root / "capture-shot"
        self.helper.write_bytes(HELPER_BYTES)
        self.out_dir = self.root / "record"
        self.camera = FakeCamera()
        self.cfg = runner.Config(
            helper=self.helper,
            helper_sha256=HELPER_SHA,
            deadline_seconds=5,
            http_timeout=30.0,
        )

    def run_it(self):
        recorder = runner.Recorder(self.out_dir)
        result = runner.run_load_check(self.camera, self.cfg, recorder)
        return result, recorder

    def staged_script(self, name: str) -> bytes:
        """The bytes the camera holds for a staged script."""
        for path, data in self.camera.uploaded.items():
            if path.endswith("/" + name):
                return data
        raise AssertionError(f"{name} was never staged")

    def assert_helper_never_ran(self) -> None:
        self.assertEqual(
            self.camera.executed_scripts(),
            [],
            "the helper must not be executed when a guard refuses",
        )


# --- the shell harness -----------------------------------------------------


class FakeProc:
    """A fabricated /proc tree for the generated shell to read.

    The tree is what the camera's /proc would show for a process: its
    state and start tick in `stat`, its command line in `cmdline`, and the
    image it is running in `exe`.
    """

    def __init__(self, root: Path) -> None:
        self.root = root
        self.root.mkdir(parents=True, exist_ok=True)
        (self.root / "uptime").write_text("1000.00 5000.00\n")

    def add(self, pid: int, tick: str, path: str, state: str = "S",
            empty_tick: bool = False) -> Path:
        entry = self.root / str(pid)
        entry.mkdir(parents=True, exist_ok=True)
        fields = ["1", "(helper)", state] + ["0"] * 18 + [str(tick)]
        if empty_tick:
            # The state can be read but the start tick cannot: the file has
            # one field less, so field 22 is not there.
            fields = fields[:-1]
        (entry / "stat").write_text(" ".join(fields) + "\n")
        (entry / "cmdline").write_bytes(path.encode() + b"\0--load-only\0")
        link = entry / "exe"
        if not link.exists():
            os.symlink(path, link)
        return entry

    def set_image(self, pid: int, path: str) -> None:
        """The process at the pid runs another image."""
        entry = self.root / str(pid)
        (entry / "cmdline").write_bytes(path.encode() + b"\0--load-only\0")
        link = entry / "exe"
        if link.exists():
            link.unlink()
        os.symlink(path, link)

    def break_stat(self, pid: int) -> None:
        """The stat cannot be read: the file is gone."""
        (self.root / str(pid) / "stat").unlink()

    def make_stat_unreadable(self, pid: int) -> None:
        """The stat cannot be read: the path is not a file."""
        entry = self.root / str(pid) / "stat"
        if entry.exists():
            entry.unlink()
        entry.mkdir()

    def become_zombie(self, pid: int) -> None:
        """What /proc shows for a process that has exited: state Z, no
        command line, no readable image."""
        entry = self.root / str(pid)
        fields = ["1", "(helper)", "Z"] + ["0"] * 18 + ["0"]
        (entry / "stat").write_text(" ".join(fields) + "\n")
        (entry / "cmdline").write_bytes(b"")
        link = entry / "exe"
        if link.exists():
            link.unlink()

    def change_tick(self, pid: int, tick: str) -> None:
        entry = self.root / str(pid)
        fields = ["1", "(helper)", "S"] + ["0"] * 18 + [str(tick)]
        (entry / "stat").write_text(" ".join(fields) + "\n")


def start_dummy() -> subprocess.Popen:
    """A real process for the generated shell to signal."""
    dummy = subprocess.Popen(["/bin/sleep", "300"])
    return dummy


def alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


class ZombieWatcher(threading.Thread):
    """Keeps the fabricated /proc honest: when the dummy dies, the tree
    shows it as a zombie, the way the kernel would."""

    def __init__(self, proc: FakeProc, pid: int) -> None:
        super().__init__(daemon=True)
        self.proc = proc
        self.pid = pid
        self.stop = threading.Event()

    def run(self) -> None:
        while not self.stop.is_set():
            gone = False
            try:
                done, _ = os.waitpid(self.pid, os.WNOHANG)
                gone = done == self.pid
            except ChildProcessError:
                # Not a child of this process: it is another shell's child.
                gone = not alive(self.pid)
            if gone:
                self.proc.become_zombie(self.pid)
                return
            time.sleep(0.02)


def run_shell(script: str, env: dict[str, str], timeout: float = 60.0):
    environment = dict(os.environ)
    environment.update(env)
    return subprocess.run(
        ["sh", "-c", script],
        capture_output=True,
        text=True,
        env=environment,
        timeout=timeout,
    )


class ShellTestCase(unittest.TestCase):
    """Tests that run the generated shell for real."""

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.proc = FakeProc(self.root / "proc")
        self.remote_dir = self.root / "gear360-load-0123456789ab"
        self.remote_dir.mkdir()
        self.helper_path = self.remote_dir / "capture-shot"
        self.helper_path.write_text("#!/bin/sh\nsleep 300\n")
        self.helper_path.chmod(0o755)
        self.dummy = None
        self.addCleanup(self.stop_dummy)

    def stop_dummy(self) -> None:
        if self.dummy is not None and self.dummy.poll() is None:
            self.dummy.kill()
            self.dummy.wait()

    def shell_env(self) -> dict[str, str]:
        return {"PROC_ROOT": str(self.proc.root)}

    def script_path(self, text: str, name: str = "generated.sh") -> Path:
        """Write a generated script to a file, as staging does."""
        path = self.root / name
        path.write_text(text)
        path.chmod(0o755)
        return path

    def run_generated(self, text: str, name: str = "generated.sh") -> subprocess.CompletedProcess:
        """Run a generated script the way the camera will: /bin/sh <file>.

        The script is not handed to the shell as a command string: on the
        camera it is staged over FTP and then run by name, and the test
        follows that path.
        """
        path = self.script_path(text, name)
        return run_shell(f"/bin/sh {shlex.quote(str(path))}", self.shell_env())

    def popen_generated(self, text: str, name: str = "generated.sh") -> subprocess.Popen:
        path = self.script_path(text, name)
        return self.popen_shell(f"/bin/sh {shlex.quote(str(path))}")

    def publish_helper(self) -> Path:
        """A helper that publishes the pid it was started with."""
        pidfile = self.root / "helper.pid"
        self.helper_path.write_text(f"#!/bin/sh\necho $$ > {pidfile}\nsleep 300\n")
        self.helper_path.chmod(0o755)
        return pidfile

    def wait_for_helper(self, pidfile: Path, tick: str = "4242") -> int:
        """Wait for the helper to publish its pid and put it in the tree."""
        deadline = time.time() + 10
        while time.time() < deadline and not pidfile.exists():
            time.sleep(0.02)
        self.assertTrue(pidfile.exists(), "the helper did not start")
        pid = int(pidfile.read_text().strip())
        self.proc.add(pid, tick, str(self.helper_path))
        self.addCleanup(lambda: os.kill(pid, 9) if alive(pid) else None)
        return pid

    def popen_shell(self, script: str) -> subprocess.Popen:
        environment = dict(os.environ)
        environment.update(self.shell_env())
        process = subprocess.Popen(
            ["sh", "-c", script],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        # The pipes belong to this test: they are closed on every path out
        # of the test, including the one where an assertion fails.
        self.addCleanup(self.close_process, process)
        return process

    def wait_for_file(self, path: Path, timeout: float = 40.0) -> str:
        deadline = time.time() + timeout
        while time.time() < deadline and not path.exists():
            time.sleep(0.05)
        self.assertTrue(path.exists(), f"{path} was never written")
        return path.read_text()

    def wait_for_owner(self, timeout: float = 40.0) -> str:
        """The owner file is written in two steps, so wait for both."""
        path = self.remote_dir / "owner"
        deadline = time.time() + timeout
        while time.time() < deadline:
            if path.exists():
                text = path.read_text()
                if "pid=" in text and "tick=" in text:
                    return text
            time.sleep(0.05)
        self.fail("the owner file was never complete")
        return ""

    def close_process(self, process: subprocess.Popen) -> None:
        """End an owned subprocess and close its pipes.

        Called from a cleanup, so it runs on every path out of a test,
        including the one where an assertion fails.
        """
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        for stream in (process.stdout, process.stderr):
            if stream is not None:
                stream.close()


# --- the runner ------------------------------------------------------------


class GuardRefusalTests(RunnerTestCase):
    def test_local_helper_hash_mismatch_never_touches_the_camera(self):
        self.cfg.helper_sha256 = "0" * 64
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertIn("helper sha256", result.errors[0])
        self.assertEqual(self.camera.commands, [])

    def test_ambiguous_app_identity_refuses_before_the_helper_runs(self):
        self.camera.apps_before = [("111", "1"), ("222", "2")]
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertIn("exactly one", result.errors[0])
        self.assert_helper_never_ran()
        self.assertEqual(self.camera.directories, [], "nothing may be staged")

    def test_wrong_pinned_library_hash_refuses_before_the_helper_runs(self):
        self.camera.hashes[runner.PINNED_LIBRARY] = "1" * 64
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertIn(runner.PINNED_LIBRARY, result.errors[0])
        self.assert_helper_never_ran()

    def test_wrong_pinned_app_hash_refuses_before_the_helper_runs(self):
        self.camera.hashes[runner.PINNED_APP] = "2" * 64
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertIn(runner.PINNED_APP, result.errors[0])
        self.assert_helper_never_ran()

    def test_uploaded_bytes_mismatch_stops_before_the_helper_runs(self):
        self.camera.corrupt_uploaded_bytes = True
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertIn("bytes", result.errors[0])
        self.assert_helper_never_ran()

    def test_uploaded_hash_mismatch_stops_before_the_helper_runs(self):
        self.camera.corrupt_uploaded_hash = True
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertIn("sha256", result.errors[0])
        self.assert_helper_never_ran()

    def test_cleanup_refuses_a_path_it_did_not_generate(self):
        camera = FakeCamera()
        with self.assertRaises(runner.LoadCheckError):
            runner.cleanup_remote(
                camera, "/tmp/somebody-elses-directory", runner.Recorder(self.root / "r")
            )
        self.assertEqual(camera.removals(), [])


class DependencyGuardTests(RunnerTestCase):
    """The pinned native dependencies are guarded like the other pins.

    Each dependency is checked before anything is staged, immediately
    before the helper is executed and again afterwards.  A dependency that
    is missing, that changed or whose digest cannot be read refuses the
    run before the helper could be executed.
    """

    def run_with_camera(self, camera: FakeCamera, name: str):
        """Run the runner against another camera, in its own record."""
        recorder = runner.Recorder(self.root / name)
        result = runner.run_load_check(camera, self.cfg, recorder)
        return result, recorder

    def fresh_camera(self) -> FakeCamera:
        return FakeCamera()

    def record_name(self, prefix: str, path: str) -> str:
        return f"{prefix}-{path.rsplit('/', 1)[-1]}"

    def assert_helper_never_ran_on(self, camera: FakeCamera) -> None:
        self.assertEqual(
            camera.executed_scripts(),
            [],
            "the helper must not be executed when a guard refuses",
        )

    def assert_nothing_was_staged(self, camera: FakeCamera) -> None:
        self.assertEqual(camera.directories, [],
                         "no directory may be created when a pin refuses")
        self.assertEqual(camera.uploaded, {},
                         "nothing may be uploaded when a pin refuses")
        self.assertEqual(camera.removals(), [],
                         "there is nothing to remove when nothing was staged")

    def assert_staging_happened(self, camera: FakeCamera) -> None:
        """The helper and both scripts were put on the camera."""
        staged = set(camera.uploaded)
        self.assertTrue(any(p.endswith("/" + runner.HELPER_NAME)
                            for p in staged),
                        f"the helper was not staged: {sorted(staged)}")
        self.assertTrue(any(p.endswith("/" + runner.WATCHDOG_NAME)
                            for p in staged),
                        f"the watchdog was not staged: {sorted(staged)}")
        self.assertTrue(any(p.endswith("/" + runner.RECOVERY_NAME)
                            for p in staged),
                        f"the recovery was not staged: {sorted(staged)}")

    def test_matching_dependency_pins_let_the_run_proceed(self):
        result, _ = self.run_it()
        self.assertTrue(result.ok, result.errors)
        self.assertTrue(self.camera.helper_ran)
        record = json.loads((self.out_dir / "run.json").read_text())
        self.assertEqual(record["guards_before"]["dependency_sha256"],
                         dict(runner.PINNED_DEPENDENCIES))
        self.assertEqual(record["dependency_sha256"],
                         dict(runner.PINNED_DEPENDENCIES))
        for path in runner.PINNED_DEPENDENCIES:
            # the initial guard and both later guard passes read it
            self.assertGreaterEqual(self.camera.hash_reads.get(path, 0), 3)

    def test_a_wrong_dependency_hash_refuses_before_anything_is_staged(self):
        for path in runner.PINNED_DEPENDENCIES:
            with self.subTest(dependency=path):
                camera = self.fresh_camera()
                camera.hashes[path] = "1" * 64
                result, _ = self.run_with_camera(
                    camera, self.record_name("mismatch", path))
                self.assertFalse(result.ok)
                self.assertIn(path, result.errors[0])
                self.assertIn("sha256", result.errors[0])
                self.assertFalse(camera.helper_ran)
                self.assert_helper_never_ran_on(camera)
                self.assert_nothing_was_staged(camera)

    def test_a_missing_dependency_hash_refuses_before_anything_is_staged(self):
        for path in runner.PINNED_DEPENDENCIES:
            with self.subTest(dependency=path):
                camera = self.fresh_camera()
                camera.absent_paths.add(path)
                result, _ = self.run_with_camera(
                    camera, self.record_name("absent", path))
                self.assertFalse(result.ok)
                self.assertIn(path, result.errors[0])
                self.assertFalse(camera.helper_ran)
                self.assert_helper_never_ran_on(camera)
                self.assert_nothing_was_staged(camera)

    def test_an_unreadable_dependency_hash_refuses_before_anything_is_staged(self):
        for path in runner.PINNED_DEPENDENCIES:
            with self.subTest(dependency=path):
                camera = self.fresh_camera()
                camera.unreadable_paths.add(path)
                result, _ = self.run_with_camera(
                    camera, self.record_name("unreadable", path))
                self.assertFalse(result.ok)
                self.assertIn(path, result.errors[0])
                self.assertFalse(camera.helper_ran)
                self.assert_helper_never_ran_on(camera)
                self.assert_nothing_was_staged(camera)

    def test_a_dependency_that_changes_before_the_execution_stops_the_run(self):
        for path in runner.PINNED_DEPENDENCIES:
            with self.subTest(dependency=path):
                camera = self.fresh_camera()
                camera.changed_after_first_read[path] = "3" * 64
                result, _ = self.run_with_camera(
                    camera, self.record_name("pre", path))
                self.assertFalse(result.ok)
                self.assertIn(path, result.errors[0])
                self.assertIn("before execution", result.errors[0])
                # the helper must not run, but the staging had happened
                self.assertFalse(camera.helper_ran)
                self.assert_helper_never_ran_on(camera)
                self.assert_staging_happened(camera)
                # and the staged directory is still cleaned up
                self.assertEqual(len(camera.removals()), 1)
                self.assertTrue(result.cleanup_verified)

    def test_a_dependency_that_changes_after_the_execution_is_reported(self):
        for path in runner.PINNED_DEPENDENCIES:
            with self.subTest(dependency=path):
                camera = self.fresh_camera()
                camera.hashes_after[path] = "4" * 64
                result, _ = self.run_with_camera(
                    camera, self.record_name("post", path))
                self.assertFalse(result.ok)
                self.assertTrue(any(path in e for e in result.errors),
                                result.errors)
                self.assertTrue(result.uncertainty, result.errors)
                # the helper did run: the change is found after the fact
                self.assertTrue(camera.helper_ran)
                self.assertEqual(len(camera.executed_scripts()), 1)
                self.assert_staging_happened(camera)
                # the run's own child was proved gone, so the directory goes
                self.assertEqual(len(camera.removals()), 1)
                self.assertTrue(result.cleanup_verified)



class SuccessfulRunTests(RunnerTestCase):
    def test_the_run_sends_only_the_fixed_load_only_argv(self):
        result, recorder = self.run_it()
        self.assertTrue(result.ok, result.errors)
        self.assertEqual(result.outcome, "rc:0")
        self.assertEqual(result.rc, 0)
        self.assertTrue(result.cleanup_verified)
        # The argv is in the staged script, not in the command.
        script = self.staged_script(runner.WATCHDOG_NAME).decode()
        self.assertIn("--load-only --library \"$library_path\" --log \"$progress_path\"",
                      script)
        self.assertIn("library_path=/usr/lib/libcapture-fw-prod.so", script)
        for forbidden in ("--probe", "--jpeg", "--raw", "--iso", "--shutter",
                          "--timeout", "--command", "--capture-mode",
                          "--remaining", "--free", "--verbose"):
            self.assertNotIn(forbidden, script)

    def test_the_log_stays_inside_the_generated_directory(self):
        result, _ = self.run_it()
        self.assertTrue(result.ok, result.errors)
        script = self.staged_script(runner.WATCHDOG_NAME).decode()
        match = re.search(r"progress_path='?([^'\s]+)'?", script)
        self.assertIsNotNone(match)
        self.assertTrue(match.group(1).startswith(result.remote_dir + "/"))
        self.assertNotIn("/opt", script)

    def test_the_record_holds_the_outputs_and_the_command(self):
        result, recorder = self.run_it()
        self.assertTrue(result.ok, result.errors)
        record = json.loads((self.out_dir / "run.json").read_text())
        self.assertIn(runner.WATCHDOG_NAME, record["remote_command"])
        self.assertEqual((self.out_dir / "progress.txt").read_bytes(),
                         self.camera.progress_content)
        self.assertEqual((self.out_dir / "stderr.txt").read_bytes(),
                         self.camera.stderr_content)
        self.assertIn("guards_before", record)
        self.assertIn("boot_identity", record["guards_before"])

    def test_the_console_summary_carries_no_boot_identity(self):
        result, _ = self.run_it()
        summary = json.dumps(result.console())
        self.assertNotIn("boot", summary.lower())
        self.assertNotIn("root", summary.lower())


class TransportBoundaryTests(RunnerTestCase):
    """The camera's execute endpoint takes its command in a URL.

    A script of this size does not fit in a URL, so the scripts go over
    FTP and the endpoint is only asked to run one short fixed command.
    """

    def test_the_success_workflow_passes_a_transport_that_refuses_long_commands(self):
        result, _ = self.run_it()
        self.assertTrue(result.ok, result.errors)
        self.assertLessEqual(self.camera.longest_command(), self.camera.MAX_COMMAND)
        self.assertEqual(self.camera.executed_scripts(),
                         [f"/bin/sh {result.remote_dir}/{runner.WATCHDOG_NAME}"])

    def test_the_recovery_workflow_passes_the_same_transport(self):
        self.camera.execute_failure = socket.timeout("timed out")
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertLessEqual(self.camera.longest_command(), self.camera.MAX_COMMAND)
        self.assertEqual(
            self.camera.recovery_commands(),
            [f"/bin/sh {result.remote_dir}/{runner.RECOVERY_NAME}"],
        )

    def test_the_scripts_are_staged_with_their_exact_contents(self):
        result, _ = self.run_it()
        self.assertTrue(result.ok, result.errors)
        builders = {
            runner.WATCHDOG_NAME: runner.build_watchdog_script(
                self.cfg, result.remote_dir),
            runner.RECOVERY_NAME: runner.build_recovery_script(result.remote_dir),
        }
        for name, generated in builders.items():
            staged = self.staged_script(name)
            self.assertGreater(len(staged), 1000,
                               "the staged script is bigger than the URL limit, "
                               "which is why it is staged and not sent")
            self.assertEqual(staged.decode(), generated)

    def test_the_record_keeps_the_script_bytes_and_their_hashes(self):
        result, _ = self.run_it()
        self.assertTrue(result.ok, result.errors)
        record = json.loads((self.out_dir / "run.json").read_text())
        for name in (runner.WATCHDOG_NAME, runner.RECOVERY_NAME):
            on_disk = (self.out_dir / name).read_bytes()
            self.assertEqual(hashlib.sha256(on_disk).hexdigest(),
                             record["staged_scripts"][name])
            self.assertEqual(on_disk, self.staged_script(name))

    def test_a_script_that_does_not_read_back_is_not_run(self):
        self.camera.corrupt_script_bytes = True
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.executed_scripts(), [],
                         "a script that failed verification must not be run")
        self.assertTrue(any("watchdog.sh" in e for e in result.errors), result.errors)
        # the directory was created, so it is still cleaned up
        self.assertEqual(len(self.camera.removals()), 1)

    def test_a_failed_script_digest_is_not_run(self):
        self.camera.corrupt_uploaded_hash = True
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.executed_scripts(), [])
        self.assertEqual(len(self.camera.removals()), 1)

    def test_no_script_body_ever_goes_into_a_command(self):
        result, _ = self.run_it()
        self.assertTrue(result.ok, result.errors)
        for command in self.camera.commands:
            self.assertNotIn("own_child", command)
            self.assertNotIn("--load-only", command)
            self.assertLessEqual(len(command), self.camera.MAX_COMMAND)


class HelperFailureTests(RunnerTestCase):
    def test_a_nonzero_helper_exit_is_a_failure(self):
        self.camera.status_content = "outcome=rc:1\nrc=1\nterminated=yes\n"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(result.rc, 1)
        self.assertTrue(any("rc=1" in e for e in result.errors))
        self.assertTrue(result.cleanup_verified)

    def test_the_deadline_is_uncertainty_and_not_a_success(self):
        self.camera.status_content = "outcome=timeout\nrc=143\nterminated=yes\n"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(result.outcome, "timeout")
        self.assertTrue(result.uncertainty)
        self.assertEqual(runner.exit_code(result), 2)

    def test_a_lost_identity_is_uncertainty(self):
        self.camera.status_content = "outcome=identity-lost\nrc=unknown\nterminated=no\n"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(result.outcome, "identity-lost")
        self.assertTrue(any("identity" in u for u in result.uncertainty))

    def test_a_missing_status_file_leaves_the_run_undecided(self):
        self.camera.status_content = ""
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(result.outcome, "status-invalid")
        self.assertTrue(result.uncertainty)
        self.assertTrue(any("status" in e for e in result.errors))


class IdentityAndCleanupTests(RunnerTestCase):
    def test_an_identity_change_after_the_run_keeps_the_original_error(self):
        self.camera.status_content = "outcome=rc:1\nrc=1\nterminated=yes\n"
        self.camera.apps_after = [("9999", "11111")]
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertTrue(any("rc=1" in e for e in result.errors), result.errors)
        self.assertTrue(any("identity" in e for e in result.errors), result.errors)
        self.assertLess(
            [i for i, e in enumerate(result.errors) if "rc=1" in e][0],
            [i for i, e in enumerate(result.errors) if "identity" in e][0],
        )

    def test_an_identity_change_after_a_clean_run_is_reported(self):
        self.camera.apps_after = [("9999", "11111")]
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertTrue(any("identity" in e for e in result.errors), result.errors)

    def test_a_boot_change_after_the_run_is_reported(self):
        self.camera.boot_after = "ffffffff-bbbb-cccc-dddd-eeeeeeeeeeee\n1712349999"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertTrue(any("boot" in e for e in result.errors), result.errors)

    def test_a_failed_cleanup_is_uncertainty_and_keeps_the_original_error(self):
        self.camera.status_content = "outcome=rc:1\nrc=1\nterminated=yes\n"
        self.camera.remove_works = False
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertTrue(any("rc=1" in e for e in result.errors), result.errors)
        self.assertTrue(result.uncertainty)
        self.assertFalse(result.cleanup_verified)
        self.assertEqual(runner.exit_code(result), 2)

    def test_the_staged_directory_is_removed_on_the_way_out(self):
        result, _ = self.run_it()
        self.assertTrue(result.ok, result.errors)
        removals = self.camera.removals()
        self.assertEqual(len(removals), 1)
        self.assertIn(result.remote_dir, removals[0])
        self.assertTrue(result.cleanup_verified)
        self.assertEqual(result.residual_path, "")


class StagingOwnershipTests(RunnerTestCase):
    """The directory has to be cleaned up whatever the staging step does."""

    def test_a_failed_upload_still_cleans_the_directory_up(self):
        self.camera.put_failure = OSError("the connection was lost during the upload")
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(len(self.camera.removals()), 1,
                         "the directory was created, so it must be removed")
        self.assertTrue(result.cleanup_verified)

    def test_a_failed_hash_check_still_cleans_the_directory_up(self):
        self.camera.corrupt_uploaded_hash = True
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(len(self.camera.removals()), 1)
        self.assertTrue(result.cleanup_verified)

    def test_a_refused_mkd_leaves_nothing_to_clean_up(self):
        self.camera.mkdir_failure = "refused"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertTrue(any("refused" in e for e in result.errors), result.errors)
        self.assertEqual(self.camera.removals(), [],
                         "a refused directory is not this run's to remove")
        self.assertTrue(result.cleanup_verified)

    def test_a_lost_connection_at_mkd_is_uncertain_and_names_the_path(self):
        self.camera.mkdir_failure = "transport"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertTrue(result.uncertainty)
        self.assertFalse(result.cleanup_verified)
        self.assertEqual(result.residual_path, result.remote_dir)
        self.assertEqual(self.camera.removals(), [],
                         "a directory of unknown ownership is not removed")

    def test_an_unconfirmed_child_keeps_the_directory_and_names_the_path(self):
        self.camera.status_content = "outcome=kill-unconfirmed\nrc=unknown\nterminated=no\n"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.removals(), [],
                         "a directory that may hold a running helper is kept")
        self.assertFalse(result.cleanup_verified)
        self.assertEqual(result.residual_path, result.remote_dir)
        self.assertTrue(any(result.remote_dir in u for u in result.uncertainty))

    def test_an_unknown_state_keeps_the_directory_and_names_the_path(self):
        self.camera.status_content = "outcome=state-unknown\nrc=unknown\nterminated=no\n"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.removals(), [],
                         "a child that cannot be read is not a child that has gone")
        self.assertFalse(result.cleanup_verified)
        self.assertEqual(result.residual_path, result.remote_dir)

    def test_a_changed_image_keeps_the_directory_and_names_the_path(self):
        self.camera.status_content = "outcome=image-changed\nrc=unknown\nterminated=no\n"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.removals(), [])
        self.assertEqual(result.residual_path, result.remote_dir)


class TransportFailureTests(RunnerTestCase):
    def test_an_execute_timeout_triggers_the_owned_child_recovery(self):
        self.camera.execute_failure = socket.timeout("timed out")
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(result.outcome, "http-timeout")
        self.assertTrue(any("timed out" in u for u in result.uncertainty))
        recovery = self.camera.recovery_commands()
        self.assertEqual(len(recovery), 1, "the own child must be looked for")
        self.assertTrue(recovery[0].endswith(runner.RECOVERY_NAME))

    def test_a_recovery_that_cannot_identify_the_child_keeps_the_directory(self):
        self.camera.execute_failure = socket.timeout("timed out")
        self.camera.recovery_reply = "OWNER-UNUSABLE\nNO-STATUS\n"
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.removals(), [])
        self.assertEqual(result.residual_path, result.remote_dir)

    def test_a_broken_transport_in_the_guards_is_a_refusal(self):
        self.camera.execute_failure = socket.timeout("timed out")
        result, _ = self.run_it()
        self.assertFalse(result.ok)
        self.assertTrue(result.errors)


class RecoveryVerdictTests(RunnerTestCase):
    """What the recovery proved decides whether the directory is removed."""

    def run_with_recovery(self, reply: str):
        self.camera.execute_failure = socket.timeout("timed out")
        self.camera.recovery_reply = reply
        return self.run_it()

    def test_a_proven_termination_allows_the_cleanup(self):
        result, _ = self.run_with_recovery("OWNED-KILLED-VERIFIED\nNO-STATUS\n")
        self.assertFalse(result.ok)
        self.assertEqual(len(self.camera.removals()), 1)
        self.assertTrue(result.cleanup_verified)

    def test_a_reused_pid_allows_the_cleanup(self):
        result, _ = self.run_with_recovery("IDENTITY-LOST\nNO-STATUS\n")
        self.assertFalse(result.ok)
        self.assertEqual(len(self.camera.removals()), 1)
        self.assertTrue(result.cleanup_verified)

    def test_an_unknown_state_keeps_the_directory(self):
        result, _ = self.run_with_recovery("STATE-UNKNOWN\nNO-STATUS\n")
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.removals(), [])
        self.assertFalse(result.cleanup_verified)
        self.assertEqual(result.residual_path, result.remote_dir)

    def test_a_changed_image_keeps_the_directory(self):
        result, _ = self.run_with_recovery("IMAGE-CHANGED\nNO-STATUS\n")
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.removals(), [])
        self.assertEqual(result.residual_path, result.remote_dir)

    def test_an_unknown_state_after_a_signal_keeps_the_directory(self):
        result, _ = self.run_with_recovery("OWNED-KILL-STATE-UNKNOWN\nNO-STATUS\n")
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.removals(), [])
        self.assertEqual(result.residual_path, result.remote_dir)

    def test_a_live_child_keeps_the_directory(self):
        result, _ = self.run_with_recovery("OWNED-KILL-UNCERTAIN\nNO-STATUS\n")
        self.assertFalse(result.ok)
        self.assertEqual(self.camera.removals(), [])
        self.assertEqual(result.residual_path, result.remote_dir)


# --- the strict formats ----------------------------------------------------


class StatusGrammarTests(unittest.TestCase):
    def test_a_good_status_is_read(self):
        outcome, rc, terminated = runner.parse_status(
            "outcome=rc:0\nrc=0\nterminated=yes\n"
        )
        self.assertEqual((outcome, rc, terminated), ("rc:0", 0, "yes"))

    def test_a_missing_rc_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_status("outcome=rc:0\nterminated=yes\n")

    def test_a_missing_outcome_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_status("rc=0\nterminated=yes\n")

    def test_an_unknown_outcome_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_status("outcome=rc:0\nrc=0\nterminated=yes\noutcome=other\n")

    def test_an_outcome_that_disagrees_with_the_rc_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_status("outcome=rc:0\nrc=1\nterminated=yes\n")

    def test_an_exited_helper_cannot_be_unterminated(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_status("outcome=rc:0\nrc=0\nterminated=no\n")

    def test_a_timeout_must_be_terminated(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_status("outcome=timeout\nrc=143\nterminated=no\n")

    def test_a_kill_that_was_not_confirmed_must_not_be_terminated(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_status("outcome=kill-unconfirmed\nrc=unknown\nterminated=yes\n")

    def test_an_unknown_outcome_token_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_status("outcome=pid-reused\nrc=0\nterminated=no\n")

    def test_a_non_numeric_rc_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_status("outcome=rc:0\nrc=zero\nterminated=yes\n")


class OwnerGrammarTests(unittest.TestCase):
    def test_a_good_owner_file_is_read(self):
        self.assertEqual(runner.parse_owner("pid=1234\ntick=42\n"), ("1234", "42"))

    def test_the_old_one_line_format_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_owner("pid=1234 tick=42\n")

    def test_a_missing_tick_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_owner("pid=1234\n")

    def test_a_non_numeric_tick_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_owner("pid=1234\ntick=\n")

    def test_a_duplicated_key_is_refused(self):
        with self.assertRaises(runner.LoadCheckError):
            runner.parse_owner("pid=1234\npid=5678\ntick=42\n")


# --- the generated shell, run for real -------------------------------------


class IdentityGuardTests(ShellTestCase):
    """The identity check itself, executed by a real shell."""

    def guard(self, pid: int, tick: str, path: str) -> subprocess.CompletedProcess:
        script = runner.build_identity_guard()
        script += "\nown_child {} {} {}\n".format(
            shlex.quote(str(pid)), shlex.quote(tick), shlex.quote(path)
        )
        return self.run_generated(script, "guard.sh")

    def test_it_accepts_the_process_it_started(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        result = self.guard(pid, "4242", str(self.helper_path))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_it_refuses_an_empty_tick(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        result = self.guard(pid, "", str(self.helper_path))
        self.assertNotEqual(result.returncode, 0)

    def test_it_refuses_a_changed_tick(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        result = self.guard(pid, "9999", str(self.helper_path))
        self.assertNotEqual(result.returncode, 0)

    def test_it_refuses_a_different_image_even_with_the_same_base_name(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        for path in ("/somewhere/else/capture-shot", "capture-shot"):
            result = self.guard(pid, "4242", path)
            self.assertNotEqual(result.returncode, 0,
                                f"the guard accepted {path!r}")

    def test_it_refuses_a_process_that_is_not_there(self):
        result = self.guard(999999, "4242", str(self.helper_path))
        self.assertNotEqual(result.returncode, 0)


class RecoveryScriptTests(ShellTestCase):
    """The recovery script, run for real against a real process."""

    def write_owner(self, text: str) -> None:
        (self.remote_dir / "owner").write_text(text)

    def run_recovery(self) -> subprocess.CompletedProcess:
        script = runner.build_recovery_script(str(self.remote_dir))
        return self.run_generated(script, "recovery.sh")

    def test_it_kills_the_child_it_can_identify_and_says_so(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        self.write_owner(f"pid={pid}\ntick=4242\n")
        watcher = ZombieWatcher(self.proc, pid)
        watcher.start()
        result = self.run_recovery()
        self.assertIn("OWNED-KILLED-VERIFIED", result.stdout, result.stderr)
        deadline = time.time() + 5
        while time.time() < deadline and alive(pid):
            time.sleep(0.05)
        self.assertFalse(alive(pid), "the child was identified and signalled")

    def test_it_does_not_signal_a_pid_with_a_different_tick(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        self.write_owner(f"pid={pid}\ntick=9999\n")
        result = self.run_recovery()
        self.assertIn("IDENTITY-LOST", result.stdout, result.stderr)
        self.assertTrue(alive(pid), "a pid that is not the child must not be signalled")

    def test_it_does_not_signal_a_process_running_another_image(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        other = self.root / "other" / "capture-shot"
        other.parent.mkdir(parents=True, exist_ok=True)
        other.write_text("x")
        self.proc.add(pid, "4242", str(other))
        self.write_owner(f"pid={pid}\ntick=4242\n")
        result = self.run_recovery()
        # The same start, another image: the process is alive, so this is
        # not a reused pid and the child is not gone.
        self.assertIn("IMAGE-CHANGED", result.stdout, result.stderr)
        self.assertTrue(alive(pid), "another program must not be signalled")

    def test_it_refuses_an_owner_file_it_cannot_read(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        for content in ("pid=%d tick=4242\n" % pid, f"pid={pid}\n", "", "pid=abc\ntick=42\n"):
            self.write_owner(content)
            result = self.run_recovery()
            self.assertIn("OWNER-UNUSABLE", result.stdout, result.stderr)
            self.assertTrue(alive(pid), "nothing may be signalled")

    def test_it_reports_a_child_that_is_not_running(self):
        self.write_owner("pid=999999\ntick=4242\n")
        result = self.run_recovery()
        self.assertIn("NOT-RUNNING", result.stdout, result.stderr)

    def test_it_does_not_kill_again_after_the_identity_changed(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        self.write_owner(f"pid={pid}\ntick=4242\n")

        # The identity changes while the grace period runs, the way a
        # recycled pid would.  The second signal must not be sent.
        def change() -> None:
            time.sleep(0.5)
            self.proc.change_tick(pid, "7777")

        threading.Thread(target=change, daemon=True).start()
        result = self.run_recovery()
        self.assertIn("OWNED-KILL-UNCERTAIN", result.stdout, result.stderr)


class WatchdogScriptTests(ShellTestCase):
    """The watchdog script, run for real."""

    def watchdog(self, deadline_seconds: int = 0) -> str:
        cfg = runner.Config(helper=self.helper_path, helper_sha256="0" * 64,
                            deadline_seconds=deadline_seconds)
        return runner.build_watchdog_script(cfg, str(self.remote_dir))

    def test_it_signals_nothing_when_the_identity_cannot_be_established(self):
        pidfile = self.publish_helper()
        process = self.popen_shell(self.watchdog())
        # The entry is there, but its start tick cannot be read.
        pid = self.wait_for_helper(pidfile, tick="")
        status = self.wait_for_file(self.remote_dir / "status")
        process.wait(timeout=30)

        outcome, rc, terminated = runner.parse_status(status)
        self.assertEqual(outcome, "identity-unknown")
        self.assertEqual(terminated, "no")
        time.sleep(0.3)
        self.assertTrue(alive(pid), "without an identity nothing may be signalled")

    def test_it_signals_the_child_it_can_identify(self):
        pidfile = self.publish_helper()
        process = self.popen_shell(self.watchdog())
        pid = self.wait_for_helper(pidfile)
        watcher = ZombieWatcher(self.proc, pid)
        watcher.start()
        status = self.wait_for_file(self.remote_dir / "status")
        process.wait(timeout=30)

        outcome, rc, terminated = runner.parse_status(status)
        self.assertEqual(outcome, "timeout")
        self.assertEqual(terminated, "yes")
        deadline = time.time() + 5
        while time.time() < deadline and alive(pid):
            time.sleep(0.05)
        self.assertFalse(alive(pid), "the child was identified and signalled")

    def test_the_owner_file_it_writes_is_in_the_expected_format(self):
        pidfile = self.publish_helper()
        process = self.popen_shell(self.watchdog())
        pid = self.wait_for_helper(pidfile)
        owner = self.wait_for_owner()
        process.wait(timeout=30)
        self.close_process(process)

        written_pid, tick = runner.parse_owner(owner)
        self.assertEqual(written_pid, str(pid))
        self.assertEqual(tick, "4242")


class UnknownStateTests(ShellTestCase):
    """A child that cannot be read is not a child that has gone.

    These are the two cases the review called out: an unreadable or
    missing `stat` on a process that is still alive, and a process that
    carries the right tick but another image.  In both, the run must not
    certify a termination, must not block on a wait, must not clean up a
    possibly live child, and must not signal anything.
    """

    def write_owner(self, text: str) -> None:
        (self.remote_dir / "owner").write_text(text)

    def run_recovery(self) -> subprocess.CompletedProcess:
        script = runner.build_recovery_script(str(self.remote_dir))
        return self.run_generated(script, "recovery.sh")

    # -- the stat cannot be read ------------------------------------------

    def test_the_recovery_does_not_call_an_unreadable_child_stopped(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        self.write_owner(f"pid={pid}\ntick=4242\n")
        self.proc.break_stat(pid)
        result = self.run_recovery()
        self.assertIn("STATE-UNKNOWN", result.stdout, result.stderr)
        self.assertNotIn("NOT-RUNNING", result.stdout,
                         "an unreadable child is not a stopped child")
        self.assertTrue(alive(pid), "nothing may be signalled")

    def test_the_recovery_does_not_call_an_unreadable_stat_stopped(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        self.write_owner(f"pid={pid}\ntick=4242\n")
        self.proc.make_stat_unreadable(pid)
        result = self.run_recovery()
        self.assertIn("STATE-UNKNOWN", result.stdout, result.stderr)
        self.assertNotIn("NOT-RUNNING", result.stdout)
        self.assertTrue(alive(pid), "nothing may be signalled")

    def test_the_recovery_does_not_signal_when_the_tree_is_gone(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "4242", str(self.helper_path))
        self.write_owner(f"pid={pid}\ntick=4242\n")
        shutil.rmtree(self.proc.root)
        result = self.run_recovery()
        self.assertIn("STATE-UNKNOWN", result.stdout, result.stderr)
        self.assertNotIn("NOT-RUNNING", result.stdout,
                         "an absent /proc tree proves nothing")
        self.assertTrue(alive(pid), "nothing may be signalled")

    def test_the_watchdog_does_not_block_on_a_live_child_it_cannot_read(self):
        pidfile = self.publish_helper()
        cfg = runner.Config(helper=self.helper_path, helper_sha256="0" * 64,
                            deadline_seconds=0)
        script = runner.build_watchdog_script(cfg, str(self.remote_dir))
        process = self.popen_generated(script, "watchdog.sh")
        pid = self.wait_for_helper(pidfile)
        self.proc.break_stat(pid)

        started = time.time()
        status = self.wait_for_file(self.remote_dir / "status")
        elapsed = time.time() - started
        process.wait(timeout=30)

        outcome, rc, terminated = runner.parse_status(status)
        self.assertEqual(outcome, "state-unknown")
        self.assertEqual(terminated, "no")
        self.assertLess(elapsed, 20,
                        "the watchdog must not wait on a child it cannot read")
        time.sleep(0.3)
        self.assertTrue(alive(pid), "without an identity nothing may be signalled")

    # -- the same tick, another image --------------------------------------

    def test_the_recovery_does_not_call_a_changed_image_stopped(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        other = self.root / "other" / "capture-shot"
        other.parent.mkdir(parents=True, exist_ok=True)
        other.write_text("x")
        self.proc.add(pid, "4242", str(other))
        self.write_owner(f"pid={pid}\ntick=4242\n")
        result = self.run_recovery()
        self.assertIn("IMAGE-CHANGED", result.stdout, result.stderr)
        self.assertNotIn("NOT-RUNNING", result.stdout)
        self.assertNotIn("IDENTITY-LOST", result.stdout,
                         "a different image is not a reused pid")
        self.assertTrue(alive(pid), "the process is alive and must not be signalled")

    def test_the_watchdog_does_not_signal_a_child_running_another_image(self):
        pidfile = self.publish_helper()
        cfg = runner.Config(helper=self.helper_path, helper_sha256="0" * 64,
                            deadline_seconds=0)
        script = runner.build_watchdog_script(cfg, str(self.remote_dir))
        process = self.popen_generated(script, "watchdog.sh")
        pid = self.wait_for_helper(pidfile)
        # The same start, but the image is not the helper's.
        self.proc.set_image(pid, str(self.root / "somewhere-else"))

        status = self.wait_for_file(self.remote_dir / "status")
        process.wait(timeout=30)

        outcome, rc, terminated = runner.parse_status(status)
        self.assertEqual(outcome, "image-changed")
        self.assertEqual(terminated, "no")
        time.sleep(0.3)
        self.assertTrue(alive(pid), "the process is alive and must not be signalled")

    # -- the identity cannot be read ---------------------------------------

    def test_the_recovery_does_not_signal_when_the_identity_read_fails(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        self.proc.add(pid, "", str(self.helper_path), empty_tick=True)
        self.write_owner(f"pid={pid}\ntick=4242\n")
        result = self.run_recovery()
        self.assertIn("IDENTITY-UNKNOWN", result.stdout, result.stderr)
        self.assertNotIn("NOT-RUNNING", result.stdout)
        self.assertTrue(alive(pid), "nothing may be signalled")

    def test_the_recovery_reports_a_reused_pid_as_gone(self):
        self.dummy = start_dummy()
        pid = self.dummy.pid
        # A different, readable start tick: the pid was reused, so the old
        # child was reaped and is gone.
        self.proc.add(pid, "9999", str(self.helper_path))
        self.write_owner(f"pid={pid}\ntick=4242\n")
        result = self.run_recovery()
        self.assertIn("IDENTITY-LOST", result.stdout, result.stderr)
        self.assertTrue(alive(pid), "the process at the pid is not ours")

    def test_the_verdicts_map_to_what_they_prove(self):
        self.assertEqual(runner.RECOVERY_VERDICTS["NOT-RUNNING"][0], True)
        self.assertEqual(runner.RECOVERY_VERDICTS["IDENTITY-LOST"][0], True)
        self.assertEqual(runner.RECOVERY_VERDICTS["OWNED-KILLED-VERIFIED"][0], True)
        self.assertEqual(runner.RECOVERY_VERDICTS["IMAGE-CHANGED"][0], False)
        self.assertEqual(runner.RECOVERY_VERDICTS["OWNED-KILL-UNCERTAIN"][0], False)
        for verdict in ("STATE-UNKNOWN", "IDENTITY-UNKNOWN", "OWNER-UNUSABLE",
                        "OWNED-KILL-STATE-UNKNOWN"):
            self.assertIsNone(runner.RECOVERY_VERDICTS[verdict][0], verdict)


class OwnerFormatRoundTripTests(ShellTestCase):
    """The format the watchdog writes is the format the recovery reads.

    The writer and the reader are the generated ones, and the child is a
    real process, so this is the round trip itself and not a description
    of it.
    """

    def test_the_recovery_reads_the_file_the_watchdog_wrote(self):
        pidfile = self.publish_helper()
        cfg = runner.Config(helper=self.helper_path, helper_sha256="0" * 64,
                            deadline_seconds=3600)
        watchdog = runner.build_watchdog_script(cfg, str(self.remote_dir))
        recovery = runner.build_recovery_script(str(self.remote_dir))

        process = self.popen_generated(watchdog, "watchdog.sh")
        pid = self.wait_for_helper(pidfile)
        owner = self.wait_for_owner()
        # The watchdog is stopped before its deadline: the child stays
        # alive, so the recovery has a live child to find.
        process.terminate()
        process.wait(timeout=30)
        self.close_process(process)

        written_pid, tick = runner.parse_owner(owner)
        self.assertEqual(written_pid, str(pid))
        self.assertEqual(tick, "4242")

        watcher = ZombieWatcher(self.proc, pid)
        watcher.start()
        result = self.run_generated(recovery, "recovery.sh")
        self.assertIn("OWNED-KILLED-VERIFIED", result.stdout, result.stderr)
        deadline = time.time() + 5
        while time.time() < deadline and alive(pid):
            time.sleep(0.05)
        self.assertFalse(alive(pid),
                         "the recovery read the owner's format and ended the child")


if __name__ == "__main__":
    unittest.main(verbosity=2)
