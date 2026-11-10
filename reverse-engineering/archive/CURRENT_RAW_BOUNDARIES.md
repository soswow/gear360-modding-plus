> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Current RAW acquisition boundaries

**Current selected backing, 17 October:** [work-buffer review](RAW_LIVE_WORK_BUFFER_20261017.md) binds observed PData to the cached movie-work allocation, confirms the image's backing-byte count, records request`ffffffff`/kind0, and closes the traced Pause leaves as register setters without an explicit drain. Capacity27×extent does not prove ring semantics. Generic exporter callers are Still-adjust callbacks without a selected-preview identity join. [Latest handoff](INVESTIGATION_CHECKPOINT_20261017_0105.md) records preservation and the binding weekly usage limit.

**Latest, 17 October:** [full-buffer and Bayer review](RAW_FULL_BUFFER_AND_BAYER_20261017.md) validates the other agent's complete 2,764,800-byte preview snapshot and adds two passive copies. Firmware format1 is explicitly labelled `bayer`; numerical phase tests strongly support GRBG/GBRG, with red/blue order unresolved. Seven and nine VD-falling increments occurred during the new copies. Those are cadence observations, not WDMA completion; coherent frame identity, backing retention and sensor provenance remain open. The older fragment-only statements below are historical.

**Live update, 16 October:** [authorized read-only RAM observations](RAW_LIVE_RAM_OBSERVATIONS_20261016.md) now establish two fixed source anchors, selected image 1920×960, stride 2880 and PData `0xa77bb000` and an advancing VdFalling counter. Debug image remains unconfigured and its three allocation globals were zero. These are software state observations, not sensor packing, DMA completion or ownership proof. No pixels copied or capture triggered.

**Approved register observation:** [one three-word snapshot](IMAGING_CONFIG_OBSERVATION_20261016.md) succeeded: dimension halves1920/960, low-three-bit output selection0 and raw configuration word00680102. The helper was removed; no capture or pixels. These fields do not establish input packing, DMA completion or buffer ownership.

**First passive byte sample:** [format/lease review](RAW_LIVE_FORMAT_AND_LEASE_GATES_20261016.md) records a4096-byte fragment and the exact unresolved packing question. Treat packed12-bit as a live hypothesis; width/stride also fit it. No complete frame, RAW provenance or lease is claimed.

Use this short map before historical reports. The goal is a file of **verified sensor RAW pixels**, with reproducible acquisition, provenance, geometry/packing, fresh completion and safe buffer lifetime. **That goal is unfinished.** The detailed closures below concern static software evidence; the live update above records the separately approved read-only tests. This report does not authorize capture, memory/register writes or firmware changes.

**16 October goal-focused pass:** the [stock SSIF debug capture](RAW_STOCK_SSIF_DEBUG_CAPTURE.md) is a shorter first-file candidate. Follow the [finite acquisition decision plan](RAW_ACQUISITION_DECISION_PLAN.md) before expanding static scope. Its allocation, wait and cleanup differ from the Still candidate below.

