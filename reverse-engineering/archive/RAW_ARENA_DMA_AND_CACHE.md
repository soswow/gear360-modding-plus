> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# RAW arena, PP output, and cache-maintenance investigation

15 October 2026, Australia/Sydney. The scheduled 00:40 pass resumes [the previous checkpoint](INVESTIGATION_CHECKPOINT_20261014.md). Luna workers trace captured binaries through REA; the parent reviews source bytes, resolves conflicting interpretations, and integrates accepted findings. All work remains offline: no camera commands, firmware execution, restart, preload, or deployment.

**The candidate image now has a concrete allocator, generic release mechanism, and resolved cache-maintenance service. PP output code reaches address/stride/dimension register writes. Active R210 use, sensor-pixel provenance, live geometry, and a verified saved RAW frame remain unproved.**

## Arena allocation and reuse

The state-`0x16` image callback's manager is the same `CCaptureMemoryManager` constructed through holder initializer `0x800a1258`, constructor `0x800ab41c`, and holder field `+0x24`. Getter `0x80086b20` retrieves it through `0x800a0f74`. Its assigned vptr `0x8062b790`, virtual slot `+0x20`, contains **`0x800ac834`**.

For this state, helper `0x8025d9c0` requests **`0x04000000` bytes (64 MiB)** with allocation tag **`0x1c`**. `0x8054aef4` converts the request to 4096 units of 16 KiB. The tag is stored in per-unit allocation records; it is not an address or a separately identified physical device pool.

`0x800af108` serializes allocation and can wait/retry when the request cannot be satisfied. `0x800afb80` selects the generic contiguous-run search at `0x800afef8`, which marks 4096 adjacent free units, stores run metadata, and reduces the free count. Pointer helper `0x8054ae74` returns:

```text
pointer = *(manager + 0x680b0) + allocation_index * 0x4000
```

The base field is initialized to **`0xa6000000`**, with arena length configured from `0x1a000000`. Reserved/common areas are established before general allocation. The returned index and pointer are runtime-dependent; the image does not necessarily start at the arena base. These are allocator units, not a claim about the MMU's page size.

Release virtual `+0x5c` is **`0x800adb1c`**. For an accepted arena pointer it derives `(pointer-base)>>14` and calls `0x800b04b4`, which clears allocation/run metadata and restores the free count. This proves generic reuse eligibility after release. The exact capture-image owner that invokes release remains unknown.

## PP output register programming

`CPP_Core::start_wdma` at **`0x8031c098`** reads its output-image field at byte `+0x10`. Accessors supply its buffer address, dimensions, and stride. Caller `0x8031a704`, identified by its `frame_read_out_start` diagnostic, waits for VSYNC falling and calls this method.

The tuple reaches **`0x800cb010` → `0x804ff2ac` → `0x80511950`**. The middle layer is identified as `d5_pp_core_dma_control` and checks active-group stride alignment to 16 bytes. The recovered RTOS helpers program:

| Value | Destination |
| --- | --- |
| WDMA output address | `0x20800338` |
| Row stride | `0x2080033c` |
| Packed width/height | `0x2080034c`, `(width<<16) | (height & 0xffff)` |
| Format/control | `0x20800340`, with related mode bits at `0x20800004` |

This establishes a static PP memory-output programming path. It does not alone establish sensor SSIF input, current register values, DMA completion, or R210 activation. The separately examined Linux `libudd5` WDMA exports have a TINT resize caller; that userland route must not be substituted for the RTOS capture path.

A material table formerly described as byte `+0x50` is actually **byte `+0x140`**: Ghidra's typed pointer addition scaled by four. Original ARM `ADD #0x140` confirms both its constructor zeroing and handoff to the manager. It spans six 12-byte rows and contains the image-bearing fields.

## Conditional candidate-to-output identity and order

Material method `0x802f36a0` writes `GetImage(0)` into row tag 3, at material `+0x16c`, with its enable flag at `+0x168`. Manager stage `0x80322598` copies that row's flag/pointer into frontend **C** at `+0x14/+0x18` under its helper condition. These offsets are relative to an object, not a global pointer cell.

