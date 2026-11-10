# Allocator record prefix reader

This freestanding ARM helper copies one fixed 0x7000-byte prefix from the allocator manager at
`0x81cbb000`. It does not copy image pixels. The allocator record table starts at manager+`0xb0`
and contains 16-byte records indexed by 16 KiB allocation units. The selected pixel pointer
`P = 0xa77bb000` is at arena unit 1518, byte offset `0x3000` within that unit. The last pixel byte
`P + 2,764,800 - 1` falls in unit 1687. The fixed prefix covers table entries 0 through 1780.

Before and after the copy, the helper checks these fixed values:

| Value | Address | Expected |
|---|---:|---:|
| Allocator manager pointer cell | `0x81c0d7e0` | `0x81cbb000` |
| Manager arena base field | `0x81d230b0` | `0xa6000000` |
| Selected image globals | `0x81952e50`, `0x81952fa4` | `0x82246000` |
| Image vptr | `0x82246000` | `0x8067a158` |
| Width, height, stride | `0x82246004`, `0x82246008`, `0x82246060` | `1920`, `960`, `2880` |
| PData | `0x82246084` | `0xa77bb000` |

The manager pointer and image pointers are validated as fixed scalar values; the helper never
follows a pointer from device memory. It maps only the fixed manager prefix and fixed guard pages
from ordinary RAM, all with `PROT_READ`, then copies the prefix to a Linux-owned anonymous buffer.
The device mappings and `/dev/mem` descriptor are dropped before raw bytes are written to stdout.
The only writable map is the Linux-owned bounce buffer. The helper makes no MMIO mappings, camera
commands, or device writes. Binary stdout is exactly 28,672 bytes; the `UNVERIFIED_ALLOCATOR_RECORDS`
diagnostic goes to stderr.

The existing `0x81d23000` guard-page map also supplies five volatile words at
`M+0x6859c..M+0x685ac` (`0x81d2359c..0x81d235ac`). The helper reports them on separate
`MOVIE_WORK_BEFORE` and `MOVIE_WORK_AFTER` diagnostic lines around the prefix copy. Retrieval
stores both five-word arrays and a `movie_work_descriptor_stable` flag in `metadata.json`. These
words are observations, not additional guards: a change does not reject an otherwise valid
prefix read. The helper does not follow the stored descriptor pointer.

The same already mapped image page supplies six more volatile observations from offsets `0x54`,
`0x58`, `0x5c`, `0xa4`, `0xa8`, and `0xb0`, sampled before and after the prefix copy. They are
reported as `IMAGE_FIELDS_BEFORE` and `IMAGE_FIELDS_AFTER`; retrieval stores both arrays and an
`image_fields_stable` flag. The words encode a format byte, four metadata bytes, the low byte of a
Bayer enum, an allocation-request word, backing-byte count, and a kind word. These are
observations rather than expected guards, so changes do not reject the prefix read. The fixed
image object is 0xc8 bytes. The older 160-byte active-image view ended before `0xa4`;
these new observations include that stored request word. Its value does not prove that an
allocation request was executed. Enum values are retained raw without assigning phase names.

Offline analysis identifies `getMovieWorkBuffer` as a generic allocation tagged `0x28` whose
returned base is stored at `M+0x6859c`; the descriptor's size getter multiplies fields at `+4`,
`+8`, and `+0xc`. The table rows in the copied prefix use tag `0x28`, with remaining-unit values
`4557 - index`. The selected pixel offset `0x17bb000` is nine 2,764,800-byte extents, and 4,557
16 KiB units are enough for the rounded size of 27 such extents. These arithmetic and descriptor
matches do not prove that this allocation is leased by the active image, that it owns the selected
pixel buffer, or that 27 frames were allocated or are available.

The prefix and descriptor values are unsynchronized observations. Stable guard values and copied
allocator records do not prove a lease, allocation ownership, pixel-buffer coherence, or a completed
DMA operation.
The purpose is to inspect the fixed records that may describe the units overlapped by the selected
pixel extent, while preserving that uncertainty.

## Build and host checks

```sh
sh tools/probes/allocator-record-reader/build.sh output/allocator-record-reader-build
python3 tools/probes/allocator-record-reader/test_host.py
```

The build is cross-compiled and statically inspected; it is never executed on the host. Host
contract tests emulate the syscall/mapping boundary and verify the exact read-only map ranges,
pre/post guards, both before/after observations, 28,672-byte copy and output, guard-failure
suppression, cleanup, and two identical ARM builds. Each test run snapshots the helper sources and
hashes them before production builds, then confirms the source hashes remain unchanged.

## Camera retrieval

The camera must already be reachable at `192.168.43.1`. Retrieval stages the helper over FTP,
executes only `allocator-records`, redirects its small binary stdout to a temporary camera file,
downloads that file over FTP, compares the camera and host SHA256 values, checks camera process
identity before and after, and removes the temporary files. It records the planned staging path
before upload for recovery. A unique timestamped evidence directory is used by default; `--out-dir`
selects another directory, which must be empty or new.

```sh
python3 tools/probes/allocator-record-reader/retrieve.py
python3 tools/probes/allocator-record-reader/retrieve.py --out-dir output/allocator-records
```

On success the saved file is `UNVERIFIED-allocator-records.bin`, with capture metadata and
transfer/cleanup records in the evidence directory. The reader does not treat these bytes as
proof that any allocator entry owns the selected pixel buffer.
