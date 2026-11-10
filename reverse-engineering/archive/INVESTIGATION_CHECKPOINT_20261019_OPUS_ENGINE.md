> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# RAW lifetime, software triggers and the missing hold — 19 October 2026

**Later Codex review:** [reconciliation checkpoint](INVESTIGATION_CHECKPOINT_20261019_CODEX_RECONCILIATION.md). The historical report below is preserved. Its type-1/type-2 Naejae graph is not the normal 360 type-0 graph; the supposedly missing `0x10f01` class resolves after the factory's low-byte mask. Timeline samples establish changes/reuse, not direct sensor-write or DMA-completion proof. The three proposed interventions are not an exhaustive proof that our own engine requires a firmware change. Read the reconciliation before adopting those conclusions.

**Cursor Opus session, 19 October 2026, from 20:00 Australia/Sydney (UTC+11).** Branch `cursor-opus-capture-engine-20261019`, created from `master` `36e5958`. Not a Codex review. Codex has not accepted these findings. The product goal (our own callable C/C++ capture engine with owned RAW) remains unfinished.

Evidence root: `output/decompile/rea/pass-opus-engine-20261019/`. RTOS SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, VA = `0x80000000` + file offset. Linux `libcapture-fw-prod.so` SHA-256 `fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7`, Ghidra = ELF VA + `0x10000`.

## Headline

1. **On the live camera, the RAW lives in the pool for only about 80–110 ms after readout, then its blocks are reused in the same still.** A read-only `/dev/mem` observer shows the id-22 reservation (tag 28, 64 MiB) allocated at `0xA6000000`, filled by the sensor readout about 150 ms later, then released. The same blocks are re-allocated to tag 30 and tag 53 in the same 34 ms poll. Repeated on a second shutter. A Linux-only reader cannot own the RAW; something must hold it before `Put_ImageBuffer`.
2. **The stock pipeline gives Linux no RAW.** With the user setting `RAW_SAVE_ON` and a software shutter, the camera wrote a JPEG and no RAW file. This matches the RTOS RAW image-set branch that does not export.
3. **No stock, non-debug hold of the RAW is known.** The allocator is first-fit from index 0, release does not scrub, and there is no lock flag. The one stock wait during the RAW's life is the gyro request to Linux on the source material's post callbacks; it is answered by the camera app, not by us. The debug dump flag (`st cap seq dump on ssif`) is the only demonstrated hold, and the user has excluded it as the backend.
4. **A request-level trigger outside the app's state machine took the camera offline.** `st cap capt single start` calls the same RTOS submission function as `CaptureStillImage`. After one use, the camera's access point disappeared. The camera is currently unreachable; see "Camera state" below.

So the minimal missing interface is now precise: **an RTOS-side hold or copy of the id-22 image between the end of readout and PP's `Put_ImageBuffer(R)`, with a handshake our Linux code can answer.** Each of the three ways to get it needs a user decision: an RTOS patch, code inside the camera app, or accepting the debug-dump hold.

## New tooling this pass

- Whole-image RTOS carrier `reverse-engineering/make_rtos_full_carrier.py` (code at `0x80000000`, startup-copied data at `0x81006000`, BSS to `0x81e00000`) and Ghidra headless scripts `reverse-engineering/ghidra-scripts/`. Full-text decompilations, local only: RTOS (21,979 functions, `rtos/rtos-full.decomp.c`); `libcapture-fw-prod.so`, `di-camera-app`, `libmm-photo.so`, `libshell-command.so`, `libsif.so`, `libmicom-node`, `libmulticore-bridge.so`, `libudd5.so`, `libdi-alg-imagestitch.so` (`linux/`). Every function address used in prior notes is present in the RTOS dump.
- Read-only capture timeline observer `tools/probes/capture-timeline-observer/` (freestanding ARM, `/dev/mem` `PROT_READ`, guarded manager pointer, records allocator runs and pixel-write activity). ELF SHA-256 `dd017ec8f58fb70900246ef0ed8cc1b24d53edcaeebb01444064641cac22a13d`.

