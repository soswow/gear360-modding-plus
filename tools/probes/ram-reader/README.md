# Fixed-scope read-only camera RAM reader

A small static ARM EABI diagnostic for the Samsung Gear360 SM-R210 Linux side. It opens `/dev/mem` **O_RDONLY**, maps **PROT_READ | MAP_SHARED**, copies only the fixed ranges below, prints them, and unmaps/closes. It has no memory-write, ioctl, cache-maintenance, command-dispatch, restart, firmware-patch or capture function. It accepts no arbitrary address/length and never follows an observed pointer.

| Exact argument | Fixed physical offsets (hex) | Bytes copied |
|---|---|---|
| `signature` | `80000000` | 64 |
| `copy-signature` | `81006000` | 64 |
| `text-signature` | `80541000` | 64 |
| `globals` | `818843b0/b4/b8`, `81952fc0/fc4`, `81952e50`, `81952fa4` | 4 each |
| `settings10` | `81882968` | 100 |
| `debug-image` | `81883f30` | 144 |
| `frontend-meta` | `81d7f800`, `81d7fa80` | 128 + 4 |
| `active-image` | `82246000` | 160 |
| `live-cells` | `81952d38`, `81c0d7e0` | 4 each |
| `allocator-meta` | `81d230b0` | 40 |
| `enc-ring-state` | `85401c50`, `854342ad` | 2 cursor bytes and 25 ready bytes |
| `ipcc-map-state` | root `80008218`, data `81000080`, `81000180` | 4 bytes each |
| `peer-state` | guard `814765e8`, slots/count `8147660c`, flags `814768d4`, `81483254` | guard 4, data 20 + 1 + 1 |
| `enc-export-state` | `81482ea4` | 4 bytes |
| `svc6-dispatch-state` | root `80000138`, slot `81004018` | root guard 4, data 4 |
| `manager-vptr-state` | guards `81c0d7bc`, `81c0d7e0`, vptr `81cbb000` | guard 4 + 4, data 4 |
| `profile8-fields` | guard `8188257c`, data `818828a4/a8` | guard 4, data 8 |

Each mapping is one 4096-byte ordinary-RAM page. No imaging MMIO region is mapped. Invalid arguments are rejected before opening the device.

The metadata profiles are specific to the observed instance, not generic pointer walkers. `frontend-meta` requires cell `81952fc0` to equal `81d7f800` before/after and checks derived vtable pointer `806718a0`. `active-image` requires cells `81952e50` and `81952fa4` to equal `82246000` before/after and checks vtable pointer `8067a158`. `allocator-meta` requires manager cell `81c0d7e0` to equal `81cbb000` before/after and the copied arena-base word to equal `a6000000`. On a mismatch it aborts without emitting a successful data record. New addresses after reboot require a new bounded source review; do not disable guards.

`ipcc-map-state` maps guard page `80008000` and requires the word at
`80008218` to equal `81000000` before reading the fixed data page
`81000000`. It copies only the two four-byte words at `81000080` and
`81000180`, then requires the guard word to remain exactly `81000000`.
The root value is an observation and a current mapping hypothesis; it is not
proof of an active CPU page-table entry, translation coherence, object
ownership, or a usable capture buffer. The mode never maps a pointer read from
RAM.

`enc-export-state` maps only fixed ordinary-RAM page `81482000` and reads the
u32 at `81482ea4` twice, rejecting a change. It prints one four-byte record
only after unmap and close succeed. This is the fixed global word loaded by
REA-recovered getter `80546c54`, not a pointer-selected address. Matching reads
do not exclude ABA or stale caches, and do not predict the next capture's mode.
Require exit0 and the complete record after validating identity and both code
signatures.

`svc6-dispatch-state` maps only the guard page `80000000` and the data page
`81004000`. The word at `80000138` must equal `81004000` **before** the data
page is mapped or read. It then reads the four bytes at `81004018` twice,
rejects a change between the two reads, and requires the guard word to still be
`81004000`. It unmaps both pages, closes `/dev/mem`, and only then emits one
four-byte record for `81004018`. The recorded word is an observation: the
static registration at `8002386c` composed target `80023fc0` in the pinned RTOS
image, but a different stable word is reported unchanged instead of being
rejected, so the parent decides whether to interpret or abort the route. The
root word is a guard that the expected page is the one in use; it is not proof
of an active CPU vector, an installed handler, SCTLR state, translation
coherence, freshness, ownership or a lease. The helper exposes one host-only
observation point between the two stability reads so a host test can model a
concurrent writer; the target build compiles it out.

