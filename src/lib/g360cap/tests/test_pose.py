#!/usr/bin/env python3
"""The pose maths, its XMP packet, and the packet inside a JPEG and a DNG."""
import ctypes as C
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from test_dng import Frame, Image, HERE, maker, fixture
from test_output import ExifInfo

NS = {"rdf": "http://www.w3.org/1999/02/22-rdf-syntax-ns#",
      "GPano": "http://ns.google.com/photos/1.0/panorama/",
      "g360": "http://gear360-modding.invalid/ns/pose/1.0/"}


class Stats(C.Structure):
    _fields_ = [("samples", C.c_uint32), ("accel_sum", C.c_double * 3), ("accel_sq", C.c_double),
                ("gyro_sq", C.c_double)]


class Pose(C.Structure):
    _fields_ = [("samples", C.c_uint32), ("raw_accel", C.c_double * 3), ("up", C.c_double * 3),
                ("pitch_deg", C.c_double), ("roll_deg", C.c_double), ("gyro_rms_dps", C.c_double),
                ("accel_std_g", C.c_double)]


def parse_xmp(text):
    assert text.startswith('<?xpacket begin="﻿"') and text.endswith('<?xpacket end="w"?>')
    body = text[text.index("<x:xmpmeta"):text.index("</x:xmpmeta>") + len("</x:xmpmeta>")]
    desc = ET.fromstring(body).find("rdf:RDF/rdf:Description", NS)
    out = {}
    for k, v in desc.attrib.items():
        uri, name = k[1:].split("}")
        for prefix in ("GPano", "g360"):
            if uri == NS[prefix]: out[prefix + ":" + name] = v
    return out


