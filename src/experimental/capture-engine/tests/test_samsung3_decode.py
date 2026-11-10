#!/usr/bin/env python3
"""Host round-trip tests for the from-scratch Samsung3 decoder.

A Python reference encoder builds streams with the exact bit order the
reference reader consumes (little-endian 32-bit words, bits taken from the
top), and the C decoder must reproduce the target raster. The encoder is an
independent implementation of the same documented algorithm; the round trip
catches bit-reader, predictor, Bayer-position and sign-extension mistakes.
"""
import os
import struct
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cc = os.environ.get("CC", "cc")


class BitWriter:
    """Mirrors the reference reader: bits are packed MSB-first into the
    logical stream; every 32 logical bits become a little-endian word whose
    top bits are consumed first."""

    def __init__(self):
        self.bits = []

    def write(self, value, n):
        for shift in range(n - 1, -1, -1):
            self.bits.append((value >> shift) & 1)

    def to_bytes(self):
        padded = list(self.bits)
        while len(padded) % 32:
            padded.append(0)
        out = bytearray()
        for start in range(0, len(padded), 32):
            chunk = padded[start:start + 32]
            words = [0, 0, 0, 0]
            for bit_index, bit in enumerate(chunk):
                byte_idx = 3 - (bit_index // 8)
                bit_pos = 7 - (bit_index % 8)
                words[byte_idx] |= bit << bit_pos
            out += bytes(words)
        return bytes(out)


def encode_header(depth_code, optflags, init_val):
    writer = BitWriter()
    writer.write(0, 16)          # ignored
    writer.write(0, 4)           # ignored
    writer.write(depth_code, 4)  # bit_depth = code + 1
    writer.write(0, 8)           # ignored
    writer.write(0, 16)
    writer.write(0, 16)
    writer.write(0, 16)
    writer.write(0, 4)           # ignored
    writer.write(optflags, 4)
    writer.write(0, 16)
    writer.write(0, 8)
    writer.write(0, 2)
    writer.write(init_val, 14)
    return writer.to_bytes()     # exactly 16 bytes


def bayer_rcol(row, col, i):
    if row % 2:
        return col + ((i & 0x7) << 1) + 1 - (i >> 3)
    return col + ((i & 0x7) << 1) + (i >> 3)


def encode_raster(target, width, height, depth_code, optflags, init_val,
                  diff_len):
    """Reference encoder for the motion-7 / literal-length paths only."""
    stream = bytearray(encode_header(depth_code, optflags, init_val))
    decoded = list(target)
    datamax = (1 << (depth_code + 1)) - 1

    for row in range(height):
        writer = BitWriter()
        for col in range(0, width - 15, 16):
            if (optflags & 4) == 0 and (col & 63) == 0:
                writer.write(0, 2)      # scale code 0: scale += 0
            writer.write(1, 1)          # 1-bit 1: motion stays 7
            # Predictions at sequential positions.
            for i in range(16):
                if col == 0:
                    pred = init_val
                else:
                    pred = decoded[row * width + col + i - 2]
                decoded[row * width + col + i] = pred
            # All four 2-bit flags first, then the 4-bit literals for the
            # flag==3 entries (the reference reads them in that order).
            for _ in range(4):
                writer.write(3, 2)      # flag 3: literal 4-bit length
            for _ in range(4):
                writer.write(diff_len, 4)
            # Differences at the Bayer positions.
            for i in range(16):
                rcol = bayer_rcol(row, col, i)
                bidx = row * width + rcol
                pred = decoded[bidx]
                diff = target[bidx] - pred
                diff = max(-(1 << (diff_len - 1)),
                           min((1 << (diff_len - 1)) - 1, diff))
                writer.write(diff & ((1 << diff_len) - 1), diff_len)
                value = pred + diff
                decoded[bidx] = max(0, min(datamax, value))
        body = writer.to_bytes()
        # 16-byte row alignment relative to the data offset (stream start 0
        # is congruent to the 16-byte header).
        stream += body
        while (len(stream) - 16) % 16:
            stream += b"\x00"
    return bytes(stream)


def build_decoder_cli(work):
    main = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "samsung3.h"

int main(int argc, char **argv)
{
    uint8_t *input;
    uint16_t *output;
    size_t capacity;
    uint32_t width, height;
    g360_samsung3_header header;
    FILE *f;
    long size;
    int rc;

    if (argc != 5) return 2;
    width = (uint32_t)strtoul(argv[1], 0, 0);
    height = (uint32_t)strtoul(argv[2], 0, 0);
    f = fopen(argv[3], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) return 2;
    input = (uint8_t *)malloc((size_t)size);
    if (!input) return 2;
    if (fread(input, 1, (size_t)size, f) != (size_t)size) return 2;
    fclose(f);
    capacity = (size_t)width * height * 2;
    output = (uint16_t *)malloc(capacity);
    if (!output) return 2;
    rc = g360_samsung3_decode(input, (size_t)size, width, height,
                              G360_SAMSUNG3_HEADER_BYTES, &header, output,
                              capacity);
    if (rc != 0) { fprintf(stderr, "decode rc=%d\n", rc); return 3; }
    printf("%u %u %u %u\n", header.bit_depth, header.datamax,
           header.optflags, header.init_val);
    f = fopen(argv[4], "wb");
    if (!f) return 2;
    fwrite(output, 1, capacity, f);
    fclose(f);
    return 0;
}
'''
    (work / "cli.c").write_text(main)
    subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
         f"-I{root}", str(work / "cli.c"), str(root / "samsung3.c"),
         "-o", str(work / "cli")],
        check=True)
    return work / "cli"


def decode(cli, stream, width, height):
    stream_path = cli.parent / "stream.bin"
    raster_path = cli.parent / "raster.bin"
    stream_path.write_bytes(stream)
    proc = subprocess.run(
        [str(cli), str(width), str(height), str(stream_path), str(raster_path)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert proc.returncode == 0, proc.stderr.decode()
    parts = proc.stdout.split()
    header = {
        "bit_depth": int(parts[0]),
        "datamax": int(parts[1]),
        "optflags": int(parts[2]),
        "init_val": int(parts[3]),
    }
    samples = list(struct.unpack(f"<{width * height}H", raster_path.read_bytes()))
    return header, samples


def main():
    with tempfile.TemporaryDirectory(prefix="samsung3-test-") as td:
        work = Path(td)
        cli = build_decoder_cli(work)

        width, height = 32, 4
        depth_code = 9          # bit_depth 10
        optflags = 1            # force the difference-length flags
        init_val = 512

        # Test 1: constant raster, minimal stream.
        target = [init_val] * (width * height)
        stream = encode_raster(target, width, height, depth_code,
                               optflags, init_val, 1)
        header, samples = decode(cli, stream, width, height)
        assert header["bit_depth"] == 10, header
        assert header["datamax"] == 1023, header
        assert header["optflags"] == optflags, header
        assert header["init_val"] == init_val, header
        assert samples == target, "constant round trip failed"

        # Test 2: smooth gradient, 7-bit differences; the initial sample is
        # the first pixel, so the first block's differences stay small.
        target = []
        for y in range(height):
            for x in range(width):
                target.append((x * 3 + y * 5 + 17) % 1024)
        init_val = target[0]
        stream = encode_raster(target, width, height, depth_code,
                               optflags, init_val, 7)
        header, samples = decode(cli, stream, width, height)
        assert samples == target, "gradient round trip failed"

        # Test 3: a second header's field extraction.
        stream2 = encode_raster([init_val] * (width * height), width, height,
                                12, 1, 999, 1)  # depth code 12 -> 13-bit
        header, _ = decode(cli, stream2, width, height)
        assert header["bit_depth"] == 13, header
        assert header["datamax"] == 8191, header
        assert header["init_val"] == 999, header

    print("samsung3 decode round-trip tests passed")


if __name__ == "__main__":
    main()
