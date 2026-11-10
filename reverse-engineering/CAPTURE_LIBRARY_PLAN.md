# Capture library plan: capture a still and save it

**Written 4 November 2026** after the user's direction (3–4 November):

- **First, a library** whose job is to capture an image and save it, running in
  our own process with the stock app absent.
- **Later, a replacement for the stock app** built on that library. It will
  handle button presses, screen drawing, sounds and LED control. That app is
  not part of this plan.

This plan continues ACTIVE_PLAN.md (not published: `ACTIVE_PLAN.md`) after step 4 (done 3
November, step 4y: our client's still survives). It covers the open items
listed there ("read the image data and return the image buffers like the
stock app, and take a second still") and leads into step 5 (RAW). The goal is
unchanged: PROJECT_GOAL.md (not published: `../PROJECT_GOAL.md`), Phase 2.

## What is known (static, 4 November)

From pass-stock-release-20261104 (not published: `../output/decompile/rea/pass-stock-release-20261104/RESULT.md`):

- **The stock still's images are read and returned by `libmm-photo.so`,**
  not by the stock app's own code.
- **Its image-set callback** receives `(topic, stCAPTURE_IMG_SET *set, …)`.
  `set` word 0 points to the 0x228-byte basic set (word 0 = set type);
  `set + 0x2e0` holds the maker-note buffer address.
- **The callback copies only small structures** (descriptors, EXIF data,
  maker note) and returns. A worker thread saves the file afterwards. **The
  image buffers stay valid until they are released.**
- **Each JPEG buffer is complete:** `virtual` at descriptor `+0x54`, exact
  byte count at `+0x74`, `FF D8` … `FF D9` (the stock code checks this).

  | Image | Virtual address | Size |
  |---|---|---|
  | main | basic set `+0xe4` | `+0x104` |
  | screen | `+0x16c` | `+0x18c` |
  | thumbnail | `+0x1f4` | `+0x214` |
  | first (RAW, "Raw Compress") | `+0x5c` | `+0x7c` |

- **After saving, the buffers are returned** with
  `ReleaseCaptureBuffer(type, virtual address)` in this order: maker note
  (5), RAW (1, only when present), main (7), screen (8), thumbnail (9).
  The recorded stock still returned 5, 7, 8, 9.

## Library rules

Every rule comes from something that went wrong or from the stock code.

1. **Never fault.** Any segmentation fault in our process makes the kernel
   reboot the whole camera (step 4v). Before reading any address the
   library did not allocate, check that the range lies in one readable
   mapping of the process (`/proc/self/maps`). On a failed check, skip the
   copy and log it; do not touch the memory.
2. **Keep the callback short.** The image-set callback runs on the capture
   library's receiver thread (`MCB_Receiver`), which must keep answering
   the RTOS. Copy only the small structures there (the 0x228 basic set and
   the maker-note word), then signal the main thread. Copy the image bytes
   on the main thread, as the stock worker does.
3. **Return every buffer exactly once, after copying,** in the stock order
   and with the stock types. Return them even if a copy or a file write
   failed, so the RTOS gets its memory back. Never return address 0.
4. **Gates stay in front:** the gyro gate (libdevice-node linked) and the
   APEX gate (private user-spec copy) run before any capture command.
5. **No shared-memory writes** beyond what the capture library itself does.

## Steps

| Step | What | Camera? | Status |
|---|---|---|---|
| 4z-a | Decode the image set in the callback, copy and save the main, screen and thumbnail JPEGs on the caller's thread, check SOI/EOI, give the buffers back in stock order | trial (user present) | **done 4 Nov** (4z-a result (not published: `../output/decompile/rea/pass-step4z1/RESULT.md`)): three JPEGs saved and decoded; give-backs identical to the stock still's |
| 4z-b | Several stills in one session (the next still only after capture-ready and our give-backs) | trial (user present) | **done 4 Nov** (4z-b result (not published: `../output/decompile/rea/pass-step4z2/RESULT.md`)): two stills, six JPEGs, eight give-backs; the RTOS reused the buffers |
| 4z-c | Library API (`g360cap`): open, capture, save, free, close; a command-line program on top; the probe's gates and sequence moved into it | with 4z-a/b | **done 4 Nov** (host) |
| 5 | RAW: set a RAW image quality (ACTIVE_PLAN step 5); when the set type is 2 or 3, copy the first image (SRW) as well and give it back with type 1 | trial (user present) | **5a–5d done 4 Nov, no RAW.** The normal single-still recipe has no RAW-compression stage, and (bracket trace (not published: `../output/decompile/rea/pass-bracket-command-20261104/RESULT.md`)) the bracket command has an empty handler on this firmware, so the bracket route is closed. Remaining then: the long-exposure (LTNR) recipe, since closed by E3 (row E). Debug commands are out (row E) |
| R | RAW: copy each still's RAW frame from the capture memory (written by the RTOS to `0xa6000000` on every still, not handed over) | trial (user present) | **code done 4 Nov** (`g360-shot --raw`, checks: mapped range, source re-read after the copy, 12-bit range). **R1 done 4 Nov, no RAW** (result (not published: `../output/decompile/rea/pass-rawR1/RESULT.md`)): by the time the image set arrives, the still's own stitch step has written the finished YUV picture over `0xa6000000`; only the last 15 RAW rows survive. The checks caught it. **Traced 4 Nov** (RAW keep trace (not published: `../output/decompile/rea/pass-raw-keep-trace-20261104/RESULT.md`)): the full frame's blocks are reused by the stitch (shared first-fit pool), but the two lens images (`0xab134000`, `0xad134000`) are corrected RAW per lens and only the restarted live view overwrites them; quick view HOLDZOOM (libcapture attribute 30 = `0x1e0005`) should stop that restart. **Q1 done 4 Nov** (result (not published: `../output/decompile/rea/pass-rawQ1/RESULT.md`)): live view stayed off; **lens 2's corrected RAW arrived intact** (GRBG, 16-bit, dark level about 520, matches the JPEG); lens 1's memory was reused by the same still's main JPEG and a stitched picture; no capture-ready in this mode. **Q2 done 4 Nov** (result (not published: `../output/decompile/rea/pass-rawQ2/RESULT.md`)): `0xa6000000` is the stitched picture in hold mode too; lens 2 intact again; lens 1 is overwritten in every mode by a temporary full-size picture that fits exactly between the two lens images. **Traced 4 Nov** (temporary picture (not published: `../output/decompile/rea/pass-temp-picture-trace-20261104/RESULT.md`)): it is the Resize stage's main image (the JPEG input), placed first-fit after both lens images are released by development; no normal-still setting keeps lens 1. Next: two stills per session in hold mode (needed for one-lens RAW use and for testing buffer-hold timing) |
| R2 | RAW: copy the full frame (both lenses) in the expansion callback, while the RTOS waits for the gyro reply (pause trace (not published: `../output/decompile/rea/pass-expansion-pause-20261104/RESULT.md`)) | trial F1 (user present) | **code done 4 Nov**: `g360-shot --frame`; our client subscribes to `0x04010008` before the Beauty object (a new start-up order); the frame's address comes from `/proc/self/maps` (`g360_maps_physical`); the copy happens in the callback, re-read after it, then the callback returns so the gyro reply goes out; the still fails with `G360_E_IMAGE` unless the frame was copied unchanged; host-tested, cross-compiled (sha 0a74f670) (code pass (not published: `../output/decompile/rea/pass-frame-copy-code-20261104/RESULT.md`)). **Trial F1 passed 4 Nov** (result (not published: `../output/decompile/rea/pass-frameF1/RESULT.md`)): the camera did not fault, the still completed, and the frame was copied in the expansion callback (651 ms) with both lenses intact. **Trial F2 passed 4 Nov** (result (not published: `../output/decompile/rea/pass-frameF2/RESULT.md`)): two stills in one session, both frames copied unchanged (holds 689 and 620 ms), capture-ready and give-backs as usual |
| E | Exposure control: manual shutter and ISO from our process (also the route to the long-exposure RAW recipe) | trials (user present) | **code done 4 Nov** (exposure control (not published: `../output/decompile/rea/pass-exposure-control-20261104/RESULT.md`)): `g360_set_manual_exposure`, `g360-shot --exposure TV,ISO[:…]`, `--ltnr`, `--isonr`; host-tested; **E1 passed 4 Nov** (result (not published: `../output/decompile/rea/pass-expoE1/RESULT.md`)): exactly 1/1024, 1/64, 1/4 s at ISO 100. **E2** (result (not published: `../output/decompile/rea/pass-expoE2/RESULT.md`)): 5 s at RAW quality selects the long-exposure recipe and the RTOS accepts 4.97 s, but **E3** (result (not published: `../output/decompile/rea/pass-expoE3/RESULT.md`)) confirms the frame never arrives: long exposures use a dedicated capture readout that stalls, unlike normal stills read from the running stream. The long-exposure RAW route is blocked at 5 s. **Debug routes (sequencer/front-end test commands) are out: the user wants RAW natively** |
| later | A stock-like file around the main JPEG. Two stock files (`output/360_2110.JPG`, `360_2114.JPG`, 5792×2896) are: APP1 Exif (43–50 KB, with the maker note and a thumbnail), APP2 MPF (a second image appended), the main scan, and a Samsung `SEFH…SEFT` trailer holding only `Image_UTC_Data` (a millisecond timestamp). | — | — |
| later | Exposure: ISO and shutter. The SD-card mod sets ISO with the camera's shell (`st cap capdtm setusr 5 0x050001` plus dial mode 0 and shooting mode 1 values); the library's own routes are `mm_camera_set_usr_attributes` id 4 (ISO, wire `0x1104`) and `mm_camera_set_var_attributes` `0x101` (shutter, wire `0x1302`) — facts `settings.iso-shutter-wires`, `settings.sensor-effect` (open) | trials | — |
| later | Liveview frames for a screen preview (for the app) | — | — |

The library replaces the diagnostic probe for these steps. The probe
(`one-still-probe.c`) stays as the record of steps 4–4y.

### 4z-a trial (prepared, needs the user present)

- **Run:**
  `python3 tools/observe/run_still_trial.py run --tag step4z1 --probe output/g360cap-build/g360-shot --g360 1 --net only`
- **Pass criteria:**
  1. the probe logs the decoded set (type 1; main, screen and thumbnail
     sizes nonzero, each address inside a readable mapping);
  2. three JPEG files appear on the SD card, each starting with `FF D8` and
     ending with `FF D9`, and the main one decodes on the Mac;
  3. the ring-B record shows four `0x83/0x020c` frames with types 5, 7, 8, 9
     (addresses converted by the library to physical, like the stock
     still's);
  4. no kernel fault; the camera answers with the same boot ID.
- **If a release is refused or the RTOS logs an error,** keep the files and
  record the RTOS log lines; do not retry.

### 4z-b trial

Two stills, the second one sent only after the first still's
`STL_CAPTURE_READY` and our four returns (`--g360 2`, tag `step4z2`). Pass:
two sets of files, eight `0x020c` frames, no fault. Run only after 4z-a
passes.

### Trial F1 (the full frame copied in the expansion callback) — done 4 Nov, passed

Later source and buffer review (not published: `../output/decompile/rea/pass-frame-review-20261104/RESULT.md`)
corrects the correlation calculation and sampled minimum. The stored words
have six low bits clear; the sensor's ADC precision and calibrated black
level remain unmeasured. GRBG remains a visual interpretation.

- **Run:** `python3 tools/observe/run_still_trial.py run --tag frameF1 --probe output/g360cap-build/g360-shot --g360 1 --g360-frame --net only`
  (the frame's layout is the default `0xa6000000,5824,2896,11648,16`; use
  `--g360-frame-layout PHYS,WIDTH,HEIGHT,STRIDE,BITS` for a smaller region
  while checking the route)
- **Why:** the pause trace (not published: `../output/decompile/rea/pass-expansion-pause-20261104/RESULT.md`)
  found that the RTOS waits for our process's gyro reply with the full frame
  still intact, and that our client can run before the reply. g360cap copies
  the frame in that window (`--frame`).
- **Pass criteria** (from the trace):
  1. the RTOS log shows the reply about 1 s later and the still completes;
  2. the frame did not change during the copy (`changed_during_copy: no`);
  3. each half matches its half of the JPEG (as RAW);
  4. a Bayer pattern (GRBG).
- **Risks:** another RTOS watchdog may not tolerate a pause of about 1 s —
  the trial shows it; registering before the Beauty object is a new order.
- **If the camera reboots,** keep the files, record the RTOS log lines, and
  do not retry blind.
- **Result:** all four criteria met. The RTOS waited 682 ms for the reply
  (our copy took 651 ms), the lens passes started 1.6 ms after it, the still
  completed and the camera did not fault. The frame is the sensor's readout
  — the very buffer the PP core then reads — with both lenses side by side,
  a GRBG Bayer mosaic, 10-bit values in the top of the 16-bit words.
  (result (not published: `../output/decompile/rea/pass-frameF1/RESULT.md`))

### Trial F2 (two full-frame stills in one session) — passed 4 November

Result (not published: `../output/decompile/rea/pass-frameF2/RESULT.md`): run from `master`
after a power cycle, user present; both stills complete, each with an
unchanged full frame. The preparation as recorded:

Codex continues MiniMax `4eb99135` on
`codex/frame-pause-continuation-20261104`; master remains untouched.
Review and build (not published: `../output/decompile/rea/pass-frame-review-20261104/RESULT.md`).
The new build rejects overflowing layouts, validates the orientation
expansion descriptor and rechecks the physical mapping at copy time.

```sh
python3 tools/observe/run_still_trial.py run --tag frameF2 \
  --probe output/g360cap-build/g360-shot --g360 2 --g360-frame --net only
```

The user explicitly permits this F2 test unattended (4 November follow-up).
Keep the standard identity/hash preflight, RTOS log and IPCC recorder.
**Preflight stopped:** the camera is on F1's boot with the stock app still
inactive, so the prepared active-stock-session guard fails. No still was
sent; a physical power cycle is needed to establish that validated starting
state. This is not evidence that process reentry can never work.
Result (not published: `../output/decompile/rea/pass-frameF2/RESULT.md`).
No RAW-quality, quick-view or recipe override.
Both stills must complete and report capture-ready, each with one
33,732,608-byte frame and three decodable JPEGs; expect eight stock-order
buffer returns. Check each callback is orientation mode 3, its hold precedes
the gyro reply, and each frame passes range/spot checks. Compare each frame
half against its own JPEG half using the corrected `frame_check.py`; a flat
or saturated scene can make correlation inconclusive. Preserve file hashes,
per-frame statistics and timing. An unchanged scene can produce similar
frames, so different hashes alone are not fresh-frame provenance. Stop on a
fault or failed guard; do not retry an indeterminate request.

Host tests already keep the first owned frame through source-buffer reuse,
a second capture and `g360_close`, freeing it only at caller release. That
is a source/lifetime check; it does not replace this target trial.

### Step 5 trial (RAW quality), after 4z-a passes

- **Run:** `python3 tools/observe/run_still_trial.py run --tag step5a --probe output/g360cap-build/g360-shot --g360 1 --g360-quality raw-superfine --net only`
- **Why this route:** our library sends `mm_camera_set_usr_attributes(handle,
  3, 0x30004)` (wire `0x1103`); the RTOS converts it to `IMAGEQUALITY_RAW_SUPERFINE`
  (`0x40004`), which the order sheet turns into set type 3 (first image plus
  JPEGs). The stock app sets quality through the shared user-spec block
  instead, which our library does not write.
- **Pass criteria:**
  1. the RTOS log shows the `SetUserData` line without an error;
  2. the image set has type 3 and lists the first image; `shot-001-first.srw`
     is saved, and the ring-B record shows the type-1 give-back before 7, 8, 9;
  3. `st cap capdtm usrlist` shows `IMAGEQUALITY_SUPERFINE` after the run.
- **If the set type stays 1:** the store refused the value or the Pro
  Suggest flag overrode it. Record the RTOS log lines; the next option is the
  `st cap capdtm setusr 4 0x40004` route from ACTIVE_PLAN step 5.

### Step 5d trial (WB bracket with its bracket words) — done, command accepted but idle

- **Run:** `python3 tools/observe/run_still_trial.py run --tag step5d --probe output/g360cap-build/g360-shot --g360 1 --g360-quality raw-superfine --g360-capture-type wbb --g360-bracket-words 0,0,0x1a0000,1 --g360-sets 4 --net only`
- **Why:** step 5c — libcapture requires word 13 (`0x1a0000` = AB3) and word
  14 (1 = amber-blue) for a WB bracket.
- **Pass criteria and risks:** as 5c below; the Gear 360 app never takes WB
  brackets, so this RTOS path is likely untested on the dual-lens camera.

### Step 5c trial (WB bracket requested as its capture type) — done, refused locally

- **Run:** `python3 tools/observe/run_still_trial.py run --tag step5c --probe output/g360cap-build/g360-shot --g360 1 --g360-quality raw-superfine --g360-capture-type wbb --g360-sets 4 --net only`
- **Why:** step 5b showed the still request's capture type sets the drive
  mode; mm type `6` is the WB bracket.
- **Pass criteria:** as 5b below, plus the RTOS log shows `DRIVE_WBBRK` (not
  `DRIVE_SINGLE`) at the still and a bracket command instead of
  `CCmdCaptureNormal > type: 1`.
- **Unknowns:** whether a WBB request needs 3A locked first (`0x15` locked it
  as part of the request); how the RTOS pipeline handles a bracket on the
  dual-lens camera.

### Step 5b trial (WB bracket set as a drive mode) — done, no bracket ran

- **Run:** `python3 tools/observe/run_still_trial.py run --tag step5b --probe output/g360cap-build/g360-shot --g360 1 --g360-quality raw-superfine --g360-drive wbbrk --g360-sets 4 --net only`
- **Why:** the drive mode `DRIVE_WBBRK` makes the RTOS run `RecipeBracket`
  (id 10), whose processing page adds the RAW-compression stage at a RAW
  quality; its back end sends one image set per bracket frame.
- **Pass criteria:** the RTOS log shows both `SetUserData` lines and no
  `Raw Compress Address is NULL`; one or more sets arrive with type 3 and a
  first image; `shot-001-s1-first.srw` is saved; every set's buffers go back
  (type 1 before 7, 8, 9); `usrlist` shows SUPERFINE and DRIVE_SINGLE after.
- **Risks:** the bracket back end skips two pages the normal (dual-lens)
  builder uses, so it may fail or produce single-lens images; an RTOS fault
  needs a power cycle.

### Trial E1 (manual exposure, three stills), prepared

- **Run:** `python3 tools/observe/run_still_trial.py run --tag expoE1 --probe output/g360cap-build/g360-shot --g360 3 --g360-exposure 160,100:96,100:32,100 --net only`
  (1/1000, 1/64, 1/4 s at ISO 100; automatic exposure set back after).
- **Pass criteria:** three stills saved; their mean levels rise with the
  exposure (about 6 stops from first to last, until the September ~1/5 s
  plateau); the camera's Linux log (`captureProcessA9 … exposure`) and the
  RTOS `SetVariableData (0x101, …)` lines show the values; settings back to
  PROGRAM and ISO_AUTO afterwards.

### Trial E2 (long exposure at RAW quality), after E1

- **Run:** `… --tag expoE2 --g360 1 --g360-quality raw-superfine --g360-exposure -37,100 --g360-ltnr on --net only`
  (about 5 s, ISO 100).
- **Question:** does the RTOS accept a 5 s exposure (the AE `+0x6c` value),
  pick the long-exposure recipe, and send an image set with a first (RAW)
  image? If exposures are capped near 1/5 s, the set will be dropped as in
  5a.

### Trial E3 (5 s at RAW quality, 90 s wait), prepared

- **Run:** `python3 tools/observe/run_still_trial.py run --tag expoE3 --probe output/g360cap-build/g360-shot --g360 1 --g360-quality raw-superfine --g360-exposure -37,100 --g360-ltnr on --g360-set-timeout-ms 90000 --net only`
- **Question:** is E2's stall real, or was the capture just slower than the
  15 s timeout (which restored the settings mid-capture)? If a set arrives,
  it should be set type 3 with a first (RAW) image.

### Trial R1 (a normal still plus its RAW frame), done 4 November: no RAW

Result (not published: `../output/decompile/rea/pass-rawR1/RESULT.md`): the bytes at
`0xa6000000` were the still's YUV picture, written over the RAW frame by the
stitch step before the image set arrived. The plan as run:

- **Run:** `python3 tools/observe/run_still_trial.py run --tag rawR1 --probe output/g360cap-build/g360-shot --g360 1 --g360-raw --net only`
- **Pass criteria:** `shot-001-raw.bin` (33,732,608 bytes) with `values_ok:
  yes` and `changed_during_copy: no`; the summary's preview shows the two
  fisheye circles of the same scene as the JPEG; the 2×2 colour-phase means
  differ the way a Bayer mosaic does (two equal green phases); the JPEGs and
  buffer returns as in 4z-a.
- **If the frame is not the scene** (all zeros, live-view data, or another
  still's): the address or the moment of copying is wrong; record and stop.

### Trial Q1 (quick view HOLDZOOM, both per-lens RAW images), done 4 November

Result (not published: `../output/decompile/rea/pass-rawQ1/RESULT.md`): live view stayed
off; lens 2's RAW intact, lens 1's memory reused by the main JPEG and a
stitched picture; no capture-ready. The plan as run:

- **Run:** `python3 tools/observe/run_still_trial.py run --tag rawQ1 --probe output/g360cap-build/g360-shot --g360 1 --g360-raw --g360-quickview holdzoom --net only`
  (g360-shot `ff494ec2…`).
- **What it does:**
  - reads the camera's quick-view value (`usrlist`); no still if unreadable;
  - sets libcapture attribute 30 = `0x1e0005`;
  - takes one normal still;
  - after the image set, copies lens 1 (`0xab134000`) and lens 2
    (`0xad134000`), 2896×2896 at stride 5824, before the JPEGs;
  - gives the buffers back and sets the quick-view value back.
- **Pass criteria:**
  - the RTOS log shows no `restartLiveview`/`movieWork` after the still;
  - `shot-001-raw1.bin` and `-raw2.bin` (16,866,304 bytes each) with
    `changed_during_copy: no`;
  - each correlates clearly with its half of the JPEG (one orientation);
  - the 2×2 phase means show a Bayer pattern (equal diagonal pair);
  - the JPEGs and give-backs as in 4z-a; no reboot; quick view restored.
- **If live view restarts anyway:** the log shows which check forced it
  (monitor-out, mode, boot shot); record and stop.
- **Camera state afterwards:** live view may stay off until a power cycle.

### Trial Q2 (what sits where at image-set time, quick view HOLDZOOM), done 4 November

Result (not published: `../output/decompile/rea/pass-rawQ2/RESULT.md`): `0xa6000000` is the
stitched picture plus the RAW frame's last rows; lens 2 intact again. The
plan as run:

- **Run:** `python3 tools/observe/run_still_trial.py run --tag rawQ2 --probe output/g360cap-build/g360-shot --g360 1 --g360-raw --g360-raw-layout 0xa6000000,5824,2896,11648,16 --g360-raw-layout 0xad134000,2896,2896,5824,16 --g360-quickview holdzoom --net only`
  (same build as Q1).
- **What it shows:**
  - whether the region `0xa6000000` (33.7 MB) still holds the full RAW frame
    (both lenses side by side, 16-bit) or a picture;
  - whether lens 2 again arrives intact.
- **Expected:** one still, as in Q1; the library reports a timeout (no
  capture-ready) and the quick view is set back.

## Where things are

- Library and command-line program: [g360cap/](../src/lib/g360cap/README.md).
- Earlier probe (steps 4–4y): [capture-session/camera/one-still-probe.c](../src/lib/capture-session/camera/one-still-probe.c).
- Basic-set decoder: [encoded-set/](../src/lib/encoded-set/README.md).
- Trial runner: [observe/run_still_trial.py](../tools/observe/run_still_trial.py).
