> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Stock SSIF debug capture: a shorter acquisition candidate

Reviewed 16 October 2026, offline only. **No camera command was sent and no sensor RAW file was captured.** This route is a practical alternative to the MID `0x15000` recipe, with its own completion and lifetime problems. It is not yet a safe repeated-capture procedure.

## Compiled command and object identity

The reconstructed command candidate is `st cap fenx lv rtmem lv ssif`. The second `lv` is the requested image-provider mode; the first selects the liveview debug command family. This spelling has not been tested on the camera.

The previously reviewed Linux shell fallback sends unmatched capture commands as B6 text with a trailing space. The RTOS tokenizer/registry advances the vector so the registered debug object receives `[fenx,lv,rtmem,lv,ssif]`, count 5. Constructor `0x80215980` installs vptr `0x80673a98` and registers the object under literal `fenx` through registry `0x8004a79c`/insertion `0x8004a9e0`. Its initialization wrapper `0x8022ddc4` constructs the static object at `0x81883cb8`; this is compiled initialization, not observed live registration. Vtable slot `+8` is `0x8022c928`.

That method tests vector slot 1 for `lv`, dispatches to `0x802286c8`, which tests slot 2 for `rtmem`, then invokes **`DebugStartLiveviewRunTimeCapture`, `0x802164cc`**, on the same debug object D. Slot 3 `lv` selects provider key 0. Slot 4 `ssif` selects the SSIF debug WDMA helper. This route does not depend on a Still recipe, the YCC setting, MID `0x15000`, or installation of its optional DumpSize callback.

Evidence: parent complete records `ev_3e3d29dc8a7455e2e61bedd3381e48b62f29c45b0b49bdc08c972613401683d1` (constructor), `ev_6a2a0dbb05abf8d0976601e7900aa85dcc43d2d258d84b02daa9d2c2778c5bb2` (initializer), `ev_4b278c9647a122783ab5ed05f0c598ece9196c7ac02323c0727ab286ae95dffe` (debug dispatch), `ev_169ba172da6fb5ca5873f58a054313e2594caa89454d7832aea2c1d7b448c25c` (liveview dispatch), `ev_4a202e93c3f133e72d1bf34b0ad358a882cec7b15bbaa9ec03bba2c61d2f264d` (capture). Earlier generic shell routing is documented in [the shell report](RAW_SHELL_DUMP_ENABLE_AND_CALLBACK_LINEAGE.md).

## One image and one backing address through the path

The capture handler configures the embedded image **D+`0x278`** through the published FrontendNx provider registry. Getter `0x80561ad4` returns FrontendNx+`0x254`; dispatcher `0x8020c14c` calls the selected provider's virtual `+8` with that image. Registry key 0 is populated by `0x803929e0`; its provider vptr `0x806c0680` has `+8 → 0x8057b37c`, the previously recovered embedded-info copier. Constructor metadata includes layout 2, pixel enum 2, packing enum 4 and pack type 1. These numbers are software metadata, not measured sample packing.

Provider key 0 maps to settings row 10 through the retained `0x80211c08` selector. Its two initialized profile templates, `0x806700b0` and `0x8066fd90`, both seed primary width 1920 and height 960. The descriptor calculator's previously recovered stride fallback gives 3840 for these defaults. Mutable settings and actual SSIF dimensions remain runtime inputs. A command requesting a provider does not itself prove that the running sensor stream matches that provider's geometry.

If global **`0x818843b0`** is zero, the handler requests **64 MiB** through capture-manager virtual `+0x24`, whose source table word is **`0x800acdb0`**. With its two zero arguments this uses allocation tag **`0x35`**, mode **3**, through the generic contiguous-run allocator. This differs from the Still candidate's tag `0x1c` request. The global result is installed into the same embedded image through `0x8025ccb8` and `0x8025cc24`; PData `0x80541b48` then supplies P to the WDMA helper and later dump. No allocation-failure/sentinel guard is present in this caller.

