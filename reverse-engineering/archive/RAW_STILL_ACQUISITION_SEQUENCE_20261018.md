> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Still-photo acquisition path — 18 October 18:30 kickoff

The persistent acquisition goal is active and unfinished. This pass identified a concrete Linux control API and traced conditional still output selection into a DMA-start image field. It did not capture or certify sensor RAW. The next decisive observation is the **actual R210 still request, selected output and completion association**. The camera's documented HTTP endpoint timed out twice; continuing static class searches would not establish that runtime edge.

## Reviewed identity and scope

Original RTOS: `rtos-p6.bin`, SHA256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. Code VA is file offset + `0x80000000`; copied RW banks require the separate startup mapping below. Linux `libcapture-fw-prod.so`: `fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7`; its REA/Ghidra addresses add `0x10000` to ELF VA. No live build/process/pointer identity was revalidated because the endpoint was unreachable.

REA/Ghidra native evidence, raw source windows, sanitized worker packets and review records are under `output/decompile/rea/pass-kickoff-20261018-1830/`. `source-carrier-validation.json` checks 14 carriers / 80 source-identical segments. This validates bytes, not runtime execution or ownership. Linux instruction/ELF mapping checks are in `linux-original/index.json`.

## Acquisition sequence: what is joined and what is missing

| Edge | Accepted result | Proof limit |
| --- | --- | --- |
| App → Linux request | App imports `mm_camera_capture_still`. Library implementation at Ghidra `0x3aa7c` constructs a 48-byte request and calls exported `CaptureStillImage` at `0x50b40`. Normal code `0x01010001` uses command `0x201`, length48. Adapter `0x9abc8` forwards bridge ID`0x83`. | Compiled app import/API path; no live invocation or verified public C prototype. |
| Wire command → RTOS request handler | RTOS `0x80061b44` reads command high byte2, selecting the `0x80061d28` bank. Low byte1 selects handler `0x800626f4`, which checks length48, skips the 8-byte header, and calls `0x8004f1ac(payload[0], payload,48)`. | Callback installation/receipt for ID`0x83` is not newly runtime-proven. |
| Normal request → controller message | `0x8004f1ac` case`0x01010001` calls `0x8010d938`; it uses controller+`0x24`, payload word+4, then `0x80109b6c`. The latter constructs record words `{0x0102000a, argument,0}`; two property predicates instead select `0x0102002b`. | Record fields are typed words, not image pointers. The queue-to-selected-drive association remains open. |
| Recipe → material → frontend | Reuse reviewed `RAW_RECIPE_SELECTION_AND_COMMAND_GATES.md`: `0x8027510c` supplies recipe1/3; key27/MID`0x15000` is conditional, not automatic. Ordinary Still callers and material configuration paths reach interface `StillCapture 0x8031f274`. | Do not join different materials/candidates or assert the active R210 graph from class names. |
| Still interface → frontend | Constructor chain: I+4 = wrapper W; W+0 = frontend C; compiled singleton cells `0x81952fb8/fbc/fc0`. W+7 selects sequences `0x8032049c`/`0x803205a8`. Its setter copies tag0's enabled byte; it is not independently a RAW-mode flag. | Conditional constructor/object identity; no current pointer values. |
| Configured image → submission | `0x80322598` copies route records into C. `CapSetInfo 0x803214e4` chooses an enabled output and `0x80395354` stores that image in embedded capture Q+`0x10` (Q=C+`0x50`). `0x803954e4` passes Q+`0x10` through `0x8031b894` into Core+`0x10`. | Image descriptor and pixel backing remain distinct. Actual live route, backing and geometry remain unknown. |
| Submission → writer start | `CapFrameReadout` reaches `0x8039583c → 0x8031c098`; `CapFrameReadWrite` reaches `0x803958b0 → 0x8031b9b4`. Both use output Core+`0x10`; the latter also has separate RDMA image words. | Static register-programming caller chain; not current sensor/producer provenance or completion. |
| Interrupt → selected completed image | Reuse reviewed PP_WDMA0 fanout/event-mask8 chain. Event task calls `0x80213710(C,8)` then clears C+`0x291`. Full checker body only updates C+`0x297`/`0x314`, reads state/route getters and logs. | **No frame identity, pixel lease, export or publication is established by this checker.** |
| Export → release | Existing `FileDump 0x80051618` is a usable compiled transport boundary; optional Still callbacks have conditional activation. Existing `Ipc_End` releases/clears backing before RAW-list insertion. | No new selected-still-image → completed retained backing → export → safe release join. |

For the Linux normal request, the constructed words at offsets0..`0x2c` are `{converted_command, param4, param5, param6&0xffff, param17, param7, param9, param10,0,0,0,0}`. Parameter numbers describe the observed ABI; semantic names are not invented. Do not implement a 17-argument call or use arbitrary values without a verified caller/header and current control context. This is a concrete C/C++ **control integration point**, not a working RAW acquisition program.

## Corrections and bounded conclusions

