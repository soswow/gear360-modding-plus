> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Still callback factory, pool address, and release

**Grok session (Cursor, branch `grok-own-progress`): 19 October 2026, 18:08–19:35 Australia/Sydney (UTC+11).** One IDE session. Not a Codex review and not an external `agent` CLI run. Findings below were checked against original RTOS instructions in that session. A later Codex pass should treat them as this session's record, not as already accepted by Codex.

Checkpoint written 2026-10-19T19:35:00+11:00. Offline. No camera was contacted. Product goal remains our own callable C/C++ acquisition library. It remains unfinished. No tracked source commit. Branch `grok-own-progress` is still `5b31dbf907fb65ac5a812e7e0b5756cda1a70ac9`.

Full instruction-level record: `output/decompile/rea/pass-callback-factory-20261019/parent-review.md`. Original RTOS SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. Addresses are link-base `0x80000000` plus file offset.

## Findings from this Grok session

- `0x80140E94` is the `RecipeStillSetting` factory. It allocates 200 bytes and switches on argument `r1` for `r1 <= 52`. The only caller that passes case 11 is `0x802B839C` in `0x802B8104`. Both material arms of that function (`r2 == 4` with material id `81920`, and `r2 == 2` with material id `0x00014001`) reach that one call. Case 11 stores request word `11` at `+0xA4`, layout byte `4` at `+0x80`, word `2` at `+0xB0`, byte `2` at `+0x5A`, and `0x00FFF200` at `+0xA8`. Its helper first stores `6` at `+0xA4` and calls `0x8025CC24(object, 0)`; the case body then replaces `+0xA4` and `+0xA8`.
- `0x80325098` is `RegisterPreCallback`. It stores a non-null callback at `object + 4 + count * 4` while `count + 1 <= 14`, with the count at `+0xB8`, and inserts it into the global container at `0x81952FD8` when absent. The over-limit log is `RegisterPreCallback` / `Over a limit of the registration!!`. `0x80574BA4` builds the 8-byte `CCallback_Still_Get_ImageBuffer` (vtable `0x80651020`) and stores the recipe at callback `+4`. `0x80325C60` only returns argument `+8`.
- Execute `0x80138D54` reads that recipe `+0xA4` and calls `0x8025D9C0`. Id 11 is outside the 14..42 switch, so the lookup returns sentinel `0xBFFFFFFF`. Execute logs `[IMG BUFFER] Buffer Get Error!!` and does not call `0x8025CC24`. Case 11 is not a RAW lease.
- The lookup dispatches other ids to `CCaptureMemoryManager` vtable `+0x20`. Id 22 selects slot 28, ids 23 and 24 select slot 30, id 33 selects slot 38, and id 17 selects slot 37. Ids 18–21 and 34 return the sentinel even though they are inside the switch. Id 35 uses vtable `+0x24` and the constant `0x00A8C000`, not a small slot. The full id-to-slot table is in the parent review.
- Factory case 40, at `0x80141444`, is the only caller of configurator `0x80141C78`. That configurator stores layout byte `2` (`0x80141DCC` / `0x805615D0`) and request word `22` (`0x80141DE4` / `0x80560CCC`). Two calls pass case 40. `0x802BA7B4` (`RECIPE PAGE PP`, call at `0x802BA9D4`) attaches the image, constructs `CCallback_Still_Get_ImageBuffer` with it, and registers that callback. Its optional follow-up is gated by `*(0x819521C4) & 16`. `0x802C8F74` (`RECIPE PAGE ETC` / `_page_etcmodifyimage`, call at `0x802C90DC`) also calls case 40 and attaches the image; the reviewed window does not register the callback. Case 11 does not reach this reservation. Nothing passes case 38.
- Execute treats id 22 as not equal to 33 and calls `0x8025D9C0` at `0x8013925C`. The only virtual request for memory slot 28 is `0x8025DA80`, inside that lookup. The only virtual request for slot 30 is `0x8025DAD8`.
- On success, slot `+0x20` (`0x800AC834`) returns `*(manager + 0x680B0) + (index << 14)`. The only store to `+0x680B0` is the constructor write of `0xA6000000` at `0x800AB664`. The constructor stores pool size `0x1A000000` (26624 blocks of 16 KiB) at `+0x680B4`. Id 22 therefore reserves 4096 blocks, 64 MiB, at `0xA6000000 + index * 16384`. Id 24 reserves 2048 blocks, 32 MiB. Failure returns `0xBFFFFFFF` without adding the base. The wrapper logs `allocEx` / `#### JPEG MEMORY FULL`. This is a pool address, not an identified sensor-RAW frame.
- `0xBFFFFFFF - 0xA6000000 + 1 = 0x1A000000`. That is the same window previously reviewed as `CCapVirtualAddrIf::Initialize(0xA6000000, 0xBFFFFFFF)`, whose mmap extent rounds to `0x1A000000`. The sentinel is the last byte of that window. A 16 KiB-aligned reservation cannot equal it. This pass did not re-read the Linux instructions. No live mapping was performed.
- The manager object is `*(0x81C0D7BC + 0x24)`. `0x800A1258` allocates `0x68834` bytes and `0x800AB41C` installs vtable `0x8062B790` (`21CCaptureMemoryManager`). `0x80086B20` returns that pointer and, if it is null, asserts `pcMemoryManager` in `CaptureMemoryManagerNX.cpp`.
- Release by address is vtable `+0x5C` (`0x800ADB1C`): `(address - base) >> 14`, then `0x800B04B4(this, index, 1)`. Release by address and length is vtable `+0x60` (`0x800ADC38`), which clears the occupancy byte and the words at record `+4` and `+184` for each covered block. Its visible return immediate is 1 on both the out-of-range arm and the end of the clear path, so that 1 is not a success flag. `+0x60` has no direct call.
- When the lookup result is not the sentinel, `0x8025CC24` switches on the byte at `image + 0x80`. `0x800B4F9C` then copies 16 bytes to `image + 0x84`, or zeroes them, and the size word is stored at `image + 0xA8`. Layout byte 2 spaces planes by `(word at +0x60 * word at +8) >> 2`. Layout byte 4, which case 11 writes but never reaches, spaces them by `(*(image+4) * *(image+8)) >> 1`. The constants 11648 and 2896 were not re-derived.
- `CControl_Still::PutStillBuffer` (`0x8013E140`) frees a queued `image + 0x84` through `+0x5C`. Ids 11 and 18 are Raw Lrc (queue `+180`); 14 and 29 Main Jpeg (`+108`); 15 and 30 Screen Jpeg (`+132`); 16 and 31 Thumb Jpeg (`+156`); 17 and 32 MakerNote (`+204`). Ids 12, 13, and 19..28, including request id 22, are `invalid id` and are not freed here. PutStillBuffer id 11 is a Raw Lrc release key, not the allocator slot: the allocator rejects 11.
- `0x8029BA94` creates a new 200-byte image with `0x8025C808` and `0x8029C320` queues its plane 0 as Raw Lrc. Constructor helper `0x8025D4AC` copies `+4..+0x54`, `+0xB0`, and `+0xB8..+0xC4`. It does not copy `+0x84` or `+0xA4`, and that window stores neither request 11 nor 22. This enqueue is not shown to carry the 64 MiB reservation. `0x8032E610`, vtable `+8` of `CCallbackJpegImageSet_360TimeLapse`, queues one structure's word 0 as Raw Lrc and `+4/+8/+0xC` as Main/Screen/Thumb Jpeg. It does not call the id-22 factory.
- Slot 48 is separate. `checkRawBufferFull` probes it for 4096 blocks (64 MiB) and logs an SSIF allocation failure otherwise. The getter's own slot-48 arm converts `0x03C00000` to 3840 blocks (60 MiB). A still-recipe call at `0x8028DE18`, reached from `0x8028DAE8` after `BestFace Execute`, stores that return at `object + 0x120` and 6 at `+0x124` when a destination image exists. A later failure logs `Work Buffer size < 18MB. Failed to Initialise Bestfacelibrary`. Memory slot 22 is not request id 22. `0x80337E48` requests memory slot 22 into `object + 4`, then `0x80337E78` requests slot 48 into `object + 8`.

## Not established

- That the 64 MiB id-22 reservation is one sensor RAW frame. The allocator log names JPEG memory. Geometry, packing, and Bayer order were not re-proved. The earlier 11648 by 2896 figures stay in the prior allocation note.
- The call that releases the id-22 address. `PutStillBuffer` rejects id 22.
- That `0xA6000000` is mapped on a live boot.
- Runtime freshness, cache visibility, and repeat capture.
- The request id of the 200-byte object created at `0x8029BD20` before its plane 0 is queued.

## Exact next contract

Show the non-sentinel result of Execute on the case-40 image stored at `image + 0x84` by `0x8025CC24`, then find the release of that address. Do not implement a library from case 11. Do not treat slot 48 or the BestFace work buffer as the id-22 reservation. No camera is required for that static edge.

## Resources and cleanup

Evidence directory `output/decompile/rea/pass-callback-factory-20261019/`. Carriers there are analyst ELF headers over verified slices of `rtos-p6.bin`. No subagents were launched. No camera files or settings to remove. No source commit, merge, or push.

Usage at the last check in this pass: included total about 78% used, auto about 83% used, API about 27% used, about 16.5 hours until the billing reset. This Cursor session spends the auto pool. Wind down near 90% auto and stop by about 95%. Product unfinished.