Evidence: provider `ev_916eb4eea4172ce7eb8b9e70ca89b9d99ee65175a540a9ef6b0727dd5c3a99f4`; dispatcher `ev_8451ae6b03678d75d92fe6a03b9d9c3fb4e4f4beabc2716b345991ad7c8728a8`; allocator slot `ev_9b378e18dafd035afa7a293dc9c106fd01a5e267759986dbdb8c2cc60fa58595`. Retained registry/row initializer and descriptor arithmetic are in the pass-8 active-geometry records and [allocation contract](RAW_CANDIDATE_ALLOCATION_AND_EXPORT_CONTRACT.md); the new provider and globals must remain distinct from that material's instance.

## Actual output selection, and a separate OTF diagnostic

For token `ssif`, the handler calls **`0x804ffee4(1,0x0401,P,W,H,S)`**. This helper unconditionally replaces bits 0–2 of **`0x20800024`** with **1**, programs output group 3 through `0x80511950`, and calls `0x80511a88(0,0,1)`. Its literal name is `d5_pp_core_LV_mode_debug_ssif_wdma_onoff`. The corresponding `pp` helper `0x804ffdc8` instead clears those low three bits. Both feed the same output-address/stride/geometry register group previously recovered: `0x20800338`, `0x2080033c`, `0x2080034c`, with format at `0x20800340` and related mode bits at `0x20800004`.

This is a source-bound software selection of the setting used by the firmware's SSIF debug path. It supports that intended source; it does not independently identify electrical input, sample byte order, Bayer phase, enabled transformations, or live pixels. After the wait the helper is called with enable 0, which invokes `0x80511ba8`. The fresh stop trace closes its software contract: it calls `0x80511ed0(0)`, `0x80511dd8(0)` and `0x80511cd4(0)`, clearing bit0 in words `0x20800350`, `0x20800334` and `0x20800318`. Each leaf returns immediately; there is no status poll, completion wait or demonstrated DMA drain. Hardware stop latency remains unknown. Thus VsyncFalling followed by this stop does not establish a completed or stable buffer.

Stop EIDs: `ev_f1d08693b7f2be5b2ba3ca258225ea1a75bfdd01a53d22401aa559aa94f21d31`, `ev_d99867038cd0f5aa428bf13c14c30b48acd88b47564cc8912a9cdb05f02f8492`. Complete records/source windows are in `pass-goal-20261016/debug-wdma-stop-luna`; parent independently decoded all three leaves with LLVM Cortex-A9 instruction support. The worker's earlier LLVM excerpts contain unsupported-instruction placeholders; those are decoder settings, not evidence of malformed firmware. Its event-source unknown is superseded by the parent virtual/SSIF binding below.

The existing Still candidate also conditionally reaches Core mode 4 → path 10 → identity byte table `0x806308f0` → `0x804fe6cc` → `0x80511770(10)`. That case calls `0x80512284(0)` and `0x805122d4(1)`, so it selects the same low-three-bit setting 1, while also clearing bits 16–17. This strengthens the static connection to the SSIF-output setting; it does not equate the debug allocation and Still allocation.

**Do not confuse the low-three-bit selection with OTF-to-IPC source bits 12–13.** Diagnostic `0x802dd17c` reads `(0x20800024 & 0x3000) >> 12`: value 0 prints DAMP, 1 prints “Bayer Formatter in SSIF raw dump mode,” and 2 prints AF Y. Setter `0x80512270` writes exactly those bits; path-selector cases 3/4 set them to 1. Path 10 does not set them. Therefore requiring the OTF diagnostic to print Bayer is not an established acceptance test for this WDMA route.

Evidence: SSIF helper `ev_6ca2c70431ec0a9ab1df1a2bc8365d28ec19dc0426d4e20a688d691fb3df9ac4`; path selector `ev_a6012f58b5f6491035fa9c4126461777c1b563b4aad92138341a491f4801281f`; setters `ev_02c97d315ee5fb07ae15837d0c0a0f1eccc64d7e80471eeba7fb24d26bdecacd`, `ev_0272b3870f5fb894ea55d1a22b85ff4780b0f226957687f7b00010f10ec49ed5`, `ev_a1ee5fd8c6a0cca33a54a8c4b881bb59a01f8f8ea7c85551d0b880b3c842eecc`; diagnostic `ev_56c490a8dd66743a979618eafd180be6cf6ee98eb2ed5b2cc97247469dc2beac`.

