> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Movie-work address assignment — reviewed 18 October 2026

The allocation's size now has a concrete static explanation. `CLiveviewBuffer::getBaseAddress` at`8014c500` dispatches roles14–18 through capture-memory-manager slot`+70`, requesting three role-specific strides, then selects a stride with a supplied counter modulo3. Role14's stride is`0x17bb000` (24,883,200 bytes); three strides equal the observed allocation size`0x4731000` (74,649,600 bytes). The selected image's PData equals the allocation base plus one such stride. This is stronger than capacity arithmetic, but does not establish temporal frame rotation or a safe export interval.

## Exact source path

Original p6 SHA256 remains `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. Parent reviewed Luna's saved original-byte windows and independently opened a new exact partial carrier including the previously unmapped role/name tables. Parent carrier SHA256`04f95d54bddade13afaa204e98dd9f3909f1bfcc5539f1583b83dac97c9c5714`; source mapping and mechanical checks are in `output/decompile/rea/pass-resume-20261018/movie-parent/`.

At`8014c79c..8014c7df`, roles`0x0e..0x12` set local stride count3 and invoke receiver virtual`+70` with arguments `(receiver,1,row.word0,3)`. The receiver comes from`80086b20`. Its path is:

`80086b20 ->800a0ebc ->8054a744 -> holder81c0d7bc; 800a0f74(holder) -> holder+24 = cell81c0d7e0`.

Initializer`800a1258` constructs the capture manager and stores it at holder+24. Current guarded metadata confirms cell value`81cbb000` and manager vptr`8062b790`. Original vtable word at`8062b800` (vptr+70) is`800ae348`, the already reviewed cached movie-work getter. That getter requests `row.word0*3` only when its cache is zero. It returns a nonzero cached value unchanged; allocation failure sentinel handling remains the earlier caveat.

The static table at`80654c1c` has 8-byte rows, two little-endian32-bit words. Roles14–18 have stride/offset pairs:

| Role decimal/hex | Stride hex | Stride bytes | Offset |
|---|---:|---:|---:|
|14 / e|17bb000|24,883,200|0|
|15 / f|7e9000|8,294,400|0|
|16 /10|659000|6,656,000|0|
|17 /11|1c2000|1,843,200|0|
|18 /12|a8c00|691,200|0|

Their separate name table at`80654eb8` maps all five roles to `movWrk`. The name alone does not identify one role.

For the nonzero returned base, common instructions`8014ce94..8014cf4f` write:

`out = base + row.word1 + row.word0 * (counter - 3*(counter/3))`.

With a nonnegative assignment counter this is `base+offset+stride*(counter%3)`. A zero base instead takes an explicit fallback that substitutes owner+b0 and fallback table constants; do not omit that condition. A nonzero failure sentinel is not rejected by this test.

## The counter is not a demonstrated frame number

Caller`8014c0bc`, source-labelled `setAddressFixArea`, compares the requested role with owner+`b8`. If equal, it increments owner+`b4`; otherwise it resets+b4 to0, then stores the role at+b8. It passes this repetition counter into`8014c500`, and installs the returned address into the image through`8025cc24` and`8025ccb8`. It finally copies the role name into image+78. The direct source path reaches this from `createImage` through wrapper`8014c068`.

These are image-address assignment operations. The reviewed code does not increment this counter on WDMA completion, identify a frame generation, or show that it rotates the selected image's backing every frame. Luna's phrase “rotates the output pointer” is accepted only for repeated calls under these role/counter conditions. It is not a claim about temporal frames.

## Current live join

Fresh p6 source/code64-byte signatures at physical`80000000` and`80541000` matched before the allocator read. The original reader rebuilt to its recorded SHA256`2cf81d50e299d4cde2c8c492b183881a607841e524f1ff512f2662cd9b6189a6`. A report-formatting error in its reused acquisition script added the Linux VA displacement to an irrelevant `virtual` label; `rtos-identity-runtime/comparison-corrected.json` preserves the correction. The physical addresses, source comparisons and hashes are unchanged. These observations do not inspect live RTOS MMU state.

The reviewed allocator reader's new retrieval passed host contracts, identical ARM builds, upload/download and camera/Mac hash checks, all fixed pre/post pointer/image/geometry guards, app identity, and cleanup. Evidence: `output/decompile/rea/pass-resume-20261018/allocator-live/`. Prefix SHA256`e1297b4e133df662007bf2b0c030412bbb531fba4c3e1a3510c480762f576792`; first word is manager vptr`8062b790`. All1781 prefix records have tag40/`28`, with remaining units`4557-index`. The movie descriptor is still base`a6000000`, size`4731000`, multiplicands1/1. Selected image fields still show request`ffffffff`, size2,764,800, kind0; all geometry/PData guards passed.

`a77bb000-a6000000 = 17bb000`, exactly one role14 stride; `3*17bb000=4731000`. The new skill's extent checker independently reproduces containment and rounding from the fresh metadata. Thus this snapshot is consistent with role14 selector index1, if this path assigned the selected image. The actual invocation/owner/counter and current-frame join remain unobserved. In particular **27 selected extents are not evidence of 27 temporal frames**.

## Evidence and next question

Luna dossier`ev_8f948d2f90b16e683c5c1067dee888d1aabc55cb94605d3988f25da355b9c9a2`; independent parent dossier`ev_8be1851cc6bfc5c69a0a8738d27fa8c6f8af91f3a9430d3c5f89ec0db0f977b6`. Getter/holder records and the five-claim C6 acceptance table are in `movie-parent/parent-acceptance.json`; original worker outputs remain in`luna-movie/`.

Next: bind the selected image to an active assignment/consumer lifecycle and establish a fresh completion plus a stable backing interval. Polling this assignment counter or calling the three regions a frame ring does not close that gap. No new pixel copy, imaging control, MMIO write, firmware execution or persistent camera change occurred. All temporary camera files from this work were removed; no source commit or push was made.
