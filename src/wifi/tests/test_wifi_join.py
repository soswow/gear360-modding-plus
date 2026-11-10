"""Host-side tests for wifi-join.sh with stand-ins for the camera's tools.

Each test builds a fake camera: stub gdbus (the access point agent), ip,
ifconfig, wl, wpa_supplicant, wpa_cli, udhcpc and the app
launcher, plus a fake /proc that names real sleeping processes. The script
under test is the real one, run with /bin/sh (bash 3.2, as on the camera).
"""

import os
import shutil
import signal
import subprocess
import tempfile
import textwrap
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE.parent / "wifi-join.sh"

SSID = "Home Net 2.4"
PASSWORD = "s3cret-pass"
AP_SSID = "Gear 360(ABCD)"
AP_PASSWORD = "camerapw"

STUBS = {
    "gdbus": r"""
        S=$G360_TEST_STATE
        method= settings=
        while [ $# -gt 0 ]; do
            case $1 in
            call|--system) shift ;;
            --method) method=${2##*.}; shift 2 ;;
            --timeout|--dest|--object-path) shift 2 ;;
            *) settings=$1; shift ;;
            esac
        done
        echo "$method $settings" >>"$S/gdbus.calls"
        case $method in
        disable_wifi_ap|disable_wifi_tethering)
            if [ -f "$S/hostapd.pid" ] && [ ! -f "$S/ap-stays-on" ]; then
                kill "$(cat "$S/hostapd.pid")"
                rm -f "$S/hostapd.pid" "$S/ap"
                # Like the real agent: its config is gone while the AP is off.
                rm -f "$G360_WIFI_AGENT_CONF"
            fi
            echo '(uint32 1, uint32 0)' ;;
        enable_wifi_ap)
            if [ -f "$S/ap-restore-fails" ]; then echo '(uint32 1, uint32 3)'; exit 0; fi
            pid=$(sh -c 'sleep 600 >/dev/null 2>&1 & echo $!')
            mkdir -p "$G360_WIFI_PROC/$pid"
            echo hostapd >"$G360_WIFI_PROC/$pid/comm"
            echo "$pid" >"$S/hostapd.pid"
            touch "$S/ap"
            ssid=$(echo "$settings" | sed -n "s/.*'ssid': <'\([^']*\)'>.*/\1/p")
            printf 'interface=wlan0\nssid=%s\nchannel=6\n' "$ssid" >"$G360_WIFI_AGENT_CONF"
            echo '(uint32 1, uint32 0)' ;;
        esac
    """,
    "ip": r"""
        S=$G360_TEST_STATE
        if [ "$*" = "addr show dev wlan0" ]; then
            [ -f "$S/ap" ] && echo '    inet 192.168.43.1/24 brd 192.168.43.255 scope global wlan0'
            exit 0
        fi
        echo "ip $*" >>"$S/net.calls"
    """,
    "ifconfig": r"""
        echo "ifconfig $*" >>"$G360_TEST_STATE/net.calls"
        if [ "$*" = "wlan0 up" ] && [ "$(cat "$G360_TEST_STATE/scenario")" = hang ]; then
            sleep 20
        fi
    """,
    "wl": r"""
        case $1 in
        ver) echo 7.10; echo 'wl0: test station firmware' ;;
        country) echo 'US (US/0) UNITED STATES' ;;
        scan) echo "wl $*" >>"$G360_TEST_STATE/net.calls" ;;
        scanresults)
            m="$G360_TEST_STATE/scan-misses"
            if [ -s "$m" ] && [ "$(cat "$m")" -gt 0 ]; then
                echo $(( $(cat "$m") - 1 )) >"$m"
                exit 0
            fi
            cat "$G360_TEST_STATE/scanresults" 2>/dev/null ;;
        esac
        exit 0
    """,
    "wpa_supplicant": r"""
        S=$G360_TEST_STATE
        conf= pidf= logf=
        while [ $# -gt 0 ]; do
            case $1 in
            -c) conf=$2; shift 2 ;;
            -P) pidf=$2; shift 2 ;;
            -f) logf=$2; shift 2 ;;
            -i|-D) shift 2 ;;
            *) shift ;;
            esac
        done
        echo $(( $(cat "$S/wpa.count" 2>/dev/null || echo 0) + 1 )) >"$S/wpa.count"
        cp "$conf" "$S/wpa.conf.seen"
        sed -n 's/^[[:space:]]*ssid="\(.*\)"/\1/p' "$conf" >>"$S/attempts"
        ls -l "$conf" | awk '{ print substr($1, 1, 10) }' >"$S/wpa.conf.mode"
        ls /dev/fd >"$S/wpa.fds" 2>/dev/null
        pid=$(sh -c 'sleep 600 >/dev/null 2>&1 & echo $!')
        mkdir -p "$G360_WIFI_PROC/$pid"
        echo wpa_supplicant >"$G360_WIFI_PROC/$pid/comm"
        echo "$pid" >"$pidf"
        echo "$pid" >"$S/wpa.realpid"
        case $(cat "$S/scenario") in
        firstwrong) [ "$(cat "$S/wpa.count")" = 1 ] && echo "reason=WRONG_KEY" >"$logf" || echo started >"$logf" ;;
        wrongkey) echo '1.0: wlan0: CTRL-EVENT-SSID-TEMP-DISABLED id=0 auth_failures=1 duration=10 reason=WRONG_KEY' >"$logf" ;;
        *) echo '1.0: wlan0: started' >"$logf" ;;
        esac
    """,
    "wpa_cli": r"""
        S=$G360_TEST_STATE
        cmd=
        while [ $# -gt 0 ]; do
            case $1 in -p|-i) shift 2 ;; *) cmd=$1; shift ;; esac
        done
        kill -0 "$(cat "$S/wpa.realpid" 2>/dev/null)" 2>/dev/null || exit 1
        ssid=$(head -n 1 "$G360_WIFI_CARD/wifi")
        case $cmd in
        status)
            if [ -f "$S/wpa_state" ]; then
                echo "wpa_state=$(cat "$S/wpa_state")"; exit 0
            fi
            case $(cat "$S/scenario") in
            stuckonce) [ "$(cat "$S/wpa.count")" = 1 ] && echo wpa_state=SCANNING || echo wpa_state=COMPLETED ;;
            firstwrong) [ "$(cat "$S/wpa.count")" = 1 ] && echo wpa_state=4WAY_HANDSHAKE || echo wpa_state=COMPLETED ;;
            wrongkey) echo wpa_state=4WAY_HANDSHAKE ;;
            notfound) echo wpa_state=SCANNING ;;
            seen) echo wpa_state=ASSOCIATING ;;
            *) echo wpa_state=COMPLETED ;;
            esac
            echo "ssid=$ssid"; echo freq=2437; echo key_mgmt=WPA2-PSK ;;
        scan_results)
            echo 'bssid / frequency / signal level / flags / ssid'
            [ "$(cat "$S/scenario")" = seen ] &&
                printf '00:11:22:33:44:55\t2437\t-50\t[WPA2-PSK-CCMP][ESS]\t%s\n' "$ssid" ;;
        esac
        exit 0
    """,
    "udhcpc": r"""
        S=$G360_TEST_STATE
        script=
        while [ $# -gt 0 ]; do
            case $1 in -s) script=$2; shift 2 ;; -i|-t|-T|-H) shift 2 ;; *) shift ;; esac
        done
        ls /dev/fd >"$S/udhcpc.fds" 2>/dev/null
        if [ "$(cat "$S/scenario")" = nodhcp ]; then sleep 1; exit 1; fi
        mkdir -p "$G360_WIFI_PROC/$$"
        echo udhcpc >"$G360_WIFI_PROC/$$/comm"
        interface=wlan0 ip=192.168.1.50 subnet=255.255.255.0 router=192.168.1.1 \
            dns=1.1.1.1 lease=86400 sh "$script" bound
        exec sleep 600
    """,
    "setsid": r"""exec "$@" """,
    "vconftool": r"""
        [ "$*" = "get db/setting/device_name" ] || exit 1
        echo "db/setting/device_name, value = $(cat "$G360_TEST_STATE/device_name") (string)"
    """,
    "flock": "exit 0",
    "id": "echo 0",
    "sync": "exit 0",
}

