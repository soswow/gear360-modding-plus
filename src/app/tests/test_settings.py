#!/usr/bin/env python3
"""Stored settings: defaults, two-slot saves, torn and damaged slots."""
import ctypes as C
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parents[1]
RE = HERE.parent / 'lib'


class Settings(C.Structure):
    _fields_ = [('mode', C.c_uint32), ('save', C.c_uint32), ('wifi', C.c_uint32), ('delay', C.c_uint32),
                ('sounds', C.c_uint32), ('output', C.c_uint32), ('sequence', C.c_uint32), ('slot', C.c_uint)]


def main():
    with tempfile.TemporaryDirectory(prefix='g360-settings-') as td:
        work = Path(td)
        library = work / 'settings.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1',
                        '-I' + str(HERE), '-I' + str(RE / 'capture-session'), '-shared', '-fPIC',
                        str(HERE / 'settings.c'), '-o', str(library)], check=True)
        lib = C.CDLL(str(library))
        lib.g360_settings_load.argtypes = [C.c_char_p, C.POINTER(Settings)]
        lib.g360_settings_save.argtypes = [C.c_char_p, C.POINTER(Settings)]
        state = work / 'state'
        d = os.fsencode(state)

        def load():
            s = Settings()
            status = lib.g360_settings_load(d, C.byref(s))
            return status, (s.mode, s.save, s.wifi) + ((s.delay,) if s.delay else ()) + (('sounds off',) if not s.sounds else ()) + \
                (('output '+('raw' if s.output == 0 else 'jpeg'),) if s.output != 2 else ())

        # No directory, then an empty one: the defaults (single, flash).
        assert load() == (0, (0, 0, 0))
        state.mkdir()
        assert load() == (0, (0, 0, 0))
        s = Settings()
        lib.g360_settings_load(d, C.byref(s))
        s.mode, s.save = 1, 0
        assert lib.g360_settings_save(d, C.byref(s)) == 0 and s.slot == 0 and s.sequence == 1
        assert sorted(p.name for p in state.iterdir()) == ['settings.0']
        assert (state / 'settings.0').stat().st_size == 20
        assert load() == (1, (1, 0, 0))
        s.save = 1
        assert lib.g360_settings_save(d, C.byref(s)) == 0 and s.slot == 1 and s.sequence == 2
        assert load() == (1, (1, 1, 0))
        s.mode = 0
        assert lib.g360_settings_save(d, C.byref(s)) == 0 and s.slot == 0 and s.sequence == 3
        assert load() == (1, (0, 1, 0))
        # A torn newest slot falls back to the other one.
        (state / 'settings.0').write_bytes((state / 'settings.0').read_bytes()[:11])
        assert load() == (1, (1, 1, 0))
        # Byte 13 held the HDR shot count: saves write 6, and a slot from
        # when 8 was offered still loads.
        assert (state / 'settings.1').read_bytes()[13] == 6
        data = bytearray((state / 'settings.1').read_bytes())
        data[13] = 8
        h = 2166136261
        for b in data[:16]:
            h = ((h ^ b) * 16777619) & 0xffffffff
        data[16:20] = h.to_bytes(4, 'little')
        (state / 'settings.1').write_bytes(bytes(data))
        assert load() == (1, (1, 1, 0))
        # A damaged checksum or an impossible value is not used.
        data = bytearray((state / 'settings.1').read_bytes())
        data[13] = 7
        (state / 'settings.1').write_bytes(bytes(data))
        assert load() == (0, (0, 0, 0))
        # Saving refuses impossible values and writes nothing.
        before = {p.name: p.read_bytes() for p in state.iterdir()}
        bad = Settings(2, 0, 0, 1, 1, 1)
        assert lib.g360_settings_save(d, C.byref(bad)) == -1
        bad = Settings(0, 4, 0, 1, 1, 1)
        assert lib.g360_settings_save(d, C.byref(bad)) == -1
        assert {p.name: p.read_bytes() for p in state.iterdir()} == before
        # Recovering from both damaged: the next save writes a valid slot.
        s = Settings()
        lib.g360_settings_load(d, C.byref(s))
        s.mode = 1
        assert lib.g360_settings_save(d, C.byref(s)) == 0 and load() == (1, (1, 0, 0))
        # A symbolic link is never followed.
        target = work / 'elsewhere'
        target.write_bytes(b'keep')
        for name in ('settings.0', 'settings.1'):
            (state / name).unlink()
        (state / 'settings.0').symlink_to(target)
        assert load()[0] == -1
        s = Settings(1, 0, 0, 1, 1, 5, 1)
        assert lib.g360_settings_save(d, C.byref(s)) == -1 and target.read_bytes() == b'keep'
        (state / 'settings.0').unlink()
        # Wi-Fi: byte 15. A slot from before it existed (0) reads as not set;
        # each choice is kept; unused bit 3 is refused.
        s = Settings()
        lib.g360_settings_load(d, C.byref(s))
        for wifi in (1, 2, 3, 0):
            s.wifi = wifi
            assert lib.g360_settings_save(d, C.byref(s)) == 0 and load() == (1, (0, 0, wifi))
            assert (state / f'settings.{s.slot}').read_bytes()[15] == wifi
        data = bytearray((state / f'settings.{s.slot}').read_bytes())
        data[15] = 8
        import struct
        h = 2166136261
        for b in data[:16]:
            h = ((h ^ b) * 16777619) & 0xffffffff
        data[16:20] = struct.pack('<I', h)
        (state / f'settings.{s.slot}').write_bytes(bytes(data))
        assert load() == (1, (0, 0, 3)), 'a slot with an unused bit is not used; the other slot is'
        # The self-timer delay shares byte 15's high nibble: 0, 2, 5 and 10 s
        # are kept (with the Wi-Fi choice beside them); other values are
        # refused; a damaged nibble is not used; older slots read as OFF.
        s = Settings()
        lib.g360_settings_load(d, C.byref(s))
        for wifi, delay, nibble in ((0, 2, 1), (3, 5, 2), (2, 10, 3), (1, 0, 0)):
            s.wifi, s.delay = wifi, delay
            assert lib.g360_settings_save(d, C.byref(s)) == 0
            assert load() == (1, (0, 0, wifi) + ((delay,) if delay else ()))
            assert (state / f'settings.{s.slot}').read_bytes()[15] == wifi | (nibble << 4)
        before = {p.name: p.read_bytes() for p in state.iterdir()}
        for bad_delay in (1, 3, 11):
            bad = Settings(0, 0, bad_delay, 1, 1, 0, 1)
            assert lib.g360_settings_save(d, C.byref(bad)) == -1
        assert {p.name: p.read_bytes() for p in state.iterdir()} == before
        s.delay = 5
        assert lib.g360_settings_save(d, C.byref(s)) == 0
        data = bytearray((state / f'settings.{s.slot}').read_bytes())
        data[15] |= 0x08  # an unused bit
        h = 2166136261
        for b in data[:16]:
            h = ((h ^ b) * 16777619) & 0xffffffff
        data[16:20] = struct.pack('<I', h)
        (state / f'settings.{s.slot}').write_bytes(bytes(data))
        assert load() == (1, (0, 0, 1)), 'the other slot (delay OFF) is used instead'
        # Sounds: ON by default (older slots, bit 6 clear); OFF sets bit 6 and
        # keeps the delay and Wi-Fi beside it.
        s = Settings()
        lib.g360_settings_load(d, C.byref(s))
        assert s.sounds == 1
        s.wifi, s.delay, s.sounds = 2, 10, 0
        assert lib.g360_settings_save(d, C.byref(s)) == 0
        assert (state / f'settings.{s.slot}').read_bytes()[15] == 2 | (3 << 4) | 0x40
        assert load() == (1, (0, 0, 2, 10, 'sounds off'))
        s.sounds = 2
        assert lib.g360_settings_save(d, C.byref(s)) == -1
        s.sounds = 1
        assert lib.g360_settings_save(d, C.byref(s)) == 0
        assert load() == (1, (0, 0, 2, 10)) and (state / f'settings.{s.slot}').read_bytes()[15] == 2 | (3 << 4)
        # Output: old RAW ON (bit 7 clear) reads as BOTH, old RAW OFF as
        # JPEG. RAW-only uses the spare bit 2 without changing the other fields.
        assert s.output == 2
        s.output, s.sounds = 1, 0
        assert lib.g360_settings_save(d, C.byref(s)) == 0
        assert (state / f'settings.{s.slot}').read_bytes()[15] == 2 | (3 << 4) | 0x40 | 0x80
        assert load() == (1, (0, 0, 2, 10, 'sounds off', 'output jpeg'))
        for bad_output in (3, 0xffffffff):
            s.output = bad_output
            assert lib.g360_settings_save(d, C.byref(s)) == -1
        s.output, s.sounds = 0, 1
        assert lib.g360_settings_save(d, C.byref(s)) == 0
        assert load() == (1, (0, 0, 2, 10, 'output raw'))
        assert (state / f'settings.{s.slot}').read_bytes()[15] == 2 | (3 << 4) | 0x04
        # Impossible RAW-only + JPEG-only bits invalidate the newest slot.
        data = bytearray((state / f'settings.{s.slot}').read_bytes())
        data[15] |= 0x80
        h = 2166136261
        for b in data[:16]:
            h = ((h ^ b) * 16777619) & 0xffffffff
        data[16:20] = struct.pack('<I', h)
        (state / f'settings.{s.slot}').write_bytes(data)
        assert load() == (1, (0, 0, 2, 10, 'sounds off', 'output jpeg'))
        s.output = 2
        assert lib.g360_settings_save(d, C.byref(s)) == 0
        assert load() == (1, (0, 0, 2, 10))
        assert (state / f'settings.{s.slot}').read_bytes()[15] == 2 | (3 << 4)
    print('settings defaults, alternating slots, torn/damaged slots, refusals, no symlinks, the Wi-Fi byte, sounds, output modes and legacy output migration passed')


if __name__ == '__main__':
    main()
