# g360cap: capture a still and save it

A C library (usable from C++) that runs in our own process on the Gear 360
(SM-R210), with the stock app stopped. It takes a still through the camera's
own capture library (`/usr/lib/libcapture-fw-prod.so`), copies the finished
images into memory it owns, gives the camera's buffers back the way the stock
photo library does, and lets the caller save the images. `g360-shot` is a
small command-line program on top of it.

This is the library described in [CAPTURE_LIBRARY_PLAN.md](../../../reverse-engineering/CAPTURE_LIBRARY_PLAN.md).
A later stock-app replacement (buttons, screen, sound, LEDs) is meant to be
built on it.

**Validated 4–5 November 2026:** the [experiment kit](../../../tools/experiments/README.md)
adds `--jpeg save|discard|skip-copy`, `--frame-output save|discard` and per-shot
timing rows. Defaults still save all images. `skip-copy` requires `--frame`,
marks omitted JPEGs `G360_IMAGE_SKIPPED`, and still returns every buffer and
requires a valid frame and capture-ready. It does not disable RTOS encoding.
The appended `g360_capture_options.skip_jpeg_copy` field requires rebuilding
callers. **309 captures across 44 processes passed on one boot**, including
manual/auto 100-shot zero-gap runs and all 8/6 canonical HDR exposures. In
three matched blocks, `skip-copy` reduced mean per-still work by 7.03% with
RAW saving. These measurements are specific to the tested settings/dark room.
See results and limits (not published: `../../../output/experiments-live-20261104/RESULT.md`).

`--library-log sync|off` exposes the existing API's optional log path. The
default `sync` fsyncs diagnostic lines; `off` passes NULL and creates no
library logfile, while stdout still reports completion/timing metrics. Native
SDK/kernel and external observer logging remain separate. A matched 24-frame
in-memory cohort measured 5.77% less per-still work with `off`; this does not
reduce the ~0.65 s RAW hold or establish additive gains with JPEG omission.

**Phase 2 finished (4 November 2026):** trial F2 demonstrated two normal stills
in one session, each with the full RAW frame of both lenses, complete JPEGs,
capture-ready and give-backs. See the [handoff](../../../reverse-engineering/handovers/PHASE2_COMPLETE.md)
and F2 result (not published: `../../../output/decompile/rea/pass-frameF2/RESULT.md`). Historical
captured RAW samples were discarded at the owner's request; their findings and
logs remain, and firmware/evidence can be restored from
retained artifacts (not published: `../../../retained-artifacts/README.md`).

**Validation history (4–5 November 2026):**

- **Same-boot suite:** 309 successful full frames, 44 processes, 1,236
  independently observed returns, no reboot/service restart. JPEG policies,
  normal-still manual exposure, full HDR sweeps, stress and optional logging
  passed. Physical app controls, charge transitions and other native drives
  remain unvalidated.
- **On the camera:** one still (step 4z-a) and two stills in one session
  (4z-b) work. Each still's three JPEGs are copied, saved and decode; the
  buffers go back with the same types and physical addresses as the stock
  app's.
- **RAW quality on a single still (5a):** the camera drops the still (its
  normal recipe has no RAW-compression stage), and the library reports it.
- **Host-tested, not yet on the camera:** several image sets per still
  (brackets), late sets, the drive-mode setting.
- **On the camera:** the full RAW frame copied during the camera's gyro
  hand-off (`--frame`, trial F1): one normal still, both lenses in one frame,
  the camera did not fault and the still completed.

## JPEG Exif and camera DNG (T1/T2)

`g360-shot` now inserts Exif into each saved main, screen and thumbnail JPEG
by default. It includes SAMSUNG/SM-R210, F2.2, focal length, sRGB, each image's
dimensions, the native still's exposure/ISO/program/WB where available, and
Samsung Type 2 MakerNote AWB (twice), AE and colour records. The original
encoded JPEG bytes are streamed unchanged after the new APP1 segment.
`--exif off` saves the bare encoder JPEGs. Missing/invalid IQ metadata fails
the requested Exif save; unknown exposure fields are omitted.