APP_START = r"""echo "started skip=${G360_WIFI_SKIP_APPLY:-}" >>"$G360_TEST_STATE/app-started" """


def write_exec(path: Path, body: str) -> None:
    path.write_text("#!/bin/sh\n" + textwrap.dedent(body).strip() + "\n")
    path.chmod(0o755)


class Camera:
    """A fake camera in a temporary directory."""

    def __init__(self, scenario="ok", password=PASSWORD, **timings):
        self.root = Path(tempfile.mkdtemp(prefix="g360wifi-"))
        r = self.root
        for d in ("bin", "card/mods", "run", "proc", "dhd", "locks", "state", "fw"):
            (r / d).mkdir(parents=True)
        for name, body in STUBS.items():
            write_exec(r / "bin" / name, body)
        write_exec(r / "app-start.sh", APP_START)
        (r / "card/wifi").write_text(f"{SSID}\n{password}")
        (r / "card/mods/ap-passphrase").write_text(AP_PASSWORD)
        (r / "hostapd.conf").write_text(
            f"interface=wlan0\nssid={AP_SSID}\nchannel=6\nhw_mode=g\nwpa=2\n")
        (r / "fw/wlan_net.bin_blob").write_text("fw")
        (r / "fw/nvram.txt").write_text("nv")
        (r / "dhd/firmware_path").write_text("/lib/firmware/wlan_apsta_bcm43455.bin\n")
        (r / "dhd/nvram_path").write_text("/opt/etc/nvram/nvram_net_bcm43455.txt\n")
        (r / "proc/stat").write_text("btime 1793781685\n")
        (r / "state/scenario").write_text(scenario)
        (r / "state/device_name").write_text(AP_SSID)
        self.env = {
            "PATH": f"{r}/bin:/usr/bin:/bin",
            "HOME": str(r),
            "G360_TEST_STATE": str(r / "state"),
            "G360_WIFI_PATH": f"{r}/bin:/usr/bin:/bin",
            "G360_WIFI_CARD": str(r / "card"),
            "G360_WIFI_RUN": str(r / "run"),
            "G360_WIFI_PROC": str(r / "proc"),
            "G360_WIFI_DHD": str(r / "dhd"),
            "G360_WIFI_LOCKS": str(r / "locks"),
            "G360_WIFI_AGENT_CONF": str(r / "hostapd.conf"),
            "G360_WIFI_APP_START": str(r / "app-start.sh"),
            "G360_WIFI_LOG": str(r / "state/wifi.log"),
            "G360_WIFI_AP_CACHE": str(r / "state/ap-settings"),
            "G360_WIFI_FW": str(r / "fw/wlan_net.bin"),
            "G360_WIFI_NVRAM": str(r / "fw/nvram.txt"),
            "G360_WIFI_DETACH_DELAY": "0",
            "G360_WIFI_APP_STOP_WAIT": "5",
            "G360_WIFI_JOIN_WAIT": "3",
            "G360_WIFI_DHCP_WAIT": "5",
            "G360_WIFI_HOST_WAIT": "30",
            "G360_WIFI_AP_WAIT": "3",
            "G360_WIFI_HOLD": "0",
            "G360_WIFI_FAIL_HOLD": "0",
            "G360_WIFI_WATCH_LIMIT": "120",
            "G360_WIFI_WATCH_POLL": "1",
            "G360_WIFI_SCAN_WAIT": "0",
            "G360_WIFI_APP_JOIN_WAIT": "3",
            "G360_WIFI_APPLY_WAIT": "20",
            "G360_WIFI_RETRY_AFTER": "1",
            "G360_WIFI_KEEP_POLL": "1",
            "G360_WIFI_KEEP_LOST": "2",
            "G360_WIFI_AP_SETTLE": "1",
        }
        self.env.update({f"G360_WIFI_{k.upper()}": str(v) for k, v in timings.items()})
        self.pids = []

    # Fake processes named in the fake /proc.
    def spawn(self, comm: str) -> int:
        out = subprocess.run(["sh", "-c", "sleep 600 >/dev/null 2>&1 & echo $!"],
                             capture_output=True, text=True, check=True)
        pid = int(out.stdout)
        (self.root / "proc" / str(pid)).mkdir()
        (self.root / "proc" / str(pid) / "comm").write_text(comm + "\n")
        self.pids.append(pid)
        return pid

    def in_range(self):
        self.state("scanresults").write_text(
            'SSID: "Neighbour"\nMode: Managed\tRSSI: -80 dBm\tChannel: 6\n'
            f'SSID: "{SSID}"\nMode: Managed\tRSSI: -62 dBm\tChannel: 3\n')

    def keeper_pid(self):
        p = self.root / "run/keeper.pid"
        return int(p.read_text()) if p.exists() else None

    def ap_up(self):
        pid = self.spawn("hostapd")
        self.state("hostapd.pid").write_text(str(pid))
        self.state("ap").touch()

    def run(self, *args, check=True):
        result = subprocess.run(["/bin/sh", str(SCRIPT), *args], env=self.env,
                                capture_output=True, text=True, timeout=60)
        if check and result.returncode != 0:
            raise AssertionError(f"{args} failed: {result.stdout}{result.stderr}")
        return result

    def state(self, name) -> Path:
        return self.root / "state" / name

    def text(self, name) -> str:
        p = self.state(name)
        return p.read_text() if p.exists() else ""

    def wait_for(self, predicate, timeout=40.0, what="condition"):
        end = time.time() + timeout
        while time.time() < end:
            if predicate():
                return
            time.sleep(0.2)
        raise AssertionError(f"timed out waiting for {what}\nlog:\n{self.text('wifi.log')}")

    def wait_finished(self, runs=1):
        self.wait_for(lambda: self.text("wifi.log").count("run finished") >= runs,
                      what=f"{runs} finished run(s)")

    def alive(self, pid) -> bool:
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False

    def close(self):
        run = self.root / "run"
        for name in ("watchdog.pid", "udhcpc.pid", "wpa.pid", "keeper.pid"):
            p = run / name
            if p.exists() and p.read_text().strip().isdigit():
                self.pids.append(int(p.read_text()))
        for p in self.root.glob("proc/[0-9]*"):
            self.pids.append(int(p.name))
        for name in ("hostapd.pid", "wpa.realpid"):
            p = self.state(name)
            if p.exists() and p.read_text().strip().isdigit():
                self.pids.append(int(p.read_text()))
        for pid in set(self.pids):
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
        shutil.rmtree(self.root, ignore_errors=True)


