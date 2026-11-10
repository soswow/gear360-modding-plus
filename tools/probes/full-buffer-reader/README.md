# Passive full-buffer reader

This freestanding ARM helper copies one fixed ordinary-RAM extent into Linux-owned memory and writes those bytes to stdout. It is a snapshot of the currently selected image buffer, not a capture and not a verified RAW file.

The extent is exactly `2880 * 960 = 2,764,800` bytes (`675` pages) at `0xa77bb000`. Expected image fields, checked before and after the copy:

| Field | Address | Expected |
|---|---|---|
| Selected-image global | `0x81952e50` | `0x82246000` |
| Selected-image global | `0x81952fa4` | `0x82246000` |
| vptr | `0x82246000` | `0x8067a158` |
| width | `0x82246004` | 1920 |
| height | `0x82246008` | 960 |
| stride | `0x82246060` | 2880 |
| PData | `0x82246084` | `0xa77bb000` |

Any mismatch aborts. The helper does not follow a different pointer, change the length, map MMIO, issue an imaging command, or write camera memory. Stable guards do not prove that DMA was idle or that the copied bytes are one coherent sensor frame.

Stdout is raw bytes only, and only after the postchecks, device unmap, and `/dev/mem` close. The one-line diagnostic goes to stderr. A hex dump of this buffer would exceed the existing 1 MiB HTTP response cap, so retrieval redirects stdout to a temporary file and downloads that file over FTP.

The earlier 4096-byte reader in `../preview-sample-reader/` is unchanged.

## Optional cadence observation

The default `full-buffer` mode retains its existing diagnostic and behavior. The optional
`full-buffer-observed` mode reads the known read-only VD-falling counter at `0x81952d38`
(offset `0xd38` in the already mapped `0x81952000` page) immediately before and after the
volatile pixel copy. It reports both values in its stderr diagnostic. Firmware analysis records
that this callback counter wraps at 10,000, so `retrieve.py` records `delta_mod_10000` only when
both samples are in the range 0–9,999. For example, 9,998 → 2 yields a modulo delta of 4. If a
sample is outside that range, the read remains accepted and the metrics record the raw values,
mark the observation unexpected, and leave the delta null.

The two observed runs advanced by 9,102 → 9,111 (9 modulo 10,000) and 74 → 81 (7). These
positive differences show that the cadence callback counter advanced during the copy interval,
so it would be inaccurate to call the copy instantaneous. They do not prove DMA wrote the bytes
being copied or establish WDMA completion, buffer ownership, cache coherence, or that the bytes
belong to one frame. Multiple full counter wraps cannot be distinguished from these two samples,
so the reported delta is not total elapsed frames or elapsed time. The mode adds no device
mapping, syscall, pointer traversal, or write.

## Build

`build.sh` uses `-O2` with both loop and SLP vectorization disabled. The volatile source loop
therefore compiles to scalar 32-bit loads and stores instead of vector reads beyond its exact
word count. This is a performance optimization only; it cannot make an unsynchronized buffer
copy coherent or prove that DMA has finished.

```sh
sh tools/probes/full-buffer-reader/build.sh output/full-buffer-reader-build
```

The ELF is not executed on the Mac. `build.sh` only prints its ELF headers.

## Host tests

```sh
python3 tools/probes/full-buffer-reader/test_host.py
```

The tests mock the mappings and run the C logic as an optimized macOS binary. They check the fixed pages, the 2,764,800-byte read-only extent, the anonymous bounce buffer, guard failure, cleanup, and exact binary output. They also rebuild the ELF twice, compare it, and check that the optimized static ARM ELF has no interpreter or SIMD/NEON memory instructions. The complete optimized disassembly is saved in `optimized-reader-review/full-buffer-reader.disasm.txt` inside the test output directory (the optional first argument to `test_host.py`). They do not execute camera firmware.

## Camera retrieval

The camera must be the access point at `192.168.43.1`. This does not join that network for you.

```sh
python3 tools/probes/full-buffer-reader/retrieve.py
```

The script creates a unique timestamped evidence directory under `output/decompile/rea/`.
Pass `--out-dir PATH` to choose a destination; it must be new or empty. A nonempty destination
is refused before camera access so existing evidence is preserved. On success the snapshot is
`UNVERIFIED-full-buffer.bin`, with the helper hash, camera and Mac SHA256, commands, and
cleanup record in that directory. The planned camera staging path is recorded before upload
for recovery if staging is interrupted. The temporary helper, sample, and directory are
removed from the camera.

To also record the VD-falling cadence values around the copy:

```sh
python3 tools/probes/full-buffer-reader/retrieve.py --observed
```

Choose an explicit evidence directory with `--out-dir PATH --observed`. Observed results remain
unverified snapshots and the counter values do not prove completion or coherence. Inspect bytes with:

```sh
python3 tools/probes/full-buffer-reader/analyze_buffer.py \
  --input output/decompile/rea/full-buffer-read-20261016/UNVERIFIED-full-buffer.bin \
  --out-dir output/decompile/rea/full-buffer-read-20261016/inspection
```

`--self-check` checks the decoder against the saved 4096-byte fragment only.

An earlier camera snapshot is saved at `output/decompile/rea/full-buffer-read-20261016/UNVERIFIED-full-buffer.bin` (SHA256 `5c7a7c6ce9fb20d467e43806cd85350024311ee288a4dafcdd1a73c2e19f06e9`). It is an unverified memory image. The inspection pictures are in that directory's `inspection/` folder.