`--frame --frame-output dng` writes `shot-NNN.dng` instead of the `.bin` frame
and layout text. This is the whole 5824×2896 frame, uncompressed, with per-shot
colour matrices and gains; it matches [make_dng.py](../../../reverse-engineering/dng/make_dng.py).
Since 6 November it also carries the still's Exif (exposure time, ISO,
program, white-balance mode, F2.2, 1.2 mm) and the Samsung IQ MakerNote
(`g360_save_dng_full`, matching `make_dng.build_full`); its header is
40,960 bytes. g360-shot writes the firmware's ExposureTime (a third-stop name);
the app writes the real time for a set shutter with
`g360_exif_exposure_from_tv` (`g360_exif.h`).
It requires one set per still and the normal frame layout. `save` and
`discard` retain their existing meanings. Example capture options for the
validated session wrapper:

```sh
--frame --frame-output dng --count 3 --gap-ms 0
```

Library users include `g360_output.h` and link `g360_output.c`, `g360_dng.c`
and `g360_exif.c` alongside g360cap. With a successful `g360_capture_shot`:

```c
/* Keep shot alive until each output call has completed. */
g360_exif_info info;
g360_image *jpeg = &shot.sets[0].images[G360_IMAGE_MAIN];
g360_exif_from_still(&shot.sets[0], jpeg, &info);  /* info.datetime: "" or a date */
g360_save_dng_full(&shot.frame, &shot.sets[0].maker_note, &info, NULL, "shot.dng");
g360_save_jpeg_exif(jpeg, &shot.sets[0].maker_note, &info, "shot.jpg");
```

The new `g360_still.capture_exif` record is copied from the callback's image
set before it expires; callers must rebuild for the larger struct. It is
separate from the MakerNote IQ buffer. The native fraction and ISO come from
this per-shot record, including automatic exposure, rather than a conversion
of requested settings. [Native trace and validation](../../../reverse-engineering/dng/T1_T2_VALIDATION.md).

The formatting APIs (`g360_dng_header`, `g360_exif_app1`) are pure. Output
helpers use only the retained result, never SDK state: DNG output is a
header (588 bytes plain, 40,960 bytes with metadata) followed by the
existing RAW buffer, with no extra frame allocation/copy. CLI output currently waits for each file's write/fsync.
A future batch API can queue retained results for a bounded output worker,
with explicit memory limits, backpressure, error reporting and a final drain.
This preserves a short capture path without introducing that queue here.

The minimum Exif profile does not yet reproduce MPF, the embedded IFD1
thumbnail, Samsung orientation tags/UTC trailer or all stock MakerNote tags.
CLI dates are omitted until their capture-time source is established; callers
can supply a date to the pure builder. Stitching-app compatibility with those
omissions is not established.

## API

```c
#include "g360cap.h"

g360_open_options o = {0};
o.log_path = "/opt/storage/sdcard/g360.log";   /* optional; synced per line */
o.free_bytes = card_free_bytes;                 /* sent with every still */
if (g360_open(&o) != G360_OK) ...               /* gates, init, registration */

g360_still s;
int st = g360_capture(NULL, &s);                /* blocks: still, copy, give back */
if (s.images[G360_IMAGE_MAIN].flags & G360_IMAGE_COPIED)
    g360_save_image(&s.images[G360_IMAGE_MAIN], "/opt/storage/sdcard/a.jpg");
g360_still_free(&s);
...
g360_close();                                    /* the capture library stays loaded */
```

For stills that send several image sets (brackets, one set per frame):

```c
g360_capture_options co = {0};
co.max_sets = 4;                 /* copy up to 4 sets */
co.extra_set_wait_ms = 3000;     /* after capture-ready, wait this long for more */
g360_shot shot;
int st = g360_capture_shot(&co, &shot);   /* shot.set_count sets in shot.sets[] */
...
g360_shot_free(&shot);
g360_service(5000);              /* idle: give back sets that arrive late */
```

`g360_capture_options.capture_type` picks the still request's capture type
(`G360_CAPTURE_*`; default `G360_CAPTURE_LOCK3A`, the request that works on the
camera). The capture type also sets the camera's drive mode for that still
(step 5b), so a bracket is requested with `G360_CAPTURE_WBB` and similar, not
with `g360_set_drive_mode`.