`manager-vptr-state` maps only the guard page `81c0d000` and the data page
`81cbb000`. Two guard words must hold **before** the data page is mapped: the
manager cell at `81c0d7e0` must equal `81cbb000` (the same fixed manager the
`allocator-meta` profile already guards) and the factory vptr at `81c0d7bc`
must equal `80626500`. It then reads the four bytes at `81cbb000` twice,
rejects a change between the two reads, re-checks both guard words and requires
them to be unchanged, unmaps both pages, closes `/dev/mem`, and only then emits
one four-byte record for `81cbb000`. The recorded word is the manager object's
vptr as an **observation**: an unexpected but stable word is reported unchanged
instead of being rejected, so the parent decides what it means. REA recovered
the fixed source coordinates in the pinned RTOS image (`8054a744` constructs and
returns the factory at `81c0d7bc`, `800a0d04` writes its vptr `80626500`,
`800a0f74` returns factory+`0x24`, `80086b20` uses it as the memory manager);
those are source coordinates in the pinned image, not proof about a live object.
The helper never follows an observed pointer. Both pages are ordinary RAM
already covered by the fixed profile context; no MMIO region is mapped. Require
exit0 and the complete four-byte record. Stable guards do not establish
atomicity, coherence, object ownership, a live manager instance or a lease.

`peer-state` maps only pages `81476000` and `81483000`. The receiver vptr
at `814765e8` must equal `805ed2c0` before and after the read. It copies four
fixed category-`0x10` callback slots and their count at `8147660c` (20 bytes),
then the RTOS destination-alive and pre-reply flags at `814768d4` and
`81483254`. It rejects count >4 or a changed count, unmaps both pages, closes,
and only then emits three records. Callback pointers are values, never followed.
REA bound the singleton, constructor, subscription layout and guard pool to the
pinned RTOS. Require exit0 and all three complete records; output failure may
leave a partial record. Stable guards do not establish atomicity, coherence,
a fresh handshake, listener invocation or ownership.

`enc-ring-state` maps the fixed pages `0x85401000` and `0x85434000`. It
reads the two ENC cursors before and after the ready bytes and rejects a cursor
change. It emits its two records only after both unmaps and close succeed.
Require exit status 0 and both complete output lines; a failed stdout write can
leave partial output. The helper does not interpret the cursors or ready bytes.
The selector-to-base and ring offsets were checked through REA Ghidra; see
the ring contract (not published: `../../../output/decompile/rea/pass-enc-ring-20261020/parent-review.md`).
Validate camera identity and the two immutable signatures before using this
profile on a running camera.

These guards do not prevent ABA, concurrent field changes, byte tearing or stale cache observations. They do not pin an object or pixel buffer. The parent must also inspect the vtable pointer in the copied bytes; the separate live guard read is not an atomic snapshot.

## Build and validation

Run `sh tools/probes/ram-reader/build.sh`. It uses existing host LLVM
clang/lld and produces `output/ram-reader-build/ram-reader.elf`, without
installation or target execution. To preserve a separately reviewed build:

```sh
sh tools/probes/ram-reader/build.sh output/enc-ring-reader-build
sh tools/probes/ram-reader/tests/run.sh
CC=/usr/bin/clang SANITIZE=1 sh tools/probes/ram-reader/tests/run.sh
```

`tests/run.sh` keeps its build products inside the tree in `tests/.out/`
(override with `OUT_DIR=`), so nothing is written outside the workspace.

Version 10 adds `manager-vptr-state`: 14872 bytes, SHA-256:

`34ea3aea04378bd7d4794b3193c0b25f8bc7a502448c7fb460bf01974e21c507`

