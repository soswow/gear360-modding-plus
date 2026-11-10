#!/usr/bin/env python3
"""The battery, charger and Wi-Fi sound rules (alerts.c) and the station count (wifi.c)."""
import ctypes as C
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parents[1]
RE = HERE.parent / 'lib'
OFF, HOME, AP = 0, 1, 2
LOW, NO, CONN, DISC, CHARGER = 1, 2, 3, 4, 5


class Alerts(C.Structure):
    _fields_ = [('started', C.c_int), ('level', C.c_int), ('cable', C.c_int), ('mode', C.c_int), ('link', C.c_int)]


class Input(C.Structure):
    _fields_ = [('battery', C.c_int), ('cable', C.c_int), ('wifi', C.c_int), ('clients', C.c_int)]


def main():
    with tempfile.TemporaryDirectory(prefix='g360-alerts-') as td:
        work = Path(td)
        library = work / 'alerts.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1',
                        '-I' + str(HERE), '-I' + str(RE / 'capture-session'), '-I' + str(RE / 'g360display'),
                        '-shared', '-fPIC', str(HERE / 'alerts.c'), str(HERE / 'wifi.c'), '-o', str(library)], check=True)
        lib = C.CDLL(str(library))
        lib.g360_alerts_step.argtypes = [C.POINTER(Alerts), C.POINTER(Input), C.POINTER(C.c_int)]
        lib.g360_alert_file.restype = C.c_char_p
        lib.g360_wifi_ap_clients.argtypes = [C.c_char_p]

        a = Alerts()
        lib.g360_alerts_init(C.byref(a))

        def step(battery=87, cable=0, wifi=OFF, clients=-1):
            out = (C.c_int * 8)()
            n = lib.g360_alerts_step(C.byref(a), C.byref(Input(battery, cable, wifi, clients)), out)
            return list(out[:n])

        # Battery: 20% once, 10% three times, nothing more while it stays or rises.
        assert step(87) == []
        assert step(21) == []
        assert step(20) == [LOW]
        assert step(20) == [] and step(15) == [] and step(11) == []
        assert step(10) == [NO, NO, NO]
        assert step(10) == [] and step(3) == [] and step(0) == []
        assert step(-1) == [], 'unreadable: nothing, level kept'
        assert step(5) == []
        # A charger silences it; unplugging at a low level warns again.
        assert step(5, cable=1) == [CHARGER]
        assert step(5, cable=1) == [] and step(4, cable=1) == []
        assert step(4, cable=0) == [NO, NO, NO]
        assert step(30, cable=0) == [] and step(18) == [LOW]
        assert step(18, cable=1) == [CHARGER] and step(18, cable=0) == [LOW]
        # 10% reached straight from above 20%: only the stronger warning.
        assert step(50) == [] and step(9) == [NO, NO, NO]
        # Climbing from 10% to the low band and back down to 10%: low band is not worse than 10%.
        assert step(15) == [] and step(10) == [NO, NO, NO]

        # First reading: already low warns; a charger already in is silent; home network announces itself.
        lib.g360_alerts_init(C.byref(a))
        assert step(8, cable=0, wifi=HOME) == [CONN, NO, NO, NO]
        lib.g360_alerts_init(C.byref(a))
        assert step(8, cable=1) == []
        assert step(8, cable=1) == []
        lib.g360_alerts_init(C.byref(a))
        assert step(18) == [LOW]

        # Wi-Fi.
        lib.g360_alerts_init(C.byref(a))
        assert step(wifi=OFF) == []
        assert step(wifi=HOME) == [CONN]
        assert step(wifi=HOME) == []
        assert step(wifi=OFF) == [DISC]
        assert step(wifi=OFF) == []
        assert step(wifi=AP, clients=0) == [], 'the access point alone is not a link'
        assert step(wifi=AP, clients=-1) == [], 'unknown keeps the count'
        assert step(wifi=AP, clients=1) == [CONN]
        assert step(wifi=AP, clients=-1) == [], 'unknown keeps the count'
        assert step(wifi=AP, clients=1) == []
        assert step(wifi=AP, clients=2) == [CONN]
        assert step(wifi=AP, clients=1) == [DISC]
        assert step(wifi=AP, clients=0) == [DISC]
        assert step(wifi=AP, clients=1) == [CONN]
        assert step(wifi=OFF) == [DISC], 'the access point stopped with a station on it'
        assert step(wifi=AP, clients=1) == [CONN]
        assert step(wifi=HOME) == [DISC, CONN], 'access point to home network'
        assert step(wifi=AP, clients=0) == [DISC], 'home network to an empty access point'
        assert step(wifi=7) == [], 'an unknown mode is no network'

        # Sound files.
        assert [lib.g360_alert_file(k) for k in (LOW, NO, CONN, DISC, CHARGER)] == [
            b'low_battery.wav', b'13_N_NoBattery.wav', b'BT_connected.wav', b'BT_disconnected.wav', b'charger_connect.wav']
        assert lib.g360_alert_file(0) is None and lib.g360_alert_file(9) is None

        # The station count, from a stand-in for `wl`.
        def tool(body, name='wl'):
            path = work / name
            path.write_text('#!/bin/sh\n' + body)
            path.chmod(0o755)
            return os.fsencode(path)
        assert lib.g360_wifi_ap_clients(tool('exit 0\n')) == 0
        assert lib.g360_wifi_ap_clients(tool('echo "assoclist 00:11:22:33:44:55"\n')) == 1
        assert lib.g360_wifi_ap_clients(tool('echo "assoclist 00:11:22:33:44:55"; echo "assoclist 66:77:88:99:AA:BB"\n')) == 2
        assert lib.g360_wifi_ap_clients(tool('echo "something else"; echo "assoclist AA"\n')) == 1
        assert lib.g360_wifi_ap_clients(tool('echo "assoclist 00:11:22:33:44:55"; exit 1\n')) == -1, 'failed tool'
        assert lib.g360_wifi_ap_clients(tool('echo "assoclist 00:11:22:33:44:55" >&2\n')) == 0, 'standard error is not read'
        assert lib.g360_wifi_ap_clients(tool('kill -9 $$\n')) == -1, 'killed tool'
        assert lib.g360_wifi_ap_clients(os.fsencode(work / 'missing')) == -1
        assert lib.g360_wifi_ap_clients(None) == -1
        big = 'i=0; while [ $i -lt 400 ]; do echo "assoclist 00:11:22:33:44:55"; i=$((i+1)); done\n'
        assert lib.g360_wifi_ap_clients(tool(big)) > 0, 'a long list does not block the tool'
    print('alerts: 20% and 10% warnings, charger silence and re-warning, charger sound, home network and access point '
          'station links, modes changing; station count from wl passed')


if __name__ == '__main__':
    main()
