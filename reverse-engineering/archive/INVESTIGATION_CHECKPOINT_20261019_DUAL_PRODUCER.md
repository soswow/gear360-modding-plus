> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Dual producer and hardware completion — 19 October 2026

Continued from `278a8e5` under the user's explicit request to work until **2% weekly allowance remains**. Product goal unfinished. Full typed relationships, exact functions/Evidence IDs, acceptance table and limits: parent review (not published: `../../output/decompile/rea/pass-dual-producer-20261019/parent-review.md`).

## New reviewed contracts

- **Descriptor → hardware addresses closed statically.** S's route-3 output R reaches CPP_Preview/Core and `start_wdma` `0x8031c098`. Later PP memory-to-memory work takes GetSource(PP,0)'s image into route 1, then CPP_Capture input 0 and CPP_Core input 0; `start_rwdma` `0x8031b9b4` programs that address as RDMA0. Original helpers write RDMA0 address/stride at `0x20800300/304` and WDMA0 address/stride at `0x20800338/33c`. These instructions were analyzed offline, never executed by us.
- **Half-width processing is metadata plus aliasing.** PP temporarily halves source word +0x0c and restores it at postprocessing. E aliases R at stride/2. Under the measured 5824×2896 / stride-11648 / two-byte-sample layout, that is a 5824-byte offset and two 2912-sample horizontal views; the last view ends exactly at the recorded 33732608-byte raster extent. Physical lens/CFA identity remains unproved.
- **Event 3 has a hardware-backed source.** Driver callback index 6 is table entry +24 at `0x8144a780`; PP IRQ dispatcher reads bit 6 from `0x20800018`, calls it, then acknowledges through `0x20800020`. It reaches the event-3 listener fanout. The two missing secondary-interface thunks subtract material +0x120 and branch to the known PP/S handlers. Parent original-byte checks close the worker's packet gaps.
- **Completion still needs association and ownership.** Fanout invokes `(listener,3,0)` without image address, request token or generation. Registration/gate bytes and current object pointers route it into Notify/Finish. This is not a fresh requested-frame lease, cache-coherence proof or safe owned result.
- **Constant-return trap found.** `0x800cb010` calls the lower DMA validator but always returns initialized -1. The lower validator's result is discarded; a library cannot use this wrapper's return to decide success/failure or retry.

The shared FrontEnd service/Core and direct calls are **RTOS-internal**, not an established Linux descriptor/retention ABI. No exhaustive claim that firmware changes are required is made.

## Evidence and resources

Evidence root `output/decompile/rea/pass-dual-producer-20261019/`: bundle 112 records, 106 native analysis records / 104 unique procedures, including 34 inherited unique procedures and 70 additional ones. All 139 body ranges match unchanged original RTOS/carrier bytes; tables and conditional half-raster arithmetic were checked mechanically. RTOS SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`; carrier `a13031c2070743a068e67de194bfc91518a7698657043f14b2f2ea6b4fb99b82`. Snapshot saved; REA session closed. No source behavior change, so unrelated host suites were not rerun.

One scoped Cursor worker, init-confirmed Grok 4.7 High, session `09164a48-1609-402b-9e90-2a534589e48f`, exit 0. Read/arithmetic scope audited; decisive claims parent-reviewed. Worker reaped; no owned resource left. No camera command, new helper/file, capture, settings change, device-memory write, injection, restart, automation, reset credit or push in this pass. Camera status remains the preceding 21:49 read-only observation, not a new live check.

Intake 300-minute 24%, weekly 93%. Final measured usage: 300-minute 52% used, weekly 98% used / 2% remaining. Local integration is recorded in CURRENT_INVESTIGATION. User's 98% weekly limit applies to this continuation only. Markdown research is committed locally; binaries, tables/numerical records, snapshots and streams stay ignored.

## Next contract

Recover the required **sensor preparation and rollback** before S's path: Nx `VideoSetSensorMode` `0x8020eda8`, sensor object at service +0x250, exposure/stop/error transitions. Join this producer to an existing Linux request/result interface with exclusive reception and retained backing, or precisely bound the missing ABI operations. No request-level target capture is justified until those contracts are reviewed.
