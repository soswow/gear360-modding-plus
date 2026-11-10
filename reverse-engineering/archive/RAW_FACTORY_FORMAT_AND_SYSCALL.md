> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# RAW factory, metadata, and syscall continuation

2026-10-14, Australia/Sydney. This extends [the image/cache pass](RAW_IMAGE_AND_CACHE_FINDINGS.md). Luna workers performed focused REA tracing and an independent byte/provenance review; the parent reviewed, qualified, and integrated their results. All work used captured artifacts offline. No camera command, firmware execution, restart, preload, or deployment occurred.

**Selector `0x28` has branch-qualified RAW metadata and a concrete static material-factory path. The 10:45 continuation corrected its provider/geometry association: entry `0x0d` → row `0x16`, initialized to 5824×2896 with derived stride 11648. Active dimensions, receiving sensor buffer, R210 graph and saved sensor pixels remain unresolved.** See [the reviewed correction](RAW_EXECUTION_PROVENANCE_AND_LIFETIME.md).

## Questions and current answers

| Question | Status | Established boundary |
| --- | --- | --- |
| Final selector-`0x28` metadata bytes? | Answered for the traced provider branch | Format/pixel/packing/pack-type bytes are `1/2/4/1` |
| Numeric width, height, and stride? | Initialized default recovered in pass8 | Entry `0x0d` selects seeded row `0x16`: 5824×2896, derived stride 11648; active runtime values unobserved |
| Concrete object supplied to manager factory field? | Answered statically | `CFridge_NX1`, followed by nested Still and Still_PP factories |
| MID `0x15000` material class? | Answered conditionally | The populated NX1 manager routes this MID to `CMaterial_PP_Core_Liveview_VideoFull_Adjust` |
| Identity and backing update of the material/pre-callback image? | Partial | Same image pointer; state `0x16` requests pool `0x1c` and installs plane descriptors |
| Identity with an SSIF receive/DMA buffer? | Unresolved | IPC SRA output and generic event synchronization do not establish it |
| Exact cache syscall ABI and dispatch? | Partial | SVC 6, selector `0x010c0311`, one-based record 17, unchanged argument block |
| Cache hardware effects and address domain? | Unresolved | Operation-specific dynamic registry callback remains to be identified |

## Metadata and geometry

The selector configurator `0x80141c78` requests provider entry **`0x0d` (decimal 13)** through `0x8020c14c`, not `0x13` (decimal 19). The previous exact-candidate association with entry 19 is withdrawn. Registry constructor `0x80308534` constructs entry 13 through `0x80309bfc(...,1,2)`, vptr `0x806a6c08`; virtual `+8` is `0x8057b37c`, which calls `0x8025c960(targetImage, provider+0x10)`. That provider independently supplies the same numeric metadata described below. Entry 19 / constructor `0x803916b8(...,1,0)` is a separate provider whose geometry must not be substituted for this candidate. The [pass8 report](RAW_EXECUTION_PROVENANCE_AND_LIFETIME.md) supplies the corrected source evidence.

`0x8025c960` is assignment, rather than a format calculation. It copies the source image-info fields, including format BYTE `+0x54`, the block `+0x58..+0x63`, WORD `+0xb0`, and the embedded buffer wrapper at `+0x80`. The traced provider constructor sets **`+0x54=1`, `+0x58=2`, `+0x59=4`, `+0x5a=1`**. The configurator then stores its separately verified literal `1` at `+0xb0`; stride belongs at `+0x60`.

Linux `CCapImageUtility::imageInfoConvert` binds format 1 to RAW. Pixel enum 2 converts to ABI pixel enum 3 with mask **`0x3ffc`**, which contains **12** set bits at positions 2 through 13. Packing enum 4 converts to ABI packing enum 5; the RTOS stride calculation uses 16 bits for this packing enum. Pack-type enum 1 converts to ABI pack-type 1, whose symbolic meaning remains unknown.

For this packing value, the established stride calculation simplifies to `2 * align_up(width,16)` for widths within the calculation's valid integer range. Neither the enum translation nor the mask proves actual payload bit placement, byte order, unpacking, or padding. `SrwEncoder::execute` calling `SetEndian(0)` is container/metadata evidence and does not establish sample byte order. No unpacker was written.

