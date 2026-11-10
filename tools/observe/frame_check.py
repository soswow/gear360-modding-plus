#!/usr/bin/env python3
"""Check a still's RAW frame against the still's JPEG (no numpy, no PIL).

The frame is two lens images side by side: 5824 samples wide (2912 each) at
a 11648-byte stride, 2896 rows, 16-bit little-endian words. The JPEG is 5792
wide (2896 per lens). This prints:

  * the frame's phase means (a Bayer mosaic shows four different levels),
  * the correlation of each half of the frame with each half of the JPEG,
  * a colour preview for each of the four standard Bayer orders, so the order
    can be judged by eye (the method trial Q1 used).

The two correlation grids sample the *same* normalised areas: cell (cy, cx) of
a lens half is the same relative patch of the scene in the frame and in the
JPEG, and a cell's value is the mean of every sample taken inside that cell
(over all of the cell's sampled rows, not just the last one).

The JPEG is read through `sips`, which turns it into a BMP.

    python3 tools/observe/frame_check.py FRAME.bin MAIN.jpg
"""
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

STRIDE, WIDTH, HEIGHT, LENS = 11648, 5824, 2896, 2912
DARK = 3264          # historical F1 preview offset; not a calibrated black level
STEP = 8             # pixels per grid cell
Y0, Y1 = 600, 2400   # the rows the previews cover

CELL_SAMPLES_Y = 30  # sampled rows per cell
CELL_SAMPLES_X = 15  # sampled columns per cell

ORDERS = {
    "GRBG": {(0, 0): "G", (0, 1): "R", (1, 0): "B", (1, 1): "G"},
    "GBRG": {(0, 0): "G", (0, 1): "B", (1, 0): "R", (1, 1): "G"},
    "BGGR": {(0, 0): "B", (0, 1): "G", (1, 0): "G", (1, 1): "R"},
    "RGGB": {(0, 0): "R", (0, 1): "G", (1, 0): "G", (1, 1): "B"},
}


class FrameCheckError(Exception):
    """A user-facing error: bad input, a bad BMP, or a failed conversion."""


def check_frame_size(data):
    """Reject a frame that is too short to hold every row of the grid."""
    expected = HEIGHT * STRIDE
    if len(data) < expected:
        raise FrameCheckError(
            f"frame is truncated: {len(data)} bytes, need {expected} "
            f"({HEIGHT} rows x {STRIDE} bytes)")
    return expected


def read_frame(path):
    """The frame's bytes, with the size checked."""
    try:
        data = Path(path).read_bytes()
    except OSError as exc:
        raise FrameCheckError(f"cannot read the frame {path}: {exc}") from exc
    check_frame_size(data)
    return data


def cell_bounds(extent, cells, index):
    """The [start, end) span of one cell along an axis of `extent` pixels."""
    return (index * extent) // cells, ((index + 1) * extent) // cells


