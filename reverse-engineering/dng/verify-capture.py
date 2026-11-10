#!/usr/bin/env python3
"""Verify fetched camera DNGs against make_dng.py and JPEG IQ against sidecars.

    python3 verify-capture.py output/dng-T1T2 --expect-exif --render

Rendering uses macOS sips for the first DNG. Large captures remain local.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess

import make_dng
import stock_iq


def verify(directory, expect_exif=False, render=False):
    results = {"directory": str(directory), "dng": [], "jpeg": []}
    for path in sorted(directory.glob("shot-*.dng")):
        data = path.read_bytes()
        if data[:4] != b"II*\0": raise ValueError(f"{path}: not little-endian TIFF")
        entries = {tag: value for tag, _, _, value, _ in stock_iq._ifd(
            data, struct.unpack_from("<I", data, 4)[0], "<")}
        offset = struct.unpack("<I", entries[273])[0]
        count = struct.unpack("<I", entries[279])[0]
        if offset + count != len(data): raise ValueError(f"{path}: strip extent differs")
        note = path.with_name(path.stem + "-makernote.bin")
        if offset == make_dng.FULL_HEADER_BYTES:
            # With metadata: rebuild from the strip, the sidecar buffer and the
            # Exif fields the file carries, then also check its MakerNote.
            exif, description = make_dng.read_full_exif(data)
            expected = make_dng.build_full(data[offset:], stock_iq.read(str(note)), note.read_bytes(),
                                           exif, description)
        else:
            expected = make_dng.build(data[offset:], stock_iq.read(str(note)))
        if data != expected: raise ValueError(f"{path}: differs from Python reference")
        item = {"file": path.name, "bytes": len(data), "header_bytes": offset, "python_byte_parity": True}
        if offset == make_dng.FULL_HEADER_BYTES:
            item["exif"] = make_dng.read_full_exif(data)[0]
        results["dng"].append(item)
    for path in sorted(directory.glob("shot-*.jpg")):
        data = path.read_bytes()
        if not expect_exif: continue
        tags = stock_iq.makernote(data)
        stem = path.stem.rsplit("-", 1)[0]
        note = stock_iq.buffer_records((directory / (stem + "-makernote.bin")).read_bytes())
        for tag in (0x60, 0xa0, 0xa1, 0xc0):
            if tags[tag] != note[tag]: raise ValueError(f"{path}: IQ record {tag:x} differs")
        stock_iq.read(str(path))  # all records decode, not merely signature checks
        tiff = stock_iq.exif_tiff(data)
        ifd0 = {tag: v for tag, _, _, v, _ in stock_iq._ifd(tiff, 8, ">")}
        at = struct.unpack(">I", ifd0[0x8769])[0]
        exif = {tag: v for tag, _, _, v, _ in stock_iq._ifd(tiff, at, ">")}
        results["jpeg"].append({"file": path.name,
            "exposure": list(struct.unpack(">II", exif[0x829a])),
            "iso": struct.unpack(">H", exif[0x8827])[0], "iq_sidecar_equal": True})
    # Independent structural validator, including every JPEG when Exif is expected.
    files = [directory / x["file"] for x in results["dng"] + results["jpeg"]]
    if shutil.which("exiftool") and files:
        r = subprocess.run(["exiftool", "-j", "-n", "-validate", "-warning", "-error",
                            *map(str, files)], capture_output=True, text=True, check=True)
        for item in json.loads(r.stdout):
            if item["Validate"] not in (0, "0 0 0") or "Warning" in item or "Error" in item:
                raise ValueError(f"ExifTool: {item}")
        results["exiftool"] = "all OK"
    if render and results["dng"]:
        path = directory / results["dng"][0]["file"]
        target = path.with_suffix(".png")
        subprocess.run(["sips", "-s", "format", "png", str(path), "--out", str(target)],
                       check=True, capture_output=True)
        results["render"] = str(target)
    if not files: raise ValueError("no requested outputs found")
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("directory", type=Path)
    p.add_argument("--expect-exif", action="store_true")
    p.add_argument("--render", action="store_true")
    args = p.parse_args()
    print(json.dumps(verify(args.directory, args.expect_exif, args.render), indent=2))


if __name__ == "__main__":
    main()
