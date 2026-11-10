> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Investigation checkpoint — 18 October 18:30 kickoff

Reviewed 2026-10-18T19:40:34.634577+11:00. The exact persistent goal was created in this session and remains **active and unfinished**. No verified sensor RAW acquisition is complete.

## Result and acquisition implication

Read [the reviewed still sequence](RAW_STILL_ACQUISITION_SEQUENCE_20261018.md). This is the latest acquisition-focused result and supersedes the earlier preliminary still-first-edge queue.

- Concrete Linux control boundary: app-imported `mm_camera_capture_still` constructs48 bytes; `CaptureStillImage` forwards normal code01010001 through MCB83/201. RTOS method banks select800626f4, which validates48 and forwards the first payload word to8004f1ac. The copied RW bank uses source807f9148→startup alias8100f848; source addresses are not live-table addresses.
- Conditional still interface/output chain: I+4→W+0→C; W+7 comes from tag0 configuration. CapSetInfo passes a configured image into Q+10; CapSetPPCore forwards it to Core+10; frame readout/readwrite reach named DMA-start callers. Descriptor, backing and frame identity remain distinct. No active R210 still buffer was observed.
- Rejected acquisition shortcut: full80213710 body is `CheckPPInterruptStatus`, status/order checking without image publication, export or retained backing. Bit8 and C+291 clearing do not establish a fresh leased RAW frame. Stop expanding that checker.
- Request-to-drive queue, actual recipe/material activation and completion-listener-to-image ownership remain gaps. Existing recipe1/key27 candidate and release-before-RAW-list limitations remain conditional and authoritative. No guessed capture client was implemented.

## Why this pass stops

The documented camera endpoint failed two bounded read-only HTTPHEAD probes; the second exact request/result/timestamps are in `camera-connectivity.json`. The decisive next edge is runtime: bind an actual R210 still request to its selected output, producer and completion. Idle preview metadata and more shared-library class cataloguing cannot replace that observation. This is a camera availability block, **not** resource exhaustion, goal completion, or newly granted camera-state-changing authority.

Next: reconnect camera, run existing read-only identity/process/mapping/helper guards, inspect fixed singleton cells and verified I/W/C/Q/Core selected-output metadata, then arrange a single observed ordinary still event under the existing camera boundaries. A user-operated shutter plus read-only diagnostics is distinct from agent-issued capture/instrumentation. Prepare any separately authorized RTOS instrumentation/export change only after the active route is bound. See report for exact fields and hook boundaries.

## Evidence, review and cleanup

Output: `output/decompile/rea/pass-kickoff-20261018-1830/`. Native bundles, closed snapshots, source windows, manifest validation, original Linux instruction checks, frozen packets, complete external streams/model records, failures and C6 tables are saved. Fourteen carriers /80 source-identical segments validated; principal report EIDs resolve to original retained records. These checks do not certify activation, sensor source, packing or lifetime.

Cursor Grok4.7 High client routing was confirmed. First transport attempt failed before analysis; same-session completion pass succeeded49 calls, consumer follow-up succeeded10 calls. Parent reviewed decisive original instructions, the missing tail and getter meanings. Luna mode/message extraction was reviewed; initial formatting-only checker mismatch was corrected to numeric comparison. No hidden-backend or exact token-savings claim.

Owned external PIDs3117,6789,12005 are absent, recorded exit statuses1/0/0. Luna is done. All parent native REA sessions closed. No camera helper/upload, temporary camera file, capture, MMIO/firmware write, injection, restart, persistent settings change or remote action occurred. Historical kernel amendment stays unreviewed/secondary. No tracked source edit; no commit or merge needed. Local master remains2874ae9c01099ced1bbf798c0c5af9cb9c18e7c6, clean.

Archive: `reverse-engineering/evidence/pass-kickoff-20261018-1830-reviewed.tar.gz`, per-member hash index `.index.json`. Notes/evidence remain ignored; no force-add or push. Original working artifacts are preserved.

## Usage

Kickoff five-hour1%used/99%remaining; last44%used/56%remaining. Five-hour reset 2026-10-18T23:30:31+11:00. Weekly22%used. No purchased/reset credits consumed. The5% reserve was not reached; stopping is for the first required live acquisition edge. Goal remains active/unfinished; first resumed-goal camera-block audit in this turn only, not the three-turn threshold for setting goal blocked.

Final archive/usage reconciliation 2026-10-18T19:41:31.704097+11:00: archive verified146members, SHA256`79157b6d2bb03e191a55ddc481edc24b3d017239bb21b962589b6a4a06df329d`; frozen checkpoint contains44%pre-archiveusage. Final account reading **46%five-hourused/54%remaining, weekly23%**; reset23:30:31Sydney. Post-archive record: `reverse-engineering/evidence/pass-kickoff-20261018-1830-final-credit.json`. Goal active/unfinished. All owned workers/native sessions closed; no camera files deployed.

Camera availability audit2 — 2026-10-18T19:47:40.296076+11:00: specific TCP connects to documented192.168.43.1 ports80/21 both timed out. Prior goal turn classifiedprogress; current check confirms same live-access blocker (2 consecutive goal turns, below3-turnblocked threshold). Goalactive/unfinished; no camera commands/deployment, workers or native sessions. Latest48%five-hourused/52%remaining, weekly23%. Records: `output/decompile/rea/pass-kickoff-20261018-1830/camera-access-audit-turn2.json` and`block-audit-turn2.json`; post-archive supplements, original archive unchanged. Next authoritative action needs camera access restored for guarded R210 still-path binding.

## Blocked goal — 2026-10-18T19:53:28.868810+11:00

The goal tool returned **blocked** after the same camera-access condition repeated across3 consecutive goal turns. Latest specific TCPconnects to192.168.43.1 ports80/21 both timed out; see`camera-access-audit-turn3.json` and`block-audit-turn3.json` in the kickoff pass output. Previous turn classified no acquisition progress with a revalidated blocker; no live process/session was being waited on. The first required current R210 still-image/completion binding needs camera access. No verified RAW acquisition is complete. Resume after camera connectivity is restored and the user resumes this goal. Latest five-hour51%used/49%remaining, weekly23%; reset23:30:31Sydney. No credit/reset consumed, new automation, source change, camera command/upload or publication. Evidence/archive preserved; all owned workers/native sessions are closed. These audit files are post-archive supplements.
