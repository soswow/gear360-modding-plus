> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Live RAM observations — 16 October 2026

After the user explicitly approved temporary upload and execution of the fixed read-only reader, the parent staged hash-verified static ARM helpers under unique `/tmp/gear360-ram-reader-*` directories. These observations used O_RDONLY `/dev/mem`, PROT_READ mappings of ordinary RAM and fixed addresses. No camera-memory write, MMIO access, capture command, mode change, service restart or firmware modification occurred. No pixels were copied.

## What is now observed

Two independent 64-byte windows at physical offsets `0x80000000` and `0x80541000` exactly match the pinned RTOS image. The latter is executable code, independently establishing a second source anchor. This validates these sampled physical backing locations; it does not prove the complete RTOS address space or current MMU/cache configuration.

The retained startup-copy window at `0x81006000` matches the source for its first 36 bytes, but differs at seven positions in the remaining 28 bytes. That failed full-window comparison is retained. This block contains mutable copied data and is not accepted as an immutable64-byte signature. We have not established writers or semantic meanings for those changed words.

| Fixed observation | Returned state | Meaning and limit |
|---|---|---|
| Debug globals `818843b0/b4/b8` | All zero | No retained debug allocation pointer in these cells at that moment. Not a future allocation guarantee. |
| Published frontend cell `81952fc0` | `81d7f800` | Its fixed-address snapshot begins with expected derived vtable pointer `806718a0`. Guard matched before and after. |
| Standalone event-wrapper cell `81952fc4` | `81d9e800` | Nonzero pointer observation; event registration/freshness was not inspected. |
| Preview/core selected-image cells `81952e50` and `81952fa4` | Both `82246000` | Same selected image value; both guards remained equal before/after the image snapshot. No lease or candidate-MID association follows. |
| Settings row10 at `81882968` | Primary 1920×960 | Actual row content, still not independent SSIF input geometry. |
| Embedded debug image at `81883f30` | Expected vptr `8067a158`; remaining 140 bytes zero | Its dimensions, stride and PData are unconfigured in this snapshot. Distinct from the selected image. |
| Frontend input words at C+`64/68` | Both zero | Does not exclude direct hardware SSIF input. |
| Frontend C+`280` | 30 | A live scalar on the recovered SSIF configuration path; not dimensions. |

All addresses in the table are hexadecimal except dimensions/scalar values.

## Selected image metadata

The 160-byte snapshot at fixed address `0x82246000` starts with the expected C++ image vptr `0x8067a158`. Original instructions bind primary width/height to offsets `+4/+8`, secondary dimensions to `+c/+10`, stride to `+60`, and PData to `+84` through the embedded wrapper at `+80`.

- Primary and secondary dimensions: **1920×960**.
- Stride field: **2880bytes**; stride×height is **2,764,800bytes**.
- Embedded wrapper type byte: **2**.
- PData field: **`0xa77bb000`**.
- Metadata bytes at offsets54/58/59/5a/5b: `01/02/02/00/00`.

This is the C++ object layout, not the exported `stCAPTURE_IMAGE` ABI. Wrapper type 2 is not a Bayer/RAW format identifier and is not a link to the separate MID0x15000 candidate. The stride is 1.5 times width, consistent with some processed eight-bit YUV layouts; no packing label is verified without the pixel/source contract. The reader never mapped or dereferenced PData. These fields establish selected software geometry, not measured sensor geometry or stable completed pixels.

## Activity and allocator identity

The known VdFalling callback counter at `0x81952d38` changed from 6013 to 6365 between host request starts 09:57:05.265736 UTC and 09:57:17.041513 UTC: 352 increments in about 11.776 seconds. This is consistent with roughly 30 events/second. It is not proof that the selected buffer completed WDMA or that a future wait belongs to a new submission.

The allocator-manager pointer cell `0x81c0d7e0` returned `0x81cbb000` in both reads. The retained allocator implementation places its arena base/free-page scalars at manager+`680b0/680b8`. Version 4 then read only the fixed 40-byte allocator metadata at `0x81d230b0`, guarding that same manager pointer before/after and requiring arena base `0xa6000000`. It returned **20,050 free 16-KiB pages**, equivalent to **328,499,200 bytes / 313.28125 MiB**. This passes the necessary aggregate threshold of 4096 pages for 64 MiB. The table was not scanned: no sufficiently large contiguous eligible run, reservation or capture-time allocation success is proved. The snapshot is unsynchronized and can change immediately.

## Review, cleanup and next decision

The reader's exact executable was independently rebuilt and compared, FTP-downloaded back after upload, and camera-side SHA256 checked before each version's use. Host mocked-syscall tests cover fixed mappings, read-only flags, guards, failures and cleanup; actual target execution additionally confirms ARM startup/syscall compatibility. Runtime guards cannot prevent ABA, byte tearing, stale caches or changes after the read. The parent separately checked the vptr in the copied bytes.

Versions 1–4 were removed after their checks. Following version 4 removal, the camera app remained PID 254 with starttime 1042 and 21 threads, all TracerPid 0; no reader staging directories remained. Battery was 100%, reported temperature 39°C. No root `.raw` glob entries were returned; this is not a full-card search.

The successful RAM access replaces the earlier direct-read EFAULT as the practical observation method. It also shows why row10/default geometry cannot be substituted for the current output descriptor: selected stride 2880 differs from the debug provider's conditional 3840. Fresh SSIF output completion, actual input packing and safe backing retention remain open. The stock capture command has not been executed and must not be blindly repeated because its release path leaves stale globals.

Versioned evidence bundle (not published: `../evidence/pass-runtime-20261016-ram-observation.tar.gz`) and member/hash index (not published: `../evidence/pass-runtime-20261016-ram-observation.index.json`) preserve exact commands, response bytes, UTC request timing, executable sources/hashes, parsed snapshots, source-bound windows, tests and reviews alongside the working copies in `output/decompile/rea/pass-runtime-20261016/`. The overarching verified sensor RAW-file goal remains unfinished.
