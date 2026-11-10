> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Codex reconciliation of Grok/Opus — 19 October 2026

Continuation after `74431cf` on `cursor-opus-capture-engine-20261019`. The capture-library goal remains **unfinished**. Full C0–C6 acceptance record and native Evidence IDs: parent-review.md (not published: `../../output/decompile/rea/pass-codex-reconcile-20261019/parent-review.md`).

## Reviewed advances

1. **360 mode maps to capture type 0.** `0x80138188` maps userdata `0x810000` to sheet byte `+0x229 = 0`; right/left 180 modes map to 2/1. The current camera reports 360 mode. Naejae `0x802b3b90` rejects 0, so its type-1/type-2 graph does not establish the dual-still path. Recipe keys 0/4 dispatch to `0x802a9570`; 0x1b selects Naejae. The live recipe key still depends on unread sheet fields/overrides.
2. **The conditional normal dual graph is recovered.** Normal key 0, no alignment override, sheet `+0x218 == 0`, `+0x64 == 0`, `+0x170 == 0`: `0x802ab858` builds S → PP-A → E → PP-B, with additional PP-A dependencies on E/PP-B. **Put(R) is on PP-B post.** Two processed branches join through stitch, resize and JPEG. Under a normally started invocation, S's synchronous gyro wait gates downstream start/release. The app owns the reply; this is not a caller-owned hold or a DMA/cache proof.
3. **Missing class resolved.** Factory `0x80290e54` masks off the low byte at `0x80290e74`. MID `0x10f01` therefore selects constructor `0x802ee880`; type-0 `0x10200/0x10201` share `0x80293138` / vtable `0x80684fb0`. The old equality-only reading missed the mask.
4. **MainRaw is a Conti object-recycling ring.** Grok corrected the singleton identity. The checked IPC-end path frees/clears backing before enqueue; parent follow-up found a GetRaw consumer outside the packet that attaches the object to another material. Initializers use image request ids 23/36, not SSIF id 22. This is not a demonstrated completed sensor-RAW lease.
5. **Timeline claims are narrowed.** The retained logs show pool/sample changes and reuse. Observed allocation-identity durations replay as 265600/311600 microseconds. Sparse unsynchronized samples do not directly prove the sensor writer, exact readout completion, an 80–110 ms RAW-valid window or unmodified dual-lens content. The proposed patch/injection/debug options are not an exhaustive proof that our own engine requires firmware changes.

## Source and validation

Observer runner rejects `--trigger capt` before output-directory creation or network operations; its request command branch is removed. Capture-engine and still-request host suites/C++ linkage pass; ARMv7 objects compile. Both retained timeline logs replay; observer rebuild matches Opus's `dd017ec8…a13d` executable hash. No new executable ran on the camera. Documentation corrects mandatory-patch/one-missing-part claims and PROJECT_GOAL's stale ignored-Markdown sentence. The integrated initialization/configuration/submission/completion/ownership/release backend and optional PNG remain unfinished.

REA used original RTOS SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, carrier `a13031c2070743a068e67de194bfc91518a7698657043f14b2f2ea6b4fb99b82`. All 52 body ranges from 34 native functions match original bytes. The 35-record bundle, native index and snapshot are in `output/decompile/rea/pass-codex-reconcile-20261019/`, ignored. The large converter exceeded MCP transport size; its retained export/assembly view was used. High-pcode truncation remains explicit.

## Camera, resources and cleanup

Camera **reachable again at 21:49 Sydney**, checked after the user's reconnection message. App hash matches the extracted artifact; short uptime indicates a fresh boot. It reports 360 mode and RAW_SAVE/SSIF_RAW/interval/OSC OFF. The old incident directory is absent. No settings/files to revert from this pass; no capture, memory helper, patch, injection or restart executed. Current manager mapping remains unchecked. Read-only result (not published: `../../output/decompile/rea/pass-codex-reconcile-20261019/camera-reconnect/RESULT.md`).

One Cursor worker: requested `grok-4.7-high`, init confirms `Grok 4.7 256K High`, session `18e9077a-1bb8-480e-b01a-2672079ebd48`, exit 0. Scoped read audit/input hashes/stream preserved. Decisive claims parent-reviewed; no worker remains. REA exported/saved/closed. No persistent goal exposed; product objective retained without creating a duplicate. No automation or reset credit used.

Intake usage: 300-minute 1%, weekly 90%; wind-down 300-minute 15%, weekly 92%. Weekly headroom is the binding reserve. Resets: 20 October 02:36:57 Sydney (300-minute), 25 October 13:24:45 Sydney (weekly). Final usage and local integration are in the floating index. Source/Markdown committed locally; binaries, numerical records, buffers, images and agent streams stay ignored. Never push; preserve the pre-push guard.

## Exact next contract

Trace type-0 PP constructor **`0x80293138` / vtable `0x80684fb0`** through source-address programming and driver completion. Join its source image to the S descriptor and SSIF finish `0x802f3928`; distinguish Notify/post callbacks from completed DMA/cache visibility. Identify the concrete existing Linux-callable control edge, or precisely document the RTOS-internal boundary still missing. Recover this before selecting a retention implementation; do not substitute a Conti ring, debug dump or stock-process hook for the product backend.