A bracket request also needs its bracket words (`bracket_words`, the
request's words 11–14): a WB bracket requires word 13 = `0x1a0000`–`0x1a0005`
(AB3, AB2, AB1, MG3, MG2, MG1) and word 14 = 1 (amber-blue) or 2
(magenta-green), else libcapture refuses it locally (step 5c).

`g360_set_image_quality(G360_QUALITY_*)` and `g360_set_drive_mode(G360_DRIVE_*)`
change the camera's quality and drive mode through the capture library's own
call (`mm_camera_set_usr_attributes`, attributes 3 and 8). Both settings
outlive the process: set `G360_QUALITY_SUPERFINE` and `G360_DRIVE_SINGLE`
back when done (`g360-shot` does).

Exposure: `g360_set_manual_exposure(tv, iso)` sets the dial, shooting mode
(MANUAL), ISO and shutter (`tv` = 16 × log2(1/seconds): 96 = 1/64 s, 32 =
1/4 s, 0 = 1 s); `g360_set_auto_exposure()` sets the usual automatic
exposure back. `g360_set_user_attribute` and `g360_set_variable` send raw
settings (`G360_ATTR_*`, `G360_VAR_TV`). See
exposure control (not published: `../../../output/decompile/rea/pass-exposure-control-20261104/RESULT.md`)
for the value tables. `g360-shot --exposure 160,100:96,100:32,100` takes three
stills at three exposures.

RAW (experimental; trial Q1 (not published: `../../../output/decompile/rea/pass-rawQ1/RESULT.md`)
delivered lens 2 only): with
`g360_capture_options.raw` = 2 the library copies the still's two per-lens
RAW images into `g360_still.raw[0]` and `raw[1]`. They are the PP core's
RAW after lens-shading, crosstalk and defect correction, 2896×2896, one
16-bit word per sample at stride 5824, at physical `0xab134000` and
`0xad134000` (RAW keep trace (not published: `../../../output/decompile/rea/pass-raw-keep-trace-20261104/RESULT.md`)).
The copies are made after the image set arrives and before the JPEGs are
copied and the buffers given back.

The RTOS does not hand these images over. The live view restarted after
the still overwrites them; the quick-view time HOLDZOOM
(`g360_set_quickview(G360_QUICKVIEW_HOLDZOOM)`, libcapture attribute 30)
stops that restart. In trial Q1, lens 2 then arrived intact (GRBG, 16-bit
values, dark level about 520, 32 bytes of row padding). Lens 1's memory
was reused by the same still's main JPEG and a stitched picture.

In that mode the camera sends no capture-ready, so `g360_capture` reports
`G360_E_TIMEOUT` and latches the session after the image set has been
copied and given back. The quick-view setting stays in the camera after
exit; set the previous value back.

Each copy is checked: the range must be readable, spots of the source are
re-read after the copy (`G360_IMAGE_RAW_CHANGED` if they differ), and every
sample must fit the layout's bit depth (`G360_IMAGE_RAW_VALUES_OK`; the
default 16 accepts any value, since the real depth is not known yet).
`g360_capture_options.raw_layout[]` overrides a region. The full
side-by-side frame at `0xa6000000` is overwritten by the stitched YUV
picture before the image set arrives
(trial R1 (not published: `../../../output/decompile/rea/pass-rawR1/RESULT.md`)).

`g360-shot --raw` saves `NAME-NNN-raw1.bin` and `-raw2.bin`, each with a
description (`-raw1.txt`: size, stride, physical address, lens, checks).
`--quickview holdzoom --restore-quickview NAME` sets the quick-view time
before the first still and sets `NAME` (or nothing, with `none`) back
afterwards.

The full frame (on the camera since trial F1): with
`g360_open_options.frame_pause` the library registers its callback client
**before** the capture library's Beauty object, so its callback runs first on
the still-expansion topic (`0x04010008`). The RTOS waits for the gyro reply
during that callback and the frame is still intact then
(pause trace (not published: `../../../output/decompile/rea/pass-expansion-pause-20261104/RESULT.md`),
code pass (not published: `../../../output/decompile/rea/pass-frame-copy-code-20261104/RESULT.md`),
trial F1 (not published: `../../../output/decompile/rea/pass-frameF1/RESULT.md`)).
The library copies the frame there, into memory it owns, and returns so the
reply goes out; the still then completes normally.