- The zero W+7 sequence uses frontend slots`40,44,50(req,0,0),58`; nonzero uses`40,44,48,4c,50(req,1,0),54`. Slot`4c`'s result is discarded, as is slot`58`'s result. The preliminary blanket “every step success-gated” statement was too broad.
- The RTOS method cell at source`0x807f9148` has startup-copy alias`0x8100f848`, delta`0x816700`. The bank starts`0x8100f840`; index1 contains`0x800626f4` with adjustment0. The high-byte bank entry at`0x8100f7d8` contains`0x80061d28`, adjustment0. Source ROM table searches are not runtime-table binding. Copy proof: `RAW_EXECUTION_PROVENANCE_AND_LIFETIME.md`, source`807ef900..80c31840` → destination`81006000..81447f40`.
- `CMaterial_PP_Core_Still` constructor installs primary vptr`0x80684c30`, secondary subobject+`0x120` vptr`0x80684c90`. Historical shorthand `0x80684c5c` is primary slot`+0x2c`, not the installed address point. Do not use that shorthand for new virtual-slot calculations.
- `0x80213710` is an interrupt-sequence diagnostic, not the missing export callback. Its complete tail through`0x80214097` contains logging, status writes and return. Stop pursuing that checker as an acquisition interface.

## Smallest next observation / integration proposal

1. Restore camera connectivity; perform the existing read-only app/process, immutable RTOS code, mapping and helper-hash guards. Do not reuse old live pointers without matching guards.
2. Read the fixed singleton cells, then capture bounded metadata for verified I/W/C objects. Record W+7, C route enable/image pairs, Q+`0x10`, Core+`0x10`, selected descriptor/backing/geometry, and any route changes. An idle preview descriptor must remain labelled idle preview.
3. Associate these with **one ordinary still request on the R210**. A user-operated shutter can supply that event while permitted read-only diagnostics observe it; an agent-issued capture or firmware instrumentation needs a concrete separately authorized trial under the retained camera boundaries. Polling an arming byte or reading post-capture metadata alone cannot certify fresh completion.
4. If stock diagnostics cannot expose the association, prepare an RTOS trace at the named configuration/submission boundaries and the actual completion listener. Record request identity, output image and allocation identity, producer/geometry, and completion. A software generation counter alone is insufficient if pending old hardware/software events are not excluded. The completion checker above is not that listener.
5. Only after the active path is bound, choose the existing synchronous dump callback or an RTOS copy/export change. Any change must retain backing or copy to separately owned storage before all relevant release/reuse paths, establish cache/address handling for that allocation, and verify file bytes/hash after copy. Linux cannot call the RTOS addresses directly.

The immediate blocker is current camera availability for step1/2 and the first runtime R210 binding. This is not goal completion or a credit cutoff. Conditional queue/material and completion-listener details remain explicit gaps; do not reopen a broad function census before that runtime observation.

## Principal retained native evidence

- Wrapper dispatch: `ev_6262f5ddab58c715d1c64bf3f75f149a061f379e6670f953e1a252c6481fd7d9`, `ev_3beabe6bc5c3c7853e57323d51b328508c399dbc10b79172ad82b4605b1869a3`; phase1 bundle/snapshot.
- Q image store and Core forwarding: `ev_9b1c57cb383861086fb182a756950ea2e43b7bbfcb114aa2a0b7866e4d7a0f27`, `ev_7401ffaf2eec862ebdece617e77d3a68ee4e52eb114a8d30024d54508631336a`, `ev_3232704c77957bfbaa1b005a2424da9038d2d7af2aa93ca7560ff7843f6fbf53`; original windows in `parent-source-windows/`.
- Linux still implementation: `ev_0c024b53393e43bd73a55535cdecd9ffe18089f60b753292710a1804beb1f9a0`; wrapper `ev_e86875259a88fac6c6c334623d4d84cca20acb9d7c747da9821c25df51f73977`; adapter `ev_255e2b9398fac11120e05ee1ad152ee27507a5eb5f97018b21110b25466e69ee`.
- RTOS bank/handler: `ev_04d9589404c3830ea3cde773b6f621b9449732d870f65647603954b4b44bbb97`, `ev_ff2133e34e877f62b64520c426dbcc08e1c2c65cbcb6a564919f2f4f1b76342c`, `ev_c91cfca4594a3c7e248e31fa2d7d451504cf5311d272012dc1aeff543ae2627d`; copied-table manifest and source-bound cells.
- Complete interrupt checker: `ev_00c821da1e26dfadf86f006f6c0580abeafc93991d965f6351f9413607995868`, body`80213710..802137d7` and`80213818..80214097`; checker bundle/snapshot.

## Delegation, cleanup and source state

Cursor requested `grok-4.7-high`; all client init records confirm `Grok 4.7 256K High`. First attempt failed at provider transport before analysis (exit1). Same-session completion research succeeded (49 calls), then precise missing-consumer follow-up succeeded (10 calls). Parent accepted decisive instructions and narrowed packet-absence / no-explicit-image claims; full checker tail reviewed natively. Luna's mode-byte and packed-message extraction were checked against original instructions and numeric calculations. No exact net Codex savings or hidden backend identity is claimed.

A helper formatting-only assertion initially compared padded/unpadded hex strings; corrected numeric comparisons passed. A trimmed sender carrier cut `0x800aa1c4` at its return; its warning is not evidence of real bad firmware. Use only its proved local virtual call, not a complete-function conclusion. No source change was made.

All owned external PIDs are verified absent; Luna is done; REA is closed. No camera helper was deployed, so no temporary camera files need cleanup. No capture, firmware/MMIO write, injection, restart, persistent settings change, source edit, commit or push occurred. Master remains`2874ae9c01099ced1bbf798c0c5af9cb9c18e7c6`. Notes/evidence stay ignored and local. Historical preview/kernel results are unchanged; the old kernel amendment remains unreviewed and secondary.

Reviewed checkpoint time: 2026-10-18T19:38:53.341475+11:00.