The three receivers are distinct:

| Receiver | Construction/address | Relevant field |
| --- | --- | --- |
| C, FrontendNx-derived | `C=*(u32*)0x81952fc0`; `0x8020d948` allocates `0x318`, calls `0x8020d260`, then stores C at `0x8020d9cc` | Row-tag-3 enable/pointer `+0x14/+0x18` |
| B, CPP_Preview | Fixed singleton `0x81952e3c`, returned by `0x80319188` | Selected image pointer `+0x14` |
| A, CPP_Core | Fixed singleton `0x81952f94`, returned by `0x8031aa88` | Output0 image pointer `+0x10`; its separate input0 is `+0x14` |

C's base constructor temporarily installs vptr `0x806af070`, but the derived constructor leaves **`0x806718a0`**. Actual byte slot `+0x18` therefore calls derived `VideoSetInfo` **`0x8020ebe8`**, which explicitly delegates to base **`0x80321df4`**. When its flag/selection gates choose row tag 3, the selected image reaches B's `+0x14` through setter `0x8031a250`.

Actual derived slot `+0x38` calls **`0x8020f6e4`**, which explicitly delegates to base frame method **`0x803221cc`**. That method passes B to `0x8031a704`, which copies B's `+0x14` into A's `+0x10` through `0x8031b894`, then calls `start_wdma`.

Stream-wrapper constructor `0x8031fbac` obtains C from its getter and stores it in wrapper word 0. `StartVideoStream` (`0x8031feac`) chooses between two routes using wrapper flags. **Route A (`0x80320ce8`) explicitly calls slots `+0x14,+0x18,+0x20,+0x24,+0x1c`, then `+0x38`, proceeding only while prior methods return zero.** Thus configuration-before-frame-start order is established for that route, including the real derived overrides. Route B (`0x80320b30`) ends with `+0x28` and has no `+0x38` frame-start call.

This closes a conditional static image-pointer path from the material into PP WDMA output. It does not establish which route the R210 takes, whether every helper succeeds, whether the selected image contains sensor RAW, or whether output DMA completes. The traced frame-start chain has no demonstrated post-start completion wait; a separate `start_rwdma` event sequence cannot supply that proof.

## Concrete cache callback and operations

Negative selector **`0x80650200`**, wrapper entry **`0x805178a0`**, selects registration entry **`0x800211d8`** through a signed dispatch index:

```text
0x80043dac + sign_extend(0x8065) * 4 = 0x80023f40
*(u32*)0x80023f40 = 0x800211d8
```

That negative-selector vector is distinct from the positive subsystem registry. Registration caller **`0x80025270`** passes ID **17** and a stack descriptor whose word `+8` is callback **`0x80024e14`**. Setup routine `0x80028e98` calls that stage. Registration installs the callback in positive-registry record 17:

```text
record = *(u32*)0x814482d4 + (17-1)*0x28
callback = *(u32*)(record+0x0c)
```

Callback `0x80024e14` handles **`0x010c0311`**, forwarding pointer, size and operation to **`0x800245c8`**. It rejects zero size/operation and uses another subsystem operation for a nonzero-pointer check; this is not a demonstrated full bounds check.