The frame's address comes from `/proc/self/maps` (`g360_maps_physical`: the
physical address inside a readable `/dev/mem` mapping), not from the camera.
`g360_capture_options.frame` = 1 arms the copy for the next still;
`g360_capture_options.frame_layout` describes it (physical address, width,
height, stride, bits; the default is `0xa6000000`, 5824×2896, stride 11648,
16-bit — both lenses side by side). The copy is checked the same way the RAW
copies are (readable range, spots re-read after the copy, values within the
bit depth). The still fails with `G360_E_IMAGE` if the requested frame fails these
checks. The spot checks are sampled overwrite detection.

**Maker note (5 November 2026).** Each still's maker-note buffer is copied
before it is given back (`g360_still.maker_note`, the largest readable of
64, 32 or 24 KiB from `maker_note_address`). `g360-shot` saves it with the
JPEGs as `NAME-NNN-makernote.bin`. It holds the RTOS's AWB, AE and colour
records that the stock app puts in its JPEG MakerNote; our JPEGs carry no
Exif. See [dng](../../../reverse-engineering/dng/README.md). `maker_note` is appended to
`g360_still`, so existing field offsets are unchanged, but callers must be
rebuilt.

`g360-shot --frame` saves `NAME-NNN-frame.bin` with a description
(`-frame.txt`: size, stride, physical address, the hold time in
microseconds, the expansion mode, the checks). `--frame-layout
PHYS,WIDTH,HEIGHT,STRIDE,BITS` replaces the frame's layout. The session must
be opened with `frame_pause`; a capture that asks for a frame in a session
without it fails with `G360_E_STATE`.

Layout validation uses division before multiplication, for both CLI and
API callers: width must fit the stride, the byte extent must fit the 96 MiB
limit, and the physical range must fit the camera's 32-bit address space.
Invalid layouts return `G360_E_ARGUMENT` before arming or sending a still.
The trial runner checks those limits before staging helpers or stopping
the stock app.

Only a readable expansion descriptor of size `0x3c8` with orientation mode
3 triggers the frame copy. A different mode leaves it armed; duplicate
requests do not overwrite a finished copy. The physical mapping is resolved
again in the callback and must still match the armed source address.
Unreadable or replaced mappings produce an image error. Mapping checks do
not pin memory against concurrent unmapping by unrelated code.

`G360_IMAGE_RAW_CHANGED` checks sampled source regions, not every byte.
Passing those checks alone is not proof of source coherence or sensor
provenance; this route also relies on the measured gyro pause. F1's stored
words have six low bits clear, but that does not measure ADC precision or
calibrate a black level. See the continuation review (not published: `../../../output/decompile/rea/pass-frame-review-20261104/RESULT.md`).

`g360_capture` (and `g360_capture_shot`) return:

| Status | Meaning |
|---|---|
| `G360_OK` | The image set arrived; requested copies passed their checks; every buffer was given back; the camera reported capture-ready. By default this includes complete main/screen/thumbnail JPEGs. With `skip_jpeg_copy`, skipped JPEG payloads are not validated and the requested RAW frame must pass instead. |
| `G360_E_IMAGE` | The set arrived, but an image was not copied (unmapped, too large, no memory) or is not a complete JPEG. Whatever was copied is in the result, and the buffers were given back. |
| `G360_E_RELEASE` | Everything was copied, but a give-back call returned nonzero. |
| `G360_E_TIMEOUT` | No image set, or no capture-ready, in time. The session is latched. |
| `G360_E_NO_SET` | The camera reported capture-ready but sent no image set within 3 s (or `extra_set_wait_ms`): it dropped the still, as it does for a RAW quality on the normal recipe. Latched. |
| `G360_E_SET` | The set's structure was not readable, or its type is unknown. Nothing was read; the session is latched. |
| `G360_E_SEND` | The still call returned nonzero. Indeterminate; latched; never retried. |

