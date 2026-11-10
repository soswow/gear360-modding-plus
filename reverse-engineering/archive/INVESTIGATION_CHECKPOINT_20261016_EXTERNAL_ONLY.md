> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Reviewed external-only continuation, 16 October 2026

The overarching goal remains **verified sensor RAW pixels written to a file**, with reproducible acquisition, provenance, geometry/packing, fresh completion and safe backing lifetime. It is unfinished. All work in this pass was offline; no camera/firmware execution, service restart, preload, deployment or remote push occurred.

The parent initially misread the provider instruction and used Luna alongside external workers. Those internal workers were stopped when the user clarified **external subagents exclusively**. Subsequent delegation used only the existing Cursor **Grok 4.6 Medium** and MiniMax **M3.1-Flash-Preview High** sessions. This preference supersedes older automation text. Parent reviewed evidence and owns local master integration.

## New reusable evidence

- [Control contract](RAW_ADJUST_YCC_CONTROL_ROUTE.md): Linux bridge Send arguments81/0288/length4; separately, the RTOS logical dispatcher01090006 requires four bytes. These are not yet a serialized transport proof.
- [RTOS lookup](RAW_ADJUST_YCC_RTOS_DISPATCH_BOUNDARY.md): helper801300a8 reads the low byte of a halfword at *(this+c)+4;88 maps to20. Native8012a084 uses bank81011ba4, selecting runtime81011c44/source807fb544 →8012e0e8 →logical01090006. Outer handler, this+c supplier and payload setup remain unknown. The parent’s guessed base81010800 was not promoted; actual source there contains strings.
- [Frontend input initialization](RAW_FRONTEND_INPUT_INITIALIZATION.md): vptr806718a0 slot50 is CapSetPPCore80210a88. DescriptorC+50 forwards wordsC+64/C+68 to Core inputs under known guards. Base constructor→descriptor initializer→complete fill helper proves local zero initialization; later runtime writers remain unknown. The nearby selector is updateWaveDetectionWindow, not established input-mode programming.
- [Cleanup/event wrappers](RAW_CLEANUP_STATE_AND_EVENT_WRAPPERS.md): stop-liveview can dispatch Sleep virtual+44 when current state index1 selects it. Constructor801022f4 differs from destructor80102350. Standalone event wrapper81952fc4 is allocated separately and has a mask0f creation call, distinct from frontend inline+298/mask3f. Its mask8 waiter is not proved on submission.

[Provider review](MULTI_PROVIDER_REVIEW_20261016_EXTERNAL_ONLY.md) preserves substantial rejected claims, corrections and selection details. Descriptor slots80393f5c/80393fdc form destructor/deleting destructor; do not query slot4 as a promising input setter. Handwritten offset census counts were wrong and withdrawn. Use official decoded assembly and exact receiver flow.

## Resume priorities

1. Follow the actual outer handler/this+c assignment into8012a084 and join bridge serialization to the received halfword and four-byte payload. Reuse the recovered lookup rather than repeat it.
2. Find runtime producers of descriptorC+64/C+68, and the actual sensor/SSIF input route into the candidate output. Local zero initialization is not proof of missing hardware input.
3. Bind active R210 invocation/recipe/parameters/callback, hardware completion producer and frame identity, packing, cache/address coordination and backing retention through export. No pixel claim without verified bytes.

## Saved state

Local master already contains c859933 and6d0e5ed; this handoff and final reviewed artifacts are committed together afterward. The evidence indexes bind three compressed source-bound archives, full records, snapshots, worker traces and parent audits. The final native range audit checks19 external records/29 provider ranges with zero errors; this verifies source/carrier mapping, not semantic or runtime truth. Native schema/window validators pass for each completed group. All owned workers/one-shot engines are closed; REA MCP reports open=false; unrelated shared MCP servers and untracked captures/projects are preserved.

Winddown began at88% five-hour use, aiming to preserve the requested approximately10% reserve. The final account reading is reported in chat. Checkpoints and commits are milestones, not completion of the RAW goal.
