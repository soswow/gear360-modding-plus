> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# RAW activation, geometry and completion investigation

15 October 2026, 05:40 Sydney restart. This pass continues the [allocator/output/cache checkpoint](INVESTIGATION_CHECKPOINT_20261015.md). The five-hour allowance refreshed from 93% used to 0% just after the scheduled wake; a fresh investigation goal was created. Luna workers trace focused questions and the parent reviews original-byte bindings and integrates accepted results. All investigation remains offline.

## Material configuration selects the frame-start route

The previous pass established the conditional material-to-output pointer path but left the stream-wrapper flags unexplained. The first reviewed milestone closes that static connection.

Material method `0x802e530c` submits its six 12-byte rows at material byte `+0x140` to manager method `0x8031e578`. That manager first parses the table using its stream-wrapper receiver at `+4`, then, on success, copies rows to frontend C through its receiver at `+8`. The MID `0x15000` material's method `0x802f36a0` writes tag 3, enable byte 1, and `GetImage(0)` into the corresponding row.

Parser **`0x803209a4`** maps tag 3's enable byte to setter **`0x8057b9ec`**, which stores it at **wrapper byte `+5`**. Tag 4 maps to setter `0x8057ba1c` and wrapper byte `+6`. Both setters were decompiled in a carrier containing their actual code, and original instructions retain the byte stores. The wrapper constructor explicitly initializes both flags to zero; flat bytes at singleton pointer-cell locations were not used to infer runtime state.

Both `StartVideoStream` (`0x8031feac`, called from `0x8031e630`) and the selector at `0x803200e4` (called from `UpdateVideoStream`, `0x8031ee60`) select **route A whenever either flag is nonzero**. Route A calls derived slots `+0x14,+0x18,+0x20,+0x24,+0x1c,+0x38` in order while preceding methods succeed. Route B ends with `+0x28` and omits frame start at `+0x38`.

Consequently, applying the candidate material's enabled tag-3 row successfully sets the flag selecting route A on a subsequent dispatch. This is a concrete conditional static bridge from the material table into the already traced frame-start path. Active R210 recipe selection, invocation order on the device, intervening flag updates, helper success and actual pixels remain unobserved.

| Evidence | Complete REA record |
| --- | --- |
| Parser tag-to-setter mapping | `ev_d622e54fdfdb9f6823829840039546652c3a944d112aa414ab42f169f11de3bb` |
| Actual byte setters and UpdateVideoStream wrapper | `ev_5373341ae29eea0140f03e227e2bc86f4b3cfff2188d1d27f6277c93cc2a5b62` |
| Route selector instructions | `ev_57f86947c3001ffbed9829f07b05f563ec587751c4c7f0567be4edf8d732e3a1` |
| Constructor's explicit flag initialization | `ev_15ab367c3137ed01cef40ef12febbb07054a7e877b50455ba846fad65444d22f` |
| Previously reviewed manager stage order | `ev_587850260d19105876ba38e9792dfe39ffb80a3810c1e32ae5488c3b0784b782` |

