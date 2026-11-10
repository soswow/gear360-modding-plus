#!/usr/bin/env python3
"""Offline tests for ap-boot.sh (the access-point request helper).

Standard library only.  No camera, no network, no native tool: every external
command the helper can run (gdbus, timeout, ps) is replaced by a fake script
in a temporary directory.  The credentials used here are synthetic.

Run:  python3 output/test_ap_boot.py
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "ap-boot.sh"

SYNTH_PSK = "synthetic-passphrase-0123456789"   # 30 chars, not a real key
SYNTH_HEX_PSK = "a" * 64                        # the derived-key shape
SYNTH_SSID = "Gear 360(Test)"

FAKE_GDBUS = """#!/bin/sh
{
  echo CALL
  for a in "$@"; do printf 'ARG:%s\\n' "$a"; done
} >> "$FAKE_CALL_LOG"
case " $* " in
  *enable_wifi_ap*)
      printf '%s' "${FAKE_ENABLE_OUT:-}"
      exit "${FAKE_ENABLE_RC:-0}"
      ;;
  *)
      printf '%s' "${FAKE_PROBE_OUT:-}"
      exit "${FAKE_PROBE_RC:-0}"
      ;;
esac
"""

# The fake ps reports the "before" process list until the request has been
# sent, and the "after" list afterwards - that is the real sequence.  The
# break switches make ps unreadable, before or after the request.
FAKE_PS = """#!/bin/sh
if grep -q enable_wifi_ap "$FAKE_CALL_LOG" 2>/dev/null; then
    [ "${FAKE_PS_BREAK_AFTER:-0}" = "1" ] && exit 1
    printf '%s' "${FAKE_PS_OUT:-}"
else
    [ "${FAKE_PS_BREAK_BEFORE:-0}" = "1" ] && exit 1
    printf '%s' "${FAKE_PS_OUT_BEFORE:-}"
