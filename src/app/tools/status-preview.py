#!/usr/bin/env python3
"""Renders screens with the status corner through the real g360display code
and writes them, enlarged, as one PNG: python3 status-preview.py OUT.png"""
import ctypes as C
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

RE = Path(__file__).resolve().parents[2] / 'lib'
OFF, HOME, AP = 0, 1, 2
# (text, wifi, battery, charging, mode, timer)
SCREENS = [('245 SHOTS', HOME, 100, 1, 1, 0), ('48 SWEEPS\nHDR 6 FLASH', AP, 64, 0, 2, 0),
           ('SWEEP 3/8', OFF, 9, 0, 2, 0), ('SETTINGS 1/6\nMODE: SINGLE', HOME, 87, 1, 1, 0),
           ('CAPTURE IN\n5', AP, 100, 1, 2, 1), ('SWEEP IN\n10', HOME, 87, 0, 1, 1)]


class Status(C.Structure):
    _fields_ = [('wifi', C.c_int), ('battery', C.c_int), ('charging', C.c_int), ('mode', C.c_int), ('timer', C.c_int),
                ('charge_frame', C.c_int), ('raw', C.c_int), ('sound', C.c_int),
                ('wifi_searching', C.c_int), ('wifi_frame', C.c_int),
                ('wifi_error', C.c_int), ('ap_connected', C.c_int)]


def png(path, screens, scale=8, gap=8):
    width = 72 * scale + 2 * gap
    rows = []
    for pixels in screens:
        rows += [b'\x00' + b'\x30\x30\x30' * width] * gap
        for y in range(32):
            line = b'\x30\x30\x30' * gap
            for x in range(72):
                on = pixels[y * 72 + x]
                cell = b'\x78\xc8\xff' if on else b'\x00\x00\x00'
                line += cell * (scale - 1) + (b'\x50\x90\xb8' if on else b'\x10\x10\x10')
            line += b'\x30\x30\x30' * gap
            dark = b'\x30\x30\x30' * gap + b''.join(
                (b'\x50\x90\xb8' if pixels[y * 72 + x] else b'\x10\x10\x10') * scale for x in range(72)) + b'\x30\x30\x30' * gap
            rows += [b'\x00' + line] * (scale - 1) + [b'\x00' + dark]
    rows += [b'\x00' + b'\x30\x30\x30' * width] * gap

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, len(rows), 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: status-preview.py OUT.png')
    with tempfile.TemporaryDirectory(prefix='g360-preview-') as td:
        library = Path(td) / 'display.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-shared', '-fPIC', '-I' + str(RE / 'capture-session'),
                        str(RE / 'g360display/g360display.c'), '-o', str(library)], check=True)
        lib = C.CDLL(str(library))
        lib.g360display_render_status.argtypes = [C.c_char_p, C.POINTER(Status), C.c_char_p]
        screens = []
        for text, wifi, battery, charging, mode, timer in SCREENS:
            pixels = C.create_string_buffer(2304)
            assert lib.g360display_render_status(text.encode(), C.byref(Status(wifi, battery, charging, mode, timer, 3 if charging else 0, 1, 1)), pixels) == 0
            screens.append(pixels.raw)
    png(sys.argv[1], screens)


if __name__ == '__main__':
    main()