A latched session sends no more stills; the process should exit.

Each image in `g360_still.images[]` has its own copy (`data`, `size`), the
descriptor's width, height and two stride words, the camera address it came
from, and flags (`G360_IMAGE_PRESENT`, `_COPIED`, `_JPEG_OK`, `_RELEASED`,
…). The fourth image (`G360_IMAGE_FIRST`) is present only for set types 2
and 3 (the RAW qualities); it is the camera's "Raw Compress" stream, not yet
decoded.

## Movie preview (T20, experimental)

`g360_preview_start(&options)` starts the camera's movie preview the way the
stock Wi-Fi stream does: `RecordMovie(0x1020001 MV_START)` with the stock
28-byte layout (size, rate, type 0), then a wait for `VIDEOSTART_DONE`
(`0x2030001`). Each frame goes to `options.callback` on the capture
library's receiver thread as a `g360_preview_frame`: size, address type,
lens side, timestamp and, when the whole range is mapped readable, the NV12
planes `y` and `c`. Copy what you need and return quickly; never keep the
pointers. `g360_preview_stop` sends `MV_STOP` and waits for
`VIDEOSTOP_DONE` (`0x2030002`). While a preview runs `g360_capture`
refuses with `G360_E_STATE`.

Sizes are the camera's movie sizes (1280×640 is the old stream's) and rates
are given times ten (125, 150, 240, 250, 300, 500, 600, 1000, 1200); anything
else is refused before a command is sent. On the camera (trial 1, 7 November)
the frames came only on the image set `0x4010007` (`options.with_set`), at
about 30 per second even with 15 requested.

`g360-preview` is the trial program: it runs a preview for `--seconds`,
counts frames per topic, copies one frame a second, stops, saves the last
frame (`frame.yuv`, `frame.txt`), times a libturbojpeg encode
(`frame-half.jpg`, `frame-full.jpg`), and with `--still` takes a still after.
`--check-refusal` tries a capture during the preview (it must be refused).
Static result (not published: `../../../output/live-preview-T20/RESULT.md`),
trial 1 (not published: `../../../output/live-preview-T20/trial-01/RESULT.md`).


1. **Never fault, never abort.** The camera's kernel reboots the camera
   when it raises SIGSEGV, SIGBUS, SIGFPE, SIGABRT or a forced SIGKILL on
   any process, before any signal handler could run, and when a process
   dies of SIGABRT or SIGFPE (kernel fault reboot (not published: `../../../output/decompile/rea/pass-kernel-fault-reboot-20261104/RESULT.md`)).
   So a crash handler cannot help, and `abort()`/`assert()` are as fatal
   as a bad pointer. Every address the library did not allocate is checked
   against `/proc/self/maps` before it is read (`g360_maps_covers`, which
   accepts a range spread over adjacent readable mappings); the library
   never calls `abort`; the host tests run under AddressSanitizer.
2. **Callbacks.** The image-set callback runs on the capture library's
   receiver thread. It copies only the set's small structures (the 0x228-byte
   basic set and the maker-note word) and returns. The image bytes are
   copied on the caller's thread, as the stock photo library's worker does.
   The camera's buffers stay valid until they are given back. With `--frame`,
   the separate orientation expansion callback copies the preallocated full
   frame synchronously while the camera waits for the gyro reply; F1 took
   651 ms there. Other expansion modes are ignored.
3. **Give back every buffer once, after copying**, with the stock types and
   order: maker note 5, first (RAW) image 1, main 7, screen 8, thumbnail 9,
   using the virtual addresses the capture library wrote into the set.
   They are given back even when a copy failed. Address 0 is never given.
4. **Gates first.** Before any capture-library initialization: the gyro
   history check (the executable must link `libm.so.6` and
   `libdevice-node.so.0.1`) and the APEX check (the CApex singleton gets a
   private copy of the user-spec block; nothing is written to shared memory).
5. **Every image set is given back, even late ones.** Sets beyond what the
   caller asked to copy are given back uncopied. A set that arrives after its
   still returned is given back at the start of the next still, during
   `g360_service`, or at `g360_close`.