| Question | Current static evidence | Remaining boundary |
|---|---|---|
| Which candidate? | MID0x15000 builder binds selector0x28 image, material's first successful append and allocation callback. | Active R210 invocation and later alias/replacement. |
| What configuration? | Provider role13 selects seeded settings row22. Layout byte+0x80 becomes2; request+a4 becomes22. | Runtime geometry and later writes. Role, row, request, layout and pack-type are separate domains. |
| What backing/layout? | Request22's selected arm requests allocator tag28/64MiB. Type2 installer stores four quarter-span addresses and size. | Allocation success/address, backing ownership and meaning of the four regions. |
| Core memory inputs? | Adjacent setter writes Core+0x14/+0x18 for channels0/1; caller0x803954e4 conditionally forwards its descriptor fields. Another setup path clears both. | Descriptor is FrontendNx C+0x50, words C+0x64/+0x68. Its local initializer clears both; runtime writers and active input mode remain open; null memory inputs do not identify or exclude a direct hardware source. |
| What does WDMA receive? | The traced submission supplies the PData accessor's first address P, selected dimensions and stride. | Hardware interpretation of that address and actual sample layout. Four stored words do not prove four submitted planes. |
| Recipe factory guard? | Recipe word reaches selector unchanged; word1 can select key27 under parameter guards, word3 normally selects key18. Parameter+0x170 derives from global0x81482ea4 low bits. | Active parameter values, dispatch and one-shot override. This is separate from dump-enable0x819521c4. |
| Which recipe map/factory? | Selector0 uses Still-book map+4. Its population inserts key27 decimal/0x1b →0x802b3b90 →0x802ba7b4 →MID0x15000. Recipe1 can select that key under the documented gates. | Active cook/recipe, parameter gates and one-shot override. Map+0x1c is distinct; no need to rediscover the closed static-map binding. |
| Material lifecycle trigger? | Runner state machines call material virtual+0x10; its concrete candidate route submits the table/start path. | Active graph, gates and helper success. |
| Start/Pause behavior? | Material+0x18 starts video; adjacent+0x1c can Pause. Pause resolves to the frontend and discards its virtual result. | Physical pause success; software state/posts do not establish it. |
| Completion/event identity? | Generic pending bit6 selects the registered software callback; its fanout sets frontend event mask8. The **dispatcher**, after callback return, writes the one-hot word. | Silicon source/ack meaning, clear-before-submit and fresh frame/allocation identity. |
| Cleanup trigger? | Stop-liveview forwarder validates manager index0..5; index1 selects constructed Sleep object and virtual+0x44 reaches the known cleanup caller. | Active index, candidate page overlap and ordering against FileDump; no lease is proved. |
| Separate event helper? | Global0x81952fc4 gets a separate16-byte wrapper; factory attempts mask0x0f creation; helper clears/waits/clears mask8. | Caller of that helper and relation to submission; this construction differs from frontend inline+0x298/mask0x3f. |
| Initial event pattern? | Negative SVC selectors resolve creation and OR/clear paths; request word2 initializes control-block+0x14 to zero for that create call. | Group reuse and per-frame clearing. A selector is not a matching numeric code address. |
| Post-lifecycle material? | The event handler conditionally passes the same material through an Assistant queue to post lifecycle0x80250880. | Runtime Assistant association/gates, delivery, callback execution and ordering against reuse. |
| Cache operation? | Positive service record17 resolves to callback0x80024e14; operation3 reaches the recovered maintenance loop0x800245c8. | Runtime translation/hierarchy, arithmetic limits and successful execution. The operation-specific callback is no longer unknown. |
| External dump-enable route? | Linux capture fallback sends B6 text; RTOS token shifts select `seq/dump`, whose `on ssif` path assigns flag0x10. | Imported transport execution, live registry and flag-before-active-recipe ordering. The static candidate spelling is recovered; it has not been run. |
| Export extent? | Optional DumpSize reads the candidate's base and asks for stride×height on the configured branch:33,732,608 bytes at initialized defaults. | Enabling it before recipe construction, a stable backing lease, successful output and verified pixels. ACK alone proves none of these. |
| Shorter stock debug route? | Registered `fenx/lv/rtmem` path selects embedded imageD+0x278, allocationtag0x35/64MiB, SSIF WDMA mux setting1 and FileDump(P,S*H,1). | Live availability, matching current SSIF geometry, fresh completed output, stable ownership and actual file/pixels. Provider0 defaults1920×960/stride3840 are conditional. |
| What does its wait mean? | WaitVsyncFalling clears/waits/clears mask2. Conditional SSIF interruptID1 →falling callback →descriptor event0 →CFrontEndVsync →mask2 post is linked. | This is distinct from PP WDMA-done. Actual registration, pending state and same-P frame freshness remain unobserved; failed wait does not stop the debug handler. |
| Can debug capture repeat safely? | Dump cleanup attempts ordinary release of nonzero globalsb0/b4/b8 without clearing them; next capture can skip allocation and reinstall b0. | Other reset writers/runtime ordering, allocation identity and reuse. No safe repeated-use guarantee; mode3/tag0x35 does not establish bulk-clean exemption. |

