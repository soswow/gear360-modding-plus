#!/usr/bin/env python3
"""Inspect an unverified buffer snapshot. This does not establish sensor origin."""

import argparse
import hashlib
import json
import math
import struct
import zlib
from pathlib import Path

FRAGMENT_SHA256 = "59c5a5d172ed0487a0d2f76d935c175ae57e3a6ce75a4b16f366afebcb1ec9cb"
WIDTH = 1920
HEIGHT = 960
STRIDE = 2880
BUFFER_BYTES = STRIDE * HEIGHT


def unpack_pair_le(byte0, byte1, byte2):
    """Little-endian packed 12-bit: low byte first, two samples in three bytes."""
    sample0 = byte0 | ((byte1 & 0x0F) << 8)
    sample1 = (byte1 >> 4) | (byte2 << 4)
    return sample0, sample1


def unpack_pair_be(byte0, byte1, byte2):
    """MSB-first packed 12-bit, the other common split of the same three bytes."""
    sample0 = (byte0 << 4) | (byte1 >> 4)
    sample1 = ((byte1 & 0x0F) << 8) | byte2
    return sample0, sample1


def unpack_pairs(data, unpack_pair):
    count = len(data) // 3
    samples = [0] * (count * 2)
    for index in range(count):
        sample0, sample1 = unpack_pair(data[3 * index], data[3 * index + 1], data[3 * index + 2])
        samples[2 * index] = sample0
        samples[2 * index + 1] = sample1
    return samples


def pearson_adjacent(samples, width):
    """Correlation of horizontally neighboring samples. None when a side is constant."""
    count = len(samples) // width
    n = 0
    sum_x = sum_y = sum_xx = sum_yy = sum_xy = 0
    for row in range(count):
        base = row * width
        for col in range(width - 1):
            x = samples[base + col]
            y = samples[base + col + 1]
            n += 1
            sum_x += x
            sum_y += y
            sum_xx += x * x
            sum_yy += y * y
            sum_xy += x * y
    return _pearson(n, sum_x, sum_y, sum_xx, sum_yy, sum_xy)


def pearson_vertical(samples, width):
    height = len(samples) // width
    n = 0
    sum_x = sum_y = sum_xx = sum_yy = sum_xy = 0
    for row in range(height - 1):
        base = row * width
        nxt = base + width
        for col in range(width):
            x = samples[base + col]
            y = samples[nxt + col]
            n += 1
            sum_x += x
            sum_y += y
            sum_xx += x * x
            sum_yy += y * y
            sum_xy += x * y
    return _pearson(n, sum_x, sum_y, sum_xx, sum_yy, sum_xy)


def _pearson(n, sum_x, sum_y, sum_xx, sum_yy, sum_xy):
    if n < 2:
        return None
    num = n * sum_xy - sum_x * sum_y
    den_x = n * sum_xx - sum_x * sum_x
    den_y = n * sum_yy - sum_y * sum_y
    if den_x <= 0 or den_y <= 0:
        return None
    return num / math.sqrt(den_x * den_y)


def byte_lag_correlation(data, lag):
    n = len(data) - lag
    if n < 2:
        return None
    sum_x = sum_y = sum_xx = sum_yy = sum_xy = 0
    for index in range(n):
        x = data[index]
        y = data[index + lag]
        sum_x += x
        sum_y += y
        sum_xx += x * x
        sum_yy += y * y
        sum_xy += x * y
    return _pearson(n, sum_x, sum_y, sum_xx, sum_yy, sum_xy)