class WifiJoinTest(unittest.TestCase):
    def camera(self, *args, **kwargs) -> Camera:
        cam = Camera(*args, **kwargs)
        self.addCleanup(cam.close)
        return cam

    def assertNoSecrets(self, cam, *texts):
        for t in (cam.text("wifi.log"), *texts):
            for secret in (SSID, PASSWORD, AP_PASSWORD):
                self.assertNotIn(secret, t)

    def assertAppStarts(self, cam, n):
        """The relaunch runs in the background just before "run finished"."""
        cam.wait_for(lambda: cam.text("app-started").count("started") >= n,
                     timeout=10, what=f"{n} app start(s)")
        time.sleep(0.5)
        self.assertEqual(cam.text("app-started").count("started"), n)

    def assertApRestored(self, cam):
        calls = cam.text("gdbus.calls")
        self.assertIn("enable_wifi_ap", calls)
        self.assertIn(f"'ssid': <'{AP_SSID}'>", calls)
        self.assertIn(f"'key': <'{AP_PASSWORD}'>", calls)
        self.assertIn("'channel': <int32 6>", calls)
        self.assertTrue(cam.state("ap").exists())
        self.assertIn("access point back (try", cam.text("wifi.log"))
        self.assertFalse((cam.root / "run/wpa.conf").exists())
        wpa = cam.text("wpa.realpid").strip()
        if wpa:
            self.assertFalse(cam.alive(int(wpa)), "wpa_supplicant left running")
        self.assertIn("ifconfig wlan0 down", cam.text("net.calls"))

    def test_check_passes_without_printing_secrets(self):
        cam = self.camera()
        cam.ap_up()
        out = cam.run("check")
        self.assertIn("check passed", out.stdout)
        self.assertIn("the access point is up", out.stdout)
        self.assertIn("key: passphrase", out.stdout)
        self.assertNoSecrets(cam, out.stdout, out.stderr)
        self.assertEqual(cam.text("gdbus.calls"), "")

    def test_join_refuses_a_short_password_and_changes_nothing(self):
        cam = self.camera(password="short")
        cam.ap_up()
        cam.spawn("g360-app")
        out = cam.run("join", check=False)
        self.assertNotEqual(out.returncode, 0)
        self.assertIn("the password is 5 characters", out.stdout)
        time.sleep(1)
        self.assertEqual(cam.text("gdbus.calls"), "")
        self.assertTrue(all(cam.alive(p) for p in cam.pids))

    def test_join_success_then_revert(self):
        cam = self.camera()
        cam.ap_up()
        app = cam.spawn("g360-app")
        out = cam.run("join")
        self.assertIn("started join", out.stdout)
        cam.wait_for(lambda: "address 192.168.1.50" in cam.text("wifi.log"), what="the address")
        self.assertFalse(cam.alive(app), "g360-app was not paused")
        cam.run("host-ok")
        cam.wait_finished()
        log = cam.text("wifi.log")
        self.assertIn("staying on the network at 192.168.1.50", log)
        self.assertNotIn("enable_wifi_ap", cam.text("gdbus.calls"))
        self.assertIn("disable_wifi_ap", cam.text("gdbus.calls"))
        self.assertEqual((cam.root / "dhd/firmware_path").read_text().strip(),
                         str(cam.root / "fw/wlan_net.bin"))
        conf = cam.text("wpa.conf.seen")
        self.assertIn(f'\tssid="{SSID}"\n', conf)
        self.assertIn(f'\tpsk="{PASSWORD}"\n', conf)
        self.assertEqual(cam.text("wpa.conf.mode").strip(), "-rw-------")
        self.assertFalse((cam.root / "run/wpa.conf").exists(), "password left on disk")
        self.assertTrue(cam.alive(int(cam.text("wpa.realpid"))))
        for name in ("wpa.fds", "udhcpc.fds"):
            fds = set(cam.text(name).split())
            self.assertTrue(fds, f"{name} not recorded")
            self.assertFalse(fds & {"7", "8", "9"}, f"{name} inherited a lock: {fds}")
        self.assertIn("ip route replace default via 192.168.1.1 dev wlan0", cam.text("net.calls"))
        self.assertAppStarts(cam, 1)
        self.assertFalse((cam.root / "run/active").exists())
        self.assertNoSecrets(cam, out.stdout)

        status = cam.run("status").stdout
        self.assertIn("wpa_state: COMPLETED", status)
        self.assertIn("ip=192.168.1.50", status)

        self.assertFalse((cam.root / "hostapd.conf").exists())
        self.assertEqual(cam.text("ap-settings"), f"{AP_SSID}\n6\n")
        cam.spawn("g360-app")  # what the real start.sh would have started
        cam.run("revert")
        cam.wait_finished(runs=2)
        self.assertApRestored(cam)
        self.assertAppStarts(cam, 2)
        status = cam.run("status").stdout
        self.assertIn("access point: up", status)
        self.assertNotIn("ip=", status)

    def assertFallsBack(self, scenario, reason, **timings):
        cam = self.camera(scenario, **timings)
        cam.ap_up()
        cam.spawn("g360-app")
        cam.run("join")
        cam.wait_finished()
        self.assertIn(f"join failed: {reason}", cam.text("wifi.log"))
        self.assertApRestored(cam)
        # The relaunch runs in the background just before "run finished".
        self.assertAppStarts(cam, 1)
        self.assertNoSecrets(cam)
        return cam

    def test_wrong_password_falls_back(self):
        self.assertFallsBack("wrongkey", "WRONG PASS")

    def test_network_not_found_falls_back(self):
        self.assertFallsBack("notfound", "NOT FOUND")

    def test_seen_but_not_joined_falls_back(self):
        cam = self.assertFallsBack("seen", "NO JOIN")
        self.assertIn("flags=[WPA2-PSK-CCMP][ESS]", cam.text("wifi.log"))

    def test_no_address_falls_back(self):
        self.assertFallsBack("nodhcp", "NO ADDRESS")

    def test_no_host_falls_back(self):
        self.assertFallsBack("ok", "NO HOST", host_wait=2)

    def test_ap_that_does_not_return_asks_for_reboot_and_restarts_app(self):
        cam = self.camera("wrongkey")
        cam.ap_up()
        cam.spawn("g360-app")
        cam.state("ap-restore-fails").touch()
        cam.run("join")
        cam.wait_finished()
        self.assertIn("THE ACCESS POINT DID NOT COME BACK", cam.text("wifi.log"))
        self.assertEqual(cam.text("gdbus.calls").count("enable_wifi_ap"), 2)
        # The app comes back so that holding Power can still turn the camera off.
        self.assertAppStarts(cam, 1)

    def test_ap_that_stays_on_changes_nothing(self):
        cam = self.camera()
        cam.ap_up()
        cam.spawn("g360-app")
        cam.state("ap-stays-on").touch()
        cam.run("join")
        cam.wait_finished()
        self.assertIn("the access point did not go off; nothing changed", cam.text("wifi.log"))
        self.assertNotIn("enable_wifi_ap", cam.text("gdbus.calls"))
        self.assertEqual(cam.text("wpa.realpid"), "")
        self.assertAppStarts(cam, 1)

    def test_ap_cycle(self):
        cam = self.camera()
        cam.ap_up()
        cam.spawn("g360-app")
        cam.run("ap-cycle")
        cam.wait_finished()
        calls = cam.text("gdbus.calls").splitlines()
        self.assertEqual([c.split()[0] for c in calls], ["disable_wifi_ap", "enable_wifi_ap"])
        self.assertApRestored(cam)

    def test_watchdog_restores_after_a_dead_run(self):
        cam = self.camera()
        (cam.root / "run/app-was-running").write_text("1\n")
        env = dict(cam.env, G360_WIFI_DETACHED="1")
        subprocess.run(["/bin/sh", str(SCRIPT), "watchdog", "999999"], env=env,
                       capture_output=True, text=True, timeout=60, check=True)
        self.assertIn("ended without finishing", cam.text("wifi.log"))
        self.assertApRestored(cam)
        cam.wait_for(lambda: "started" in cam.text("app-started"), what="app restart")

    def test_watchdog_without_any_saved_settings_uses_the_device_name(self):
        cam = self.camera()
        (cam.root / "hostapd.conf").unlink()
        (cam.root / "run/app-was-running").write_text("0\n")
        env = dict(cam.env, G360_WIFI_DETACHED="1")
        subprocess.run(["/bin/sh", str(SCRIPT), "watchdog", "999999"], env=env,
                       capture_output=True, text=True, timeout=60, check=True)
        self.assertApRestored(cam)

    def test_stale_pid_file_does_not_kill_another_process(self):
        cam = self.camera()
        other = cam.spawn("dnsmasq")
        (cam.root / "run/wpa.pid").write_text(f"{other}\n")
        cam.run("revert")
        cam.wait_finished()
        self.assertTrue(cam.alive(other), "revert killed a process that was not ours")
        self.assertIn("is no longer wpa_supplicant", cam.text("wifi.log"))

    # --- apply off|ap|home: g360-app's WIFI setting ---

    def apply(self, cam, mode, check=True):
        out = cam.run("apply", mode, check=check)
        self.assertEqual(cam.text("app-started"), "", "apply must not start the app")
        self.assertFalse((cam.root / "run/active").exists())
        return out

    def test_apply_home_joins_when_in_range_then_keeper_falls_back(self):
        cam = self.camera()
        cam.ap_up()
        cam.in_range()
        self.apply(cam, "home")
        log = cam.text("wifi.log")
        self.assertIn("address 192.168.1.50", log)
        self.assertIn("apply home finished", log)
        self.assertIn("disable_wifi_ap", cam.text("gdbus.calls"))
        self.assertNotIn("enable_wifi_ap", cam.text("gdbus.calls"))
        for name in ("wpa.fds", "udhcpc.fds"):
            self.assertFalse(set(cam.text(name).split()) & {"7", "8", "9"}, name)
        cam.wait_for(lambda: cam.keeper_pid() and cam.alive(cam.keeper_pid()), what="keeper")
        self.assertNoSecrets(cam)
        # Leaving home: the keeper brings the access point back.
        cam.state("wpa_state").write_text("SCANNING")
        cam.wait_for(lambda: "keeper: no link" in cam.text("wifi.log"), what="keeper fallback")
        cam.wait_for(lambda: "access point back (try" in cam.text("wifi.log"), what="AP back")
        self.assertApRestored(cam)
        self.assertEqual(cam.text("app-started"), "")

    def test_multiple_networks_prioritise_and_retry_after_wrong_password(self):
        cam = self.camera("firstwrong")
        cam.ap_up()
        cam.in_range()
        (cam.root / "card/wifi").write_text(
            f"# first\r\n{SSID}\r\n{PASSWORD}\r\n\r\n# second\r\nBackup Net\r\nbackup-password\r\n")
        self.apply(cam, "home")
        self.assertEqual(cam.text("attempts").splitlines(), [SSID, "Backup Net"])
        self.assertIn("address 192.168.1.50", cam.text("wifi.log"))
        self.assertNotIn("enable_wifi_ap", cam.text("gdbus.calls"))
        self.assertNotIn("backup-password", cam.text("wifi.log"))

    def test_preferred_network_wins_when_both_available(self):
        cam = self.camera()
        cam.ap_up()
        cam.in_range()
        (cam.root / "card/wifi").write_text(f"{SSID}\n{PASSWORD}\nBackup Net\nbackup-password\n")
        self.apply(cam, "home")
        self.assertEqual(cam.text("attempts").splitlines(), [SSID])

    def test_scan_skips_unavailable_networks_before_third(self):
        cam = self.camera()
        cam.ap_up()
        cam.state("scanresults").write_text('SSID: "Third Net"\n')
        (cam.root / "card/wifi").write_text(
            "First Net\nfirst-password\nSecond Net\nsecond-password\nThird Net\nthird-password\n")
        self.apply(cam, "home")
        self.assertEqual(cam.text("attempts").splitlines(), ["Third Net"])
        self.assertIn("address 192.168.1.50", cam.text("wifi.log"))

    def test_scanned_candidate_failure_tries_later_network(self):
        cam = self.camera("firstwrong")
        cam.ap_up()
        cam.state("scanresults").write_text('SSID: "Second Net"\n')
        (cam.root / "card/wifi").write_text(
            "First Net\nfirst-password\nSecond Net\nsecond-password\nThird Net\nthird-password\n")
        self.apply(cam, "home")
        self.assertEqual(cam.text("attempts").splitlines(), ["Second Net", "Third Net"])
        self.assertIn("address 192.168.1.50", cam.text("wifi.log"))

    def test_all_networks_fail_then_restore_ap(self):
        cam = self.camera("wrongkey")
        cam.ap_up()
        cam.in_range()
        (cam.root / "card/wifi").write_text(f"{SSID}\n{PASSWORD}\nBackup Net\nbackup-password\n")
        self.apply(cam, "home", check=False)
        self.assertEqual(cam.text("attempts").splitlines(), [SSID, "Backup Net"])
        self.assertApRestored(cam)

    def test_manual_join_tries_second_network(self):
        cam = self.camera("firstwrong")
        cam.ap_up()
        (cam.root / "card/wifi").write_text(f"{SSID}\n{PASSWORD}\nBackup Net\nbackup-password\n")
        cam.run("join")
        cam.wait_for(lambda: "address 192.168.1.50" in cam.text("wifi.log"))
        cam.run("host-ok")
        cam.wait_finished()
        self.assertEqual(cam.text("attempts").splitlines(), [SSID, "Backup Net"])

    def test_incomplete_pairs_are_rejected_before_switching(self):
        cam = self.camera()
        cam.ap_up()
        (cam.root / "card/wifi").write_text(f"{SSID}\n{PASSWORD}\nMissing password\n")
        result = cam.run("check", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("incomplete pair", result.stdout)
        self.assertEqual(cam.text("gdbus.calls"), "")

    def test_apply_home_retries_a_missed_named_scan(self):
        cam = self.camera()
        cam.ap_up()
        cam.in_range()
        cam.state("scan-misses").write_text("1")
        self.apply(cam, "home")
        log = cam.text("wifi.log")
        self.assertIn("named scan 1: the network is not in the results", log)
        self.assertIn("named scan 2: the network is in range", log)
        self.assertIn("address 192.168.1.50", log)
        self.assertIn(f"wl scan -s {SSID}", cam.text("net.calls"))

    def test_search_indicator_covers_scan_and_connection_then_clears(self):
        cam = self.camera("notfound")
        cam.ap_up()
        cam.in_range()
        cam.env["G360_WIFI_SCAN_WAIT"] = "2"
        proc = subprocess.Popen(["/bin/sh", str(SCRIPT), "apply", "home"], env=cam.env)
        self.addCleanup(lambda: proc.poll() is None and proc.kill())
        marker = cam.root / "run/searching"
        cam.wait_for(lambda: marker.exists() and cam.state("ap").exists(), what="search during AP scan")
        cam.wait_for(lambda: "wpa_state SCANNING" in cam.text("wifi.log"), what="station connection")
        self.assertEqual(marker.read_text(), "1\n")
        self.assertEqual(proc.wait(30), 1)
        self.assertFalse(marker.exists())
        self.assertEqual((cam.root / "run/error").read_text(), "1\n")
        self.assertApRestored(cam)

    def test_apply_home_two_missed_scans_keep_the_ap(self):
        cam = self.camera()
        cam.ap_up()
        cam.in_range()
        cam.state("scan-misses").write_text("2")
        self.apply(cam, "home")
        self.assertIn("not in range; keeping the access point", cam.text("wifi.log"))
        self.assertEqual(cam.text("gdbus.calls"), "")

    def test_apply_home_starts_wpa_supplicant_again_when_the_first_stalls(self):
        cam = self.camera("stuckonce")
        cam.ap_up()
        cam.in_range()
        (cam.root / "run/wpa.log").write_text("an earlier attempt\n")
        self.apply(cam, "home")
        log = cam.text("wifi.log")
        self.assertIn("no join after 1s (NOT FOUND); starting wpa_supplicant again", log)
        self.assertIn("address 192.168.1.50", log)
        self.assertEqual(cam.text("wpa.count").strip(), "2")
        self.assertNotIn("enable_wifi_ap", cam.text("gdbus.calls"))
        # One wpa_supplicant left, the first one stopped; its log kept.
        self.assertTrue((cam.root / "run/wpa.log.prev").exists())
        self.assertNoSecrets(cam)

    def test_wrong_password_is_not_tried_twice(self):
        cam = self.camera("wrongkey")
        cam.ap_up()
        cam.in_range()
        self.apply(cam, "home", check=False)
        self.assertEqual(cam.text("wpa.count").strip(), "1")
        self.assertNotIn("starting wpa_supplicant again", cam.text("wifi.log"))

    def test_apply_home_keeps_the_ap_when_out_of_range(self):
        cam = self.camera()
        cam.ap_up()
        self.apply(cam, "home")
        self.assertIn("not in range; keeping the access point", cam.text("wifi.log"))
        self.assertEqual(cam.text("gdbus.calls"), "")
        self.assertTrue(cam.state("ap").exists())
        self.assertEqual(cam.text("wpa.realpid"), "")

    def test_apply_home_wrong_password_brings_the_ap_back(self):
        cam = self.camera("wrongkey")
        cam.ap_up()
        cam.in_range()
        out = self.apply(cam, "home", check=False)
        self.assertEqual(out.returncode, 1)
        self.assertIn("join failed: WRONG PASS", cam.text("wifi.log"))
        self.assertApRestored(cam)
        self.assertNoSecrets(cam)

    def test_apply_home_without_ap_tries_the_network_directly(self):
        cam = self.camera("notfound")
        out = self.apply(cam, "home", check=False)
        self.assertEqual(out.returncode, 1)
        self.assertIn("trying the network directly", cam.text("wifi.log"))
        self.assertEqual(cam.text("wpa.count").strip(), "2", "a network not found gets a second wpa_supplicant")
        self.assertApRestored(cam)

    def test_apply_home_waits_for_an_access_point_still_starting(self):
        cam = self.camera()
        cam.in_range()
        cam.env["G360_WIFI_AP_SETTLE"] = "8"
        # The stock app's request: the access point appears 2 s later.
        starter = textwrap.dedent(f"""
            sleep 2
            G360_TEST_STATE={cam.root}/state G360_WIFI_PROC={cam.root}/proc \\
            G360_WIFI_AGENT_CONF={cam.root}/hostapd.conf \\
            {cam.root}/bin/gdbus call --method x.enable_wifi_ap "{{'ssid': <'{AP_SSID}'>}}" >/dev/null
            rm -f {cam.root}/state/gdbus.calls
        """)
        starter_process = subprocess.Popen(["sh", "-c", starter], start_new_session=True)
        self.addCleanup(starter_process.wait, 10)
        self.apply(cam, "home")
        log = cam.text("wifi.log")
        self.assertIn("apply home: the access point came up", log)
        self.assertNotIn("trying the network directly", log)
        self.assertIn("address 192.168.1.50", log)
        self.assertIn("disable_wifi_ap", cam.text("gdbus.calls"))

    def test_apply_home_without_the_network_file_is_the_access_point(self):
        cam = self.camera()
        cam.ap_up()
        (cam.root / "card/wifi").unlink()
        self.apply(cam, "home")
        self.assertIn("the access point instead", cam.text("wifi.log"))
        self.assertIn("apply ap: the access point is up", cam.text("wifi.log"))
        self.assertEqual(cam.text("gdbus.calls") + cam.text("net.calls"), "")

    def test_apply_home_already_on_the_network_changes_nothing(self):
        cam = self.camera()
        cam.ap_up()
        cam.in_range()
        self.apply(cam, "home")
        cam.wait_for(lambda: cam.keeper_pid() and cam.alive(cam.keeper_pid()), what="keeper")
        keeper = cam.keeper_pid()
        calls = cam.text("gdbus.calls") + cam.text("net.calls")
        self.apply(cam, "home")
        self.assertIn("already on the network at 192.168.1.50", cam.text("wifi.log"))
        self.assertEqual(cam.text("gdbus.calls") + cam.text("net.calls"), calls)
        self.assertEqual(cam.keeper_pid(), keeper, "a second keeper was started")

    def test_apply_home_that_hangs_is_stopped_by_the_watchdog(self):
        cam = self.camera("hang", watch_limit=3)
        cam.ap_up()
        cam.in_range()
        started = time.time()
        # Not cam.run: the hanging stub would keep the output pipe open.
        run = subprocess.Popen(["/bin/sh", str(SCRIPT), "apply", "home"], env=cam.env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.assertNotEqual(run.wait(15), 0)
        self.assertLess(time.time() - started, 15)
        cam.wait_for(lambda: "still going after 3s" in cam.text("wifi.log"), what="watchdog")
        cam.wait_for(lambda: cam.state("ap").exists(), what="AP back")
        cam.wait_finished()
        self.assertEqual(cam.text("app-started"), "", "the watchdog must not start a second app")

    def test_apply_off_from_the_access_point(self):
        cam = self.camera()
        cam.ap_up()
        self.apply(cam, "off")
        self.assertEqual([c.split()[0] for c in cam.text("gdbus.calls").splitlines()], ["disable_wifi_ap"])
        self.assertFalse(cam.state("ap").exists())
        self.assertIn("ifconfig wlan0 down", cam.text("net.calls"))
        self.assertTrue((cam.root / "run/wifi-off").exists())
        self.assertIn("apply off finished", cam.text("wifi.log"))
        # Off again: nothing to do, and no wait for an access point.
        cam.env["G360_WIFI_AP_SETTLE"] = "8"
        started = time.time()
        self.apply(cam, "off")
        self.assertLess(time.time() - started, 5)
        self.assertEqual(len(cam.text("gdbus.calls").splitlines()), 1)

    def test_apply_off_from_the_home_network(self):
        cam = self.camera()
        cam.ap_up()
        cam.in_range()
        self.apply(cam, "home")
        cam.wait_for(lambda: cam.keeper_pid() and cam.alive(cam.keeper_pid()), what="keeper")
        keeper, wpa = cam.keeper_pid(), int(cam.text("wpa.realpid"))
        cam.env["G360_WIFI_AP_SETTLE"] = "8"
        started = time.time()
        self.apply(cam, "off")
        self.assertLess(time.time() - started, 5, "waited for an access point after leaving the network")
        self.assertIn("left the network", cam.text("wifi.log"))
        cam.wait_for(lambda: not cam.alive(keeper) and not cam.alive(wpa), what="keeper and wpa_supplicant gone")
        self.assertNotIn("enable_wifi_ap", cam.text("gdbus.calls"))
        self.assertFalse((cam.root / "run/ip").exists())

    def test_apply_off_waits_for_an_access_point_still_starting(self):
        cam = self.camera()
        cam.env["G360_WIFI_AP_SETTLE"] = "8"
        starter = textwrap.dedent(f"""
            sleep 2
            G360_TEST_STATE={cam.root}/state G360_WIFI_PROC={cam.root}/proc \\
            G360_WIFI_AGENT_CONF={cam.root}/hostapd.conf \\
            {cam.root}/bin/gdbus call --method x.enable_wifi_ap "{{'ssid': <'{AP_SSID}'>}}" >/dev/null
        """)
        starter_process = subprocess.Popen(["sh", "-c", starter], start_new_session=True)
        self.addCleanup(starter_process.wait, 10)
        self.apply(cam, "off")
        self.assertIn("apply off: the access point came up", cam.text("wifi.log"))
        self.assertIn("disable_wifi_ap", cam.text("gdbus.calls"))
        self.assertFalse(cam.state("ap").exists())

    def test_apply_ap_from_the_home_network_and_when_already_up(self):
        cam = self.camera()
        cam.ap_up()
        cam.in_range()
        self.apply(cam, "home")
        cam.wait_for(lambda: cam.keeper_pid() and cam.alive(cam.keeper_pid()), what="keeper")
        keeper = cam.keeper_pid()
        self.apply(cam, "ap")
        self.assertApRestored(cam)
        cam.wait_for(lambda: not cam.alive(keeper), what="keeper stopped")
        calls = cam.text("gdbus.calls")
        self.apply(cam, "ap")
        self.assertIn("apply ap: the access point is up", cam.text("wifi.log"))
        self.assertEqual(cam.text("gdbus.calls"), calls)

    def test_apply_after_off_does_not_wait_for_the_access_point(self):
        cam = self.camera("notfound")
        cam.ap_up()
        self.apply(cam, "off")
        cam.env["G360_WIFI_AP_SETTLE"] = "30"
        started = time.time()
        self.apply(cam, "ap")
        self.assertLess(time.time() - started, 15)
        self.assertTrue(cam.state("ap").exists())
        self.assertFalse((cam.root / "run/wifi-off").exists())
        self.apply(cam, "off")
        started = time.time()
        self.apply(cam, "home", check=False)   # not found: the access point again
        self.assertLess(time.time() - started, 25)
        self.assertIn("trying the network directly", cam.text("wifi.log"))
        self.assertTrue(cam.state("ap").exists())

    def test_a_newer_choice_replaces_one_still_waiting(self):
        cam = self.camera()
        cam.ap_up()
        (cam.root / "run/active").mkdir()   # a run in progress
        first = subprocess.Popen(["/bin/sh", str(SCRIPT), "apply", "off"], env=cam.env)
        time.sleep(0.5)
        second = subprocess.Popen(["/bin/sh", str(SCRIPT), "apply", "ap"], env=cam.env)
        time.sleep(0.5)
        (cam.root / "run/active").rmdir()
        self.assertEqual(first.wait(20), 0)
        self.assertEqual(second.wait(20), 0)
        log = cam.text("wifi.log")
        self.assertIn("apply off: replaced by a newer choice (ap)", log)
        self.assertIn("apply ap: the access point is up", log)
        self.assertEqual(cam.text("gdbus.calls"), "")
        self.assertFalse((cam.root / "run/active").exists())

    def test_apply_refuses_an_unknown_choice(self):
        cam = self.camera()
        out = cam.run("apply", "maybe", check=False)
        self.assertEqual(out.returncode, 2)
        self.assertEqual(cam.text("gdbus.calls"), "")

    def test_revert_stops_the_keeper_and_its_relaunch_skips_the_apply(self):
        cam = self.camera()
        cam.ap_up()
        cam.in_range()
        self.apply(cam, "home")
        cam.wait_for(lambda: cam.keeper_pid() and cam.alive(cam.keeper_pid()), what="keeper")
        keeper = cam.keeper_pid()
        cam.spawn("g360-app")
        cam.run("revert")
        cam.wait_finished()
        self.assertApRestored(cam)
        cam.wait_for(lambda: not cam.alive(keeper), what="keeper stopped")
        self.assertIn("started skip=1", cam.text("app-started"))

    def test_second_run_is_refused_while_one_is_active(self):
        cam = self.camera()
        cam.ap_up()
        (cam.root / "run/active").mkdir()
        out = cam.run("join", check=False)
        self.assertNotEqual(out.returncode, 0)
        self.assertIn("a run is in progress", out.stdout)


if __name__ == "__main__":
    unittest.main()