## Event source: VdFalling is distinct from WDMA done

`0x80323514(0x96)` clears/waits/clears mask 2 on the separate event wrapper `*(0x81952fc4)`, once before programming and once after start. Its literal name is **`WaitVsyncFalling`**. Timeout maps to `0xffffff95` (**-107**); the debug handler logs nonzero results and continues. A non-timeout failure from the lower wrapper is not generally propagated by this helper. Fresh native evidence also establishes that **`0x80215354` is a 40-byte no-op**: the surrounding calls to it do not arm anything in this image.

The parent follow-up closes the virtual mask-2 producer beyond the initial worker report:

1. Create `0x80322f70` constructs CFrontEndVsync, creates the separate mask-`0x0f` event group, and calls registration `0x80323af4` with owner `*(0x81952fc8)`.
2. Registration inserts that same observer into the VdFalling descriptor returned by `0x8031117c`. Descriptor constructor `0x803110b0` installs vptr `0x806a7848` and event code 0 at descriptor+`0x10`. The base registry/fanout methods are `0x8030f23c` and `0x8030f518`.
3. Descriptor virtual `+4`, original code `0x80311268`, passes callback **`0x803112b8`** to `0x80313820`, which stores it at the separate CPP_SSIF service's +8. `0x80313444(service,0,...)` builds interrupt descriptor ID **1** from that callback and passes it through `0x8032ad28 → 0x804e2bb8 → 0x804e295c`. The last writes callback table **`0x8144a814 + 1*4`**; the registration path clears the corresponding pending/mask bits at `0x2080602c/28`.
4. Retained SSIF dispatcher `0x8003937c` services low-six pending bits at `0x2080602c` through that table. The registered callback increments counter `0x81952d38`, wraps at 10000, and invokes descriptor virtual `+0x14`. Fanout calls each enabled observer's virtual `+8` with code 0.
5. CFrontEndVsync vptr `0x806af620`, slot `+8 → 0x803238ec`, maps code 0 to posting **mask 2** on the same event wrapper.

This proves a conditional compiled **SSIF interrupt ID 1 → VdFalling callback → mask-2 waiter** path. Create inserts the software observer; invocation of the descriptor hook method and the interrupt-enable call are additional conditions, not effects proved merely by observer insertion. Runtime registration, fresh association with P and WDMA completion remain unobserved. PP WDMA-done uses the distinct `0x20800018`/bit-6 registry at `0x8144a798`. Group-bit clearing is not a request sequence number or proof that a pre-existing hardware interrupt cannot arrive after start.

Key parent EIDs: registration `ev_6385033b27529357866c04e1fbfb9dccd018ee357b23bffa6bfa420e8e8f8dba`; observer insertion `ev_2d7280c26761ebf62e5b366783f6f1e521d60fdeae96f75341bfc717fe07a1e2`; fanout `ev_596ea8fdb5af4c5887f6d0d851225e4c884af83b96202e443a0a61aad4bf8eae`; falling callback `ev_8394d215ef6a6140ee6a34d2eaa1158f9c40cfa8623f3c91f602e2f6a4bd1bbf`; hook setter `ev_a421313df5de75cd4ea465fe560855dcbb9b9241ae079119fd10136fac1c4653`; interrupt record `ev_6d9ea73a4170fcfa1d15fd5bb926f38416ac3d70350123de00d742542e9d598a`; driver `ev_7da469dbb076d51ba87a80c7dacf93f6d3113eba4159c80c4639bb3f0d4d629e`; installer `ev_cd433fd1aa307ad072f307331331efa3fe116fce40352e30102faba715fd89c5`; observer `ev_f37c3a132c0c932c729deba2610e21b0f879c0fb40484e9e264d05869a857c5a`.

## Filename, transport and backing hazards