Six host profile tests (the five earlier profiles plus `manager-vptr-state`), a
plain run and an Apple Clang ASan/UBSan run, the Linux ARM host-seam rejection
and the ARMv7 static build all passed during initial local validation.
The later parent camera observation is recorded below. The new host test covers the exact two pages and
their order, O_RDONLY and read-only mappings, a wrong manager cell or a wrong
factory vptr that never maps the data page, changed manager cell and changed
factory vptr rejection after the reads, a changed manager vptr word between the
two stability reads, map/unmap/close failures, short and failed stdout writes,
argument rejection before open, and an unexpected but stable vptr word accepted
as a raw observation. The ARM build is reproducible: two builds of the same
source produced the same bytes and the same hash.

Version 9 adds `svc6-dispatch-state`: 13732 bytes, SHA-256:

`48eed6900179e58ee2d8c0dc082a0eacb067a9fecba74c2e369ba2f9ef954498`

Five host profile tests (the four earlier profiles plus `svc6-dispatch-state`),
a plain run and an Apple Clang ASan/UBSan run, the Linux ARM host-seam
rejection and the ARMv7 static build all passed. **This build is LOCAL and
source-only: it was not uploaded to a camera and no camera observation was
made.** The new host test covers the exact two pages, the root-first order,
O_RDONLY and read-only mappings, the single exact record, a wrong initial root
that never maps the data page, changed-root and changed-slot rejection before
cleanup, map/unmap/close failures, short and failed stdout writes, argument
rejection before open, and a valid but unexpected stable word accepted as an
observation.

Version 8 adds `enc-export-state`: 12836 bytes, SHA-256:

`4a5ecf7a3aa34fdf4f840b621ddbea731b51a8f3cd0b0c1048d2b34a81849df3`

All four profile host tests, ASan/UBSan, ARM shim rejection and ARM build passed.
The21 October10:52 AEDT read returned gate0. Both immutable anchors, app identity,
upload round-trip and camera hash matched; peer/map checks also passed again.
The exact temporary helper and directory were removed and absence verified.
This is an observation of the gate, not a completed capture or lease. See
the export-gate review (not published: `../../../output/decompile/rea/pass-peer-alive-20261021/parent-review.md`).

Version 7 adds `peer-state`: 12424 bytes, SHA-256:

`19364ca54dc64567598364a4f9f16f7e6b09aebea408aeed80a31eb9070e7145`

Host profile/error-path tests, ARM shim rejection and ARM build passed. The
21 October09:38 AEDT read-only check found one callback, `8005e13c`, and both
flags1; code signatures, app identity, upload round-trip and camera hash matched.
The owned helper and temporary directory were removed, absence verified. See
the reviewed peer check (not published: `../../../output/decompile/rea/pass-peer-alive-20261021/parent-review.md`).

Version 6 adds `ipcc-map-state`: 11388 bytes, SHA-256:

`edaa56b2ad7ba0a9dbb2ab4a536a164859681a8824268899fdc318d10846c249`

Its host syscall-shim checks and ARM build passed. On 21 October the parent
verified upload round-trip, camera hash, two immutable RTOS anchors and app
identity, then read the two mapping words successfully and removed the helper.
See the reviewed mapping check (not published: `../../../output/decompile/rea/pass-owned-receive-20261021/parent-review.md`).
Require exit0 and both complete records; an output failure can leave a partial
record. Neither the root guard nor the two word copies are an atomic CPU
translation-state observation.

Previous version 5, including `enc-ring-state`, is 10548 bytes, SHA-256:

`1d711d6d43f75eb367eef7f5d622dd30c22fbb1734a06cc329fd9472c7e65958`

Parent target build: ARM32 little-endian static EXEC, no interpreter, dynamic
dependencies or undefined imports, with a non-executable stack. The camera's
libc and earlier runs independently confirmed mmap2 syscall 192 and 4096-byte
page offsets. **Version 5 passed its first on-camera read-only check on 20 October 2026.**
After reconnection, uploaded bytes and camera hash matched; both immutable RTOS
signatures matched. `enc-ring-state` exited 0 with both cursors and all 25 ready
bytes zero. App identity was unchanged and temporary files were removed. See
the live review (not published: `../../../output/decompile/rea/pass-enc-ring-20261020/parent-review.md`).
This validates the diagnostic profile, not result freshness or RAW ownership.