## Accepted findings (parent-checked against the decompile)

**Allocation, store and release of the id-22 image (closes the previous "exact next contract").** `CCallback_Still_Get_ImageBuffer::Execute` `0x80138d54` calls `0x8025d9c0(id)` and, when the result is not `0xBFFFFFFF`, stores it with `0x8025cc24(image, address)` (the 16-byte descriptor at image `+0x84`). `CCallback_Still_Put_ImageBuffer::Execute` `0x801396fc` reads image `+0x84` (`0x80541b48`), frees it through memory-manager vtable `+0x5c`, then clears the descriptor (`0x8025cc24(image, 0)`) and the request word. It skips request ids `0x14`, `0x15`, `0x2b` and the QVIEW case. In the ordinary still factory `0x802b3b90` (log tag `RecipeNaejae`), `Put_ImageBuffer(R)` with R = GetImage(S, 0) is registered on the PP material's post list. That is the only release of R in the recipe.

**Fixed Linux-supplied ("CMA") buffers.** Get_ImageBuffer gives request ids `0x14`, `0x15`, `0x2b` the fixed addresses at `0x85180294`, `0x85180298`, `0x8518029c` instead of a pool allocation. The RTOS image never writes those words, so they come from outside. Put_ImageBuffer never frees them. These ids belong to the GPU-stitch path (factory cases 0x24/0x25/0x2b/0x2c/0x2d), which runs only in OSC mode. This shows that the RTOS can already write into memory that Linux owns. No recipe sends the RAW there.

**A9 processing handoff.** The RTOS publishes topic `0x3010008` (a 0x3c8-byte set of up to eight 0x88-byte image descriptors, with the type at `+0x3c0`) through a 25-entry shared ring and MCB command `0x20b`. Linux `captureProcessA9` (`0xaef7c`, callback client `fd_alg`) dispatches type 0 BeautyFace, 1 GPU stitch, 2 HDR, 3 gyro, 4 SW-LSC (Bayer). Linux replies with MCB command `0x210` and a 4-byte event id (`BeautyCaptureProcessEvent`). RTOS `0x800630fc` → `0x80111e9c` sets the waiting event bit. Every RTOS wait is infinite. Reachability: gyro on every ordinary still; GPU stitch only in OSC mode (key 0x20); HDR only in key 6; BeautyFace and SW-LSC builders have no callers. Parent-checked: the gyro callback `0x80139df8` publishes type 3 and waits on `0x81483518` with timeout `0xffffffff`; SW-LSC `0x80261860` publishes type 4 and waits on its interface event. Worker report: `workers/a9-handoff/report.md`.

**Ordinary still recipe map.** S (`0x802ba7b4`, MID 0x15000, case 40, id 22) → optional E alias (type 1) → PP (MID 0x10e00/0x10f01, id 0x17/0x18; post: Put(R)) → MakerNote → IPC → LDC → 3D LUT (id 0x16, no Get) → Resize (main id 0x16 with Get) → JPEG. No copy of R into another buffer. Later images reuse request id 22, which explains the extra tag-28 runs seen live. Worker report: `workers/recipe-map/report.md`.

**Sequencer and unit-test commands (static, not executed).** `st cap seq order <id> <param>` posts straight to the kitchen (`0x80138a40`) and bypasses the controller and CDriveSingle (no liveview stop, preflash or sensor preparation). Param is a one-shot recipe-key override. `order 255 51` selects a debug recipe with one PP RAW image and Get/Put, so nothing is kept. `demand` only stops or advances cooks. `ut still` runs real captures through the controller with request `0x01010016`. Numeric arguments are decimal only. Worker report: `workers/seq-debug/report.md`.

