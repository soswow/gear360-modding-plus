> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Investigation checkpoint: multi-provider continuation

Date: 16 October 2026, Australia/Sydney. The overarching goal remains **capturing verified sensor RAW pixels into a file**, with reproducible acquisition, provenance, geometry/packing, fresh completion and safe backing lifetime. It is unfinished. This investigation was offline: no camera commands, firmware execution, restart, preload or deployment.

## Reviewed milestones

- `27fd1dc`: earlier review of the other agent's 35 continuation commits. Most instruction-level observations were supported; candidate identity, adjacent material-method attribution and evidence coverage needed reconciliation.
- `fab6956`: candidate identity/configuration restored; request22's mutually exclusive allocation arm corrected; type2 descriptor and conditional dump length documented.
- `05c5b50`: material image-count initialization, Pause gates/concrete receiver, initial event-bit pattern and CCook recipe-map selector integrated with source-bound evidence.

Primary references:

- [Candidate allocation and export contract](RAW_CANDIDATE_ALLOCATION_AND_EXPORT_CONTRACT.md).
- [Pause gates, image slots and recipe selector](RAW_PAUSE_GATES_AND_RECIPE_SELECTOR.md), with [33-citation evidence index](../pause-gates-recipe-selector-evidence.json).
- [Owner event and order messages](RAW_OWNER_EVENT_AND_ORDER_MESSAGE.md), covering the continuation after the preceding milestone, with its [18-citation evidence index](../owner-event-order-message-evidence.json).
- [Multi-provider review](MULTI_PROVIDER_REVIEW_20261015.md), including accepted results and rejected worker conclusions.

## What is established

The known MID0x15000 builder passes the same selector0x28 image into the material and the allocation callback. Configuration changes its embedded layout byte from0 to2 and sets allocation request22. The request22 arm selects allocator tag28/64MiB, conditionally on the field still holding22 and allocation succeeding. Type2 layout divides stride×height into four quarter-sized address regions. Initialized stride11648 and height2896 imply 33,732,608 bytes, also the conditional export length; these are static defaults rather than captured live geometry or verified sample packing.

The material constructor explicitly zeroes its image count. Its first successful append therefore writes material+0xc4, without assuming that fresh heap memory starts zeroed. Pause's wrapper follows the inner frontend receiver and discards its virtual result. The software state update and semaphore post consequently cannot establish physical pause success.

CCook+0x54, forwarded as Still-book argument seven, selects map+4 for zero and map+0x1c for nonzero. The message loop can overwrite the field from message+0x10, so the base constructor's zero is insufficient to prove active selection. Recipe ID+0x70 remains a distinct field used to choose the book.

The event-create SVC takes a negative-selector table route to0x80019824. Request word2 is stored at event control-block+0x14; the event-operation target0x80019a44 reads/ORs/clears that field. Creation's zero initializes the pattern for that invocation. It does not prove a new group or a per-frame clear.

The event-3 handler passes its material through an owner queue to the selector-six callback consumer. The actual material owner setter is0x80250504; CProcedure SetMaterial supplies procedure+0xc. Four source-checked kitchen callers supply recipe values4/5/6/9 and zero map selector; caller0x80138a40 forwards its parameter7 as the target selector argument8. The same material also conditionally travels through Assistant selector3 into post lifecycle0x80250880. Runtime graph identity, callback implementation and allocation retention remain conditional.

## What remains open

1. The active R210 recipe and the message senders/values selecting that recipe and map. Static factory availability is not activation.
2. Sensor/SSIF receive allocation and actual pixel provenance. Row+8 values copied into service/capture/Core inputs still need a source-to-address interpretation; RWDMA consumption and SSIF receive destination are distinct paths.
3. The silicon source and semantics of generic pending bit6 behind vector0x27, plus a current-frame identity joining that event to the candidate allocation. Software PP_WDMA0 naming and event mask8 are insufficient.
4. A retained backing allocation through a successful export copy, including alias/reuse/cache behavior. The file protocol ACK protects its request descriptor, not a proven pixel-buffer lease.
5. Delivery of the dump-enable record before recipe creation and successful file output. No verified sensor RAW file exists.

The retained next-query plan is `output/decompile/rea/multi-provider-run/producer-next-query-review.md`. Its priority is the upstream row producer and hardware pending-bit source. Before following the row proposal, account for pass14's already identified zero-valued caller records rather than repeat those traces.

## Evidence and delegation

Source `rtos-p6.bin` SHA-256: `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. Fulltext carrier SHA-256: `3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`.

Local artifacts are under `output/decompile/rea/multi-provider-run/`. `reviewed-phase1/freeze-manifest.json` freezes reviewed reports and closed snapshots. Citation validation checks source/carrier correspondence and exact record IDs, not runtime activation or every semantic inference. Bounded raw kernel windows are not fabricated complete function bodies.

Luna performed focused native tracing and independent evidence review. Cursor used Claude Haiku5.5; MiniMax reported M3.1 Flash Preview. The parent checked their claims before integration. Both external agents made invalid broad negatives after skipping ignored output files, and MiniMax made pointer/branch errors; raw responses are preserved but those conclusions are excluded. Fifteen completed CLI streams are indexed in `external-agent-stream-audit.json`.

The continuation automation `gear360-investigation-03-45-continuation` is scheduled in this chat for **03:45 am Sydney, 16 October 2026**, preserving offline boundaries and a 3% five-hour reserve. The next pass must keep the RAW goal unfinished until verified pixels have actually been saved.

## Final validation and shutdown

The Pause/selector index covers33 exact full IDs,20 source-validated carriers/262 segments, four source-matched REA reads and11 parent byte windows, with zero validator errors. The owner/order index covers18 exact full IDs,21 source-validated carriers/266 segments, four source-matched reads and six parent byte windows, with zero errors. Whole native-phase validation additionally checked39 owner-phase reads (one empty probe) and12 order-phase reads, with zero errors. Parent order-window review matched155 decoded instruction words to original bytes; the earlier receiver-constructor review matched49. These checks overlap and are not summed as unique coverage.

All worker native sessions were closed. A parent REA session query reports `open:false`, no provider binding/run and no observed provider activity. The CLI process audit found no matching external workers for this workspace; fifteen completed streams and observed exit polls are retained. The phase1 and final freeze manifests pin the local reports, snapshots and source checks. No camera was contacted.

Shutdown preparation began with93% of the five-hour allowance used. A final40-byte original-source check resolved getter0x8057f8ec as receiver+0x60; caller-object/callback identity remains open. Validation, local integration and shutdown stayed within the requested97% boundary. No reset credit was consumed. Check live usage before the03:45 continuation; do not infer current allowance from this historical figure. Keep the RAW goal unfinished.

Final index integrity audit checked307 path references across301 unique files and both freeze manifests, with zero errors. The tracked task tree was clean after commit09845f4. The [next-pass task list](NEXT_PASS_TASKS_20261016.md) narrows the remaining queries and delegation boundaries. Final local integration adds this handoff and audit note; no remote push is authorized.
