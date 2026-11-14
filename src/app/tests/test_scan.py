#!/usr/bin/env python3
"""The sweep's RAW scan (scan.h): blocks, circle, stop rules and file headers.

Checks the C scan against a numpy model of the same rules on synthetic
frames. With G360_SCAN_DNG=path/to/frame.dng it also compares one real
uncompressed DNG from the app (strip at byte 40960) and prints the host time.
"""
import ctypes as C
import math
import os
from pathlib import Path
import subprocess
import tempfile
import time

import numpy as np

HERE = Path(__file__).resolve().parents[1]
RE = HERE.parent / 'lib'
W, H, HALF, STRIDE = 5824, 2896, 2912, 11648
BLACK, WHITE, DARK = 4106, 65472, 1024
COLS = ROWS = 91
CELL = 5
LENS_BYTES = COLS * ROWS * CELL


class Frame(C.Structure):
    _fields_ = [('stamp', C.c_char * 16), ('index', C.c_uint32), ('nd', C.c_uint32), ('tv', C.c_int32),
                ('exposure_numerator', C.c_uint32), ('exposure_denominator', C.c_uint32), ('scan_us', C.c_uint32),
                ('quads', C.c_uint32 * 2), ('clipped', C.c_uint32 * 2), ('dark', C.c_uint32 * 2),
                ('last', C.c_uint32), ('still_clipped', C.c_uint32), ('cells', C.c_uint8 * (2 * LENS_BYTES))]


def levels_table():
    t = [0]
    for level in range(1, 256):
        v = max(0, math.ceil(2 ** ((level - 0.5) / 16) - 1))
        while 16 * math.log2(1 + v) < level - 0.5:
            v += 1
        while v > 0 and 16 * math.log2(v) >= level - 0.5:
            v -= 1
        t.append(v)
    return np.array(t)


LEVELS = levels_table()


def level(v):
    return np.searchsorted(LEVELS, v, side='right') - 1