The tracked host tests execute the same C entry point through an explicit
syscall shim. They cover the new profiles' exact pages, O_RDONLY/read-only
mappings, output bytes, cursor change, invalid arguments before open, short and
failed writes, map/unmap/close failures and cleanup. The `svc6-dispatch-state`
test additionally covers the root-first guard order, a changed dispatch slot
between the two stability reads, and an unexpected but stable slot word. The
`manager-vptr-state` test additionally covers the two-guard order, each guard
mismatch refusing to map the data page, each guard changing after the reads, and
an unexpected but stable manager vptr word. The
test script also verifies
that a Linux ARM build rejects the host shim. These checks do not establish live
mapping, cache coherence, fresh completion or sample ownership.

Previously deployed version 4 was 9604 bytes, SHA-256
`2cf81d50e299d4cde2c8c492b183881a607841e524f1ff512f2662cd9b6189a6`.
Its earlier evidence includes host checks for the ten original profiles and
on-camera signature/metadata observations. Do not use that hash for version 5.

## Authorized camera use and observed results

The user explicitly approved temporary upload/execution of this fixed read-only reader. Each staged version was FTP-downloaded back for an exact byte comparison, checked with camera-side SHA256, and tested first without arguments, which exits2 without memory access. Each version was deleted with its unique temporary directory after its checks. No existing camera binary, bootstrap, service or process configuration was changed.

The source prefix and independent code signature at `80541000` match the pinned RTOS bytes. **The copied-data profile is not an immutable signature:** only its first36bytes match, with seven changed bytes later in the64-byte window. Preserve that mismatch. For a future authorized use, validate the two accepted source/code anchors before interpreting mutable snapshots; stop on errors or unexpected code mismatches.

See [live observations](../../../reverse-engineering/archive/RAW_LIVE_RAM_OBSERVATIONS_20261016.md) for returned image geometry and allocator state. Successful reads are observations, not a memory lease or frame-completion proof. Pointer presence, aggregate free memory and an advancing VdFalling counter do not establish capture safety or RAW pixels. Capture remains a separate action.

Parent validation,21 October22:05 AEDT: version9 passed its first authorized
read-only camera check. The root guard passed; the four bytes at0x81004018
were c0 3f 02 80 (word0x80023fc0). Pinned app hash,PID/start, both immutable
anchors, upload round-trip and camera hash matched. The exact owned helper
and directory were removed and absence verified. This is a RAM-cell observation,
not an active CPU register, capture completion or ownership proof. See
review (not published: `../../../output/decompile/rea/pass-sc-parent-review-20261021/parent-review.md`).
Parent retained the original unique-mktemp test defaults and added optional
OUT_DIR isolation; new and previous suites passed with Apple-Clang sanitizers.

Parent validation,23 October2026 12:32 AEDT: version10 passed its first
authorized read-only manager-vptr check. The fixed guards matched and the
four bytes at0x81cbb000 were90 b7 62 80 (word0x8062b790). App identity and
code anchors matched; uploaded bytes and camera hash matched. The exact owned
helper and directory were removed and absence verified. This is a vptr snapshot,
not pool ownership or an invoked capture. See
review (not published: `../../../output/decompile/rea/pass-8315d77-review-20261023/parent-review.md`).


`profile8-fields` maps only ordinary RAM page `81882000`, already covered by
`settings10`. It requires object word `8188257c` to equal native constructor
vptr `80670310` before reading the two fixed metadata words at `818828a4/a8`.
Both words are read twice; a difference or changed guard refuses a successful
record. Unmap and close must succeed before the single eight-byte record is
emitted. Unexpected stable values are reported unchanged. The helper follows
no observed pointer. Native getter `8020ca7c` returns the guarded object;
its profile records start at object+4 with 100-byte spacing, and index8 supplies
these fields. This is configuration metadata, not observed image dimensions,
sensor samples, per-shot completion, coherence or retained ownership. Equal
reads do not exclude ABA or changes outside the observation interval.

Host tests default to Apple Clang (`CC` can override). The installed Homebrew
LLVM sanitizer runtime hangs even on the unchanged original ring test here;
Apple Clang ASan/UBSan and the plain Homebrew build pass that baseline. The
ARM target build continues to use the pinned LLVM/LLD paths in `build.sh`.