For operation **3**, the helper rounds the start down to 32 bytes and iterates by 32 while the address is below the unrounded `pointer+size`. Its instructions request data/unified cache clean-and-invalidate by MVA, instruction-cache invalidate by MVA, then branch-predictor invalidation and a data synchronization barrier. The instruction meanings are supported by [Arm's ARMv7-A/R Architecture Reference Manual, Figure B3-32](https://documentation-service.arm.com/static/5f8daeb7f86e16515cdb8c4e); the original encodings are retained separately. Branch-predictor invalidation is not an instruction synchronization barrier.

This closes the previous operation-specific callback gap. The static code does not measure live cache hierarchy, actual line geometry, translation state or successful execution. Pointer-plus-size wraparound handling is not established. The software step of 32 bytes must not be mistaken for a measured hardware cache-line size.

## Address and Linux-consumer boundaries

The examined RTOS descriptor setters, address conversion, PData access, `dumpImage` and `FileDump` forwarding preserve the numerical pointer. Linux `DumpFileData` supplies its FileDump descriptor address field directly to a `/dev/mem` mapping helper. That establishes a **physical-memory-offset expectation at the consumer**, rather than proving the arena's live address translation.

The two descriptor layouts remain distinct: `stCAPTURE_IMAGE+0x40` begins an address-set structure with a type word, while the separate 76-byte FileDump descriptor `+0x40` is the buffer-address word. A copied pointer alone cannot prove its live physical identity.

The captured **non-`0xc09` MIDR branch** builds section tables, writes TTBR/TTBCR, enables SCTLR.M, and applies configured mappings before the captured startup's SVC-0 handoff. Replaying original table bytes yields identity section-output addresses throughout **`[0xa6000000,0xc0000000)`**, covering the arena. Relevant overrides include half-open ranges `[0xa6000000,0xb8c00000)`, `[0xb8c00000,0xbf400000)`, and `[0xbf400000,0xc0000000)`. Raw aligned ends are exclusive boundaries; unaligned ends are rounded to the next 1-MiB boundary. The section-format interpretation follows the [Arm short-descriptor architecture](https://support.arm.com/documentation/ddi0406/c/System-Level-Architecture/Virtual-Memory-System-Architecture--VMSA-/Short-descriptor-translation-table-format/Selecting-between-TTBR0-and-TTBR1--Short-descriptor-translation-table-format?lang=en).

The `0xc09` branch instead jumps to address zero outside the captured RTOS. Actual R210 MIDR, live TTBR/SCTLR, later table mutations, and which allocation reaches FileDump remain unknown. The result is a **conditional startup identity map**, not a live physical-memory observation. The analyst's LLVM Cortex-A9 setting never identified the hardware core.

Linux `libmm-photo.so`'s `CaptureDataParser::convertRaw` receives a copied 136-byte descriptor through its image-generation callback queue. The queue preserves descriptor identity across the eight-byte record header. The parser requires RAW pack-type field **`+0x28==3`** and reads pixel enum `+0x2c`. The candidate provider converts to ABI pack type **1**, so it would fail this parser if delivered unchanged. No producer-to-queue binding or upstream transformation has been demonstrated. The statically registered image-set IDs cover YCC/encoded/MPO/UD-YCC; they do not establish a RAW image-set delivery.

The product app's crop ioctl `0xa4` maps to capture request `0x01080039` and MCB attribute `0x191`; chip ioctl `0x8b` maps to `0x0108002d` and attribute `0x18e`. RTOS reply-method table entries and producer code are now found: crop-named attribute `0x191` sends one predicate byte, whose underlying implementation returns zero. It supplies no W/H dimensions. Chip-named attribute `0x18e` has a seven-byte packed producer but a wrapper declaring 56 bytes; exact outbound read and field meanings remain unresolved. These bodies do not establish an active sensor model or a complete active-instance dispatch binding.

Review of already decompiled derived `VideoSetInfo` and `VideoSetSensorMode` finds no direct W/H/stride or row-25/26 writes. Device virtual-call effects remain a precise unresolved geometry boundary. Captured configuration/log searches and multiple sensor-labelled RTOS routines also do not establish which imaging sensor is active.

## Release, completion and dump failure boundaries

`Ipc_End` (`0x8032be1c`) selects image-set slot 0, calls `0x8013b47c` → **`0x8013d598`**, which reads PData, attempts release through manager virtual `+0x5c`, then clears the same image descriptor with `0x8025cc24(image,0)`. Only afterward does it call `SetMainRawImage` and remove the image from its set. Thus the image object can be queued with cleared backing; container insertion is not pixel retention. The exact identity of this image with the state-`0x16`/tag-`0x1c` candidate remains unproved.

`FileDump` does not check either descriptor/pixel maintenance return. It posts its stack descriptor and calls a semaphore wait with count 1 and all-ones timeout; the lower wrapper checks wait failures, but FileDump suppresses that result and returns its initially zero status. Exact all-ones deadline semantics remain outside the expanded kernel helper. The synchronous wait keeps the descriptor stack storage live, but **establishes no pixel-buffer lease or refcount**. ACK can still follow file-open failure in the separately traced Linux consumer. Saved file size/content and buffer lifetime must be verified independently.

## Evidence and next boundaries

The original RTOS SHA-256 remains `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. Reports, source-bound carrier manifests, complete REA responses and closed snapshots are retained under `output/decompile/rea/continuation-20261015/pass6/`. Selected source-byte and checkpoint evidence (not published: `../arena-dma-cache-evidence.json`) binds the reviewed milestone to original bytes and retained evidence.

| Claim | REA evidence |
| --- | --- |
| Concrete allocation virtual | `ev_a0ed1b1094f57ed311702207d37b33b7ba0f64cc71ff3a651693a74ac8b3ae96` |
| Arena pointer formula | `ev_9a478fdfb58a9704226e1de30e0e90409d247668230b8b0e2ab9e5d523a59cb1` |
| Generic release | `ev_868bd4f6e3757025a52c61323957c5cbfe2b2c0b6f2e987ca7393add20f995e4` |
| Registration caller/descriptor | `ev_9354c10d2b3027cb0d8c916a2fef1c0d052f815a101402a5416b9fd1280c5c04` |
| Concrete subsystem callback | `ev_0e48cea509e7ace6b248fda01f05ca61f134fe717ae1a01ecb1874a93026b5e9` |
| Original cache-helper bytes | `ev_374cd533ab7748d431bc11098bac3cacdb5dae6b211a4917e78758491ccbd0d3` |
| PP output extraction | `ev_f8d8bde2cf6f1cd8e51739f2fece3fd7121eb3c68e38d4ffa8893b4294c52340` |
| WDMA address register writer | `ev_d389b9d85cf053c0fa6b2f51445eac3152f9da6e838c63277fddb7c6cf177d57` |
| WDMA dimension register writer | `ev_816802302fd94c552387bad9a1107bab49088ba25e27b65269c73653ab35be58` |
| Linux RAW parser | `ev_3e0efdfc66f506d67f0d2ed302996342a962eac28c8a48c7db148e42b55c1ff6` |
| Linux dump physical-offset consumer | `ev_76e94c4912dc23315c51e5007ae9c1aa30d80a789f074a38f9b0526dbf55452c` |
| Frontend pointer-cell store | `ev_05212fdef412b4266919a1a619b0df3b2be25a82025180bb6ae390dc8624200a` |
| Derived method delegation | `ev_f759f0aa259c5f6ba2d777efc40b8c2afe1a1af9de9f0a4a229aca9b87330a57` |
| Ordered route A | `ev_6fca28d9868cdfe28a58bcf58c9c16efefc6bbbcf8b78a1ed58ff17f50fa9932` |
| Actual derived frame-start | `ev_67eca241c00b35ae0a996eb00e5da05d5a4f59c11c89c05e51735584cf58c9b2` |
| Ipc_End release/clear | `ev_6b3349726edf6879bfe2627743966d82622cc000c0e442ddff4ac27132d48345` |
| FileDump error/wait contract | `ev_6d8a6527c10dec253860400f29555138da9f7a89280f80872b3690c2993ba67d` |
| Crop producer | `ev_b20f69ed6f5a2d95f571557656f3294aaa09b90e7c857e10c4168fcdd99446e0` |

The next checks are R210 route flags and selected descriptors, the geometry/device-call writer, sensor-input provenance, frame-specific completion, exact candidate owner release and export timing, and startup versus current address mapping. No sensor RAW frame has been captured. The [final scheduled-pass checkpoint](INVESTIGATION_CHECKPOINT_20261015.md) carries these boundaries into the next restart.