def write_png(path, width, height, pixels):
    """8-bit grayscale PNG. pixels is one byte per pixel, row-major."""
    if len(pixels) != width * height:
        raise ValueError("png size does not match dimensions")

    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(
            ">I", zlib.crc32(tag + payload) & 0xFFFFFFFF
        )

    raw = b"".join(b"\x00" + pixels[row * width : (row + 1) * width] for row in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def scale_shift4(samples):
    return bytes((value >> 4) & 0xFF for value in samples)


def scale_stretch(samples):
    histogram = [0] * 4096
    for value in samples:
        histogram[value] += 1
    total = len(samples)
    low_target = max(1, total // 100)
    high_target = total - low_target
    seen = 0
    low = 0
    high = 4095
    for value, count in enumerate(histogram):
        seen += count
        if seen >= low_target:
            low = value
            break
    seen = 0
    for value, count in enumerate(histogram):
        seen += count
        if seen >= high_target:
            high = value
            break
    if high <= low:
        high = low + 1
    span = high - low
    return bytes(min(255, max(0, (value - low) * 255 // span)) for value in samples)


def mask_fit(data):
    """How many aligned 16-bit words sit inside the old candidate masks."""
    views = []
    for endian in ("little", "big"):
        for phase in (0, 1):
            blob = data[phase:]
            count = len(blob) // 2
            fit_3ffc = 0
            fit_0fff = 0
            maximum = 0
            for index in range(count):
                word = int.from_bytes(blob[index * 2 : index * 2 + 2], endian)
                maximum = max(maximum, word)
                if word & ~0x3FFC == 0:
                    fit_3ffc += 1
                if word & ~0x0FFF == 0:
                    fit_0fff += 1
            views.append(
                {
                    "endian": endian,
                    "byte_phase": phase,
                    "words": count,
                    "max": maximum,
                    "fit_mask_3ffc": fit_3ffc,
                    "fit_mask_0fff": fit_0fff,
                }
            )
    return views


def summarize_samples(samples):
    total = len(samples)
    if total == 0:
        return {"count": 0}
    maximum = max(samples)
    bits_10_and_11 = sum(1 for value in samples if value & 0xC00)
    return {
        "count": total,
        "min": min(samples),
        "max": maximum,
        "mean": sum(samples) / total,
        "samples_with_bits_10_or_11": bits_10_and_11,
    }


def self_check(fragment_path):
    data = Path(fragment_path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != 4096 or digest != FRAGMENT_SHA256:
        raise SystemExit("fragment identity does not match the saved 4096-byte sample")
    if unpack_pair_le(0x06, 0x90, 0x01) != (0x006, 0x019):
        raise SystemExit("packed12 pair decode changed")
    # Round-trip the extremes of a 12-bit field.
    for sample0, sample1 in ((0, 0), (1, 2), (0x3FF, 0x155), (0xFFF, 0xFFF), (630, 107)):
        byte0 = sample0 & 0xFF
        byte1 = ((sample0 >> 8) & 0x0F) | ((sample1 & 0x0F) << 4)
        byte2 = (sample1 >> 4) & 0xFF
        if unpack_pair_le(byte0, byte1, byte2) != (sample0, sample1):
            raise SystemExit("packed12 round-trip failed")
    samples = unpack_pairs(data[: len(data) // 3 * 3], unpack_pair_le)
    sample0 = samples[0::2]
    sample1 = samples[1::2]
    stats0 = summarize_samples(sample0)
    stats1 = summarize_samples(sample1)
    if round(stats0["mean"]) != 49 or round(stats1["mean"]) != 34:
        raise SystemExit("fragment means do not match the published packed12 reading")
    if stats0["max"] != 630 or stats1["max"] != 107:
        raise SystemExit("fragment maxima do not match the published packed12 reading")
    if stats0["samples_with_bits_10_or_11"] or stats1["samples_with_bits_10_or_11"]:
        raise SystemExit("fragment high bits do not match the published packed12 reading")
    window = sample0[640:768]
    if sum(window) / len(window) < 150:
        raise SystemExit("bright-run window was not found at first-sample indexes 640:768")
    if sum(sample0[600:640]) / 40 > 80:
        raise SystemExit("bytes before the bright run are no longer the dark floor")
    print("PASS: packed12 decoder matches the saved 4096-byte fragment")


def analyze_full(data, fragment, out_dir):
    if len(data) != BUFFER_BYTES:
        raise SystemExit(f"expected {BUFFER_BYTES} bytes, got {len(data)}")
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(data).hexdigest()
    groups = BUFFER_BYTES // 3
    le = unpack_pairs(data, unpack_pair_le)
    be = unpack_pairs(data, unpack_pair_be)
    if len(le) != WIDTH * HEIGHT:
        raise SystemExit("packed12 sample count is not 1920x960")

    def layout_scores(samples):
        return {
            "horizontal_correlation": pearson_adjacent(samples, WIDTH),
            "vertical_correlation": pearson_vertical(samples, WIDTH),
            **summarize_samples(samples),
        }

    sample0 = le[0::2]
    sample1 = le[1::2]
    # 128 groups: the published window ends at byte offset 2304, which is index 768 exclusive.
    window = 128
    row_window_means = []
    for row in range(HEIGHT):
        base = row * (WIDTH // 2)
        chunk = sample0[base + 640 : base + 640 + window]
        row_window_means.append(sum(chunk) / len(chunk))

    identical_rows = 0
    max_row_delta = 0.0
    max_row_delta_index = 0
    row_means = []
    for row in range(HEIGHT):
        base = row * WIDTH
        row_values = le[base : base + WIDTH]
        row_means.append(sum(row_values) / WIDTH)
        if row + 1 < HEIGHT:
            nxt = le[base + WIDTH : base + 2 * WIDTH]
            if row_values == nxt:
                identical_rows += 1
            delta = sum(abs(a - b) for a, b in zip(row_values, nxt)) / WIDTH
            if delta > max_row_delta:
                max_row_delta = delta
                max_row_delta_index = row

    byte2_values = set()
    low_nibble_values = set()
    for index in range(0, BUFFER_BYTES, 3):
        low_nibble_values.add(data[index + 1] & 0x0F)
        byte2_values.add(data[index + 2])

    prefix = data[:4096]
    prefix_sha = hashlib.sha256(prefix).hexdigest()
    report = {
        "label": "UNVERIFIED",
        "bytes": len(data),
        "sha256": digest,
        "width": WIDTH,
        "height": HEIGHT,
        "stride": STRIDE,
        "prefix_4096_sha256": prefix_sha,
        "prefix_matches_saved_fragment": prefix_sha == FRAGMENT_SHA256,
        "saved_fragment_sha256": None if fragment is None else hashlib.sha256(fragment).hexdigest(),
        "packed12_le": layout_scores(le),
        "packed12_be": layout_scores(be),
        "packed12_le_sample0": {
            **summarize_samples(sample0),
            "horizontal_correlation": pearson_adjacent(sample0, WIDTH // 2),
            "vertical_correlation": pearson_vertical(sample0, WIDTH // 2),
        },
        "packed12_le_sample1": {
            **summarize_samples(sample1),
            "horizontal_correlation": pearson_adjacent(sample1, WIDTH // 2),
            "vertical_correlation": pearson_vertical(sample1, WIDTH // 2),
        },
        "byte2_min": min(byte2_values),
        "byte2_max": max(byte2_values),
        "byte2_distinct": len(byte2_values),
        "middle_low_nibble_distinct": sorted(low_nibble_values),
        "byte_lag_correlation": {
            "2880": byte_lag_correlation(data, 2880),
            "1920": byte_lag_correlation(data, 1920),
            "2400": byte_lag_correlation(data, 2400),
            "3840": byte_lag_correlation(data, 3840),
        },
        "raw8_stride2880": {
            "horizontal_correlation": pearson_adjacent(data, STRIDE),
            "vertical_correlation": pearson_vertical(data, STRIDE),
        },
        "row0_s0_window_640_768_mean": row_window_means[0],
        "row1_s0_window_640_768_mean": row_window_means[1],
        "rows_with_s0_window_mean_above_100": sum(1 for value in row_window_means if value > 100),
        "identical_consecutive_rows": identical_rows,
        "largest_mean_abs_row_step": max_row_delta,
        "largest_mean_abs_row_step_after_row": max_row_delta_index,
        "row_mean_first8": row_means[:8],
        "row_mean_last8": row_means[-8:],
        "word16_masks": mask_fit(data),
        "limits": (
            "Unverified memory snapshot. Stable guards do not exclude a torn frame, "
            "concurrent DMA, reclamation, or ABA, and do not establish sensor origin, "
            "fresh completion, or full native resolution."
        ),
    }
    # 8-bit views are for inspection. Shift-4 keeps the 12-bit code in a byte; stretch is display-only.
    images = {
        "packed12-le-shift4.png": scale_shift4(le),
        "packed12-le-stretched.png": scale_stretch(le),
        "packed12-le-sample0-stretched.png": scale_stretch(sample0),
        "packed12-le-sample1-stretched.png": scale_stretch(sample1),
        "packed12-be-stretched.png": scale_stretch(be),
    }
    write_png(out_dir / "packed12-le-shift4.png", WIDTH, HEIGHT, images["packed12-le-shift4.png"])
    write_png(out_dir / "packed12-le-stretched.png", WIDTH, HEIGHT, images["packed12-le-stretched.png"])
    write_png(out_dir / "packed12-le-sample0-stretched.png", WIDTH // 2, HEIGHT, images["packed12-le-sample0-stretched.png"])
    write_png(out_dir / "packed12-le-sample1-stretched.png", WIDTH // 2, HEIGHT, images["packed12-le-sample1-stretched.png"])
    write_png(out_dir / "packed12-be-stretched.png", WIDTH, HEIGHT, images["packed12-be-stretched.png"])
    # Side-by-side halves of the stretched 1920-wide frame, in case the surface is two squares.
    left_half = bytearray(WIDTH // 2 * HEIGHT)
    right_half = bytearray(WIDTH // 2 * HEIGHT)
    stretched = images["packed12-le-stretched.png"]
    for row in range(HEIGHT):
        src = row * WIDTH
        dst = row * (WIDTH // 2)
        left_half[dst : dst + WIDTH // 2] = stretched[src : src + WIDTH // 2]
        right_half[dst : dst + WIDTH // 2] = stretched[src + WIDTH // 2 : src + WIDTH]
    write_png(out_dir / "packed12-le-left-stretched.png", WIDTH // 2, HEIGHT, bytes(left_half))
    write_png(out_dir / "packed12-le-right-stretched.png", WIDTH // 2, HEIGHT, bytes(right_half))
    (out_dir / "analysis.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description="Inspect an unverified full-buffer snapshot")
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--fragment", type=Path)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()
    fragment = args.fragment or Path(
        "output/decompile/rea/pass-completion-20261016/preview-sample-runtime/UNVERIFIED-preview-sample.bin"
    )
    if args.self_check:
        self_check(fragment)
        return
    if args.input is None or args.out_dir is None:
        raise SystemExit("full inspection requires --input and --out-dir")
    data = args.input.read_bytes()
    saved = fragment.read_bytes() if fragment.exists() else None
    report = analyze_full(data, saved, args.out_dir)
    print(json.dumps({
        "sha256": report["sha256"],
        "prefix_matches_saved_fragment": report["prefix_matches_saved_fragment"],
        "packed12_le_horizontal": report["packed12_le"]["horizontal_correlation"],
        "packed12_le_vertical": report["packed12_le"]["vertical_correlation"],
        "packed12_be_horizontal": report["packed12_be"]["horizontal_correlation"],
        "row0_window": report["row0_s0_window_640_768_mean"],
        "row1_window": report["row1_s0_window_640_768_mean"],
    }, indent=2))


if __name__ == "__main__":
    main()
