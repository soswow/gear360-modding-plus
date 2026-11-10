#!/usr/bin/env python3
"""Tests for frame_check: synthetic data only, no real images.

    python3 -m unittest test_frame_check -v
    python3 test_frame_check.py

Importing frame_check does not run its main(), so these tests never touch
the command line path.
"""
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import frame_check  # noqa: E402  (path set up above)


# --- synthetic builders -----------------------------------------------------

def build_frame(height, width, stride, value_fn):
    """A synthetic frame: value_fn(y, x) -> a 16-bit sample."""
    data = bytearray(height * stride)
    for y in range(height):
        for x in range(width):
            struct.pack_into("<H", data, y * stride + x * 2, value_fn(y, x) & 0xFFFF)
    return bytes(data)


def build_bmp(width, height, pixel_fn, depth=24, compression=0, signature=b"BM",
              dib_size=40, planes=1, file_size=None, drop_bytes=0, bottom_up=False):
    """A synthetic BMP: pixel_fn(x, y) -> (r, g, b).

    `dib_size` is the size the header claims; the header itself is always a
    full 40-byte BITMAPINFOHEADER.  `bottom_up` stores the rows the way a
    positive height says they are stored.
    """
    row_stride = ((width * 3 + 3) // 4) * 4
    off = 54
    body = bytearray()
    ys = range(height - 1, -1, -1) if bottom_up else range(height)
    for y in ys:
        row = bytearray()
        for x in range(width):
            r, g, b = pixel_fn(x, y)
            row += bytes((b, g, r))   # BMP stores BGR
        row += bytes(row_stride - len(row))
        body += row
    header = bytearray(off)
    header[0:2] = signature
    size = off + len(body) if file_size is None else file_size
    struct.pack_into("<I", header, 2, size)
    struct.pack_into("<I", header, 10, off)
    struct.pack_into("<I", header, 14, dib_size)
    struct.pack_into("<i", header, 18, width)
    struct.pack_into("<i", header, 22, height)
    struct.pack_into("<H", header, 26, planes)
    struct.pack_into("<H", header, 28, depth)
    struct.pack_into("<I", header, 30, compression)
    data = bytes(header) + bytes(body)
    return data[:-drop_bytes] if drop_bytes else data


def luma(r, g, b):
    """The same luma the checker uses (77+150+29 == 256, so grey == its luma)."""
    return (r * 77 + g * 150 + b * 29) >> 8


class Geometry:
    """Temporarily shrink the frame geometry so the tests stay small."""

    def __init__(self, height=8, width=8, stride=16, lens=4):
        self.new = (height, width, stride, lens)

    def __enter__(self):
        self.old = (frame_check.HEIGHT, frame_check.WIDTH,
                    frame_check.STRIDE, frame_check.LENS)
        (frame_check.HEIGHT, frame_check.WIDTH,
         frame_check.STRIDE, frame_check.LENS) = self.new
        return self

    def __exit__(self, *exc):
        (frame_check.HEIGHT, frame_check.WIDTH,
         frame_check.STRIDE, frame_check.LENS) = self.old
        return False


# --- the grids --------------------------------------------------------------

class FrameGridTests(unittest.TestCase):
    """The frame grid must average a cell over all of its sampled rows."""

    def setUp(self):
        # value(y, x): the left lens climbs with the row, the right lens is flat.
        self.value = lambda y, x: (1000 + 10 * y + x) if x < 4 else (100 + y)
        self.data = build_frame(8, 8, 16, self.value)

    def test_cell_mean_covers_every_sampled_row(self):
        """A cell holds the mean of all its rows, not the last row's mean."""
        with Geometry():
            grid = frame_check.frame_grid(self.data, 0, 2)
        # cell (cy, cx) covers rows cy*4..cy*4+3 and (left lens) columns cx*2..cx*2+1
        # value = 1000 + 10*y + x, so the mean is 1000 + 10*mean(y) + mean(x).
        self.assertAlmostEqual(grid[0][0], 1000 + 10 * 1.5 + 0.5)
        self.assertAlmostEqual(grid[0][1], 1000 + 10 * 1.5 + 2.5)
        self.assertAlmostEqual(grid[1][0], 1000 + 10 * 5.5 + 0.5)
        self.assertAlmostEqual(grid[1][1], 1000 + 10 * 5.5 + 2.5)
        # A single row's mean must not stand in for the cell: the old code
        # kept one sampled row (the last one at the real geometry).
        self.assertNotAlmostEqual(grid[0][0], 1000 + 10 * 3 + 0.5)
        self.assertNotAlmostEqual(grid[0][0], 1000 + 10 * 0 + 0.5)

    def test_a_middle_row_counts_towards_the_cell(self):
        """One bright row inside a cell must lift the whole cell's mean."""
        def value(y, x):
            return 1000 + (1000 if y == 3 else 0) if x < 4 else 100

        data = build_frame(8, 8, 16, value)
        with Geometry():
            grid = frame_check.frame_grid(data, 0, 2)
        # cell (0, 0) covers rows 0..3: three rows at 1000 and one at 2000.
        self.assertAlmostEqual(grid[0][0], 1250.0)
        self.assertAlmostEqual(grid[1][0], 1000.0)

    def test_halves_are_selected_separately(self):
        """half=0 is the left lens, half=1 is the right lens."""
        with Geometry():
            left = frame_check.frame_grid(self.data, 0, 2)
            right = frame_check.frame_grid(self.data, 1, 2)
        self.assertAlmostEqual(left[0][0], 1015.5)
        self.assertAlmostEqual(right[0][0], 101.5)
        self.assertTrue(all(left[r][c] > right[r][c] for r in range(2) for c in range(2)))

    def test_grids_sample_matching_areas(self):
        """The frame's cells and the JPEG's cells cover the same scene area.

        The JPEG is grey, so its luma equals its pixel value; the frame holds
        1000 + 10 * luma of the same pixel.  When both grids sample the same
        footprint the two grids are affine images of each other and correlate
        at 1.0; a mismatch of footprints breaks that.
        """
        # v(x, y) climbs across the cell, so a different footprint gives a
        # different mean.
        def v(x, y):
            if x >= 4:
                return 0
            idx = (y // 4) * 2 + (x // 2)
            return 10 * (idx + 1) + x

        jpeg_rows = []
        for y in range(8):
            row = bytearray()
            for x in range(8):
                g = v(x, y)
                row += bytes((g, g, g))
            jpeg_rows.append(row)

        def value(y, x):
            return 1000 + 10 * v(x, y) if x < 4 else 500

        data = build_frame(8, 8, 16, value)
        with Geometry():
            fgrid = frame_check.frame_grid(data, 0, 2)
            jgrid = frame_check.jpeg_grid(jpeg_rows, 8, 8, 0, 2)
        r = frame_check.corr(frame_check.flat(fgrid), frame_check.flat(jgrid))
        self.assertAlmostEqual(r, 1.0, places=9)

    def test_truncated_frame_is_rejected(self):
        with Geometry():
            with self.assertRaises(frame_check.FrameCheckError) as ctx:
                frame_check.frame_grid(self.data[:-16], 0, 2)
        self.assertIn("truncated", str(ctx.exception))


class JpegGridTests(unittest.TestCase):
    def test_half_selection(self):
        rows = []
        for y in range(8):
            row = bytearray()
            for x in range(8):
                g = 200 if x < 4 else 10
                row += bytes((g, g, g))
            rows.append(row)
        left = frame_check.jpeg_grid(rows, 8, 8, 0, 2)
        right = frame_check.jpeg_grid(rows, 8, 8, 1, 2)
        self.assertTrue(all(left[r][c] > right[r][c] for r in range(2) for c in range(2)))

    def test_odd_width_is_rejected(self):
        rows = [bytearray(9 * 3) for _ in range(4)]
        with self.assertRaises(frame_check.FrameCheckError) as ctx:
            frame_check.jpeg_grid(rows, 9, 4, 0, 2)
        self.assertIn("odd", str(ctx.exception))


# --- input and BMP validation ---------------------------------------------

class FrameValidationTests(unittest.TestCase):
    def test_truncated_frame_is_rejected(self):
        with Geometry():
            with self.assertRaises(frame_check.FrameCheckError) as ctx:
                frame_check.check_frame_size(b"\x00" * 32)
        self.assertIn("truncated", str(ctx.exception))

    def test_read_frame_rejects_a_short_file(self):
        with Geometry():
            with tempfile.TemporaryDirectory() as tmp:
                bad = Path(tmp) / "frame.bin"
                bad.write_bytes(b"\x00" * 32)
                with self.assertRaises(frame_check.FrameCheckError) as ctx:
                    frame_check.read_frame(bad)
        self.assertIn("truncated", str(ctx.exception))

    def test_read_frame_reports_a_missing_file(self):
        with self.assertRaises(frame_check.FrameCheckError) as ctx:
            frame_check.read_frame("/nonexistent/frame.bin")
        self.assertIn("cannot read", str(ctx.exception))

    def test_full_sized_frame_is_accepted(self):
        with Geometry():
            data = build_frame(8, 8, 16, lambda y, x: 1000)
            frame_check.check_frame_size(data)


class BmpTests(unittest.TestCase):
    def test_24bit_bmp_is_read(self):
        data = build_bmp(4, 2, lambda x, y: (10, 20, 30))
        rows, w, h = frame_check.parse_bmp(data)
        self.assertEqual((w, h), (4, 2))
        self.assertEqual(bytes(rows[0][:3]), bytes((10, 20, 30)))

    def test_bottom_up_bmp_is_flipped(self):
        # A positive height means the rows are stored bottom row first.
        data = build_bmp(2, 2, lambda x, y: (y * 100, 0, 0), bottom_up=True)
        parsed, w, h = frame_check.parse_bmp(data)
        self.assertEqual((w, h), (2, 2))
        self.assertEqual(bytes(parsed[0][:3]), bytes((0, 0, 0)))       # top row first
        self.assertEqual(bytes(parsed[1][:3]), bytes((100, 0, 0)))

    def test_non_24bit_bmp_is_rejected(self):
        for depth in (8, 16, 32):
            data = build_bmp(4, 2, lambda x, y: (10, 20, 30), depth=depth)
            with self.assertRaises(frame_check.FrameCheckError) as ctx:
                frame_check.parse_bmp(data)
            self.assertIn("24", str(ctx.exception))

    def test_compressed_bmp_is_rejected(self):
        data = build_bmp(4, 2, lambda x, y: (10, 20, 30), compression=1)
        with self.assertRaises(frame_check.FrameCheckError) as ctx:
            frame_check.parse_bmp(data)
        self.assertIn("compressed", str(ctx.exception))

    def test_truncated_bmp_is_rejected(self):
        data = build_bmp(4, 2, lambda x, y: (10, 20, 30))
        with self.assertRaises(frame_check.FrameCheckError) as ctx:
            frame_check.parse_bmp(data[:-4])
        self.assertIn("truncated", str(ctx.exception))

    def test_bad_signature_is_rejected(self):
        data = build_bmp(4, 2, lambda x, y: (10, 20, 30), signature=b"XX")
        with self.assertRaises(frame_check.FrameCheckError) as ctx:
            frame_check.parse_bmp(data)
        self.assertIn("signature", str(ctx.exception))

    def test_bad_dib_header_is_rejected(self):
        data = build_bmp(4, 2, lambda x, y: (10, 20, 30), dib_size=12)
        with self.assertRaises(frame_check.FrameCheckError) as ctx:
            frame_check.parse_bmp(data)
        self.assertIn("DIB", str(ctx.exception))

    def test_header_only_bmp_is_rejected(self):
        with self.assertRaises(frame_check.FrameCheckError) as ctx:
            frame_check.parse_bmp(b"BM" + b"\x00" * 20)
        self.assertIn("malformed", str(ctx.exception))


class JpegBmpTests(unittest.TestCase):
    """The BMP conversion must not use one shared temporary path."""

    def setUp(self):
        self.bmp = build_bmp(4, 2, lambda x, y: (10, 20, 30))
        self.calls = []
        self.saved = (frame_check.have_sips, frame_check.run_tool)

    def tearDown(self):
        frame_check.have_sips, frame_check.run_tool = self.saved

    def fake_sips(self, returncode=0, stderr=""):
        def run_tool(cmd):
            self.calls.append(list(cmd))
            if returncode == 0:
                out = cmd[cmd.index("--out") + 1]
                Path(out).write_bytes(self.bmp)
            return subprocess.CompletedProcess(cmd, returncode, "", stderr)
        return run_tool

    def test_each_call_uses_a_private_temp_path(self):
        frame_check.have_sips = lambda: True
        frame_check.run_tool = self.fake_sips()
        rows, w, h = frame_check.jpeg_bmp("main.jpg")
        self.assertEqual((w, h), (4, 2))
        self.assertEqual(len(rows), 2)
        outs = [c[c.index("--out") + 1] for c in self.calls]
        self.assertNotEqual(outs[0], "/tmp/_frame_check.bmp")
        self.assertFalse(Path(outs[0]).exists(), "the temporary BMP must be cleaned up")

        frame_check.jpeg_bmp("main.jpg")
        outs2 = [c[c.index("--out") + 1] for c in self.calls]
        self.assertNotEqual(outs[0], outs2[1], "each call needs its own path")

    def test_conversion_failure_is_reported(self):
        frame_check.have_sips = lambda: True
        frame_check.run_tool = self.fake_sips(returncode=1, stderr="sips: cannot open")
        with self.assertRaises(frame_check.FrameCheckError) as ctx:
            frame_check.jpeg_bmp("main.jpg")
        self.assertIn("sips", str(ctx.exception))

    def test_missing_sips_is_reported(self):
        frame_check.have_sips = lambda: False
        with self.assertRaises(frame_check.FrameCheckError) as ctx:
            frame_check.jpeg_bmp("main.jpg")
        self.assertIn("sips", str(ctx.exception))


class CliTests(unittest.TestCase):
    def test_usage_without_arguments(self):
        self.assertEqual(frame_check.main([]), 2)

    def test_import_does_not_run_main(self):
        self.assertTrue(hasattr(frame_check, "main"))
        self.assertFalse(getattr(frame_check, "__main__", False))


if __name__ == "__main__":
    unittest.main(verbosity=2)