6. **One session per process; no teardown.** The capture library's
   callbacks carry no user pointer. Process exit ends the camera session.

## Evidence

| What | Where |
|---|---|
| Start-up and still request that survive | step 4y (not published: `../../../output/decompile/rea/pass-step4y/RESULT.md`) |
| Why the image-set handler needs the APEX gate | step 4v (not published: `../../../output/decompile/rea/pass-step4v/RESULT.md`) |
| Why libdevice-node and libm must be linked | ACTIVE_PLAN step 4e |
| Image-set layout (`set+0` → basic set, `+0x2e0` maker note) and the give-back order and types | stock release (not published: `../../../output/decompile/rea/pass-stock-release-20261104/RESULT.md`), fact `session.stock-photo-library-releases-buffers-after-save-20261104` |
| JPEG buffers are complete, size word exact | fact `session.still-jpeg-size-word-is-exact-length-20261104` |
| Quality route; why RAW needs a bracket; drive-mode route | RAW quality route (not published: `../../../output/decompile/rea/pass-raw-quality-route-20261104/RESULT.md`), RAW-compress trace (not published: `../../../output/decompile/rea/pass-raw-compress-trace-20261104/RESULT.md`), drive → recipe (not published: `../../../output/decompile/rea/pass-drive-recipe-20261104/RESULT.md`) |
| Basic-set field offsets | [encoded-set decoder](../encoded-set/README.md) |
| Release message `0x83/0x020c`, RTOS exact-address match | facts `raw.encoded-capture-release-route`, `raw.capture-release-address-conversion-branches` |

## Build and test

```sh
CC=/usr/bin/clang sh src/lib/g360cap/tests/run.sh   # host tests
sh src/lib/g360cap/build-arm.sh                      # camera build -> output/g360cap-build/g360-shot
```

On the camera, the capture buffers sit in one `/dev/mem` mapping of
physical `0xa6000000`–`0xc0000000` (416 MB, readable; seen in step 4v's
memory map), and the whole map text is about 36 KB, well inside the
library's 256 KB buffer. ARM Linux normally maps `/dev/mem` memory outside
its own RAM uncached, so the copies should read what the encoder wrote; that
is not checked on this kernel, and the trial's JPEG checks (start/end
markers, decoding on the Mac) are what show it.

The host tests build the library with AddressSanitizer against a fake
capture library that delivers the image set from its own thread, overwrites
each buffer when it is given back (so a copy taken too late shows), and in
one mode makes the images inaccessible during the callback (so reading them
there crashes the test). They cover: one still, two stills, a RAW set type,
an unmapped image, a broken JPEG, an unreadable set, an unknown set type, no
image set, no capture-ready, a refused give-back, a failed still call, and
the gyro, APEX and bridge gates, the image-quality and drive-mode options,
several sets per still (a three-set bracket, more sets than asked for, a
late set), capture-ready without a set, and that the header compiles and
links as C++.

## Running on the camera (needs the user present)

```sh
python3 tools/observe/run_still_trial.py run --tag step4z1 \
    --probe output/g360cap-build/g360-shot --g360 1 --net only
```

The full-frame trial (F1) adds `--g360-frame`:

```sh
python3 tools/observe/run_still_trial.py run --tag frameF1 \
    --probe output/g360cap-build/g360-shot --g360 1 --g360-frame --net only
```

The runner stops the stock app gracefully, starts the recorders, runs
`g360-shot`, and fetches its log and images into
`output/decompile/rea/pass-<tag>/`. The summary lists the saved images (with
a JPEG marker check) and the `0x83/0x020c` give-back frames from ring B.

The prepared repeat-frame trial (F2) uses the same normal recipe twice:

```sh
python3 tools/observe/run_still_trial.py run --tag frameF2 \
    --probe output/g360cap-build/g360-shot --g360 2 --g360-frame --net only
```

F2 subsequently passed on the camera; see the result linked above. The earlier ARM build and offline review are
recorded in the continuation checkpoint (not published: `../../../output/decompile/rea/pass-frame-review-20261104/RESULT.md`).
