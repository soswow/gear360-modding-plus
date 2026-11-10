#!/usr/bin/env python3
"""The flash wear reader (EXT_CSD through debugfs) and the warning texts."""
import ctypes as C
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parents[1]
RE = HERE.parent / 'lib'


class Wear(C.Structure):
    _fields_ = [('pre_eol', C.c_int), ('life_a', C.c_int), ('life_b', C.c_int)]


def ext_csd(pre_eol=1, life_a=1, life_b=1, rev=7):
    data = bytearray(512)
    data[192] = rev
    data[267], data[268], data[269] = pre_eol, life_a, life_b
    return data.hex() + '\n'


def main():
    with tempfile.TemporaryDirectory(prefix='g360-wear-') as td:
        work = Path(td)
        library = work / 'wear.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1',
                        '-I' + str(HERE), '-I' + str(RE / 'capture-session'), '-I' + str(RE / 'g360display'),
                        '-shared', '-fPIC', str(HERE / 'wear.c'), str(RE / 'g360display/g360display.c'),
                        '-o', str(library)], check=True)
        lib = C.CDLL(str(library))
        lib.g360_wear_read.argtypes = [C.c_char_p, C.POINTER(Wear)]
        lib.g360_wear_level.argtypes = [C.POINTER(Wear)]

        def read(contents=None, entry='mmc0:0001', extra=()):
            d = work / f'mmc{len(list(work.iterdir()))}'
            d.mkdir()
            for name in extra:
                (d / name).write_text('x')
            if contents is not None:
                (d / entry).mkdir()
                (d / entry / 'ext_csd').write_text(contents)
            w = Wear()
            status = lib.g360_wear_read(os.fsencode(d), C.byref(w))
            return status, (w.pre_eol, w.life_a, w.life_b), lib.g360_wear_level(C.byref(w))

        # The camera on 5 November: all three read 1.
        assert read(ext_csd(), extra=('clock', 'ios', 'regs', 'state')) == (0, (1, 1, 1), 1)
        assert read(ext_csd(2, 1, 4)) == (0, (2, 1, 4), 4)
        assert read(ext_csd(1, 11, 3).upper()) == (0, (1, 11, 3), 11)
        assert read(ext_csd(0, 0, 0)) == (0, (0, 0, 0), 0)
        bad = (-1, -1, -1)
        assert read(None) == (-1, bad, 0), 'no mmc0:RCA entry'
        assert read(ext_csd()[:1000]) == (-1, bad, 0), 'short'
        assert read('zz' + ext_csd()[2:]) == (-1, bad, 0), 'not hex'
        assert read(ext_csd(4, 1, 1)) == (-1, bad, 0), 'pre-EOL out of range'
        assert read(ext_csd(1, 12, 1)) == (-1, bad, 0), 'life out of range'
        assert lib.g360_wear_read(os.fsencode(work / 'missing'), C.byref(Wear())) == -1
        # Every warning text the app can show fits the screen.
        lib.g360display_render_center.argtypes = [C.c_char_p, C.c_char_p]
        pixels = C.create_string_buffer(2304)
        for text in [f'FLASH WEAR {n}' for n in range(4, 10)] + ['FLASH WORN', 'SAVING 16/16', 'SWEEP 8/8']:
            assert lib.g360display_render_center(text.encode(), pixels) == 0, text
        assert lib.g360display_render_center(b'FLASH WEAR 10', pixels) == -1, 'too long for the screen'
    print('flash wear reader (EXT_CSD bytes 267-269), refusals and warning texts passed')


if __name__ == '__main__':
    main()