def main():
    with tempfile.TemporaryDirectory(prefix="g360-pose-") as td:
        work = Path(td)
        cc = os.environ.get("CC", "cc")
        flags = [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
                 "-I" + str(HERE), "-I" + str(HERE.parent / "capture-session")]
        so = work / "pose.so"
        subprocess.run(flags + ["-shared", "-fPIC", *[str(HERE / n) for n in
                       ("g360_pose.c", "g360_output.c", "g360_dng.c", "g360_exif.c")], "-lm", "-o", str(so)],
                       check=True)
        lib = C.CDLL(str(so))
        lib.g360_pose_from_stats.argtypes = [C.POINTER(Stats), C.POINTER(Pose)]
        lib.g360_pose_xmp.argtypes = [C.POINTER(Pose), C.c_char_p, C.c_size_t, C.POINTER(C.c_size_t)]
        lib.g360_pose_stats_add.argtypes = [C.POINTER(Stats), C.POINTER(C.c_int16 * 3), C.POINTER(C.c_int16 * 3)]
        lib.g360_pose_stats_init.argtypes = [C.POINTER(Stats)]

        def pose_of(samples):
            stats = Stats()
            lib.g360_pose_stats_init(C.byref(stats))
            for a, g in samples:
                lib.g360_pose_stats_add(C.byref(stats), C.byref((C.c_int16 * 3)(*a)), C.byref((C.c_int16 * 3)(*g)))
            pose = Pose()
            return lib.g360_pose_from_stats(C.byref(stats), C.byref(pose)), pose

        # Counts for +1 g on a given axis as the history delivers them: 8197 * g.
        off, sc = (0, 0, 0), (8197, 8197, 8197)

        def reading(up):
            return [round(off[k] + sc[k] * up[k]) for k in range(3)]

        def pose_for(up, n=50):
            rc, pose = pose_of([(reading(up), (0, 0, 0))] * n)
            assert rc == 0 and pose.samples == n
            return pose

        p = pose_for((0, 0, -1))  # upright
        assert abs(p.pitch_deg) < 0.05 and abs(p.roll_deg) < 0.05, (p.pitch_deg, p.roll_deg)
        assert abs(p.up[2] + 1) < 1e-3 and p.gyro_rms_dps == 0 and p.accel_std_g == 0
        # Front lens (looks along -Y) tipped up by 30 deg: the lens points up, so the
        # camera's up leans toward +Y? Up in the camera frame: a lens pointing above
        # the horizon has the world's up vector with a component *against* the lens
        # direction, along +Y.
        a = math.radians(30)
        p = pose_for((0, math.sin(a), -math.cos(a)))
        assert abs(p.pitch_deg + 30) < 0.1, p.pitch_deg  # that is the lens tipped DOWN 30 deg
        p = pose_for((0, -math.sin(a), -math.cos(a)))
        assert abs(p.pitch_deg - 30) < 0.1 and abs(p.roll_deg) < 0.1, (p.pitch_deg, p.roll_deg)
        # Rolled clockwise by 20 deg as seen looking out of the front lens: the right
        # side (+X) goes down, so up leans toward -X.
        a = math.radians(20)
        p = pose_for((-math.sin(a), 0, -math.cos(a)))
        assert abs(p.roll_deg - 20) < 0.1 and abs(p.pitch_deg) < 0.1, (p.pitch_deg, p.roll_deg)
        p = pose_for((math.sin(a), 0, -math.cos(a)))
        assert abs(p.roll_deg + 20) < 0.1
        # Upside down: roll is 180 (or -180), pitch 0.
        p = pose_for((0, 0, 1))
        assert abs(abs(p.roll_deg) - 180) < 0.1 and abs(p.pitch_deg) < 0.1
        # Lens straight up: pitch 90, roll reported as 0, up still defined.
        p = pose_for((0, -1, 0))
        assert abs(p.pitch_deg - 90) < 0.1 and p.roll_deg == 0 and abs(p.up[1] + 1) < 1e-3
        # Stillness numbers: a gyro of 100 counts on one axis is 7 deg/s; accel noise shows.
        rc, p = pose_of([((0, 50, -8000 + (200 if i % 2 else -200)), (100, 0, 0)) for i in range(40)])
        assert rc == 0 and abs(p.gyro_rms_dps - 7.0) < 1e-6 and abs(p.accel_std_g - 200 / 8197.0) < 1e-6
        # No samples, or a zero vector with the offsets removed: refused, pose zeroed.
        assert pose_of([])[0] == -1

        # The packet.
        p = pose_for((-math.sin(a), 0.1, -math.cos(a)), n=37)
        buf = C.create_string_buffer(2048)
        size = C.c_size_t()
        assert lib.g360_pose_xmp(C.byref(p), buf, 2048, C.byref(size)) == 0
        text = buf.raw[:size.value].decode("utf-8")
        assert size.value < 2048 and len(buf.value) == size.value
        attrs = parse_xmp(text)
        assert abs(float(attrs["GPano:PoseRollDegrees"]) - p.roll_deg) < 0.01
        assert abs(float(attrs["GPano:PosePitchDegrees"]) - p.pitch_deg) < 0.01
        assert attrs["g360:Samples"] == "37" and attrs["g360:PoseVersion"] == "1"
        assert abs(float(attrs["g360:UpX"]) - p.up[0]) < 1e-4
        assert abs(float(attrs["g360:RawAccelZ"]) - p.raw_accel[2]) < 0.06
        assert "GPano:PoseHeadingDegrees" not in attrs and "GPano:ProjectionType" not in attrs
        assert lib.g360_pose_xmp(C.byref(p), buf, 100, C.byref(size)) == -1 and size.value == 0
        assert lib.g360_pose_xmp(None, buf, 2048, C.byref(size)) == -1
        xmp = text.encode("utf-8")

        # The packet inside a JPEG: Exif first, then XMP, the rest unchanged.
        note_bytes = maker()
        note_buf = C.create_string_buffer(note_bytes)
        note = Image(C.cast(note_buf, C.POINTER(C.c_ubyte)), len(note_bytes), 0, 0, 0, 0, 0, 2)
        original = b"\xff\xd8\xff\xe0\x00\x04ab\xff\xd9"
        jpg_buf = C.create_string_buffer(original)
        jpg = Image(C.cast(jpg_buf, C.POINTER(C.c_ubyte)), len(original), 5792, 2896, 0, 0, 0, 0x22)
        info = ExifInfo(5792, 2896, 1, 250, 100, 1, 0, b"2026:11:07 09:30:00")
        lib.g360_save_jpeg_exif_xmp.argtypes = [C.POINTER(Image), C.POINTER(Image), C.POINTER(ExifInfo),
                                                C.c_char_p, C.c_char_p]
        path = work / "pose.jpg"
        assert lib.g360_save_jpeg_exif_xmp(C.byref(jpg), C.byref(note), C.byref(info), xmp, os.fsencode(path)) == 0
        data = path.read_bytes()
        exif_len = struct.unpack_from(">H", data, 4)[0]
        at = 4 + exif_len
        assert data[:2] == b"\xff\xd8" and data[4 + 2:4 + 8] == b"Exif\0\0" and data[at:at + 2] == b"\xff\xe1"
        xmp_len = struct.unpack_from(">H", data, at + 2)[0]
        head = b"http://ns.adobe.com/xap/1.0/\0"
        assert data[at + 4:at + 4 + len(head)] == head
        assert data[at + 4 + len(head):at + 2 + xmp_len] == xmp
        assert data[at + 2 + xmp_len:] == original[2:]
        # Without XMP it is the plain Exif file, and the plain entry point agrees.
        plain = work / "plain.jpg"
        assert lib.g360_save_jpeg_exif_xmp(C.byref(jpg), C.byref(note), C.byref(info), None, os.fsencode(plain)) == 0
        assert data[:at] + data[at + 2 + xmp_len:] == plain.read_bytes()
        keep = work / "keep.jpg"
        keep.write_bytes(b"keep")
        assert lib.g360_save_jpeg_exif_xmp(C.byref(jpg), C.byref(note), C.byref(info), b"", os.fsencode(keep)) == 1
        assert lib.g360_save_jpeg_exif_xmp(C.byref(jpg), C.byref(note), C.byref(info), b"x" * 4097,
                                           os.fsencode(keep)) == 1
        assert keep.read_bytes() == b"keep"

        # The packet inside a DNG: tag 700 in IFD0, the rest of the file as without it.
        pixels = bytes(range(256)) * (11648 * 2896 // 256)
        raw = C.create_string_buffer(pixels)
        image = Image(C.cast(raw, C.POINTER(C.c_ubyte)), len(pixels), 5824, 2896, 11648, 16, 0, 0x102)
        frame = Frame(image, 0, 0, 3)
        lib.g360_save_dng_full_xmp.argtypes = [C.POINTER(Frame), C.POINTER(Image), C.POINTER(ExifInfo), C.c_char_p,
                                               C.c_char_p, C.c_char_p]
        with_xmp, without = work / "pose.dng", work / "plain.dng"
        assert lib.g360_save_dng_full_xmp(C.byref(frame), C.byref(note), C.byref(info), b"HDR SWEEP 4/6", xmp,
                                          os.fsencode(with_xmp)) == 0
        assert lib.g360_save_dng_full_xmp(C.byref(frame), C.byref(note), C.byref(info), b"HDR SWEEP 4/6", None,
                                          os.fsencode(without)) == 0
        d = with_xmp.read_bytes()
        assert len(d) == len(without.read_bytes()) == 40960 + len(pixels)
        assert d[40960:] == pixels
        ifd0 = struct.unpack_from("<I", d, 4)[0]
        count = struct.unpack_from("<H", d, ifd0)[0]
        entries = {}
        for i in range(count):
            tag, typ, cnt, val = struct.unpack_from("<HHII", d, ifd0 + 2 + 12 * i)
            entries[tag] = (typ, cnt, val)
        typ, cnt, off_xmp = entries[700]
        assert typ == 1 and cnt == len(xmp) and d[off_xmp:off_xmp + cnt] == xmp
        assert 700 in entries and list(entries) == sorted(entries)
        assert parse_xmp(d[off_xmp:off_xmp + cnt].decode())["g360:Samples"] == "37"
        # The largest packet the writer accepts still fits the fixed header, with a description.
        assert lib.g360_save_dng_full_xmp(C.byref(frame), C.byref(note), C.byref(info), b"HDR SWEEP 20261107-093000 FRAME 8/8",
                                          b"x" * 4096, os.fsencode(work / "max.dng")) == 0
        keep.write_bytes(b"keep")
        assert lib.g360_save_dng_full_xmp(C.byref(frame), C.byref(note), C.byref(info), None, b"x" * 4097,
                                          os.fsencode(keep)) == 1
        assert keep.read_bytes() == b"keep"

        if shutil.which("exiftool"):
            for f in (path, with_xmp):
                r = subprocess.run(["exiftool", "-j", "-n", "-validate", "-warning", "-error", "-G1",
                                    "-XMP-GPano:PoseRollDegrees", "-XMP-GPano:PosePitchDegrees", str(f)],
                                   capture_output=True, text=True, check=True)
                row = json.loads(r.stdout)[0]
                assert abs(row["XMP-GPano:PoseRollDegrees"] - p.roll_deg) < 0.01, (f, row)
                assert abs(row["XMP-GPano:PosePitchDegrees"] - p.pitch_deg) < 0.01
                assert not [k for k in row if k.endswith("Error")], row
    print("pose maths, XMP packet, JPEG APP1 and DNG tag 700 passed")


if __name__ == "__main__":
    main()
