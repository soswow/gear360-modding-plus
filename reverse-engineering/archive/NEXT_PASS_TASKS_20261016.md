> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Next work toward an actual RAW file

Latest user instruction,16 October: any agents including Luna are allowed. Prefer Cursor and MiniMax only where they save this chat's credit and review effort. The current pass stops at95% five-hour use, leaving5%. Parent reviews and commits validated artifacts to local master, preserving unrelated work; no remote push. Earlier external-only/90% and97% instructions in historical reports are superseded for this pass.

The overarching goal remains **verified sensor RAW pixels saved to a file reproducibly**, with provenance, geometry/stride, packing, fresh completion and safe allocation lifetime. It is unfinished. The user has now authorized bounded live access and temporary fixed read-only RAM-helper execution. See [live observations](RAW_LIVE_RAM_OBSERVATIONS_20261016.md). That authorization does not cover capture, camera-memory writes, MMIO access, service restarts, preload or firmware changes. A pass milestone does not complete the RAW goal.

The user subsequently approved exactly one [three-register snapshot](IMAGING_CONFIG_OBSERVATION_20261016.md), now completed and cleaned up. No capture, register writes, expanded address set or repeated register invocation was authorized by that approval.

**Latest priority:** [first passive pixel fragment and packing handoff](INVESTIGATION_CHECKPOINT_20261016_PIXEL_SAMPLE.md). Resolve packed12-bit versus other layouts from original configurator code and actual bytes before another broad completion trace.

## Start here

Read the [finite acquisition decision plan](RAW_ACQUISITION_DECISION_PLAN.md), [stock SSIF debug route](RAW_STOCK_SSIF_DEBUG_CAPTURE.md), and [current boundary map](CURRENT_RAW_BOUNDARIES.md). They supersede corresponding unknowns in older reports.

Prefer a bounded first-file experiment over another broad mapping pass. The direct stock command candidate `st cap fenx lv rtmem lv ssif` bypasses Still recipes/YCC. It has a complete compiled parser→image→allocation→WDMA→FileDump path, but no live test or verified pixels. The current offline pass does not authorize that experiment.

## Remaining decisions

1. **Completion and input geometry:** confirm actual SSIF source/current configuration and the same P submitted to WDMA. The debug wait is SSIF VdFalling, not proved DMA completion. Its supposed arming call is a no-op; failures do not gate capture. Stop clears control bits without a demonstrated drain. Do not infer stable pixels from Vsync+stop.
2. **Ownership and copying:** the debug allocation uses tag0x35/mode3/64MiB, distinct from the Still tag0x1c. Its selected cleanup branch releases b0/b4/b8 without clearing the globals; later calls can reuse a stale address. Establish allocation validity before submission and safe ownership through copy return. Bulk cleanup is a separate release mechanism and has a compiled stop-liveview/Sleep caller. A pointer value alone is not an allocation identity.
3. **Actual file:** validate a newly created file's size/hash and actual sample behavior. Debug filenames encode stride×height and use numeric suffixes. Initialized row10 dimensions1920×960/stride3840 imply3,686,400 bytes only if those values are active. Do not use the separate OTF-to-IPC source diagnostic as the WDMA source test.
4. **Fallback:** if the debug entry is unavailable, observe the now-bound ordinary Still branch: recipe1 under its guards →static-map key27 decimal/0x1b →factory0x802b3b90 →builder0x802ba7b4 →MID0x15000/selector0x28. The optional dump flag must be set before recipe construction. The static factory binding is closed; active R210 execution is not.

If completion/retention cannot be demonstrated using stock diagnostics, prepare one bounded copy/pin design offline, then request explicit authorization for any instrumentation/deployment. Both normal release and bulk cleanup must respect the ownership interval. Do not issue repeated stock commands hoping the stale-global hazard disappears.

## Reuse rather than rediscover

- Still-book static map+4 and key0x1b factory binding are recovered. Map+0x1c is a different runtime map.
- CAttributeHandler outer Run stores inbound msg, payload=msg+8 and payload length; its0x81/high2/low0x88 bank chain reaches the four-byte YCC leaf. Linux serialization and receive normalization must use their actual header coordinates, not equal numeric constants.
- The examined C+0x280 argument path configures a separate CPP_SSIF singleton. It does not establish writers of FrontendNx input wordsC+0x64/+0x68. Null memory inputs do not exclude direct hardware input; do not repeat offset censuses without a new concrete writer lead.
- SSIF interruptID1 has a conditional software chain to VdFalling/mask2. PP WDMA-done bit6 uses a different status/register table. Cache service17/operation3 and ordinary/bulk release bodies are recovered.

Give agents one unresolved edge and exact retained paths. Use `rg --no-ignore` for ignored output artifacts. Require full records, correct body gaps, source windows and owned-session closure. Parent checks original disputed instructions and maintains corrections; agent prose is not evidence. Keep Cursor model selection explicit and inspect MiniMax's reported model/variant rather than assume a managed default stayed unchanged.