Provider role `0x13` maps through `0x80211c08` to settings row `0x19` (decimal 25) when the LDC-bypass byte is zero or invalid, and row `0x1a` (26) when it is one. Frontend constructor `0x8020d260` calls `0x805683b4` on its `+0x260` subobject, initializing that byte at subobject `+0xf` to zero. A raw byte-store routine at `0x80568730` exists, but its invocation and live value are not established.

Settings initializer `0x8020c3dc` clears two `0x15e0`-byte tables at `0x8187f9b8` and `0x81880f98`, each containing 56 records of 100 bytes. It seeds rows `{8,10,11,17,19,22,23,24,35,36,37,38,50,51,54,55}`. **Rows 25/26 remain zero after that initializer.** `0x8020cb48` selects the first table for version `<3`, otherwise the second, and copies it into manager `+4`. These zero defaults concern the separate role-`0x13` path. The exact selector-28 candidate instead requests role `0x0d`, mapped by `0x80211c08` to row `0x16` (22). That row is explicitly seeded from immutable 100-byte templates at `0x80670178` / `0x8066fe58`, whose width/height fields are 5824/2896. The derived default stride is 11648. None of these initialized settings establishes active live geometry.

The `GPIOu`/`GPIOv` data near startup addresses `0x810069c4`/`0x81006a28` are separate **64-byte callback descriptors**, not those 100-byte geometry rows. The second descriptor starts at `0x81006a04`; its name is at `+0x24`. The phase-one worker's settings-row identification is superseded by the geometry trace and independent review.

## Factory and recipe identity

The true builder constructor begins at `0x8027afc8`. It constructs a manager at builder `+4`, allocates a 16-byte `CFridge_NX1` at builder `+8`, and calls `0x80325bc0` at `0x8027b0ac` with those exact pointers. The setter stores the fridge pointer at manager `+8`. This closes the earlier unknown second argument; `0x8027b0ac` is a callsite, not the constructor entry.

The concrete chain is:

```text
0x8027b778 lazy singleton getter
  -> 0x8027afc8 builder
  -> CFridge_NX1, ctor 0x8027c9b0, vptr 0x8067f218
  -> +4 CFridge_NX1_Still, ctor 0x8027fc90, vptr 0x8067f978
  -> +0x14 CFridge_NX1_Still_PP, ctor 0x80290d94, vptr 0x80684bc8
  -> factory slot +8 at 0x80290e54
  -> (MID & 0xffffff00) == 0x15000
  -> allocate 0x270; ctor 0x802f33a0
  -> CMaterial_PP_Core_Liveview_VideoFull_Adjust, vptr 0x806a1f70
```

The outer Still factory first selects family `MID & 0xffff0000 == 0x10000`. The inner test is a masked `0x150xx` family, rather than exact equality with `0x15000`.

Recipe producer `0x802ba7b4` supplies `0x15000` unchanged to `SetMaterial` (`0x80257bac`). That method reads global manager `0x81884634`, calls `GetFridge` (`0x80325c60`, field `+8`), and invokes the returned fridge's virtual factory slot `+8`. Manager constructor `0x803258e8` sets the global through `0x8025735c`; its resolved builder caller is `0x8027afc8`. Thus **if that manager has been populated by the traced builder and this recipe branch executes**, it requests the class above. The recovered class name is a real namespace surprise; it must not be replaced with an assumed SSIF/sensor class.

The static chain does not prove that an R210 session activates this builder, recipe branch, or material. `0x805836e0` registers an exit destructor; its callback is not evidence of object construction. Earlier labels at `0x807932e0`/`0x80793378` remain rejected as type-info pointer data.

## Image identity and receive-path correction