Original RTOS SHA-256: `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. Full records, carrier manifests and closed snapshots are retained under `output/decompile/rea/continuation-20261015/pass7/activation-route/`. The source/artifact index (not published: `../activation-geometry-completion-evidence.json`) records selected original windows and frozen reviewed artifacts. Independent review accepted the conditional route proof. The [component map](BINARY_COMPONENT_MAP.md) explains the binaries, recovered structures and C/C++ reconstruction boundary.

## Linux RAW compatibility remains a separate gate

Independent source-bound review confirms the observed converter maps internal pack types `0→2`, `2→3`, and all other values to ABI 1. Thus the candidate's internal type 1 remains ABI 1 on this converter. The examined `libmm-photo.so` RAW parser requires descriptor `+0x28==3`. Its queue copy preserves descriptor bytes and does not supply a conversion.

The examined ImageGenerator registers image-set callback IDs `0x04010001/5/3/6`, rather than RAW `0x04010002`. Linux `CCapImageSetCb` has no corresponding RAW handler, and the RTOS `transmitImageSetCallback` sends internal RAW image-set topic `0x03010002` to its default/no-export path. These are bounded findings about the inspected routes, not proof that no alternate route exists. The single-image callback and its liveview topics are separate interfaces.

No candidate-to-queue binding or alternate type transformation has been established. A reconstruction must not simply rewrite type 1 to 3: symbolic packing meaning and actual pixel layout remain unknown. Converter evidence is `ev_24cfbd6e1f509d625b8ec26f7f865966b49654f69f2221610845bb421bf2470f`; parser evidence is `ev_3e0efdfc66f506d67f0d2ed302996342a962eac28c8a48c7db148e42b55c1ff6`. The focused review and original-range hashes are retained under `pass7/independent-review/`.

## The candidate has its own conditional FileDump path

Producer `0x802ba7b4` passes the **same selector-`0x28` image pointer** to material slot zero, the `CCallback_Still_Get_ImageBuffer` constructor, and an optional `CCallback_Still_DumpSize` constructor. The last registration is guarded by **`0x819521c4 & 0x10` during recipe construction**. This is a different flag from the previously traced SSIF row-dump diagnostic.

DumpSize constructor `0x805758a4` stores that pointer at callback `+4`. Its Execute method **`0x803333fc`** uses the stored image, obtains PData via `0x80541b48`, derives the dump length from image metadata, builds a `/mnt/mmc/` filename, and calls **`0x80051618` → `FileDump`**. The previously reconstructed FileDump transport therefore has a concrete conditional caller carrying this candidate's backing pointer. It bypasses the unrelated Linux RAW parser's pack-type guard.

This closes an export-identity gap. It does not prove the flag is enabled, that this recipe runs, that the callback follows completed sensor output, or that a file is written successfully. A filename's Bayer/bit labels are generated metadata, not a pixel measurement. FileDump's ignored failures and ACK-after-open-failure behavior still apply.

Material destruction deletes the exact image object through its image-slot array. Selector-image vptr `0x8067a158`, deleting destructor `0x8025c930`, destructor `0x8025c8d8`, and embedded-wrapper helper `0x800b4cb8` establish that chain. **This destructor clears wrapper status and frees image-object storage; it does not return the arena pixel allocation.** The separate generic manager teardown explicitly releases PData for its own lists, but this candidate's membership in those lists remains unproved. No safe export lease or release/reuse schedule follows from these findings.

The buffer-allocation callback also has no existing-PData guard before its state-`0x16` request. A non-null callback image field skips only the fallback image lookup, not the memory-manager request. Repeated runtime allocation, caching or release behavior cannot be inferred without the complete owner/manager schedule.

Key records: producer `ev_0fa01a39e8cfbd589a38d032836fbb99184f4a1130f77e1589f5704a21aca82f`; callback constructor `ev_3a1bfff0657365c278ce63dbe46d43f9e15fb774587b70a89210a8f3d2c52f85`; dump Execute `ev_041e97f97459b2653208c43f68082ee82537100b5dd73ee2618587bb2909fa1c`; slot-owner cleanup `ev_49f87ccd48caa5744ab184c317720c9f44259a7b6d970480354249cc65844197`; image destructor `ev_66c5478d9c3560274f33ab714c3683fc2c26ad8c3837b36582978d8e78fb024b`. Full records and independently checked original-byte windows are under `pass7/candidate-lifetime/`.

## Configured driver binding and geometry limits

**Pass8 correction:** the exact selector-28 candidate requests provider `0x0d`, mapped to seeded row `0x16` (22), with initialized geometry 5824×2896 and derived stride 11648. The rows 25/26 discussed below belong to separate provider `0x13`; they do not supply this candidate. See [current findings](RAW_EXECUTION_PROVENANCE_AND_LIFETIME.md). Active dimensions remain unobserved.

The compiled frontend setup now has a concrete device binding:

```text
CCaptureFrameworkFactoryNX1::createFrontEnd (0x800a10d4)
  → subsystem setup 0x80214618
  → FrontendNx creation 0x8020d948 / constructor 0x8020d260
  → config singleton 0x81883b84, obtained via 0x80214c80
  → config virtual +8 initializes field +4 through 0x8022e420
  → config virtual +0xc returns device 0x81884450
  → frontend stores that device at byte +0x250