fi
"""

FAKE_TIMEOUT = """#!/bin/sh
# drop the duration argument, run the rest
shift
exec "$@"
"""

# exit codes documented by ap-boot.sh
UP, PENDING, REFUSED, UNKNOWN, PREFLIGHT = 0, 1, 2, 3, 4


class Sandbox:
    """A temporary camera-ish environment with fake commands."""

    def __init__(self):
        self.root = Path(tempfile.mkdtemp(prefix="apboot-test-"))
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.call_log = self.root / "calls.log"
        self.call_log.write_text("")
        self.conf = self.root / "hostapd.conf"
        self.psk_file = self.root / "ap-passphrase"
        self.log = self.root / "ap-boot.log"
        self._write_exec(self.bin / "gdbus", FAKE_GDBUS)
        self._write_exec(self.bin / "ps", FAKE_PS)
        self._write_exec(self.bin / "timeout", FAKE_TIMEOUT)

    def _write_exec(self, path, body):
        path.write_text(body)
        path.chmod(0o755)

    def write_conf(self, ssid=SYNTH_SSID, channel="6"):
        self.conf.write_text(
            "interface=wlan0\n"
            "driver=nl80211\n"
            "ssid=%s\n"
            "channel=%s\n"
            "ignore_broadcast_ssid=0\n"
            "hw_mode=g\n"
            % (ssid, channel)
        )

    def write_psk(self, value=SYNTH_PSK):
        self.psk_file.write_text(value + "\n")

    def env(self, **overrides):
        env = dict(os.environ)
        env.update(
            {
                "AP_CONF": str(self.conf),
                "AP_PASSPHRASE_FILE": str(self.psk_file),
                "AP_LOG": str(self.log),
                "AP_MODE": "ios",
                "AP_GDBUS": str(self.bin / "gdbus"),
                "AP_PS": str(self.bin / "ps"),
                "AP_TIMEOUT_BIN": str(self.bin / "timeout"),
                "AP_TIMEOUT_SECS": "5",
                "AP_BUS_WAIT_SECS": "1",
                "FAKE_CALL_LOG": str(self.call_log),
                "FAKE_PS_OUT": "systemd\n",
                "FAKE_PS_OUT_BEFORE": "systemd\n",
                "FAKE_PROBE_OUT": "{}",
                "FAKE_PROBE_RC": "0",
                "FAKE_ENABLE_OUT": "(uint32 0,)",
                "FAKE_ENABLE_RC": "0",
            }
        )
        env.update({k: str(v) for k, v in overrides.items()})
        return env

    def run(self, **overrides):
        return subprocess.run(
            ["sh", str(SCRIPT)],
            env=self.env(**overrides),
            capture_output=True,
            text=True,
            timeout=60,
        )

    def calls(self):
        """The fake gdbus invocations, as a list of argument lists."""
        records, current = [], None
        for line in self.call_log.read_text().splitlines():
            if line == "CALL":
                current = []
                records.append(current)
            elif line.startswith("ARG:"):
                current.append(line[4:])
        return records

    def enable_calls(self):
        return [c for c in self.calls() if any("enable_wifi_ap" in a for a in c)]

    def cleanup(self):
        shutil.rmtree(self.root, ignore_errors=True)


class ApBootTest(unittest.TestCase):
    def setUp(self):
        self.sb = Sandbox()
        self.addCleanup(self.sb.cleanup)

    # ---------------------------------------------------------------- happy
    def test_requests_once_keeps_passphrase_and_hides_the_key(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_PS_OUT="systemd\nhostapd\n")
        self.assertEqual(proc.returncode, UP, proc.stdout + proc.stderr)
        self.assertEqual(len(self.sb.enable_calls()), 1,
                         "the request must be sent exactly once")
        # the passphrase file is preserved, not consumed
        self.assertTrue(self.sb.psk_file.exists())
        self.assertEqual(self.sb.psk_file.read_text().strip(), SYNTH_PSK)
        # the key never reaches stdout, stderr or the log
        for blob in (proc.stdout, proc.stderr, self.sb.log.read_text()):
            self.assertNotIn(SYNTH_PSK, blob)
            self.assertNotIn("'key':", blob)

    def test_exact_six_keys_and_types(self):
        self.sb.write_conf()
        self.sb.write_psk()
        self.sb.run(FAKE_PS_OUT="systemd\nhostapd\n")
        self.assertEqual(len(self.sb.enable_calls()), 1)
        dict_arg = [a for a in self.sb.enable_calls()[0] if a.startswith("{")][0]
        self.assertEqual(
            dict_arg,
            "{'ssid': <'Gear 360(Test)'>, 'key': <'%s'>, 'sec_type': <int32 1>, "
            "'channel': <int32 6>, 'visibility': <true>, 'mode': <'ios'>}"
            % SYNTH_PSK,
        )

    def test_quoting_of_spaces_apostrophe_and_backslash(self):
        ssid = "Bob's Phone \\ Wi-Fi"
        key = "pa ss'w\\o rd"
        self.sb.write_conf(ssid=ssid)
        self.sb.write_psk(key)
        proc = self.sb.run(FAKE_PS_OUT="systemd\nhostapd\n")
        self.assertEqual(proc.returncode, UP, proc.stdout + proc.stderr)
        dict_arg = [a for a in self.sb.enable_calls()[0] if a.startswith("{")][0]
        self.assertEqual(
            dict_arg,
            "{'ssid': <'Bob\\'s Phone \\\\ Wi-Fi'>, 'key': <'pa ss\\'w\\\\o rd'>, "
            "'sec_type': <int32 1>, 'channel': <int32 6>, 'visibility': <true>, "
            "'mode': <'ios'>}",
        )
        # the dictionary is one argument, and it is not printed
        self.assertNotIn(dict_arg, proc.stdout)
        self.assertNotIn(key, proc.stdout)

    # ------------------------------------------------------------- refusals
    def test_hex_length_key_is_refused_before_any_bus_call(self):
        self.sb.write_conf()
        self.sb.write_psk(SYNTH_HEX_PSK)
        proc = self.sb.run()
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertEqual(self.sb.calls(), [], "no bus call may be made")
        self.assertNotIn(SYNTH_HEX_PSK, proc.stdout)
        self.assertIn("8 to 63", proc.stdout)

    def test_missing_configuration_is_refused(self):
        self.sb.write_psk()
        proc = self.sb.run()
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertEqual(self.sb.calls(), [])

    def test_missing_passphrase_file_is_refused(self):
        self.sb.write_conf()
        proc = self.sb.run()
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertEqual(self.sb.calls(), [])
        self.assertTrue(self.sb.psk_file.exists() is False)

    def test_higher_channel_is_accepted(self):
        # 1..14 is not proved by the agent's parser: only a positive value
        # that fits in the int32 field is checked.
        self.sb.write_conf(channel="149")
        self.sb.write_psk()
        proc = self.sb.run(FAKE_PS_OUT="systemd\nhostapd\n")
        self.assertEqual(proc.returncode, UP, proc.stdout + proc.stderr)
        self.assertEqual(len(self.sb.enable_calls()), 1)
        dict_arg = [a for a in self.sb.enable_calls()[0] if a.startswith("{")][0]
        self.assertIn("'channel': <int32 149>", dict_arg)

    def test_out_of_range_channel_is_refused(self):
        for channel in ("0", "2147483648"):
            with self.subTest(channel=channel):
                sb = Sandbox()
                self.addCleanup(sb.cleanup)
                sb.write_conf(channel=channel)
                sb.write_psk()
                proc = sb.run()
                self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
                self.assertEqual(sb.calls(), [])

    def test_non_numeric_channel_is_refused(self):
        self.sb.write_conf(channel="six")
        self.sb.write_psk()
        proc = self.sb.run()
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertEqual(self.sb.calls(), [])

    def test_bad_mode_is_refused(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(AP_MODE="weird")
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertEqual(self.sb.calls(), [])

    def test_missing_timeout_utility_is_refused_clearly(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(AP_TIMEOUT_BIN=str(self.sb.root / "no-such-timeout"))
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertIn("timeout", proc.stdout)
        self.assertEqual(self.sb.calls(), [])

    def test_multiline_passphrase_file_is_refused(self):
        self.sb.write_conf()
        self.sb.psk_file.write_text("first line\nsecond line\n")
        proc = self.sb.run()
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertEqual(self.sb.calls(), [])

    # --------------------------------------------------------------- results
    def test_result_3_is_a_refusal(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_ENABLE_OUT="(uint32 3,)")
        self.assertEqual(proc.returncode, REFUSED, proc.stdout)
        self.assertIn("3", proc.stdout)

    def test_pending_result_without_hostapd_is_not_claimed_up(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_ENABLE_OUT="(uint32 9,)")
        self.assertEqual(proc.returncode, PENDING, proc.stdout)
        self.assertIn("hostapd not observed", proc.stdout)

    def test_pending_result_with_hostapd_running_is_up(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_ENABLE_OUT="(uint32 9,)",
                           FAKE_PS_OUT="systemd\nhostapd\n")
        self.assertEqual(proc.returncode, UP, proc.stdout)

    def test_accepted_result_without_hostapd_is_pending(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_ENABLE_OUT="(uint32 0,)")
        self.assertEqual(proc.returncode, PENDING, proc.stdout)
        self.assertIn("hostapd not observed", proc.stdout)

    def test_ambiguous_reply_is_not_retried(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_ENABLE_RC="124", FAKE_ENABLE_OUT="")
        self.assertEqual(proc.returncode, UNKNOWN, proc.stdout)
        self.assertEqual(len(self.sb.enable_calls()), 1,
                         "an ambiguous call must not be repeated")

    def test_unreadable_reply_is_unknown_not_ready(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_ENABLE_OUT="something else entirely")
        self.assertEqual(proc.returncode, UNKNOWN, proc.stdout)
        self.assertEqual(len(self.sb.enable_calls()), 1)

    # --------------------------------------------------------- duplicate AP
    def test_running_hostapd_skips_the_request_entirely(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_PS_OUT_BEFORE="systemd\nhostapd\n")
        self.assertEqual(proc.returncode, UP, proc.stdout)
        self.assertEqual(self.sb.enable_calls(), [],
                         "a working radio must not be asked again")
        self.assertEqual(self.sb.calls(), [],
                         "nothing at all should be asked of the bus")
        self.assertIn("already running", proc.stdout)

    def test_running_hostapd_wins_over_a_bad_passphrase(self):
        self.sb.write_conf()
        self.sb.write_psk(SYNTH_HEX_PSK)
        proc = self.sb.run(FAKE_PS_OUT_BEFORE="systemd\nhostapd\n")
        self.assertEqual(proc.returncode, UP, proc.stdout)
        self.assertEqual(self.sb.calls(), [])

    # ------------------------------------------------- unreadable state
    def test_ps_failure_refuses_before_any_bus_call(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_PS_BREAK_BEFORE="1")
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertEqual(self.sb.calls(), [],
                         "an unreadable process list must stop the helper")
        self.assertIn("process list", proc.stdout)

    def test_unreadable_state_after_the_request_is_unknown(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_PS_BREAK_AFTER="1")
        self.assertEqual(proc.returncode, UNKNOWN, proc.stdout)
        self.assertIn("could not be read", proc.stdout)

    # ------------------------------------------------- the read-only probe
    def test_probe_failure_stops_before_the_request(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_PROBE_RC="1")
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertEqual(self.sb.enable_calls(), [],
                         "the request must not go into a silent interface")

    def test_probe_timeout_stops_before_the_request(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_PROBE_RC="124")
        self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
        self.assertEqual(self.sb.enable_calls(), [])

    def test_probe_uses_a_gvariant_string_argument(self):
        self.sb.write_conf()
        self.sb.write_psk()
        self.sb.run(FAKE_PS_OUT="systemd\nhostapd\n")
        probes = [c for c in self.sb.calls() if any("GetAll" in a for a in c)]
        self.assertTrue(probes, "the read-only probe must be made")
        self.assertIn("'org.tizen.tethering'", probes[0])
        self.assertNotIn("string:org.tizen.tethering", probes[0])

    # ------------------------------------------------- the controls
    def test_invalid_controls_are_refused_before_any_bus_call(self):
        for name, value in (("AP_TIMEOUT_SECS", "0"), ("AP_TIMEOUT_SECS", "-1"),
                            ("AP_TIMEOUT_SECS", "999999"),
                            ("AP_TIMEOUT_SECS", "abc"),
                            ("AP_BUS_WAIT_SECS", "0"), ("AP_BUS_WAIT_SECS", "-1"),
                            ("AP_BUS_WAIT_SECS", "999999"),
                            ("AP_BUS_WAIT_SECS", "abc")):
            with self.subTest(control=name, value=value):
                sb = Sandbox()
                self.addCleanup(sb.cleanup)
                sb.write_conf()
                sb.write_psk()
                proc = sb.run(**{name: value})
                self.assertEqual(proc.returncode, PREFLIGHT, proc.stdout)
                self.assertEqual(sb.calls(), [])
                self.assertIn(name, proc.stdout)

    # ------------------------------------------------- the reply
    def test_malformed_reply_is_unknown(self):
        for reply in ("uint32 0", "warning: uint32 0", "(uint32 0,", "(uint32 ,)"):
            with self.subTest(reply=reply):
                sb = Sandbox()
                self.addCleanup(sb.cleanup)
                sb.write_conf()
                sb.write_psk()
                proc = sb.run(FAKE_ENABLE_OUT=reply)
                self.assertEqual(proc.returncode, UNKNOWN, proc.stdout)
                self.assertEqual(len(sb.enable_calls()), 1)

    def test_multiple_uint32_fields_are_unknown(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_ENABLE_OUT="(uint32 0, uint32 1,)")
        self.assertEqual(proc.returncode, UNKNOWN, proc.stdout)
        self.assertEqual(len(self.sb.enable_calls()), 1)

    def test_lost_reply_with_hostapd_running_is_not_reported_as_up(self):
        self.sb.write_conf()
        self.sb.write_psk()
        proc = self.sb.run(FAKE_ENABLE_RC="124", FAKE_ENABLE_OUT="",
                           FAKE_PS_OUT="systemd\nhostapd\n")
        self.assertEqual(proc.returncode, UNKNOWN, proc.stdout)
        self.assertNotEqual(proc.returncode, UP)
        self.assertIn("hostapd is running", proc.stdout)
        self.assertEqual(len(self.sb.enable_calls()), 1)

    # ------------------------------------------------------------ no effects
    def test_helper_runs_no_service_or_boot_command(self):
        forbidden = {
            "systemctl", "service", "kill", "killall", "pkill", "rm", "mv",
            "reboot", "halt", "shutdown", "telnetd", "ftpd", "tcpsvd", "httpd",
            "insmod", "modprobe", "tee", "dd", "chmod", "chown", "mkdir",
        }
        for line in SCRIPT.read_text().splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            first = stripped.split()[0].split("=")[0]
            self.assertNotIn(first, forbidden,
                             "helper must not run %r" % first)

    def test_helper_defaults_are_the_camera_paths(self):
        text = SCRIPT.read_text()
        self.assertIn("/opt/var/lib/mobileap-agent/hostapd.conf", text)
        self.assertIn("/opt/storage/sdcard/mods/ap-passphrase", text)

    def test_helper_creates_nothing_besides_the_log(self):
        self.sb.write_conf()
        self.sb.write_psk()
        before = {p.name for p in self.sb.root.iterdir()}
        self.sb.run(FAKE_PS_OUT="systemd\nhostapd\n")
        after = {p.name for p in self.sb.root.iterdir()}
        self.assertEqual(after - before, {"ap-boot.log"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