Producer `0x802ba7b4` assigns the selector-`0x28` image to material slot zero through `0x80251b40`, and passes the same pointer to pre-callback wrapper constructor `0x80574ba4`. The 8-byte wrapper stores it at `+4`, installs vptr `0x80651020`, and is added through `0x80325098` (`RegisterPreCallback`). Its virtual `+8` is `CCallback_Still_Get_ImageBuffer::Execute` at `0x80138d54`. That method can reload material slot zero via `0x80251c58` and subsequently accesses the image's embedded wrapper at `+0x80`.

Original ARM disassembly now bounds the **complete Execute body** through the epilogue at `0x801395cc..0x801395d3`; its earlier bad tail was a Ghidra decoding limitation. For the selector's configured state **`0x16`** at image `+0xa4`, helper `0x8025d9c0` requests **pool ID `0x1c`, flags 0**, through the manager returned by `0x80086b20`, virtual slot **`+0x20`**. The concrete slot target and whether it allocates or reuses a buffer remain unresolved.

On a non-error result, `0x8025cc24(image,buffer)` derives case-2 layout and installs four plane pointers in the same image. `0x800b4f9c` copies 16 bytes at embedded-wrapper `+4`; size setter `0x80560cf8` writes wrapper `+0x28` (image `+0xa8`). With image `+0xb0==1` and state `0x16`, a second updater `0x8025ccb8` copies another 16-byte descriptor through `0x800b4ff8` at wrapper `+0x14`. Both copies preserve the leading selector and state `+0x24`. This supplies a concrete pool/backing-update contract, but does not tie pool `0x1c` or its returned pointer to SSIF DMA.

**Correction:** `0x801ddc20` belongs to **`CBackend_Ipc_Ctrl_View_Sra_Output`**, vptr `0x80668658`, slot `+0x30`. It is an IPC SRA output control, not a proven SSIF DMA backend. Descriptor `+0xc` receives pixel-bit getter `0x80541a30`; descriptor `+0x10` is a separate receiver for PData, stride, and geometry accessors. Their equality to each other, material slot zero, or image `+0x80` has not been established. Earlier SSIF/DMA wording based on this method is withdrawn.

`CDriveBaseNx::checkBufferFull` (`0x802756b0`) obtains `CCaptureMemoryManager` through `0x80086b20` and calls its virtual `+0x38` (`0x800ad300`). This delegates through `+0x34` (`0x800ad298`) with `0x04000000` (64 MiB), checking allocation availability. The log “Cannot alloc SSIF buffer” supports the check's purpose; it does not prove a particular image allocation or receive address.

The nearby `isr_ssif_vsync` route signals and waits on a generic event flag. Its event-handle slot is `0x81bc7ccc`; no image address, stride, or receive length has yet been tied to this synchronization path. Separately, the `SSIF to IPC` diagnostic belongs to `CheckPPInterruptStatus` at `0x80213710`: under a PP-EOF predicate it logs an unexpected interrupt and sets a state byte to 6. That status/error checker does not establish a pixel transfer. The actual SSIF receive/output controller remains a separate question.

## SVC 6 and the cache-operation boundary

The FileDump receiver path remains `CCaptureMemoryManager` virtual `+0x78` → `0x800ae6d0` → virtual `+0x7c` (`0x800ae71c`) → `0x8051772c(pointer,size,3)`. Its ARM wrapper saves `r0-r3`, passes `r0=sp`, loads **`r12=0x010c0311`**, and executes **SVC 6**. The argument block contains pointer, size, value 3, and the incoming fourth register; no meaning has been assigned to that fourth word.

Vector `0x80000088` indexes table `0x81004000`. Startup helper `0x80000f94` clears `[0x81004000,0x81004440)`; later initializer `0x8002380c` writes handler **`0x80023fc0`** at SVC slot 6, `0x81004018`. Init dispatcher `0x80013a0c` contains an indirect call to that initializer; the dispatcher's own activation remains unproved in the bounded trace.

Handler `0x80023fc0` loads callback `0x81006018`. The independently established boot copy from ROM `0x807ef900` to RAM `0x81006000` maps source file offset `0x7ef918` (`1c 40 02 80`) to callback **`0x8002401c`**. No boot-copy delta was applied to the lower SVC table.