def model(a):
    """The scan's rules in numpy: cells (2, rows, cols, 5) and totals."""
    out = np.zeros((2, ROWS, COLS, CELL), np.int64)
    totals = []
    ys = np.arange(0, H - 1, 4)
    xs = np.arange(0, HALF - 1, 4)
    yy, xx = np.meshgrid(ys, xs, indexing='ij')
    inside = (2 * xx + 1 - 2911) ** 2 + (2 * yy + 1 - 2895) ** 2 <= 2840 ** 2
    for lens in range(2):
        x0 = lens * HALF
        g1 = a[yy, xx + x0].astype(np.int64); r = a[yy, xx + x0 + 1].astype(np.int64)
        b = a[yy + 1, xx + x0].astype(np.int64); g2 = a[yy + 1, xx + x0 + 1].astype(np.int64)
        hi = np.maximum(np.maximum(g1, r), np.maximum(b, g2))
        g = np.maximum((g1 + g2) // 2 - BLACK, 0)
        hi = np.maximum(hi - BLACK, 0)
        clip = (hi >= WHITE - BLACK) & inside
        dark = (g < DARK) & inside
        cy, cx = yy // 32, xx // 32
        n = np.zeros((ROWS, COLS), np.int64); np.add.at(n, (cy[inside], cx[inside]), 1)
        c = np.zeros_like(n); np.add.at(c, (cy[clip], cx[clip]), 1)
        d = np.zeros_like(n); np.add.at(d, (cy[dark], cx[dark]), 1)
        s = np.zeros_like(n); np.add.at(s, (cy[inside], cx[inside]), g[inside])
        p = np.zeros_like(n); np.maximum.at(p, (cy[inside], cx[inside]), hi[inside])
        mean = np.where(n > 0, (s + n // 2) // np.maximum(n, 1), 0)
        out[lens, ..., 0] = n; out[lens, ..., 1] = c; out[lens, ..., 2] = d
        out[lens, ..., 3] = np.where(n > 0, level(mean), 0); out[lens, ..., 4] = np.where(n > 0, level(p), 0)
        totals.append((int(inside.sum()), int(clip.sum()), int(dark.sum())))
    return out, totals


def main():
    with tempfile.TemporaryDirectory(prefix='g360-scan-') as td:
        library = Path(td) / 'scan.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2',
                        '-I' + str(HERE), '-I' + str(RE / 'capture-session'), '-shared', '-fPIC',
                        str(HERE / 'scan.c'), '-o', str(library)], check=True)
        lib = C.CDLL(str(library))
        lib.g360_scan_raw.argtypes = [C.c_void_p, C.c_size_t, C.c_uint32, C.c_uint32, C.c_uint32, C.POINTER(Frame)]
        lib.g360_scan_plain_done.argtypes = [C.POINTER(Frame), C.c_uint32]
        lib.g360_scan_targets.argtypes = [C.POINTER(Frame), C.c_uint32, C.c_void_p]
        lib.g360_scan_targets.restype = C.c_uint32
        lib.g360_scan_nd_done.argtypes = [C.POINTER(Frame), C.c_uint32, C.c_void_p, C.c_void_p]
        lib.g360_scan_file_header.argtypes = [C.c_void_p, C.c_uint32]
        lib.g360_scan_frame_header.argtypes = [C.c_void_p, C.POINTER(Frame), C.c_int]

        def scan(a, **kw):
            f = Frame()
            a = np.ascontiguousarray(a, dtype='<u2')
            args = dict(size=a.nbytes, width=W, height=H, stride=STRIDE); args.update(kw)
            start = time.perf_counter()
            status = lib.g360_scan_raw(a.ctypes.data, args['size'], args['width'], args['height'], args['stride'],
                                       C.byref(f))
            f.scan_us = int((time.perf_counter() - start) * 1e6)
            return status, f

        def cells(f):
            return np.frombuffer(bytes(f.cells), np.uint8).reshape(2, ROWS, COLS, CELL).astype(np.int64)

        def check(a, what):
            status, f = scan(a)
            assert status == 0, what
            want, totals = model(a)
            got = cells(f)
            assert (got == want).all(), (what, np.argwhere(got != want)[:5])
            assert [(f.quads[k], f.clipped[k], f.dark[k]) for k in range(2)] == totals, (what, totals)
            return f

        # The wrong layout is refused and leaves nothing behind.
        black = np.full((H, W), BLACK, np.uint16)
        for kw in (dict(width=5792), dict(height=2880), dict(stride=11584), dict(size=16)):
            status, f = scan(black, **kw)
            assert status == -1 and f.quads[0] == 0 and not any(bytes(f.cells)), kw

        # A black frame: every sampled quad inside the circle is dark, none
        # outside it counts; both lenses see the same circle.
        f = check(black, 'black')
        assert f.quads[0] == f.quads[1] == f.dark[0] == f.dark[1] > 0 and f.clipped[0] == 0
        n = cells(f)[..., 0]
        assert n.max() == 64 and n[0, 0, 0] == 0 and n[0, 45, 45] == 64 and (n[0] == n[1]).all()
        # The circle (centre x 1455.5, y 1447.5, radius 1420) reaches blocks
        # 1..89 across and 0..89 down; corners and the outer columns are out.
        assert n[0, 45, 1] > 0 and n[0, 45, 89] > 0 and n[0, 0, 45] > 0 and n[0, 89, 45] > 0
        assert n[0, 45, 0] == 0 and n[0, 45, 90] == 0 and n[0, 90, 45] == 0

        # White everywhere: clipped inside the circle only.
        white = np.full((H, W), WHITE, np.uint16)
        f = check(white, 'white')
        assert f.clipped[0] == f.quads[0] and f.dark[0] == 0

        # One clipped red sample at a sampled quad (y, x multiples of 4) in
        # the right lens counts; one at a skipped quad does not.
        a = np.full((H, W), BLACK + 8000, np.uint16)
        a[1000, HALF + 2000 + 1] = WHITE
        a[1002, HALF + 1002] = WHITE  # quad (1002, 1002): not sampled
        f = check(a, 'one red')
        got = cells(f)
        assert (f.clipped[0], f.clipped[1]) == (0, 1)
        assert got[1, 1000 // 32, 2000 // 32, 1] == 1 and got[1, 1000 // 32, 2000 // 32, 4] == level(WHITE - BLACK)

        # Clipped corners (outside the circle) are ignored.
        a = np.full((H, W), BLACK + 8000, np.uint16)
        a[:64, :64] = WHITE; a[-64:, -64:] = WHITE
        f = check(a, 'corners')
        assert f.clipped[0] == f.clipped[1] == 0 and f.dark[0] == 0

        # A random scene against the model, cell by cell.
        rng = np.random.default_rng(31)
        a = (BLACK + rng.gamma(0.6, 6000, (H, W))).clip(0, WHITE).astype(np.uint16) // 64 * 64
        a[rng.random((H, W)) < 0.002] = WHITE
        check(a, 'random')

        # The plain stop rule: never before two frames; then dark quads at
        # most 2 per mille of the sampled ones.
        bright = np.full((H, W), BLACK + 4000, np.uint16)
        _, f = scan(bright)
        assert lib.g360_scan_plain_done(C.byref(f), 1) == 0 and lib.g360_scan_plain_done(C.byref(f), 2) == 1
        total = f.quads[0] + f.quads[1]
        f.dark[0] = total * 2 // 1000; f.dark[1] = 0
        assert lib.g360_scan_plain_done(C.byref(f), 3) == 1
        f.dark[0] += 1
        assert lib.g360_scan_plain_done(C.byref(f), 3) == 0
        _, f = scan(black)
        assert lib.g360_scan_plain_done(C.byref(f), 7) == 0

        # ND targets: the right lens's blocks that clip in the reference.
        ref_a = np.full((H, W), BLACK + 4000, np.uint16)
        ref_a[800:880, HALF + 1300:HALF + 1380] = WHITE  # a lamp in the right lens
        ref_a[800:880, 1300:1380] = WHITE                # and one in the left
        _, ref = scan(ref_a)
        target = (C.c_uint8 * (2 * COLS * ROWS))()
        count = lib.g360_scan_targets(C.byref(ref), 2, target)
        t = np.frombuffer(bytes(target), np.uint8).reshape(2, ROWS, COLS)
        assert count == t.sum() == (cells(ref)[1, ..., 1] > 0).sum() > 0 and t[0].sum() == 0
        both = (C.c_uint8 * (2 * COLS * ROWS))()
        assert lib.g360_scan_targets(C.byref(ref), 3, both) == count * 2
        # No reference: every block in the circle of the covered lens.
        every = (C.c_uint8 * (2 * COLS * ROWS))()
        assert lib.g360_scan_targets(None, 1, every) == (cells(ref)[0, ..., 0] > 0).sum()

        # ND frames, fastest first. Frame 1: the lamp still clips: stop, it
        # is still clipped (slower frames clip it too).
        measured = (C.c_uint8 * (2 * COLS * ROWS))()
        _, nd1 = scan(ref_a)
        assert lib.g360_scan_nd_done(C.byref(nd1), 1, target, measured) == 1 and nd1.still_clipped == count
        # The lamp too dark under the ND: go on.
        dim = np.full((H, W), BLACK, np.uint16); dim[800:880, HALF + 1300:HALF + 1380] = BLACK + 200
        measured = (C.c_uint8 * (2 * COLS * ROWS))()
        _, nd1 = scan(dim)
        assert lib.g360_scan_nd_done(C.byref(nd1), 1, target, measured) == 0 and nd1.still_clipped == 0
        assert not any(bytes(measured))
        # Brighter: measured (peak at least the dark level, nothing clipped): stop.
        dim[800:880, HALF + 1300:HALF + 1380] = BLACK + 20000
        _, nd2 = scan(dim)
        assert lib.g360_scan_nd_done(C.byref(nd2), 2, target, measured) == 1 and nd2.still_clipped == 0
        assert sum(bytes(measured)) == count
        # Half measured, the rest clipped: stop, reporting the clipped ones.
        measured = (C.c_uint8 * (2 * COLS * ROWS))()
        half = np.full((H, W), BLACK + 3000, np.uint16); half[800:880, HALF + 1340:HALF + 1380] = WHITE
        _, nd = scan(half)
        assert lib.g360_scan_nd_done(C.byref(nd), 1, target, measured) == 1
        assert 0 < nd.still_clipped < count and sum(bytes(measured)) + nd.still_clipped == count

        # File headers.
        head = (C.c_uint8 * 36)()
        assert lib.g360_scan_file_header(head, 9) == 36
        h = bytes(head)
        assert h[:8] == b'G360SCN1' and np.frombuffer(h[8:24], '<u2').tolist() == [1, 9, 91, 91, 32, 5, 2, 0]
        assert np.frombuffer(h[24:36], '<u4').tolist() == [BLACK, WHITE, DARK]
        f = Frame(stamp=b'20261114-085331', index=3, nd=2, tv=218, exposure_numerator=371,
                  exposure_denominator=5000000, scan_us=12345, last=1, still_clipped=7)
        f.quads[0], f.quads[1], f.clipped[1], f.dark[0] = 10, 11, 12, 13
        fh = (C.c_uint8 * 80)()
        assert lib.g360_scan_frame_header(fh, C.byref(f), 1) == 80
        b = bytes(fh)
        assert b[:15] == b'20261114-085331' and b[15] == 0
        assert np.frombuffer(b[16:80], '<u4').tolist() == [3, 2, 218, 371, 5000000, 12345, 10, 11, 0, 12, 13, 0,
                                                           1, 7, 1, 0]

        real = os.environ.get('G360_SCAN_DNG')
        if real:
            a = np.fromfile(real, '<u2', offset=40960, count=W * H).reshape(H, W)
            f = check(a, real)
            print(f'{real}: scan {f.scan_us} us on the host; quads {list(f.quads)} clipped {list(f.clipped)} '
                  f'dark {list(f.dark)}')
    print('scan: ok')


if __name__ == '__main__':
    main()
