"""Offline plans, observation decoding and session guard tests; no camera."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare", HERE / "prepare.py")
prep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prep)


class PlanTests(unittest.TestCase):
    def test_hdr_matches_canonical_words(self):
        self.assertEqual(prep.hdr_exposures("ev2"), [(48, 1600), (48, 400), (48, 100),
                         (80, 100), (112, 100), (144, 100), (176, 100), (208, 100)])
        self.assertEqual(prep.hdr_exposures("ev3"), [(48, 1600), (48, 400), (64, 100),
                         (112, 100), (160, 100), (208, 100)])
        with self.assertRaises(ValueError):
            prep.hdr_exposures("ev2", "exposures='48:100'")
        if prep.HDR_RUNNER.exists():  # the recorded lists must match the runner
            for preset, recorded in prep.RETIRED_HDR_EXPOSURES.items():
                self.assertEqual(recorded, prep.hdr_exposures(preset, prep.HDR_RUNNER.read_text()))
        cases = prep.plans()
        self.assertEqual(len({c["name"] for c in cases}), len(cases))
        self.assertEqual([c["start_mode"] for c in cases].count("fresh"), 1)
        stress = next(c for c in cases if c["name"] == "stress-100")
        self.assertEqual(stress["reserve_bytes"], 128 * 1024 * 1024)
        self.assertNotIn("--capture-type", stress["argv"])

    def test_bundle_pins_bytes_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            elf = bytearray(80)
            elf[:6] = b"\x7fELF\x01\x01"
            elf[18:20] = b"\x28\x00"
            probe = root / "probe"
            probe.write_bytes(elf + b"metric,sequence=")
            buttons = root / "buttons"
            buttons.write_bytes(elf)
            display = root / "display"
            display.write_bytes(elf + b"--center")
            out = root / "kit"
            manifest = prep.bundle(out, probe, "WB_DAYLIGHT", buttons, display)
            self.assertFalse(manifest["camera_executed"])
            for name, digest in manifest["files"].items():
                self.assertEqual(prep.digest(out / name), digest)
            for path in out.glob("*.sh"):
                subprocess.run(["sh", "-n", path], check=True)
            self.assertIn(" reenter ", (out / "reentry-1.sh").read_text())
            self.assertIn("REQUIRE_WB=WB_DAYLIGHT", (out / "hdr-ev2.sh").read_text())
            with self.assertRaises(ValueError):
                prep.bundle(out, probe, "WB_DAYLIGHT", buttons, display)
            probe.write_bytes(b"not ARM")
            with self.assertRaises(ValueError):
                prep.bundle(root / "bad", probe, "WB_DAYLIGHT", buttons, display)
            self.assertFalse((root / "bad").exists())

    def test_summary_rejects_duplicates_and_counts_failures(self):
        row = ("metric,sequence={n},status={status},capture_us=100,save_us=20,total_us=125,"
               "cycle_us={cycle},set_us=90,ready_us=95,frame_hold_us=80,"
               "frame_bytes=33732608,frame_flags=258,jpeg_policy=save,frame_output=save")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "session.txt"
            path.write_text("\n".join(row.format(n=n, status=int(n == 3), cycle=0 if n == 1 else 150)
                                      for n in range(1, 4)))
            summary = prep.summarize([path])[0]
            self.assertEqual((summary["valid_frames"], summary["failures"]), (2, 1))
            self.assertEqual(summary["cycle_us"]["n"], 1)
            other = Path(tmp) / "log-off.txt"
            other.write_text(row.format(n=1, status=0, cycle=0) + ",library_log=off")
            groups = prep.summarize([path, other])
            self.assertEqual([(g["library_log"], g["valid_frames"]) for g in groups],
                             [("off", 1), ("sync", 2)])
            with self.assertRaises(ValueError):
                prep.summarize([path, path])
            path.write_text(row.format(n=1, status=0, cycle=0) + "\n" + row.format(n=1, status=0, cycle=0))
            with self.assertRaises(ValueError):
                prep.summarize([path])
            path.write_text("no capture rows")
            with self.assertRaises(ValueError):
                prep.summarize([path])

    def test_snapshot_preserves_unknowns_and_raw_values(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            supply = root / "sys/class/power_supply/battery"
            supply.mkdir(parents=True)
            (supply / "capacity").write_text("47\n")
            (supply / "status").write_text("Charging\n")
            env = dict(os.environ, G360_SNAPSHOT_ROOT=tmp)
            result = subprocess.run(["sh", HERE / "platform-snapshot.sh", "1"], env=env,
                                    capture_output=True, text=True, check=True)
            self.assertIn("/battery/capacity value=47", result.stdout)
            self.assertIn("/battery/voltage_now value=UNKNOWN(unavailable)", result.stdout)
            self.assertEqual((supply / "capacity").read_text(), "47\n")

    def test_compact_uses_named_battery_and_keeps_unknowns(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, capacity in (("battery", "100"), ("adc_battery", "1370")):
                supply = root / "sys/class/power_supply" / name
                supply.mkdir(parents=True)
                (supply / "capacity").write_text(capacity + "\n")
                (supply / "status").write_text("Full\n")
            result = subprocess.run(["sh", HERE / "platform-snapshot.sh", "--compact", "1"],
                                    env=dict(os.environ, G360_SNAPSHOT_ROOT=tmp),
                                    capture_output=True, text=True, check=True)
            row = result.stdout.splitlines()[1].split(",")
            self.assertEqual(row[1:3], ["100", "Full"])
            self.assertEqual(row[0], "UNKNOWN")
            self.assertNotIn("1370", result.stdout)
            self.assertEqual(row[3:], ["UNKNOWN"] * 7)


class ButtonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "observer"
        subprocess.run([os.environ.get("CC", "/usr/bin/clang"), "-std=c99", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=address,undefined", "-I" + str(HERE.parents[1] / "src/lib/capture-session"),
                        str(HERE / "button-observer.c"), "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_arm_records_and_truncated_input(self):
        path = Path(self.tmp.name) / "events"
        path.write_bytes(b"".join(struct.pack("<iiHHi", 123, 456, t, c, v)
                                 for t, c, v in [(1, 126, 1), (1, 126, 0), (0, 3, 0)]))
        result = subprocess.run([self.exe, "--decode", path], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("type=1 code=126 value=1", result.stdout)
        self.assertIn("syn_dropped=1", result.stdout)
        path.write_bytes(path.read_bytes()[:-1])
        self.assertEqual(subprocess.run([self.exe, "--decode", path], capture_output=True).returncode, 1)
        self.assertEqual(subprocess.run([self.exe, "--observe", path, "601"]).returncode, 2)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.card = self.root / "card"
        self.card.mkdir()
        self.usr = self.root / "usr"
        self.usr.mkdir()
        self.proc = self.root / "proc"
        self.proc.mkdir()
        (self.proc / "stat").write_text("btime 12345\n")
        (self.proc / "uptime").write_text("100.00 99.00\n")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.trace = self.root / "trace"
        self.state = self.root / "state"
        self.state.write_text("inactive")
        self.put("uname", "echo armv7l")
        self.put("flock", "exit 0")
        self.put("df", "echo 'Filesystem 1B-blocks Used Available Use% Mounted'; echo 'card 9999999999 1 9999999998 1% /card'")
        self.put("st", "echo '| USERDATA_WB | WB_DAYLIGHT |'; echo '| USERDATA_SMARTRANGE | SMARTRANGE_OFF |'")
        self.put("openssl", "case \"$*\" in *g360-shot*) echo \"SHA256= ${PROBE_HASH:-" + "a" * 64 + "}\";; "
                 "*gear360-display*) echo 'SHA256= " + "d" * 64 + "';; "
                 "*libcapture*) echo 'SHA256= fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7';; "
                 "*) echo 'SHA256= 9129761c6ee9f6b62968d3a8307a8b5fbf02e1493323658c42d9ebe587673a20';; esac")
        self.put("systemctl", "if [ \"$1\" = --no-block ]; then echo stop >>\"$TRACE\"; echo inactive >\"$STATE\"; "
                 "else s=$(cat \"$STATE\"); echo ActiveState=$s; [ \"$s\" = active ] && echo MainPID=44 || echo MainPID=0; fi")
        self.put("g360-shot", "echo probe >>\"$TRACE\"; echo \"$2\" >\"$TRACE.out\"; exit ${PROBE_RC:-0}",
                 folder=self.root)
        self.put("gear360-display", 'if [ "$1" = --write ]; then echo screen >>"$TRACE"; exit ${SCREEN_RC:-0}; '
                 'else exit ${SCREEN_PREVIEW_RC:-0}; fi', folder=self.root)
        source = (HERE / "run-session.sh").read_text()
        # Test-only path substitution runs the actual shell control flow with
        # synthetic /proc, storage, hash tool and service; never the real host.
        for old, new in [("/opt/storage/sdcard", str(self.card)), ("/opt/usr", str(self.usr)),
                         ("/proc", str(self.proc)),
                         ("/tmp/gear360-experiments.lock", str(self.root / "lock")),
                         ("/tmp/gear360-screen.lock", str(self.root / "screen-lock")),
                         ("/usr/bin/openssl", str(self.bin / "openssl"))]:
            source = source.replace(old, new)
        self.script = self.root / "run-session.sh"
        self.script.write_text(source)

    def put(self, name, body, folder=None):
        path = (folder or self.bin) / name
        path.write_text("#!/bin/sh\n" + body + "\n")
        path.chmod(0o755)

    def run_case(self, mode="reenter", boot="12345", **env):
        return subprocess.run(["sh", self.script, mode, boot, self.card / "exp-test", "a" * 64,
                               "--count", "1"], capture_output=True, text=True,
                              env=dict(os.environ, PATH=str(self.bin) + ":/usr/bin:/bin", TRACE=str(self.trace),
                                       STATE=str(self.state), DISPLAY_SHA256="d" * 64, **env))

    def test_reentry_never_stops_stock(self):
        result = self.run_case()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.trace.read_text(), "screen\nprobe\n")
        self.assertIn("session_exit=0 btime=12345", (self.card / "exp-test/session.txt").read_text())

    def test_fresh_stops_once_then_probes(self):
        self.state.write_text("active")
        result = self.run_case("fresh")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.trace.read_text(), "stop\nscreen\nprobe\n")

    def test_wrong_boot_or_hash_never_stops_or_probes(self):
        result = self.run_case("fresh", boot="12346")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.trace.exists())
        result = self.run_case("fresh", PROBE_HASH="b" * 64)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.trace.exists())

    def test_reentry_with_active_stock_or_existing_client_refuses(self):
        self.state.write_text("active")
        self.assertNotEqual(self.run_case().returncode, 0)
        self.assertFalse(self.trace.exists())
        # Another output path for a separate refusal (no accidental reuse).
        self.state.write_text("inactive")
        process = self.proc / "55"
        process.mkdir()
        (process / "comm").write_text("g360-shot\n")
        self.assertNotEqual(self.run_case("fresh").returncode, 0)
        self.assertFalse(self.trace.exists())

    def test_hdr_mismatch_and_probe_failure_stop(self):
        result = self.run_case("fresh", REQUIRE_WB="WB_AUTO")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.trace.exists())
        result = self.run_case(PROBE_RC="1")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.trace.read_text(), "screen\nprobe\n")

    def test_display_failure_sends_no_capture(self):
        self.state.write_text("active")
        result = self.run_case("fresh", SCREEN_PREVIEW_RC="2")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.trace.exists())
        result = self.run_case("fresh", SCREEN_RC="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.trace.read_text(), "stop\nscreen\n")

    def test_running_screen_controller_refuses_takeover(self):
        process = self.proc / "55"
        process.mkdir()
        (process / "comm").write_text("gear360-screen\n")
        self.assertNotEqual(self.run_case("fresh").returncode, 0)
        self.assertFalse(self.trace.exists())

    def test_shots_default_to_the_card(self):
        result = self.run_case()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(Path(self.trace.with_suffix(".out").read_text().strip()), self.card / "exp-test/shots")

    def test_shots_dir_on_internal_flash(self):
        result = self.run_case(SHOTS_DIR=str(self.usr / "g360-exp-t1"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(Path(self.trace.with_suffix(".out").read_text().strip()), self.usr / "g360-exp-t1")
        self.assertTrue((self.usr / "g360-exp-t1").is_dir())
        self.assertFalse((self.card / "exp-test/shots").exists())

    def test_bad_shots_dir_refuses_before_screen_and_probe(self):
        for bad in (str(self.card / "exp-x"), str(self.usr / "g360-exp-"), str(self.usr / "g360-exp-a/b"),
                    str(self.usr / "other")):
            result = self.run_case(SHOTS_DIR=bad)
            self.assertNotEqual(result.returncode, 0, bad)
            self.assertFalse(self.trace.exists(), bad)
        (self.usr / "g360-exp-t2").mkdir()
        self.assertNotEqual(self.run_case(SHOTS_DIR=str(self.usr / "g360-exp-t2")).returncode, 0)
        self.assertFalse(self.trace.exists())

    def test_hdr_console_nul_separators(self):
        self.put("st", r"printf '\000| USERDATA_WB | WB_DAYLIGHT |\000\n| USERDATA_SMARTRANGE | SMARTRANGE_OFF |\000\n'")
        result = self.run_case(REQUIRE_WB="WB_DAYLIGHT")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.trace.read_text(), "screen\nprobe\n")


if __name__ == "__main__":
    unittest.main()
