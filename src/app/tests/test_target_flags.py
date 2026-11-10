#!/usr/bin/env python3
"""The camera's open() flag numbers, checked against the measured table.

The host build uses the host's own headers, so the camera's numbers in
src/app/storage_platform.h are never compiled here.  This test reads
them and compares them with the values measured on the camera by
src/app/tools/open_flags_probe.c.

The failure this guards against is concrete: the generic Linux numbers put
O_DIRECTORY at 0200000, which is the camera's O_DIRECT.  The card (FAT32
under the kernel's exfat driver) rejects it: opening a directory with it fails with
EINVAL, and the app cannot open its own photo directory at all.
"""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]

# Measured on the camera (Linux 3.5.0, ARM32) with the open-flag probe.
MEASURED = {
    'ST_RDONLY': 0o0,
    'ST_WRONLY': 0o1,
    'ST_RDWR': 0o2,
    'ST_CREAT': 0o100,
    'ST_EXCL': 0o200,
    'ST_TRUNC': 0o1000,
    'ST_APPEND': 0o2000,
    'ST_NOFOLLOW': 0o100000,
    'ST_DIRECTORY': 0o40000,
    'ST_CLOEXEC': 0o2000000,
}

# The generic numbers are wrong for this camera.  They must not come back.
GENERIC = {
    'ST_NOFOLLOW': 0o400000,
    'ST_DIRECTORY': 0o200000,
}


def target_branch(source):
    """The camera's branch of the header, which is the #else side."""
    parts = source.split('#else', 1)
    assert len(parts) == 2, 'storage_platform.h has no target branch'
    return parts[1]


def defined_flags(branch):
    values = {}
    for name, number in re.findall(r'#define\s+(ST_[A-Z]+)\s+([0-7]+)\b', branch):
        values[name] = int(number, 8)
    return values


def main():
    branch = target_branch((HERE / 'storage_platform.h').read_text())
    values = defined_flags(branch)
    missing = sorted(set(MEASURED) - set(values))
    assert not missing, 'src/app/storage_platform.h no longer defines: %s' % missing
    for name, expected in sorted(MEASURED.items()):
        actual = values[name]
        assert actual == expected, (
            '%s is 0%o (0x%x) on the camera, not 0%o (0x%x)'
            % (name, actual, actual, expected, expected))
    for name, generic in sorted(GENERIC.items()):
        assert values[name] != generic, (
            '%s is back to the generic number 0%o, which is the camera\'s '
            'O_DIRECT for O_DIRECTORY' % (name, generic))
    # The camera's O_DIRECT must not appear under any name.
    assert 0o200000 not in values.values(), (
        'the camera\'s O_DIRECT (0200000) must not be used as an open flag')
    print('target flags ok: ' + ', '.join(
        '%s=0%o' % (name, values[name]) for name in sorted(MEASURED)))


if __name__ == '__main__':
    main()