```

The device constructor `0x8022e4cc` installs primary vptr **`0x80676a70`**, identifying the `CS5K2M8SXDriver` implementation. `0x8022ee90` is its registered destructor, not its constructor. The factory itself has a compiler-style initializer and a concrete vtable entry for `createFrontEnd`; invocation of that initializer/virtual method on the R210 remains unobserved. This is a **software binding for this frontend path**, not identification of the active physical sensor.

`VideoSetSensorMode`'s device slots resolve to `+0xa8 → 0x8056a56c`, `+0xac → 0x80240400`, and `+0xd0 → 0x802431b0`. The mode dispatcher selects recipe `0x80237904` for device mode bytes `0x11` and `0x13`. That recipe includes literal writes `0x034c=0x02e0` and `0x034e=0x02d0`, encoded by the five-byte register-write helper. Their semantics as dimensions are not established here. Device mode `0x13` is not thereby equal to provider role `0x13`.

The selected manager-copy geometry fields are `0x81882f48/0x81882f4c` for row 25 and `0x81882fac/0x81882fb0` for row 26. LDC configuration selects the row. Profile construction leaves their defaults zero; identified later consumers copy/read rows. No later nonzero writer or active dimensions were established. Empty references to individual row addresses cannot exclude computed writes or generic copies.

Original vtable bytes were checked directly against the flat image. That vtable lies outside the geometry-device carrier's selected text/rodata ranges; the corrected report explicitly records this separate provenance. Parent review verified all 59 windows in the four milestone manifests against original source offsets and hashes.

Key records: config setter `ev_64d6082cdf0fc77d37ae46c9d44514889a1ecb792254dafda4c235e347712e66`; device accessor `ev_4edec3eb54f4cd57b843634b89969af21927baa64d1fa4bbc611c500dc7c5846`; factory frontend method `ev_46562e8d7a55853ce0d2fc152be035fc49bd2f15386c7dadc319defffa0ae898`; mode dispatcher `ev_60c2abd9c0c36e95c30d44585c548669717b01a58fa918051629b0d680b6ca9a`. Reports and source-bound manifests are under `pass7/platform-identity/`, `geometry-device/`, and `geometry-row-writers/`.

## WDMA tail is state programming, not a completion wait

Following the complete `start_wdma` body resolves its post-programming helpers. The initially suspected wait is a global state query followed by MMIO mask changes and an optional IQ notification. Another helper forwards operation 7 to a leaf that changes bit 19 at `0x20800000`; it does not poll or block. These calls cannot establish frame completion. Their full bodies and source windows are retained under `pass7/dma-completion-input/`, including start evidence `ev_188bb49295f69e39caa3b3bc6acebe9a8bc34c7ba5acae4d101361120b4dc568` and state-query implementation `ev_0b0f0504077d654205b53a868e5d752d27c1f52809dfbcd2ed21331cab257188`.

A later focused trace recovers a software completion protocol. FrontendNx virtual handler **`0x802128e8`**, primary slot `+0x80`, maps incoming event code **2** to event bit **8** on its receiver's event group at `+0x298`. Task loop **`0x8020e240`** waits for mask `0x3f`, clears the returned bits, and handles bit 8 as **WDMA Done** while its receiver's byte `+0x291` is set, then clears that byte. Derived frame start **`0x8020f6e4`** sets `+0x291/+0x292` before delegating to the traced output path.

These methods share the FrontendNx layout and corresponding offsets; their runtime receivers' identity with one another remains unproved. The inspected frame-start override does not explicitly clear pending bit 8, and the virtual handler's caller/hardware-status producer remains unresolved. Consequently this does not establish a fresh, frame-specific hardware completion or stable sensor pixels. The same wrapper flag `+5` that the candidate sets also gates the checker's unexpected-PP-EOF diagnostic; that message must not be read as an observed sensor transfer.

Event mapping evidence is `ev_d861446cf6e3afac09cc7cc2ac2da2c507aca7aa1ef0e647ac3731eb405243a1`; frame start is `ev_57ecd4bda3be6d3b793b8ea5914022a43e793b645e70ff4fc38d19fef1cadbb3`; task loop is `ev_4a244a5babc3bc6cfe0acb228ccee729acba3b39834bac335b8cb5c42fb6f9c5`. Original windows and closed snapshots are in `pass7/frontend-interrupt-state/`.

## Export configuration and local callback order

The candidate-export flag now has a concrete configuration writer. Handler `0x8027eb24` is registered under `dump`; its `ssif` option contributes bit `0x10`. Setter `0x80570b64` assigns the entire resulting word to `0x819521c4`. The parser compares its mode with `on`; a nonmatching mode complements the accumulated option mask before assignment. This is not a read/modify/write preserving every other flag. Its owner is a lazily initialized configuration/command-tree object. A usable shell command, default value and active runtime setting remain unproved. The flag is tested when the recipe installs callbacks, so a later setting change does not by itself establish an already-existing callback.

Callback execution is also recovered locally. Pre dispatcher `0x80324e98` invokes callback virtual `+8`; eligible pre lifecycle method `0x80250604` sets material state to 3 before invoking that list. Post lifecycle method `0x80250880` accepts states 5/6/8, sets state to 9, invokes the MID's ISR-unregister hook, then calls post dispatcher `0x80324fe0`. Thus the optional DumpSize executes after that unregister path in an eligible post transition. No backing-release call appears between these steps. This does not prove that the post transition follows the candidate's completed DMA, or that it precedes destruction in the active graph.

The graph containers have distinct levels: a parent holds CProcedure children at `+0xa4`; each `0x20`-byte CProcedure holds materials in its own vector at `+4`. GetMaterial walks those containers by MID and is not an executor. CMaterial virtual `+0x10` resolves to wrapper `0x802e508c`, which calls the `+0x44` table updater before `+0x48` and later operations. The external caller traversing the active graph remains unresolved.

Configuration evidence includes `ev_7328b3295d3fb11660e9125d0510dd1655cd310ceba9189540532039a4d57f1a` and `ev_d5d6358cb07b085c8362dc2ee0a56c87d874f0aaf56d7de704cc65064259861b`; local callback order is `ev_c6ce8f192c42ece6683f9b39ac8d4707ff9b8f7b5488423da8295398cfb13208`; common lifecycle wrapper is `ev_9b441939995cceff2f905a1d69d5c1862536da3c896371a37c56a267fd899154`. Reports are retained under `candidate-export-flag/`, `callback-export-order/`, and `activation-route/common-dispatch/` within pass7.

## FileDump has no scheduled timeout or request-matched completion

The formerly unresolved all-ones timeout is now interpreted from the kernel instructions. Wait service `0x8001860c` sign-extends `-1` into the pair `ffffffff:ffffffff`. Helper `0x80013f8c` recognizes that exact pair, self-links the timer node and returns before reading the clock or inserting a deadline. The task can still be placed on the semaphore wait list. This is **no timeout expiration scheduled**, rather than a far-future deadline; other error/wakeup paths remain possible.

The context constructor creates its semaphore once with initial count 0 and maximum 1. A post with no waiter can leave one token pending, which a subsequent wait consumes. The analyzed FileDump/ACK path has no per-request sequence or per-call clear, and FileDump ignores wait failures. A late/unmatched ACK can therefore conditionally satisfy a later wait. No such occurrence or runtime concurrency was observed. An indefinite wait does not improve the ACK's evidence of file success or create a pixel-buffer lease.

Kernel evidence: timeout helper `ev_cc3d3cac443a2e5f17f4af62f5c8c2029b8984fa88416ae286476267e00b5f3e`; wait service `ev_82ed850f0ae0ee73b3df70d45c0edc5c71979dbfe50de741862563c5aea53f38`; semaphore create/post `ev_709799367b1d0547c5db5538a811baf755cc4280a358cc3c78cb3e953930c676` / `ev_4ff8765a916da38d99046e49c394e7177a427e941a2ad8e88e3243da04fd4560`. Full source windows, instructions and closed snapshots are in `pass7/filedump-timeout/`.

## Input programming remains separate from pixel provenance

CPP_SSIF configuration reaches SSG registers and a concrete `d5_pp_bf_set` path. Its size helper copies three configuration-record words to `0x20806860/64/70`. Those records are assembled from mode-indexed tables; they do not carry an identified image allocation or receive-DMA destination. Their exact interpretation as width, height or stride was not established. Endpoint RDMA mux programming was also found but not linked to the candidate, so it is excluded from the sensor-input claim. Reports and original-byte evidence are in `pass7/pp-input-source/` and `ssif-receive-programming/`.

## Usable offline C++ artifact

The [C++ inspector](../raw-buffer-prototype/README.md) now supports `inspect-image` for saved 136-byte `stCAPTURE_IMAGE` records. It prints source-verified numeric geometry words, stride, format/packing enums, address fields and byte count, and reports only whether the observed Linux parser's pack-type guard passes. It never dereferences those addresses or accesses a camera. It keeps the 76-byte FileDump layout distinct.

The Apple C++17 build passed warnings as errors. Independent fixtures checked field offsets/endian decoding, type-1/type-3 guard behavior, 135/137-byte rejection and existing inspect/split behavior. A second Luna review rebuilt and validated it. This is reconstructed contract-handling code, not an acquisition implementation. Validation artifacts are saved under `pass7/offline-image-inspector/`.

## Remaining capture gates

The candidate's arena release owner, actual frame geometry, sensor-input provenance, fresh hardware completion, active R210 graph and successful saved pixels remain unresolved. No sensor RAW frame has been captured.