**Allocator policy (partial worker report, consistent with the live timeline).** Forward span search starts at a fixed index 0 (`0xA6000000`); `+0x680bc` is written but not used as the search start; release clears only the record table; no lock flag was found. Live: `manager+0x680b4` holds the block count `0x6800`, not a byte size. That worker was blocked before it finished; `workers/pool-reuse/report.md` is incomplete and unreviewed.

## Camera observations (this boot)

| Record | Result |
| --- | --- |
| `camera/rawsave/RESULT.md` | `RAW_SAVE_ON` + `st key click s2`: JPEG `360_0115.JPG`, no RAW. Restored to OFF and read back. |
| `camera/probe1/` | Manager `0x81cbb000`, vtable `0x8062b790`, pool base `0xA6000000`, 26,624 blocks; dump flags zero. |
| `camera/timeline1/RESULT.md` | Two software shutters: RAW reservation at `0xA6000000`, readout, release, immediate reuse, as above. |
| `camera/timeline3-capt/INCIDENT.md` | One `st cap capt single start`; camera unreachable afterwards. |

## Camera state

Unreachable since about 2026-10-19T10:00Z. Recovery needs the user: check or power-cycle the camera and reconnect the Mac to its access point. The observer directory `/tmp/gear360-timeline-aa5758d8c3` may remain on the camera. `RAW_SAVE` was restored to OFF before the incident. Do not repeat `st cap capt single start`.

## Callback order (checked after the first draft)

`Material::PostProcess` (`0x80250880`) runs the post-callback list through `0x80324fe0` before it returns. Worker report `workers/callback-order/report.md`, parent-checked at that call.

For capture type 2 (S linked directly to PP), the cook starts PP only after S's `PostProcess` returns. S's post callbacks, including the gyro wait `0x80139df8`, therefore run before PP starts and before `Put(R)`. A blocked gyro reply holds the reservation. The running app answers that reply inside `captureProcessA9`, so a second process cannot stretch the hold without replacing that client.

For capture type 1 (S → E → PP), E and then PP can start from S's Notify, before S's post callbacks return. `Put(R)` can then race the gyro wait. The class for PP's type-1 material id `0x10f01` was not found.

`0x802b3b90` (`RecipeNaejae`) rejects capture type 0. The same `Put(R)` shape also appears in `RecipeNormal` `0x802aafbc` (parent-checked: it builds S with `0x802ba7b4` and registers `0x8057498c` on the following material). Which recipe a dual 360 still selects is not established. The live tag-28 reservation does not by itself identify the recipe.

## Shell transport (worker, not re-disassembled here)

`workers/shell-transport/report.md`: `/usr/bin/st` runs `key` and `cap` in its own process. `st key click s2` is an XTest press of keycode 134 (`Super_R`), which the app's UI maps to S2. `st cap …` sends MCB command `0xb6` text and reads the reply from the ring at `0x85300000`. Only `log`, `util`, `app`, `leak` and `pmic` use the SysV queue `0x8828`. Treat the field offsets as preliminary until checked against the `st` binary.

## Not established

- Which still-capture type and recipe key the R210 dual still uses. That chooses whether the gyro wait holds R (type 2) or races it (type 1).
- Whether the RAW buffer is modified in place before release (the second run shows a rewrite of the first ~16 MB while the reservation is still held).
- What the later id-22 images hold (YCC working buffers are likely; second lens or sensor data not excluded).
- The record word2/word3 meanings.

## Next decision (user)

The engine needs one hold mechanism. Options, each outside current authorization:

1. A minimal RTOS patch: at S post (or before PP's `Put(R)`), publish R's descriptor and wait for a Linux acknowledgement, reusing the existing `0x3010008`/`0x210` handshake with a new type.
2. Code inside the camera app's process that answers a request the RTOS already makes while R is alive (the gyro handshake), delaying its reply until our copy finishes. This needs preload or injection.
3. Accept the stock debug-dump hold (`st cap seq dump on ssif`) as the transport. The user excluded this as the backend.

Without one of these, the engine can trigger and observe a still but cannot own its RAW.