def sample_positions(start, end, count):
    """`count` evenly spread sample positions inside [start, end).

    The positions are relative to the span, so the frame and the JPEG sample
    the same relative places inside a cell even though their pixel counts
    differ slightly.
    """
    span = end - start
    if span <= 0:
        return []
    n = min(count, span)
    return [start + (i * span) // n for i in range(n)]


def parse_bmp(data):
    """The BMP as (rows of (r,g,b) bytes, width, height).

    Only uncompressed 24-bit BMPs are accepted; anything else is refused
    with a message that says what was wrong.
    """
    if len(data) < 54:
        raise FrameCheckError(
            f"BMP is malformed: {len(data)} bytes, need at least a 54-byte header")
    if data[:2] != b"BM":
        raise FrameCheckError(
            f"BMP is malformed: bad file signature {data[:2]!r} (expected b'BM')")
    file_size = struct.unpack_from("<I", data, 2)[0]
    off = struct.unpack_from("<I", data, 10)[0]
    dib = struct.unpack_from("<I", data, 14)[0]
    if dib < 40:
        raise FrameCheckError(
            f"BMP is malformed: DIB header size {dib} (need 40 or more)")
    w = struct.unpack_from("<i", data, 18)[0]
    h = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    depth = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if planes != 1:
        raise FrameCheckError(
            f"BMP is malformed: {planes} colour planes (expected 1)")
    if depth != 24:
        raise FrameCheckError(
            f"BMP is not 24-bit: bit depth is {depth} (only 24-bit BMPs are supported)")
    if compression != 0:
        raise FrameCheckError(
            f"BMP is compressed: compression field is {compression} "
            "(only uncompressed BMPs are supported)")
    if w <= 0 or h == 0:
        raise FrameCheckError(f"BMP is malformed: bad dimensions {w}x{h}")
    if off < 14 + dib:
        raise FrameCheckError(
            f"BMP is malformed: pixel data offset {off} overlaps the header")
    if file_size and file_size > len(data):
        raise FrameCheckError(
            f"BMP is truncated: {len(data)} bytes, but the header claims {file_size}")

    flip = h > 0
    height = abs(h)
    row_stride = ((w * 3 + 3) // 4) * 4
    need = off + row_stride * height
    if need > len(data):
        raise FrameCheckError(
            f"BMP is truncated: {len(data)} bytes, need {need} for a "
            f"{w}x{height} 24-bit image")
    rows = []
    for y in range(height):
        y0 = (height - 1 - y) if flip else y
        base = off + y0 * row_stride
        row = bytearray()
        for x in range(w):
            b, g, r = data[base + x * 3: base + x * 3 + 3]
            row += bytes((r, g, b))
        rows.append(row)
    return rows, w, height


def have_sips():
    """True when the sips converter is available."""
    return shutil.which("sips") is not None


def run_tool(cmd):
    """Run an external tool and return its CompletedProcess."""
    return subprocess.run(cmd, capture_output=True, text=True)


def jpeg_bmp(path):
    """The JPEG as (rows of (r,g,b) bytes, width, height)."""
    if not have_sips():
        raise FrameCheckError(
            "sips was not found on PATH; it is needed to turn the JPEG into a BMP")
    # A private directory per call: no shared path for two runs to fight over.
    with tempfile.TemporaryDirectory(prefix="frame_check_") as tmp:
        bmp = Path(tmp) / "jpeg.bmp"
        proc = run_tool(["sips", "-s", "format", "bmp", str(path), "--out", str(bmp)])
        if proc.returncode != 0:
            raise FrameCheckError(
                f"sips could not convert {path} to BMP: "
                f"{(proc.stderr or proc.stdout or '').strip()}")
        data = bmp.read_bytes()
    return parse_bmp(data)


def jpeg_grid(rows, w, h, half, cells):
    """The JPEG's half as a cells x cells grid of luma means.

    The cells are the same relative patches of the scene as the frame's cells
    (see `frame_grid`), and every sample inside a cell counts towards it.
    """
    if w % 2:
        raise FrameCheckError(
            f"JPEG width {w} is odd; the frame holds two lenses side by side")
    halfw = w // 2
    x_base = half * halfw
    grid = [[0.0] * cells for _ in range(cells)]
    count = [[0] * cells for _ in range(cells)]
    for cy in range(cells):
        y0, y1 = cell_bounds(h, cells, cy)
        for y in sample_positions(y0, y1, CELL_SAMPLES_Y):
            row = rows[y]
            for cx in range(cells):
                x0, x1 = cell_bounds(halfw, cells, cx)
                t = n = 0
                for x in sample_positions(x0, x1, CELL_SAMPLES_X):
                    r, g, b = row[(x_base + x) * 3: (x_base + x) * 3 + 3]
                    t += (r * 77 + g * 150 + b * 29) >> 8
                    n += 1
                grid[cy][cx] += t
                count[cy][cx] += n
    for cy in range(cells):
        for cx in range(cells):
            grid[cy][cx] /= max(count[cy][cx], 1)
    return grid


def frame_grid(data, half, cells):
    """The frame's half as a cells x cells grid of means.

    Each cell holds the mean of every sample taken inside it, over all of the
    cell's sampled rows.  The mean of the last sampled row must never stand
    in for the cell.
    """
    check_frame_size(data)
    base_x = half * LENS
    grid = [[0.0] * cells for _ in range(cells)]
    count = [[0] * cells for _ in range(cells)]
    for cy in range(cells):
        y0, y1 = cell_bounds(HEIGHT, cells, cy)
        for y in sample_positions(y0, y1, CELL_SAMPLES_Y):
            off = y * STRIDE
            if off + STRIDE > len(data):
                raise FrameCheckError(
                    f"frame is truncated: row {y} needs {off + STRIDE} bytes, "
                    f"have {len(data)}")
            row = struct.unpack_from("<%dH" % LENS, data, off + base_x * 2)
            for cx in range(cells):
                x0, x1 = cell_bounds(LENS, cells, cx)
                t = n = 0
                for x in sample_positions(x0, x1, CELL_SAMPLES_X):
                    t += row[x]
                    n += 1
                grid[cy][cx] += t
                count[cy][cx] += n
    for cy in range(cells):
        for cx in range(cells):
            grid[cy][cx] /= max(count[cy][cx], 1)
    return grid


def phase_means(data):
    """The 2x2 phase means over the whole frame."""
    check_frame_size(data)
    out = {}
    for py in (0, 1):
        for px in (0, 1):
            t = n = 0
            # 16 is even, so every row sampled keeps the parity of py
            for y in range(py, HEIGHT, 16):
                off = y * STRIDE
                for x in range(0, WIDTH, 16):
                    xx = x + (px - x) % 2
                    t += struct.unpack_from("<H", data, off + xx * 2)[0]
                    n += 1
            out[(py, px)] = t / max(n, 1)
    return out


def flat(g):
    return [v for row in g for v in row]


def corr(a, b):
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    da = sum((x - ma) ** 2 for x in a) ** 0.5
    db = sum((y - mb) ** 2 for y in b) ** 0.5
    return num / (da * db) if da and db else float("nan")


def demosaic(data, mapping, half):
    """The half demosaiced with one Bayer order, as rows of bytes."""
    base_x = half * LENS
    rows = []
    for y in range(Y0, Y1 - STEP, STEP):
        row = bytearray()
        for x in range(0, LENS - STEP, STEP):
            acc = {"R": 0, "G": 0, "B": 0}
            cnt = {"R": 0, "G": 0, "B": 0}
            for dy in range(STEP):
                off = (y + dy) * STRIDE
                for dx in range(STEP):
                    v = struct.unpack_from("<H", data, off + (base_x + x + dx) * 2)[0]
                    col = mapping[(dy % 2, dx % 2)]
                    acc[col] += v
                    cnt[col] += 1
            row += bytes((int(max(0, acc["R"] / cnt["R"] - DARK)) >> 8,
                          int(max(0, acc["G"] / cnt["G"] - DARK)) >> 8,
                          int(max(0, acc["B"] / cnt["B"] - DARK)) >> 8))
        rows.append(row)
    return rows


def write_ppm(path, rows, scale=1.0):
    w = len(rows[0]) // 3
    out = bytearray()
    for row in rows:
        for i in range(0, len(row), 3):
            out += bytes((min(255, int(row[i] * scale)),
                          min(255, int(row[i + 1] * scale)),
                          min(255, int(row[i + 2] * scale))))
    Path(path).write_bytes(b"P6\n%d %d\n255\n" % (w, len(rows)) + bytes(out))
    jpg = str(path).replace(".ppm", ".jpg")
    proc = run_tool(["sips", "-s", "format", "jpeg", path, "--out", jpg])
    if proc.returncode != 0:
        raise FrameCheckError(
            f"sips could not convert {path} to JPEG: "
            f"{(proc.stderr or proc.stdout or '').strip()}")
    return jpg


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if len(argv) not in (2, 3):
        print("usage: frame_check.py FRAME.bin MAIN.jpg [OUTDIR]", file=sys.stderr)
        return 2
    frame, jpeg = argv[0], argv[1]
    outdir = Path(argv[2]) if len(argv) > 2 else Path("/tmp")
    try:
        return run(frame, jpeg, outdir)
    except (FrameCheckError, OSError) as exc:
        print(f"frame_check: error: {exc}", file=sys.stderr)
        return 1


def run(frame, jpeg, outdir=Path("/tmp")):
    data = read_frame(frame)
    print(f"frame: {len(data)} bytes, {WIDTH}x{HEIGHT}, stride {STRIDE}")

    means = phase_means(data)
    print("2x2 phase means: " + ", ".join(
        f"row{k[0]}/col{k[1]} {means[k]:.0f}" for k in sorted(means)))
    ordered = sorted(means, key=means.get)
    print(f"  darkest {ordered[0]}, brightest {ordered[-1]}, "
          f"the two brightest are {ordered[-2]} and {ordered[-1]}")

    rows, w, h = jpeg_bmp(jpeg)
    print(f"jpeg: {w}x{h}")
    cells = 24
    grids = {half: jpeg_grid(rows, w, h, half, cells) for half in (0, 1)}
    frames = {half: frame_grid(data, half, cells) for half in (0, 1)}
    print("\ncorrelation (rows: the frame's half, columns: the JPEG's half)")
    print("               jpeg-left  jpeg-right")
    for half, name in ((0, "left"), (1, "right")):
        print(f"frame {name:<6} {corr(flat(frames[half]), flat(grids[0])):9.4f} "
              f"{corr(flat(frames[half]), flat(grids[1])):9.4f}")

    print("\ncolour previews (the order that matches the JPEG is the right one):")
    outdir.mkdir(parents=True, exist_ok=True)
    for name, mapping in ORDERS.items():
        preview = demosaic(data, mapping, 0)
        vals = sorted(v for row in preview for i in range(0, len(row), 3) for v in row[i:i + 3])
        hi = vals[int(len(vals) * 0.995)]
        print(f"  {name}: {write_ppm(str(outdir / f'frame-{name}.ppm'), preview, 255.0 / max(hi, 1))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