At capture completion, the handler copies its vector strings into D+`0x6c0` at ten-byte spacing, then calls **`0x80226004`**. For the exact proposed vector, stored token 1 is `lv`, so the dump routine chooses **the same D+`0x278` image**. It formats `/mnt/mmc/%s_%s_%s_%dx%d.raw` using stored tokens 0/1/2 and **stride, primary height**. The name therefore encodes **stride × height, not pixel width × height**.

The request is `FileDump(base_name,P,S*H,1)`. Flag 1 requests a filename suffix; it does not select a different file transport. The retained Linux `DumpFileData` implementation searches `_001`, `_002`, etc. Thus, **if the row-10 defaults and derived stride are used**, the base request is `/mnt/mmc/fenx_lv_rtmem_3840x960.raw`, the first available final name is normally `fenx_lv_rtmem_3840x960_001.raw`, and the request extent is **3,686,400 bytes**. These are conditional initialized values, not a measured capture. Existing files change the suffix; exhausted suffix search is not handled as a success guarantee.

The whole-buffer transport retains its known `/dev/mem` address expectation, cache maintenance, reusable semaphore and ACK limitations. Actual descriptor/P mapping and file byte count require observation. `fwrite`/open failure can still be followed by ACK; ACK alone is not success.

After FileDump returns, the selected dump branch calls manager release `+0x5c` on each nonzero global `0x818843b0/b4/b8`, discarding returns and **without clearing those globals in this body**. A later capture skips allocation when b0 remains nonzero and can reinstall a released address. This is a concrete conditional repeated-use hazard, not proof that the camera has executed it. Other writers may reset the cells, but none is established on this exact return path. An unrelated allocation at the same address also defeats pointer-value-only ownership checks. Tag `0x35`/mode 3 does not establish an exemption from bulk cleanup; the retained generic run range can overlap `cleanBuffers`' sweep. A compiled stop-liveview/Sleep cleanup caller is already known; runtime overlap remains unknown.

Dump evidence `ev_6eb354cdd5b581eac7806d0c7862a546384a8c5eca941d8103e6096a9dd3fd12`. Linux suffix/ACK record: `ev_76e94c4912dc23315c51e5007ae9c1aa30d80a789f074a38f9b0526dbf55452c`, retained at `continuation-20261015/pass6/pool-provider/address-domain/analyze-linux-dumpfiledata.json`.

## Review and next decision

This route bypasses several recipe-selection unknowns and gives a precise first-file candidate, source setting, allocation, event path and expected naming algorithm. The next useful work is a bounded acquisition/observation trial after the user separately authorizes camera-state changes and after completion/retention prerequisites are addressed. Repeating broad library mapping will not establish actual pixels.

All fresh complete records and the closed parent REA snapshot are under `output/decompile/rea/pass-goal-20261016/`. `parent-native-body-source-bindings.json` independently compares provider ranges with the pinned original and carrier; `stock-debug-extra-source-windows.json` preserves data tables, literals and unrecognized small code excerpts. Worker reports retain their initial boundaries; the parent follow-up above supersedes the mask-2 source unknown and corrects the numeric timeout. Static schemas/source checks do not validate live behavior. The [acquisition decision plan](RAW_ACQUISITION_DECISION_PLAN.md) defines the finite next step and acceptance criteria.

## BF geometry relation reviewed from retained evidence

The BF builder `80318adc` overwrites the raw table dimension positions with selected settings row `+4/+8` (W/H). The packer and `804e3774` then place `(0,H)`, `(0,W)`, `(H,W)` in the low14 bits of the successive low/high halfwords written to `20806860/64/70`. Preserved top bits remain unspecified. Raw row22's template value2904 is therefore not the final height; unchanged initialized settings would substitute2896. This proves a conditional software configuration relation, not active sensor geometry. The full arithmetic, original EIDs and byte review are preserved in the pass evidence's `parent-bf-geometry-review.md`.

Versioned complete records and review traces are indexed in [the goal-focused checkpoint](INVESTIGATION_CHECKPOINT_20261016_GOAL_FOCUSED.md).
