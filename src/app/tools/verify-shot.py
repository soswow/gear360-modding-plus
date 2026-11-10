#!/usr/bin/env python3
"""Check one shot taken by the camera app: the JPEG's Exif and the DNG.

    python3 src/app/tools/verify-shot.py output/camera-app-20261105/shot-001

The DNG is rebuilt with the Python reference (make_dng.py) from the RAW
samples it contains and the same shot's image-processing records, read from
the JPEG's own MakerNote, and the two are compared byte for byte.  A report
is written next to the files as verification.json.
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

RE = Path(__file__).resolve().parents[2] / 'lib'
sys.path.insert(0, str(RE / 'dng'))
import make_dng  # noqa: E402
import stock_iq  # noqa: E402


def exiftool(*args):
    result = subprocess.run(['exiftool', *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError('exiftool failed: ' + result.stderr.strip())
    return result.stdout


def tag_value(text, name):
    """Find a tag's value.  exiftool prefixes the name with its group."""
    for line in text.splitlines():
        if ':' not in line:
            continue
        key, value = line.split(':', 1)
        key = key.strip()
        if key.startswith('[') and ']' in key:
            key = key.split(']', 1)[1].strip()
        if key == name:
            return value.strip()
    return None


def digest(path):
    hasher = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            hasher.update(block)
    return hasher.hexdigest()


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    directory = Path(argv[1])
    jpegs = sorted(directory.glob('*.jpg'))
    dngs = sorted(directory.glob('*.dng'))
    if not jpegs or not dngs:
        raise SystemExit('need one JPEG and one DNG in %s' % directory)
    jpeg, dng = jpegs[0], dngs[0]

    report = {
        'directory': str(directory),
        'jpeg': {
            'name': jpeg.name,
            'bytes': jpeg.stat().st_size,
            'sha256': digest(jpeg),
        },
        'dng': {
            'name': dng.name,
            'bytes': dng.stat().st_size,
            'sha256': digest(dng),
        },
    }

    jpeg_tags = exiftool('-a', '-G1', '-s', str(jpeg))
    for name in ('Make', 'Camera Model Name', 'ExposureTime', 'ISO',
                 'ExposureProgram', 'FNumber', 'DateTimeOriginal', 'ColorSpace',
                 'WhiteBalance', 'ExifImageWidth', 'ExifImageHeight'):
        value = tag_value(jpeg_tags, name)
        if value is not None:
            report['jpeg']['exif_' + name.replace(' ', '_').lower()] = value
    report['jpeg']['maker_note_records'] = bool(stock_iq.read(str(jpeg)).get('awb'))

    dng_validate = exiftool('-validate', '-warning', '-error', str(dng))
    report['dng']['exiftool_validate'] = tag_value(dng_validate, 'Validate')
    report['dng']['exiftool_warnings'] = tag_value(dng_validate, 'Warning')
    dng_tags = exiftool('-Make', '-Model', '-ImageWidth', '-ImageHeight',
                        '-BitsPerSample', '-CFAPattern', '-ColorMatrix1',
                        '-AsShotNeutral', '-WhiteLevel', '-BlackLevel',
                        '-PhotometricInterpretation', str(dng))
    for name in ('Make', 'Camera Model Name', 'Image Width', 'Image Height',
                 'Bits Per Sample', 'CFAPattern', 'WhiteLevel', 'BlackLevel',
                 'PhotometricInterpretation'):
        value = tag_value(dng_tags, name)
        if value is not None:
            report['dng'][name.replace(' ', '_').lower()] = value

    data = dng.read_bytes()
    entries = {tag: value for tag, _, _, value, _ in
               stock_iq._ifd(data, int.from_bytes(data[4:8], 'little'), '<')}
    offset = int.from_bytes(entries[273], 'little')
    count = int.from_bytes(entries[279], 'little')
    if offset + count != len(data):
        raise SystemExit('%s: strip extent differs from the file' % dng)
    reference = make_dng.build(data[offset:], stock_iq.read(str(jpeg)))
    report['dng']['reference_parity'] = 'byte for byte' if reference == data \
        else 'differs from the reference'
    report['dng']['reference_sha256'] = hashlib.sha256(reference).hexdigest()

    (directory / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['dng']['reference_parity'] == 'byte for byte' else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
