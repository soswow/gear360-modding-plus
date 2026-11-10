> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Investigation checkpoint — pass 11, 15 October 2026

Offline continuation after pass 9. No camera command, firmware execution, service restart, preload, or deployment. **No verified sensor RAW pixels have been written to a file.**

## What changed

Material virtual +0x18 (`0x806a1f88` → `0x802e5c74`) builds a 16-byte record from material fields and calls `CFrontEndInterface::StartVideoStream` (`0x8031e630` → `0x8031feac`). The record’s first two words are constructor values 0 and 60. A log line saying “Video frame started” is emitted before that call. The waits around the call use the existing semaphore helpers on interface +0x10 and +0x0c, which the constructor creates with count 1. This is not a WDMA completion wait and not a pixel-buffer identity.

BranchManager virtual +8 is `0x8027b888` on vptr `0x8067ee58` (`CBranchManager_NX1`). It selects Still, Liveview, or QView recipe-book virtual +8 from the top byte of CCook’s recipe id. It does not call MID builder `0x802ba7b4`. The active recipe id is still unknown.

An independent byte review agreed with all 15 instruction claims. REA CLI 6.4.0 produced 13 function dossiers. No `analyzeHeadless` process remained after those one-shot invocations.

## Evidence

- Findings: `reverse-engineering/RAW_COMPLETION_IDENTITY_AND_RECIPE_DISPATCH.md`
- Carrier and analyses: `output/decompile/rea/continuation-20261015/pass11/completion-and-branch/`
- Source windows: `completion-and-branch/source-byte-windows.json`
- Evidence index: `completion-and-branch/evidence-index.json`
- Snapshot: `completion-and-branch/session.snapshot.json` (reusable state; filename is not a close certificate)
- Byte review: `pass11/independent-byte-review/report.md`
- Session audit: `pass11/session-cleanup-audit.json`
- Earlier `pass10/rdma-vtable-route/` was left untouched.

## Historical next actions

These were the initial pass11 priorities. Later records resolve the semaphore posters and the specific selector0x28/request22/callback identity; use the current ledger and focused report for remaining questions.

1. Read the active value of CCook +0x70 and follow Still book `0x80280570` through the indirect call at `0x80280780`.
2. Find posters of the frontend semaphore at interface +0x10.
3. Identify the sensor/SSIF receive destination for the candidate allocation.
4. Show an owner that keeps that allocation alive through an export copy.
5. Trace external delivery of the dump-enable command record.

Sensor receive, packing, fresh pixels, and safe lifetime remain open.