For nonnegative selector `0x010c0311`, callback `0x8002401c` calls router `0x8002198c` with the original stack-block pointer and full selector. The low byte is `0x11`; routing is one-based:

```text
index = (selector & 0xff) - 1 = 16
record = *(uint32_t*)0x814482d4 + index * 0x28
       = registry_base + 0x280
target = *(uint32_t*)(record + 0x0c)
```

The registry global is beyond the established copy end `0x81447f40`. Initializer `0x800210a0` reads limits under keys `TMaxSsyId`/`TMaxSsyPri`, allocates `count*0x28`, and stores the returned base through `0x814482d4` at `0x8002110c`. Each callback field starts as sentinel `0x80023da4`. The init dispatcher has a pointer to this initializer; the dispatcher's activation caveat still applies.

Registration code beginning at `0x800211d8` uses the same one-based record calculation. With a nonnull descriptor, its word `+8` is copied into record callback field `+0x0c`. Function-pointer table cell **`0x80023f40`** contains this registration entry. Its consumer/indexing and the route that supplies record 17 and its descriptor remain unresolved in the bounded evidence. This explains table allocation and the callback-installation contract without assuming a live descriptor value. The operation-specific function remains unresolved, so cache clean/invalidate behavior, line rounding, barriers, address domain, completion, and hardware effects remain unknown.

## Evidence and continuation

RTOS SHA-256: `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. Linux capture library SHA-256: `fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7`. Source-byte and artifact index (not published: `../factory-format-syscall-evidence.json`) binds selected original bytes and retained reports, manifests, evidence exports, and closed snapshots under `output/decompile/rea/continuation-20261014/pass5/`.

| Claim | REA evidence |
| --- | --- |
| Image-info assignment | `ev_4b761f4331a568e2c2ca3aa4e28d719b3cbbff4e6848afcad4cefe17b94d144c` |
| Provider constructor metadata | `ev_4be3af294570fbbfe697e6f978a9dd38f04f89cabc7dfe8a4610f1b91f395441` |
| Linux format conversion | `ev_24cfbd6e1f509d625b8ec26f7f865966b49654f69f2221610845bb421bf2470f` |
| Geometry initializer | `ev_6402db6402286a30a62bc2f454a3172a2f4beea859bf1307f42da64c225a038d` |
| Still_PP full MID factory | `ev_34d16f8c49b8dfee12e5e095f4696da744d085a5519f0c86aa2a92a825229579` |
| Recipe/global-manager/factory bridge | `ev_62bca9c33a7ea6383df95354f62959eb31422d782961d766aca0c179efb69455` |
| Material image/pre-callback assignment | `ev_cef4b4cf6cdd2dac251b21e2e8016bd866ffbe5cd7fa13c2d01b8c7e5611c3da` |
| Complete Execute and state-to-pool path | `ev_5ccbeb4a0d1a5a1568e395e6009a60c354e2fdb78225a70dbd477b4ab8834c11` |
| IPC SRA SetCtrl | `ev_f206a624d3377d4b4270be8c1164b263e6abe3a70278a30f66b5047da2712420` |
| SSIF-to-IPC diagnostic owner | `ev_2fdf534be4f00397e4631315c4184ccd47c2cdadb63363e89eeca99aadfd9349` |
| SVC slot initializer | `ev_177d2f0a24941a2bc8d5aa76ae0273fe9ab93b45b43a7d8a61bba39a581d46a2` |
| SVC router | `ev_5e0e4e9a9417b20bf1126b850f0e146c4ebbcd08558af56fe828c43fb05058bf` |
| Registry pointer WRITE | `ev_19ae504c25b20043dd962c1d6e5481025b7ca9a57ee25008de78026f8e791601` |

Next priorities are the concrete memory-manager pool-`0x1c` method and its receive-buffer binding, later geometry writers, actual SSIF receive address/stride/length programming, and the syscall registry's selected callback. R210 reachability and verified saved pixels are still required before claiming sensor RAW capture.

The [final resource and restart checkpoint](INVESTIGATION_CHECKPOINT_20261014.md) records the 90%-used stop boundary, closed sessions, saved snapshot inventory, and both scheduled restarts.
