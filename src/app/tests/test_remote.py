#!/usr/bin/env python3
"""The remote control's pure parts and socket: query parsing (no Wi-Fi), the auto-photo
schedule with the self-timer overlapping the interval, and one request and
reply over the Unix socket (remote.c)."""
import ctypes as C
import os
import re
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time

HERE = Path(__file__).resolve().parents[1]
RE = HERE.parent / 'lib'

BAD, STATE, SET, CAPTURE, CANCEL, AUTO, PREVIEW = range(7)
MODE, OUTPUT, SAVE, WIFI, DELAY, SOUNDS, SHUTTER, ISO, STOP = range(9)  # WIFI: never from the remote


class Request(C.Structure):
    _fields_ = [('command', C.c_int), ('setting', C.c_int), ('value', C.c_uint32), ('sweep', C.c_int), ('nd', C.c_uint32)]


HANDLER = C.CFUNCTYPE(None, C.c_void_p, C.c_char_p, C.c_void_p, C.c_size_t)


def main():
    with tempfile.TemporaryDirectory(prefix='g360-remote-') as td:
        work = Path(td)
        library = work / 'remote.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1',
                        '-I' + str(HERE), '-I' + str(RE / 'capture-session'), '-shared', '-fPIC',
                        str(HERE / 'remote.c'), '-o', str(library)], check=True)
        lib = C.CDLL(str(library))
        lib.g360_remote_parse.argtypes = [C.c_char_p, C.POINTER(Request)]
        lib.g360_remote_auto_due.argtypes = [C.c_uint64, C.c_uint64, C.c_uint64]
        lib.g360_remote_auto_next.argtypes = [C.c_uint64, C.c_uint64, C.c_uint64]
        lib.g360_remote_auto_next.restype = C.c_uint64
        lib.g360_remote_listen.argtypes = [C.c_char_p]
        lib.g360_remote_serve_one.argtypes = [C.c_int, HANDLER, C.c_void_p]
        lib.g360_remote_call.argtypes = [C.c_char_p, C.c_char_p, C.c_char_p, C.c_size_t, C.c_int]

        def parse(query):
            r = Request()
            status = lib.g360_remote_parse(query.encode(), C.byref(r))
            assert (status == 0) == (r.command != BAD), (query, status, r.command)
            if r.nd:
                return (r.command, r.setting, r.value, r.sweep, r.nd)
            return (r.command, r.setting, r.value, r.sweep)

        # Commands and settings, as the page sends them.
        assert parse('cmd=state') == (STATE, 0, 0, 0)
        assert parse('cmd=state&_=1730977000000') == (STATE, 0, 0, 0)
        assert parse('cmd=cancel') == (CANCEL, 0, 0, 0)
        assert parse('cmd=capture') == (CAPTURE, 0, 0, 0)
        assert parse('cmd=capture&sweep=1') == (CAPTURE, 0, 0, 1)
        assert parse('cmd=capture&sweep=0') == (CAPTURE, 0, 0, 0)
        # ND sweeps: which lens the filter covers.
        assert parse('cmd=capture&sweep=1&nd=left') == (CAPTURE, 0, 0, 1, 1)
        assert parse('cmd=capture&nd=right&sweep=1') == (CAPTURE, 0, 0, 1, 2)
        assert parse('cmd=capture&sweep=1&nd=both&_=5') == (CAPTURE, 0, 0, 1, 3)
        # Early stop of sweeps (in memory).
        assert parse('cmd=set&stop=auto') == (SET, STOP, 1, 0)
        assert parse('cmd=set&stop=full') == (SET, STOP, 0, 0)
        assert parse('cmd=auto&every=10') == (AUTO, 0, 10, 0)
        assert parse('every=86400&cmd=auto') == (AUTO, 0, 86400, 0)
        assert parse('cmd=auto&every=0') == (AUTO, 0, 0, 0)
        assert parse('cmd=preview&on=1') == (PREVIEW, 0, 1, 0)
        assert parse('cmd=preview&on=0&_=1') == (PREVIEW, 0, 0, 0)
        assert parse('cmd=set&mode=hdr') == (SET, MODE, 1, 0)
        assert parse('cmd=set&mode=single') == (SET, MODE, 0, 0)
        assert parse('cmd=set&raw=off') == (SET, OUTPUT, 1, 0)
        assert parse('cmd=set&raw=on') == (SET, OUTPUT, 2, 0)
        assert parse('cmd=set&output=raw') == (SET, OUTPUT, 0, 0)
        assert parse('cmd=set&output=jpeg') == (SET, OUTPUT, 1, 0)
        for both in ('raw+jpeg', 'raw%2Bjpeg', 'raw%2bjpeg'):
            assert parse('cmd=set&output='+both) == (SET, OUTPUT, 2, 0)
        assert parse('cmd=set&save=card') == (SET, SAVE, 1, 0)
        assert parse('cmd=set&delay=10') == (SET, DELAY, 10, 0)
        assert parse('cmd=set&delay=0') == (SET, DELAY, 0, 0)
        assert parse('cmd=set&sounds=off') == (SET, SOUNDS, 0, 0)
        assert parse('cmd=set&shutter=auto') == (SET, SHUTTER, 0, 0)
        assert parse('cmd=set&shutter=41') == (SET, SHUTTER, 41, 0)
        assert parse('cmd=set&shutter=48') == (SET, SHUTTER, 48, 0)
        assert parse('cmd=set&shutter=111') == (SET, SHUTTER, 111, 0)
        assert parse('cmd=set&shutter=213') == (SET, SHUTTER, 213, 0)
        assert parse('cmd=set&shutter=218&_=1') == (SET, SHUTTER, 218, 0)
        assert parse('cmd=set&iso=100') == (SET, ISO, 100, 0)
        assert parse('cmd=set&iso=1600') == (SET, ISO, 1600, 0)
        # Every shutter the page offers is one the app takes, with the same name, in remote.h's order.
        header = (HERE / 'remote.h').read_text().replace('\\\n', '')
        tvs = [int(t) for t in re.search(r'G360_SHUTTER_TVS \{([^}]*)\}', header).group(1).split(',')]
        names = re.findall(r'"([^"]+)"', re.search(r'G360_SHUTTER_NAMES \{([^}]*)\}', header).group(1))
        page = re.search(r"const SHUTTERS=\[\['auto','Auto'\],(.*?)\];", (HERE / 'web' / 'remote.html').read_text()).group(1)
        offered = [(int(t), n) for t, n in re.findall(r"\[(\d+),'([^']+)'\]", page)]
        assert offered == list(zip(tvs, names)) and len(tvs) == 34, offered
        for tv in tvs:
            assert parse('cmd=set&shutter=%d' % tv) == (SET, SHUTTER, tv, 0), tv
        # Anything else is refused whole.
        for bad in ('', 'cmd', 'cmd=', 'cmd=nope', 'cmd=state&cmd=state', 'cmd=state&raw=on', 'cmd=set',
                    'cmd=set&raw=maybe', 'cmd=set&output=on', 'cmd=set&output=raw%20jpeg',
                    'cmd=set&output=raw%2Bjpeg&raw=off', 'cmd=set&output=raw&output=jpeg', 'cmd=set&raw=on&mode=hdr', 'cmd=set&shots=6', 'cmd=set&shots=8',
                    'cmd=set&delay=3', 'cmd=set&delay=-2', 'cmd=set&wifi=home', 'cmd=set&wifi=ap',
                    'cmd=set&wifi=off', 'cmd=set&colour=red',
                    'cmd=capture&sweep=2', 'cmd=capture&every=3', 'cmd=auto', 'cmd=auto&every=86401',
                    'cmd=auto&every=1e3', 'cmd=auto&every=100000', 'cmd=auto&every=010', 'cmd=set&raw%3Doff',
                    'cmd=state&x', 'cmd=state&' + 'a' * 300, 'cmd=preview', 'cmd=preview&on=2',
                    'cmd=set&shutter=8', 'cmd=set&shutter=8192', 'cmd=set&shutter=40', 'cmd=set&shutter=219', 'cmd=set&shutter=130', 'cmd=set&shutter=112', 'cmd=set&shutter=041',
                    'cmd=set&shutter=-8', 'cmd=set&shutter=', 'cmd=set&iso=125', 'cmd=set&iso=3200', 'cmd=set&iso=0', 'cmd=set&iso=auto',
                    'cmd=set&iso=100&shutter=auto', 'cmd=set&iso=400&iso=100',
                    'cmd=preview&off=1', 'cmd=preview&on=1&every=3', 'cmd=preview&on=01',
                    'cmd=capture&nd=left', 'cmd=capture&sweep=0&nd=left', 'cmd=capture&sweep=1&nd=top',
                    'cmd=capture&sweep=1&nd=', 'cmd=capture&sweep=1&mode=hdr', 'cmd=capture&sweep=1&nd=left&nd=right',
                    'cmd=set&stop=on', 'cmd=set&stop=', 'cmd=state&nd=left'):
            assert parse(bad) == (BAD, 0, 0, 0), bad

        # Auto photos with the self-timer overlapping the interval. Times in ms.
        def run(every, lead, busy, shots):
            """Presses and capture starts for SHOTS photos; each capture keeps
            the app busy for BUSY ms, and the countdown for LEAD ms."""
            now, target, ready, out = 0, 0, 0, []
            for _ in range(shots):
                while not lib.g360_remote_auto_due(max(now, ready), target, lead):
                    now += 1
                press = max(now, ready)
                start = press + lead
                out.append((press, start))
                target = lib.g360_remote_auto_next(press, lead, every)
                ready, now = start + busy, press
            return out

        # Auto 10 s, delay 5 s: the press 5 s after a capture starts, the
        # countdown fills the rest, a capture every 10 s.
        assert run(10000, 5000, 1500, 4) == [(0, 5000), (10000, 15000), (20000, 25000), (30000, 35000)]
        # No delay: a press every 10 s.
        assert run(10000, 0, 1500, 3) == [(0, 0), (10000, 10000), (20000, 20000)]
        # Delay 10 s longer than auto 4 s: the delay wins; the next press
        # comes as soon as the capture is done.
        assert run(4000, 10000, 1500, 3) == [(0, 10000), (11500, 21500), (23000, 33000)]
        # A capture longer than the interval: the next press when ready.
        assert run(1000, 0, 2500, 3) == [(0, 0), (2500, 2500), (5000, 5000)]

        # One request and reply over the socket.
        path = str(work / 'app.sock')
        listener = lib.g360_remote_listen(path.encode())
        assert listener >= 0
        seen = []

        @HANDLER
        def answer(context, request, reply, size):
            seen.append(request.decode())
            text = b'{"app":"on","echo":"' + request + b'"}'
            C.memmove(reply, text + b'\0', len(text) + 1)

        def serve(n):
            for _ in range(n):
                assert lib.g360_remote_serve_one(listener, answer, None) == 0

        server = threading.Thread(target=serve, args=(4,))  # the silent client takes one
        server.start()
        reply = C.create_string_buffer(2049)
        assert lib.g360_remote_call(path.encode(), b'cmd=state', reply, len(reply), 2000) == 0
        assert reply.value == b'{"app":"on","echo":"cmd=state"}', reply.value
        # A client that connects and says nothing is dropped after about 1 s,
        # and the next one is served.
        silent = socket.socket(socket.AF_UNIX)
        silent.connect(path)
        started = time.monotonic()
        assert lib.g360_remote_call(path.encode(), b'cmd=capture', reply, len(reply), 3000) == 0
        assert reply.value == b'{"app":"on","echo":"cmd=capture"}' and 0.8 < time.monotonic() - started < 2.5
        # A client that leaves before the reply doesn't stop the server.
        gone = socket.socket(socket.AF_UNIX)
        gone.connect(path)
        gone.sendall(b'cmd=cancel\n')
        gone.close()
        server.join(5)
        assert not server.is_alive() and seen == ['cmd=state', 'cmd=capture', 'cmd=cancel'], seen
        silent.close()
        # A request with a newline is never sent.
        assert lib.g360_remote_call(path.encode(), b'cmd=state\ncmd=capture', reply, len(reply), 500) == -1
        os.close(listener)
        # No app: the call fails at once.
        os.unlink(path)
        assert lib.g360_remote_call(path.encode(), b'cmd=state', reply, len(reply), 500) == -1 and reply.value == b''
        # A new listener replaces a stale socket file.
        Path(path).write_text('stale')
        listener = lib.g360_remote_listen(path.encode())
        assert listener >= 0
        os.close(listener)
    print('remote: commands and settings parsed, bad queries refused, auto photos overlap the self-timer '
          '(delay longer than the interval wins), one request per connection, silent and vanished clients survived')


if __name__ == '__main__':
    main()
