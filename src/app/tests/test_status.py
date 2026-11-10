#!/usr/bin/env python3
"""The status corner's reader (status.c) against fake /sys and /proc trees."""
import ctypes as C
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parents[1]
RE = HERE.parent / 'lib'
OFF, HOME, AP = 0, 1, 2
HEADER = 'Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n'
# The camera on the home network and on its own access point.
HOME_ROUTES = HEADER + ('wlan0\t00000000\t0100A8C0\t0003\t0\t0\t0\t00000000\t0\t0\t0\n'
                        'wlan0\t0000A8C0\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n')
AP_ROUTES = HEADER + 'wlan0\t002BA8C0\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n'


class Status(C.Structure):
    _fields_ = [(name, C.c_int) for name in ('wifi', 'battery', 'charging', 'mode', 'timer',
                'charge_frame', 'raw', 'sound', 'wifi_searching', 'wifi_frame', 'wifi_error', 'ap_connected')]


def tree(root, files):
    if root.exists():
        shutil.rmtree(root)
    for name, text in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text)


def main():
    with tempfile.TemporaryDirectory(prefix='g360-status-') as td:
        work = Path(td)
        library = work / 'status.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1',
                        '-I' + str(HERE), '-I' + str(RE / 'capture-session'), '-I' + str(RE / 'g360display'),
                        '-shared', '-fPIC', str(HERE / 'status.c'), '-o', str(library)], check=True)
        root = work / 'root'

        def read(files, keep=False):
            if not keep:
                tree(root, files)
            lib = C.CDLL(str(library))
            lib.g360_status_read.argtypes = [C.c_char_p, C.POINTER(Status)]
            s = Status(-9, -9, -9)
            status = lib.g360_status_read(os.fsencode(root), C.byref(s))
            return status, (s.wifi, s.battery, s.charging)

        battery = 'sys/class/power_supply/battery/'
        base = {battery + 'capacity': '87\n', battery + 'status': 'Discharging\n',
                'sys/class/power_supply/ac/online': '0\n', 'sys/class/power_supply/usb/online': '0\n',
                'sys/class/power_supply/wireless/online': '0\n', 'sys/class/net/wlan0/operstate': 'up\n',
                'proc/net/route': HOME_ROUTES, 'proc/1/comm': 'systemd\n', 'proc/self/comm': 'python\n'}

        lib = C.CDLL(str(library))
        lib.g360_status_home_ip.argtypes = [C.c_char_p, C.c_char_p]
        ip = C.create_string_buffer(20)
        for value, expected in [('192.168.1.103\n', b'192.168.\n1.103'),
                                ('255.255.255.255', b'255.255.\n255.255')]:
            tree(root, {'tmp/g360-wifi/ip': value})
            assert lib.g360_status_home_ip(os.fsencode(root), ip) == 0 and ip.value == expected
        for value in ('', '192.168.1', '256.1.1.1', '1.2.3.4 extra', '1.2.3.4\n\n', '1..3.4'):
            tree(root, {'tmp/g360-wifi/ip': value})
            assert lib.g360_status_home_ip(os.fsencode(root), ip) == -1, value
        tree(root, {})
        assert lib.g360_status_home_ip(os.fsencode(root), ip) == -1

        def with_(**changes):
            files = dict(base)
            for key, value in changes.items():
                name = {'capacity': battery + 'capacity', 'status': battery + 'status',
                        'ac': 'sys/class/power_supply/ac/online', 'usb': 'sys/class/power_supply/usb/online',
                        'wireless': 'sys/class/power_supply/wireless/online',
                        'operstate': 'sys/class/net/wlan0/operstate', 'route': 'proc/net/route'}.get(key, key)
                if value is None:
                    files.pop(name, None)
                else:
                    files[name] = value
            return files

        # The battery: capacity and the bolt.
        assert read(base) == (0, (HOME, 87, 0))
        assert read(with_(status='Charging\n')) == (0, (HOME, 87, 1))
        assert read(with_(status='Full\n', capacity='100\n')) == (0, (HOME, 100, 0)), 'full, no cable'
        assert read(with_(status='Full\n', ac='1\n')) == (0, (HOME, 87, 1)), 'full on a charger'
        assert read(with_(status='Full\n', usb='1\n')) == (0, (HOME, 87, 1)), 'full on USB'
        assert read(with_(status='Full\n', wireless='1\n')) == (0, (HOME, 87, 1))
        assert read(with_(status='Full\n', ac=None, usb=None, wireless=None)) == (0, (HOME, 87, 0))
        assert read(with_(status='Not charging\n', ac='1\n')) == (0, (HOME, 87, 0))
        assert read(with_(status='Charging')) == (0, (HOME, 87, 1)), 'no final newline'
        assert read(with_(status=None)) == (0, (HOME, 87, 0))
        assert read(with_(capacity='0\n')) == (0, (HOME, 0, 0))
        assert read(with_(capacity='5')) == (0, (HOME, 5, 0))
        for bad in ('101\n', '-1\n', 'abc\n', '\n', '', '0100\n', '87 \n', '87\n\n', '8 7\n'):
            assert read(with_(capacity=bad, status='Charging\n')) == (-1, (HOME, -1, 0)), bad
        assert read(with_(capacity=None)) == (-1, (HOME, -1, 0))

        # Wi-Fi: the home network needs wlan0 up and a default route on it.
        assert read(with_(operstate='down\n')) == (0, (OFF, 87, 0)), 'link lost'
        assert read(with_(operstate='dormant\n')) == (0, (OFF, 87, 0))
        assert read(with_(operstate=None)) == (0, (OFF, 87, 0))
        assert read(with_(route=HEADER)) == (0, (OFF, 87, 0)), 'no address yet'
        assert read(with_(route=HOME_ROUTES.replace('wlan0', 'eth0'))) == (0, (OFF, 87, 0)), 'another interface'
        assert read(with_(route=None)) == (0, (OFF, 87, 0))
        # The access point needs its network on wlan0 and a hostapd process.
        assert read(with_(route=AP_ROUTES)) == (0, (OFF, 87, 0)), 'no hostapd'
        assert read(with_(route=AP_ROUTES, **{'proc/321/comm': 'hostapd\n'})) == (0, (AP, 87, 0))
        tree(root, with_(route=AP_ROUTES, **{'proc/321/comm': 'hostapd\n', 'tmp/g360-wifi/searching': '1\n'}))
        lib = C.CDLL(str(library))
        lib.g360_status_read.argtypes = [C.c_char_p, C.POINTER(Status)]
        search_status = Status()
        assert lib.g360_status_read(os.fsencode(root), C.byref(search_status)) == 0
        assert search_status.wifi == AP and search_status.wifi_searching == 1
        (root / 'tmp/g360-wifi/searching').unlink()
        (root / 'tmp/g360-wifi/error').write_text('1\n')
        assert lib.g360_status_read(os.fsencode(root), C.byref(search_status)) == 0
        assert search_status.wifi_searching == 0 and search_status.wifi_error == 1
        assert read(with_(route=AP_ROUTES, operstate='down\n', **{'proc/321/comm': 'hostapd\n'})) == (0, (AP, 87, 0))
        assert read(with_(route=AP_ROUTES, **{'proc/321/comm': 'hostapd_cli\n'})) == (0, (OFF, 87, 0))
        assert read(with_(route=AP_ROUTES.replace('wlan0', 'p2p0'), **{'proc/321/comm': 'hostapd\n'})) == (0, (OFF, 87, 0))
        assert read(with_(**{'proc/321/comm': 'hostapd\n'})) == (0, (HOME, 87, 0)), 'hostapd alone is not the access point'
        # Both routes (not seen on the camera): the access point wins.
        assert read(with_(route=HOME_ROUTES + AP_ROUTES[len(HEADER):], **{'proc/321/comm': 'hostapd\n'})) == (0, (AP, 87, 0))
        # The hostapd found last is checked first; a new one is found by a scan.
        files = with_(route=AP_ROUTES, **{'proc/321/comm': 'hostapd\n'})
        tree(root, files)
        lib = C.CDLL(str(library))
        lib.g360_status_read.argtypes = [C.c_char_p, C.POINTER(Status)]
        s = Status()
        assert lib.g360_status_read(os.fsencode(root), C.byref(s)) == 0 and s.wifi == AP
        (root / 'proc/321/comm').write_text('sh\n')
        assert lib.g360_status_read(os.fsencode(root), C.byref(s)) == 0 and s.wifi == OFF, 'hostapd gone'
        (root / 'proc/4000').mkdir()
        (root / 'proc/4000/comm').write_text('hostapd\n')
        assert lib.g360_status_read(os.fsencode(root), C.byref(s)) == 0 and s.wifi == AP, 'a new hostapd'
        # Nothing readable at all.
        assert lib.g360_status_read(os.fsencode(work / 'missing'), C.byref(s)) == -1
        assert (s.wifi, s.battery, s.charging) == (OFF, -1, 0)
        assert lib.g360_status_read(None, C.byref(s)) == -1 and (s.wifi, s.battery, s.charging) == (OFF, -1, 0)
        assert lib.g360_status_read(os.fsencode(root), None) == -1
    print('status reader: capacity and charging (cable, full, not charging), home network, access point, '
          'hostapd lookup and refusals passed')


if __name__ == '__main__':
    main()
