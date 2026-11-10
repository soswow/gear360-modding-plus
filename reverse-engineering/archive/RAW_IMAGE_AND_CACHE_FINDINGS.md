> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Selector-0x28 image and dump-cache continuation

2026-10-14. This resumes the [saved checkpoint](PAUSED_CHECKPOINT.md) and extends [the dump protocol](RAW_DUMP_PROTOCOL.md). Three Luna workers performed the REA tracing; the parent reviewed their conclusions, checked original table bytes, and requested corrections where the evidence conflicted. All analysis was offline. No camera command, firmware execution, restart, preload, or deployment occurred. The later [factory, format, and syscall continuation](RAW_FACTORY_FORMAT_AND_SYSCALL.md) answers several remaining static questions and corrects the provider-index wording below.

**The dump receiver and its cache-flush call path are now identified. The selected image's actual sensor provenance and final format remain unresolved. No sensor RAW frame was extracted.**

## Selector 0x28: configuration and empty backing

The material builder requests selector `0x28` from image factory `0x80140e94`. Its image object is `0xc8` bytes. Interior case `0x80141444` calls configurator `0x80141c78`; the case must not be treated as an independent constructor.

Original call arguments resolve an ambiguity in the initial worker summary: setter `0x80561904` receives **literal 1**, which it stores in image WORD **`+0xb0`**. Getter `0x80560d24` reads that word. The computed stride belongs at **`+0x60`**, not `+0xb0`.

The configurator reads packing enum BYTE `+0x59` and width WORD `+4`. Packing values 0/1/2/3/default select 8/10/12/14/16 bits. Its exact integer calculation is:

```text
temporary = (packing_bits * width) >> 4
temporary = align_up(temporary, 16)
stride = temporary << 1
```

The initial shift occurs before alignment. Pixel-bit BYTE `+0x58` remains separate from packing BYTE `+0x59` and pack-type BYTE `+0x5a`. WORDs `+0x1c/+0x20` are cleared.

The embedded buffer wrapper begins at image `+0x80`. Configuration sets its first word to 2 and nested `+0x24` to `0x16`. `0x8025cc24(image,0)` computes its plane state for null backing: four plane addresses and byte size become zero; the size is at image `+0xa8`. Literal 2 is a layout selector in this path, not evidence of a valid buffer address. Construction of this object therefore does not itself demonstrate a received frame.

The metadata provider is now located through the frontend singleton returned by `0x80321114`, which reads the pointer cell `0x81952fc0`. The frontend-relative `+0x254` path reaches `CBayerImageInformationManager`, initialized by `0x80308534`. The cell address itself is not the frontend object base. Provider role `0x13` (decimal 19) holds a provider constructed by `0x803916b8(...,1,0)`, with vptr `0x806c04e0`. Slot `+8` is `0x8057b37c`, which calls **`0x8025c960(targetImage, provider+0x10)`**. The role indexes a separate array through manager `+4`; construction and runtime activation remain separately qualified.

The generic helper's field writes are the next precise question. Base initialization clears BYTE `+0x54`, but that initial zero must not be reported as the final format after the provider runs. Final dimensions, packing values, backing assignment, and identity with SSIF DMA remain unproven.

## FileDump receiver and cacheFlush path

The previously missing receiver assignment is in holder virtual initializer **`0x800a1258`**, selected by holder vtable `0x80626500` slot `+0x1c`. It allocates `0x68834` bytes, calls constructor `0x800ab41c`, and stores the object in holder `0x81c0d7bc` field **`+0x24`**, which getter `0x800a0f74` returns.

The constructed object has vptr **`0x8062b790`**. Its `+0x78` entry is **`0x800ae6d0`**, the exact slot used by `FileDump`. That method forwards through `+0x7c` (`0x800ae71c`) with `(this,0,pointer,size)`. The latter calls **`0x8051772c(pointer,size,3)`**, logs `cacheFlush`, and reaches an SVC 6 wrapper.

FileDump supplies the descriptor address and `0x4c` bytes, then the pixel pointer and byte count, unchanged. Original-byte review independently matched all three relevant vtable entries. This closes the static receiver/vptr/operation binding, extending the earlier inference about cache maintenance.

The virtual initializer's boot-time invocation and live receiver value have not been observed. The recovered wrapper does not establish address domain, cache levels, line rounding, exact hardware effects, coherence, or completion semantics of SVC 6. These remain validation requirements.

## Material factory: writer found, concrete object still unknown

Method **`0x80325bc0`** writes a nonnull second argument to manager **`+8`**, closing the missing setter edge. The single resolved direct callsite is **`0x8027b0ac`**, in `BranchManager_NX1`-associated builder code that also sets up still, video, and QView entries. The object supplied to the setter, its concrete vtable, MID `0x15000` class mapping, and active R210 recipe remain unresolved.

Two prior carrier labels are rejected: **`0x807932e0`** and **`0x80793378`** contain pointer/type-info data, not verified procedure bodies. Their bad-instruction pseudocode cannot identify the factory. The manager's `+8` factory pointer must remain distinct from its `+0xc` Kitchen object.

## Evidence and review

Original RTOS SHA-256 remains `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. [Selected original-byte review](../image-cache-review-evidence.json) binds vtable targets and rejected labels to that input. Full bundles, carrier manifests, source hashes, worker reports, and closed snapshots are retained under `output/decompile/rea/continuation-20261014/pass4/{image28,memory-receiver,material15000}/`.

| Finding | REA evidence |
| --- | --- |
| Configurator call arguments and stride | `ev_23b637bff0f70fdb90f9c3d5e2a0254eabae316b2927e7b1f1009bef7145a63d` |
| Provider leaf handoff | `ev_c98fab9c0bc44c1929d454dc2056a48fc9b0106441353ec136dd4d87d490b710` |
| Receiver assignment | `ev_08816f55755ee07ac8ed3e24c9e3d0053e542adb59725485dbda14d199d11a3a` |
| Concrete receiver vptr | `ev_03c781f998b4198275f7e727c22add77dc9d4d53a5c318db0df25c4b829334e8` |
| FileDump slot forwarding | `ev_252da06d64a51588ad7b90ad5ba8ba19cefd8db03153306a90b2ed42c4798240` |
| cacheFlush method | `ev_7eb9cfebbfe5aa224578c807c70d8650d0ea930015751c96a2a01b959b33c0df` |
| Manager factory setter | `ev_7ae83c879d8c93992be75af8566e768c66d3c39814801edeee47c7262c790eab` |

The account's five-hour usage meter was 1% at resume, 8% at an intermediate checkpoint, and 16% at the next check, exceeding the requested 10% threshold. Tracing ended at that boundary; final reporting and local integration followed. This is account usage, not an exact per-task token count.

Next work: trace `0x8025c960` and provider metadata, identify the object passed at `0x8027b0ac`, and connect the material's slot-zero backing to the exact receiving DMA image. Runtime R210 activation and real saved pixels are still required before claiming sensor RAW capture.