The initialized geometry is5824×2896 with stride11648, not a measured frame. The descriptor layout byte2, packing enum4, pack-type1 and internal D5 code4 are different fields/encodings. Neither their names nor the arithmetic establishes Bayer order, endian layout, bit alignment or sensor provenance.

## Authoritative detailed reports

- [Finite acquisition decision plan](RAW_ACQUISITION_DECISION_PLAN.md).
- [Direct stock SSIF debug route, output setting and falling interrupt](RAW_STOCK_SSIF_DEBUG_CAPTURE.md).
- [Closed Still factory binding and actual control-message coordinates](RAW_STILL_FACTORY_AND_CONTROL_MESSAGE.md).
- [Frontend input identity and local initialization](RAW_FRONTEND_INPUT_INITIALIZATION.md).
- [RTOS Adjust YCC dispatcher boundary](RAW_ADJUST_YCC_RTOS_DISPATCH_BOUNDARY.md).
- [Cleanup dispatch and event-wrapper construction](RAW_CLEANUP_STATE_AND_EVENT_WRAPPERS.md).
- [Adjust YCC payload contract and Linux/RTOS control boundaries](RAW_ADJUST_YCC_CONTROL_ROUTE.md).
- [Input-descriptor forwarding and recipe-byte writers](RAW_INPUT_DESCRIPTOR_PRODUCER_AND_RECIPE_BYTE.md).
- [Recipe selection and command gates](RAW_RECIPE_SELECTION_AND_COMMAND_GATES.md).
- [Core inputs and bulk cleanup caller](RAW_CORE_INPUT_AND_BUFFER_CLEANUP.md).
- [Shell dump-enable route and callback lineage](RAW_SHELL_DUMP_ENABLE_AND_CALLBACK_LINEAGE.md).
- [Candidate identity, allocation and export](RAW_CANDIDATE_ALLOCATION_AND_EXPORT_CONTRACT.md).
- [Pause, count initialization, event creation and recipe-map selector](RAW_PAUSE_GATES_AND_RECIPE_SELECTOR.md).
- [Owner messages, same-material post path and explicit kitchen callers](RAW_OWNER_EVENT_AND_ORDER_MESSAGE.md).
- [Arena, WDMA, cache callback and address-domain limits](RAW_ARENA_DMA_AND_CACHE.md).
- [Provider/geometry correction and release mechanisms](RAW_EXECUTION_PROVENANCE_AND_LIFETIME.md).
- [Software IRQ dispatch and task identity](RAW_INTERRUPT_AND_RECIPE_EXECUTION.md), interpreted with the newer event/post corrections above.

Earlier reports contain historical unknowns that these later reports close. In particular, do not reopen the cache callback, candidate/image association, configured type2, initial event-pattern field, concrete Pause receiver or conditional event-to-Assistant material handoff merely because an older paragraph calls them unknown. Keep bounded negative searches bounded.

## Next proof obligations

1. Resolve the direct debug route's stop/drain contract and prepare a bounded first-file trial. Camera-state changes require separate authorization; static discovery is not authorization.
2. Bind actual sensor/SSIF geometry and source to one WDMA submission and fresh completion. Null RDMA words do not exclude direct SSIF hardware input. The OTF source diagnostic bits12–13 are distinct from this route's low-three-bit WDMA setting.
3. Establish same-allocation retention and cache/address coordination through copy return, including cleanup/reuse and the debug globals' stale-value hazard.
4. Validate an actual newly written file and its pixels, then reproduce acquisition. A filename, default geometry or ACK is insufficient.
5. If the debug entry is unavailable, observe the now-bound Still recipe1/key0x1b factory and its gates. Its optional dump flag must precede recipe construction. Do not reopen the static factory map or recovered shell token shifts.

The captured firmware's NX1/CS5K software binding is a compiled path inside the R210 image, not identification of the physical sensor or proof of runtime activation. A prior profile review (not published: `../../output/decompile/rea/continuation-20261015/pass9/r210-profile-selection/report.md`) records those limits. No verified sensor RAW file exists.
