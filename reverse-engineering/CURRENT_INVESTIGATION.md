# Current Gear 360 capture-library investigation

**5 November 2026:** goals and open tasks moved to TODO.md (not published: `../TODO.md`).
`PROJECT_GOAL.md` and `ACTIVE_PLAN.md` are retired history. This file stays
the dated log, newest first.

## Lens-module EEPROMs decoded — 7 November 2026

TODO T14, host-only (nothing run on the camera). The stock factory library
`libfactory-mode.so` holds the EEPROM field table, so every used byte now
has a name; the camera's factory log confirms it for EEPROM B.
- **White balance:** the RTOS reads point 1 at 0x040 (`AWB_LOW_GAIN`) and
  points 2–4 at 0x228–0x239. They are exactly the stock JPEG AWB record's
  white points, and the record's lens adjustment is golden (from the RTOS
  tuning data) divided by point 1, reproduced to the last bit for both
  lenses. EEPROM A (front) is lens a, the left half.
- **Stitching:** 0x0d0–0x20f is `OPTICAL_AXIS_01–40`. The stock app copies
  0x0d0–0x16f into every JPEG as MakerNote tag 0x0012 ("OPAX"); nothing on
  the camera parses it. The first six doubles fit the two lens centres and
  image-circle radii in 2896-px halves (our ChArUco centres agree within
  3.2 px); the other nine are not identified.
- **The rest:** ISO calibration, factory test results, module ID, unit
  number and a word-sum checksum.

[eeprom/README.md](eeprom/README.md); decoder and tests in `eeprom/`; facts
`target.lens-eeprom-field-map-and-awb-points-20261107`,
`target.lens-eeprom-optical-axis-block-20261107`.

## The camera joined the home Wi-Fi and came back — 6 November 2026

The user asked whether the camera could join their network instead of
opening its own access point. A reboot was acceptable; being locked out for
good was not. [g360wifi](../src/wifi/README.md) does it for one boot with
nothing written to the built-in flash, and goes back to the camera's own
access point on any failure. A watchdog does the same if a run dies.

On the camera, attended, on one boot:
1. **Access point off and on (`ap-cycle`):** about 10 s.
2. **Join:** the station firmware loaded and the camera joined WPA2 on
   channel 3 within 1 s, then took a DHCP address. The Mac found it by its
   hardware address and checked in.
3. **Revert:** the access point was back 5 s after it started.

**Found and fixed:** the agent deletes its `hostapd.conf` while the access
point is off, which made the first revert refuse (nothing changed). The
script now keeps the name and channel beside itself, and falls back to the
device name. ConnMan saved nothing.
Result (not published: `../output/wifi-station-20261106/RESULT.md`); fact
`target.wifi-station-join-and-return-20261106`; TODO T11.

## DNGs with the shot's metadata — 6 November 2026

rawtoaces wrote `ExposureTime: 0` into EXRs because our DNGs had no Exif.
Every DNG now carries the still's own exposure time, ISO, program and
white-balance mode (the native per-shot record), the date, f/2.2 and 1.2 mm,
the camera's IQ MakerNote and, for sweep frames, `HDR SWEEP … FRAME k/n`.
C and Python writers match byte for byte; a camera DNG (pair `DNG3`) matched
the reference, and rawtoaces copied the exposure into the EXR. The white
balance and colour matrices were already the camera's per-shot values from
the MakerNote (pair P2), with the D65 label of ColorMatrix1 still assumed.
App deployed (`b00f97ac…`). Fact `raw.dng-with-shot-metadata-20261106`.

## Camera app: settings screen and sweep options — 6 November 2026

The user asked for a settings screen while they slept: hold Menu to open
it; Menu moves to the next setting or option, Capture opens or selects,
Power leaves. Settings: mode (single image or HDR, where a Capture click
takes the sweep), HDR shots (6 or 8, the sets of `hdr-capture.sh`) and
saving (flash first, or straight to the card). They are stored on the
card in two checksummed slots, like the counter. The display library now
draws up to three lines. Unattended camera runs measured 8 shots straight
to the card at 26.4 s held still (the estimate was 26.4 s), 6 shots at
19.3 s (card) and 12.5 s (flash first). Deployed (`72e552d1…`), running
at READY; the buttons themselves are untried.
[Validation](../src/app/VALIDATION.md); fact
`app.sweep-save-options-and-settings-20261106`.

## Camera app: HDR sweep by holding Capture — 5 November 2026

The user asked for a fast HDR sweep. Measurements first
(output/speed-20261105/RESULT.md (not published: `../output/speed-20261105/RESULT.md`)):
the card writes 16.5 MB/s, Linux has one core and about 70 MB free RAM, so
8 frames can't wait in memory; the camera's own flash (`/opt/usr`) writes
42 MB/s and has 1.9 GB free. A DNG is a header plus the RAW bytes unchanged,
so building it later saves nothing; moving the card write after the sweep
does.

Holding Capture for a second now takes the 8 exposures of `hdr-capture.sh`,
RAW only, each DNG saved to `/opt/usr/g360-app/queue` before the next
frame, then moved to the card with a per-sweep exposure list. Unattended on
the camera (`--sweep-once`): held still 16.7 s, moved in 19.4 s, every
exposure applied. Deployed and left running at READY. The hold gesture
itself waits for the user. After each sweep the app reads the flash's own
wear estimate (EXT_CSD through debugfs) and warns on the screen from
30–40% used on; on the camera it read 0–10% (build `b447d6e6…`).
[Validation](../src/app/VALIDATION.md); facts
`target.built-in-flash-opt-usr-for-frames-20261105`,
`app.hdr-sweep-via-flash-queue-20261105`.

## Camera app: Power hold turns the camera off — 5 November 2026

Holding Power in our app now switches the camera off. The user tested it
twice, and the camera booted normally afterwards with the card and photos
intact. The app uses the stock app's own steps, traced with REA/Ghidra
through the app, libdevice-node, the devman plugin, libdeviced, deviced
and the kernel:
1. `d5-keys/shutdown` = 1;
2. deviced's D-Bus `PowerOff`, which syncs, unmounts the card and turns
   USB off;
3. `d5-keys/shutdown` = 2. The kernel's store handler then calls the
   board power-off, which switches the power chip off.

Our app runs those steps from a copy of itself in `/tmp`. deviced's forced
unmount kills every process using the card (`fuser -m -k`), and the app
runs from the card.

Found while deploying: renaming over an existing file on the card leaves
the old directory entry live. The card is FAT32 under the kernel's exfat
driver, not FUSE exfat as recorded earlier. The duplicate was removed and
checked on the raw card.
[Validation](../src/app/VALIDATION.md); facts
`app.stock-poweroff-sequence-20261105`, `app.power-hold-poweroff-20261105`,
`target.card-rename-over-existing-leaves-old-entry-20261105`.

## Camera app: full fresh-boot flow validated — 5 November 2026

After a normal reboot (boot 1793849521) the user double-pressed Power. The
launcher stopped the stock app (`mode=fresh`) and the app reached READY.
Two Capture presses gave `00003` and `00004` (DNG + Exif JPEG). The counter
carried across the reboot, and all three new DNGs (including the earlier
`00002`) equal `make_dng.py` byte for byte.
[Validation](../src/app/VALIDATION.md); fact
`app.fresh-boot-flow-two-shots-20261105`.

## T4 storage failure fixed; first app capture on the camera — 5 November 2026

**The app works on the camera.** The fresh-boot "storage IO failure" is
resolved and an attended capture succeeded.

Root cause: `src/app/storage_platform.h` carried the generic Linux
`O_DIRECTORY` (0200000) and `O_NOFOLLOW` (0400000). On the camera those bits
are `040000` and `0100000`, and `0200000` is the camera's `O_DIRECT`. The
card is FUSE exfat with no direct I/O, so the app's first `open()` of its own
photo directory returned EINVAL and storage never initialized. The strace of
the deployed build shows the exact call.

The camera's numbers were measured, not guessed: an ARM probe
(`src/app/tools/open_flags_probe.c`) opened a directory, a regular file and
a symbolic link with one candidate bit at a time, on the card, the root
filesystem and tmpfs. ENOTDIR identifies `O_DIRECTORY` (040000), ELOOP
identifies `O_NOFOLLOW` (0100000), and the only bit that sets FD_CLOEXEC is
`O_CLOEXEC` (02000000). Directory fsync works on the card, so the counter
slots' durability needed no change; `O_CREAT` with `O_EXCL` creates and
refuses with EEXIST, which is what the no-overwrite reservation relies on.
A new host test compares the header's numbers with the measured table, since
the host build uses the host's own headers. Fact record
`target.open-flag-numbers-are-not-the-generic-ones-20261105`.

Fixed build `ab627b290487b9070f5d74ef4a674c4cb236912ccbc105b1f791bf849c12c10a`
deployed to `/opt/storage/sdcard/mods/g360-app-v1-20261105` with the hash
file updated; the earlier build is kept on the card as
`g360-app.g360-before-20261105`. Started on the same boot 1793844742 with
stock inactive (`launch mode=reenter`): no service started, no firmware
written, no power cycle. Storage initialized, the library reported
`mm_camera_create` succeeded and `open: ready`, and the screen showed READY.

The user pressed Capture once and confirmed seeing `CAPTURING...` and `READY`.
The app wrote `20261105-125537-00001.dng` (33,733,196 bytes) and
`20261105-125537-00001.jpg` (4,729,708 bytes). On the host the DNG passes
`exiftool -validate` with no warning and carries the expected SAMSUNG/SM-R210
GRBG tags; the JPEG carries the Exif profile (1/100 s, ISO 125, Program AE,
F2.2, sRGB) and the Samsung Type 2 MakerNote, which `stock_iq.py` decodes.
Rebuilding the DNG with the Python reference from the RAW samples and this
shot's own IQ is byte for byte identical.

Local evidence `output/camera-app-20261105/` (probe output, trace excerpt,
app and capture logs, shot-001 with its verification report).

## T4 fresh-boot trial: storage initialization fails — 5 November 2026

User rebooted, double-Power showed STARTING then ERROR. Boot1793844742,
stock inactive/MainPID0; app log says `storage open: storage IO failure`,
before capture SDK load or photos directory creation. Exact boot/comm/exe
checked before graceful termination of failed PID457; stock stays stopped.
Local logs `output/camera-app-20261105/fresh-boot-error.txt` and cleanup log.
Next: verify ARM32 open flags in storage_platform.h and isolate failed folder
operation; then same-boot reentry and attended capture. No root cause accepted.
Five-hour usage97%, stop boundary95%; wind-down logging/cleanup only.

## T4 minimal app implemented/staged — 5 November 2026 (trial pending)

User requested READY → capture sound/CAPTURING... → one auto RAW+DNG/Exif
JPEG → READY after saves, with persistent YYYYMMDD-HHMMSS-00001 names.
`src/app/` implements it using one capture worker and a responsive UI;
`g360display/` exposes the existing screen ABI as a library. Storage uses
O_EXCL pair reservation and two synced counter slots; no rename-overwrite.
All host/sanitizer,29 launcher lifecycle tests and ARM build passed.
[Validation](../src/app/VALIDATION.md). Staged/pinned ARM b6c19d42... at
`/opt/storage/sdcard/mods/g360-app-v1-20261105`. Existing double-Power SD
launcher retargeted after verified backup; boot/network scripts unchanged.
User confirmed ready and was asked to double-Power then Capture once; physical
result and output validation remain pending. No fresh boot/power cycle.
Local evidence `output/camera-app-20261105/`. Owned workers finished.
Five-hour usage95%,5% remaining, reset1793856676; resource wind-down only.
No reset credit consumed. Keep T4 in progress until camera behavior is checked.

## T1/T2 completed: camera JPEG metadata and C DNG — 5 November 2026

**T1 and T2 are complete**; TODO.md (not published: `../TODO.md`) records status and the
remaining tasks. [Evidence and validation](dng/T1_T2_VALIDATION.md).

- Fresh REA/Ghidra on pinned libmm-photo established the per-shot Exif
  source: image-set +8, 0x2d8 bytes; exposure fraction +0/+4, ISO +0x70.
  Accepted static fact `raw.native-capture-exif-record-20261105`. g360cap
  copies it before callback return, independently of the IQ MakerNote.
- `g360-shot` defaults to Exif + Samsung IQ in all three JPEG sizes;
  `--exif off` preserves bare streams. No MPF/IFD1/angle/UTC replication or
  guessed CLI dates. Native actual values are used, including auto stills.
- `--frame --frame-output dng` writes the Python reference format in C:
  588-byte header then unchanged owned samples, no extra frame copy. Pure
  header generation/output helpers keep future bounded async IO possible.
- All host checks and the freestanding ARM build pass. Nine same-boot
  captures passed (boot 1793781685); six DNGs equal Python byte for byte and
  validate, three render in macOS. Nine final-build Exif JPEGs validate and
  round-trip their same-shot IQ; auto 1/100s ISO125, manual 1/4s ISO400.
  Accepted runtime fact `raw.camera-exif-dng-writers-20261105`.
- Mean save/log phase: .bin 2.592s, DNG 2.529s, three each; capture ~1.34s.
  No proven DNG speedup. Queue ownership, memory limits/backpressure and
  final drain belong with a future batch API, not the capture callback.

Local evidence: `output/t1-t2-20261105/` (camera copies/manifests, host checks,
REA responses/closed snapshot, pinned final build). Camera trial folders stay
on the SD card; temporary /tmp runners removed after verified fetches. No
firmware changes, service restart or power cycle. Owned workers ended; REA
session exported/closed and its bridge stopped. Account sample: 44% used /
56% remaining in the five-hour window, weekly 32%; reset 1793856676. No reset
credit used. Source integration is recorded by the task commit on local master.
Next work follows TODO (T3 colour validation or the controller when requested),
not the historical Phase 2 pointers below.

## DNG making: the camera's own colour data — 5 November 2026

Started on the user's request. Every stock JPEG's MakerNote carries the
camera's colour state, and [stock_iq.py](dng/stock_iq.py) decodes it.

What it contains:
- a 3×3 camera→sRGB colour matrix;
- per-shot white-balance gains;
- per-lens factory calibration (lens adjustments and white points; P1 matches
  exactly across both cameras);
- the stock tone curve and the YCC saturation matrix.

All 721 saved JPEGs decode. Details, DNG mapping and next steps:
[dng/README.md](dng/README.md); fact `raw.stock-jpeg-iq-records-20261105`
(open).

**Pair P2** (result (not published: `../output/dng-P2/RESULT.md`)) verified it on a colour
checker. g360cap now saves the camera's MakerNote buffer with each still.

What P2 showed:
- **The pipeline:** the recorded gains, matrix and tone curve reproduce the
  JPEG's patches to about 6 levels RMS (23 without the matrix).
- **Levels:** black ≈ 4106, white 65472.
- **CFA:** GRBG on both lenses.
- **Lens shading:** already corrected in the frame.
- **Lens adjustment "a"** is the left half.

**DNG writer:** [make_dng.py](dng/make_dng.py) writes one DNG for the
whole frame (user's choice). P2's DNG validates and macOS renders it with
correct colours.

Next:
- check the DNG in a raw developer;
- put the camera's Exif and MakerNote into our JPEGs (user request; format
  in [dng/README.md](dng/README.md)).


## Buttons, LEDs and sound: g360ctl and trial C1 — 5 November 2026

Next step 5's remaining controls. [g360ctl](../src/lib/g360ctl/README.md):
- **What it is:** a small C library and the `g360-ctl` CLI;
- **buttons:** passive evdev read of `/dev/input/event0`; Power 73,
  Capture 126, Menu 127 from September's real presses;
- **LEDs:** the seven kernel LED-class devices `led.0`–`led.6`; save, set,
  blink, restore;
- **sound:** the firmware's WAV files through `paplay`, falling back to
  `aplay`. PulseAudio and `sound_server` run without the stock app.

Host tests pass, with five deliberate breakages caught; ARM build
`983e1ebd…`. Trial C1 needs the user present:
- an LED walk with each LED's number on the OLED;
- a blink, then restore;
- two sounds;
- 45 s of button presses.

Command: `python3 archive/g360ctl-trials/run-trial.py --tag C1`. No power
cycle: the mode follows the camera's state (`reenter` when the stock app is
already stopped, as it is on the unattended run's boot).

**C1 ran** (result (not published: `../output/controls-C1/RESULT.md`)), no power cycle:
- all seven LEDs were set, blinked and restored exactly;
- both sounds played through `paplay` with the stock app stopped;
- labels over 12 characters were refused, so the button prompt never
  showed and no presses were recorded.

**C1b** (result (not published: `../output/controls-C1b/RESULT.md`)), 10 s per LED:
- **LED map from the user:** 0 side orange, 1 side green, 2 side blue,
  3 front green, 4 back green, 5 front orange, 6 back orange.
- **Sounds:** both played again.
- **Buttons, 16 events:** Menu and Power arrive as zero-length pulses,
  Capture at 140 ms, and the unseen codes 116 (280 ms) and 215 (1.1 s)
  appeared; they await the user's account of the presses.

**C2 and C2e** (C2 (not published: `../output/controls-C2/RESULT.md`),
C2b–C2e (not published: `../output/controls-C2e/RESULT.md`)), guided prompts:
- **Menu:** short 127, hold 215.
- **Power:** short 73, hold 116.
- **Holds:** they send only the hold code, after about 1.5 s for Menu.
- **Capture:** 126 with real press and release times.
- **Two buttons together:** arrive independently.
- **Power hold:** 2 s is safe with the stock app stopped (user: only 5–10 s
  then powers off).

`g360ctl` names the hold codes.

Still open: exact hold thresholds; the devman LED setters in REA;
gestures; controls during a RAW capture.

## Unattended experiments completed — 4–5 November 2026

The owner authorized as many feasible tests as possible while absent, without
a power cycle, in a dark room. Allowance refreshed to 0% five-hour usage
(16% weekly) before acquisition. **309/309 full RAW captures passed across
44 sequential capture processes**, on unchanged boot `1793781685`, stock
inactive throughout. Independent ring observations match 309 still requests
and 1,236 ordered buffer returns; no capture failures/timeouts or reboots.
Results and exact limits (not published: `../output/experiments-live-20261104/RESULT.md`),
[compact evidence](unattended-20261104-evidence.json),
local archive (not published: `../retained-artifacts/unattended-experiments-20261104.md`).

Passed: saved baseline/reentry, manual boundaries, three balanced JPEG blocks,
both full HDR sweeps (8 + 6 normal stills, matching RTOS exposure/ISO reports),
manual and auto zero-gap 10 then 100-shot stress, saved post-stress frame,
three balanced library-log blocks and a combined fast RAW-saving case.
Skipping Linux JPEG copies/files saved 0.312 s / 7.03% mean per-still work
in its fixed-exposure RAW-saving cohort. The new CLI `--library-log off`
exposes the existing NULL log path; a separate matched in-memory cohort
measured 5.77% less work. Defaults remain JPEG save/log sync; RTOS encoding
continues. RAW hold remains ~0.65 s, with no copy/cache algorithm change.

The compact battery sampler selects named battery, preserving UNKNOWN.
3,014 samples reported Full/100/Good, AC online; raw temperature 400–420.
Passive input polling passed with no events; actual ARM option guards passed.
Physical button gestures, sounds/LED writes, charge transitions and other
native drives remain open. Darkness does not establish exposure response,
sensor precision, Bayer order or calibration. Recurring EXT4 faults map to
broken root systemd entries; root remained read-only, SD writable, pinned
libraries matched. No repair, service restart or known stalled-route retry.

Builds: first 283 captures used `c08394ca…`; the logging option's ARM build
`767d43df…` passed the last 26 captures. Host ASan/UBSan/fake-SDK/C++ checks
and all 14 experiment tests passed. The CLI timing summarizer keeps sync/off
cohorts separate; HDR guards normalize embedded console NULs.

All owned observers exited naturally or received identity-guarded SIGTERM
after capture completion. Verified local retention preceded removal of all
44 case directories, observer directory, staged helpers and transfer tar.
Final camera check: same boot, stock inactive/MainPID 0, no capture/observer
workers, battery Full/100/Good, SD 65% used. Original passive entry PID 337
is untouched; shared locks remain unlocked. Settings restored to Program,
ISO Auto, WB Auto, Superfine, Single, Smart Range On, ISO NR Mid, LTNR On.
TEST MODE is the last written screen. No workers/native sessions remain.

Updated facts: `experiments.jpeg-omission-faster-20261104`,
`process-reentry-without-power-cycle-20261104`, `frame-copy-speed-and-stress-20261104`,
`full-hdr-range-with-frame-copy-20261104`, `named-battery-provider-observed-20261104`,
`optional-library-log-timing-20261104`, `passive-input-and-arm-guards-20261104`
(all in the `experiments` namespace). Main-app/battery-controller goals stay
open. Next useful edge: profile the RAW hold under the same ownership checks;
an attended session is needed for physical control/charge-state acceptance.
Validated changes are integrated from `codex/unattended-experiments-20261104`
into local `master` (see the task commit in Git); no push requested.
Final allowance check: 60% five-hour / 25% weekly used, reset
5 November 2026 at 04:18:35 Australia/Sydney; no reset credit consumed.

## Screen trial passed: clear and centered label — 4 November 2026

The owner connected the camera and authorized testing. Read-only preflight
matches ARM, capture/bridge/app hashes; stock is active (PID 254), no capture
or screen/controller helper is running. A passive `gear360-entry` listener
is present and will be left alone. Boot time is `1793781685`.
The bounded trial gracefully stopped stock, verified its absence, and
successfully called full clear and centered `TEST MODE` (both returns 0).
No reboot; boot time stayed `1793781685`. The owner confirmed `TEST MODE`
is centered and the old screen is gone. Result (not published: `../output/screen-live-20261104/RESULT.md`), fact
`experiments.screen-clear-center-driver-returns-20261104`.

The first multiline CGI request was a parser refusal before execution;
the verified staged script then ran successfully. Post-check reported two
EXT4 lookup errors on `mmcblk0p9` from systemd during the stop, with cause
and impact unreviewed. Review these before expanding tests. No capture or
native analysis occurred. Staged files/directory were removed, the screen
lock released and stock left inactive. No owned workers/sessions remain.

Five-hour usage was 93% at startup, 97% at the next check; stopping at the
runbook reserve, reset 4 November 2026 at 23:18:09 Australia/Sydney.
Next: filesystem-error review, then the prepared
baseline/reentry tests when account allowance permits.

## Screen takeover preparation — 4 November 2026

At the owner's request, the existing 72×32 renderer now supports `--clear`
and `--center TEXT`. The ARM experiment build excludes stock-process bitmap
reads and uses the existing direct driver path. The session wrapper verifies
the renderer hash/message before stopping stock, excludes known UI controllers,
takes the screen-owner lock and writes a full blank-background centered
`TEST MODE` only after confirming stock exit. A failed draw/update prevents
capture. It uses the same graceful stop, no forced kill/restart or restore.

The 7 HDR integration tests and 12 experiment tests pass; ARM renderer build
SHA256 is `14043a98d6e62418255f974fa4c998c22a59f6c54e1b0663cd8015c582ea5add`.
Updated local kit: `output/experiment-kit-screen-20261104/`. Source and test
fixtures are committed and integrated locally; binaries/previews stay ignored.
The [screen runbook](../tools/experiments/README.md#clear-and-label-the-screen-after-stopping-stock)
records commands, text limits and the hardware check. Fact
`experiments.main-app-controls-with-capture-20261104` stays open: no camera
contact, native analysis or on-camera draw occurred. First live check is
correct clear/text orientation and no competing redraw after stock exits,
before the saved-frame baseline/reentry trials. No owned worker, native
session or temporary camera resource exists.

Five-hour usage: 90% used/10% remaining at wind-down, same reported reset
4 November 2026 23:18:09 Australia/Sydney. The first usage query stalled;
the bounded retry succeeded. This was a bounded offline source change.

## Offline experiment preparation — 4 November 2026

The owner asked to prepare/build for all six next steps in `PROJECT_GOAL.md`;
the camera is unavailable. Phase 2 stays finished. Current scope is **offline
preparation only**, with no camera contact or new native-instruction claims.
See [the experiment kit](../tools/experiments/README.md) and
[validation checkpoint](../tools/experiments/VALIDATION_20261104.md).

Prepared: JPEG save/discard/skip-copy policies preserving buffer give-backs;
per-shot timing; guarded fresh/reentry scripts; 10/100-still stress cases;
canonical HDR 8/6-shot sequences and boundary case; passive ARM button observer;
bounded read-only platform/battery inventory; staged controls/charging protocols.
Source and tests are integrated locally with this checkpoint; generated binaries,
scripts and hash manifest are in ignored `output/experiment-kit-20261104/`.

New open fact IDs are `experiments.jpeg-omission-faster-20261104`,
`experiments.process-reentry-without-power-cycle-20261104`,
`experiments.frame-copy-speed-and-stress-20261104`,
`experiments.full-hdr-range-with-frame-copy-20261104`,
`experiments.main-app-controls-with-capture-20261104` and
`experiments.battery-status-controller-20261104`; see
[their records](facts/contracts/experiments.yaml). None is a new runtime result.
Encoder bypass, other native drives and actual app/battery controller contracts
remain future work, not completed features.

Next camera edge: inspect a saved two-still baseline, then start a separate
process with stock still absent and the same expected boot time. Stop on failure;
no stock-service restart or blind request retry. No camera/native sessions,
workers or temporary camera files were created by this pass. No persistent goal
was present or created; no remote push was requested.

Host capture checks and 10 experiment tests passed; 26 existing observation
tests passed. Capture, passive buttons and existing observation helpers built
for ARM; kit generation verified all 20 bundled file hashes (15 capture cases).
Five-hour account usage at wind-down: 84% used, 16% remaining; reset
4 November 2026 at 23:18:09 Australia/Sydney. Weekly usage: 13% used.

## Repository cleanup — 4 November 2026

Phase 2 remains finished; no next product step or camera operation was started.
The owner approved preserving source and compressed private firmware/evidence,
removing captured sensor RAW/pixel dumps, and reclaiming redundant disk usage.
See cleanup record (not published: `../REPOSITORY_CLEANUP_20261104.md`),
retained artifact restoration (not published: `../retained-artifacts/README.md`), and the
[Phase 2 handoff](handovers/PHASE2_COMPLETE.md). Historical paths are preserved.


## Phase 2 finished — 4 November 2026

The user marked Phase 2 finished after trial F2 (two normal stills in one
session, each with the full RAW frame of both lenses). Nothing is open
here. The next steps are recorded in
PROJECT_GOAL.md (not published: `../PROJECT_GOAL.md`)
and are not started:
- optional JPEG (only if it saves time);
- no power cycle between sessions;
- the frame hold's speed;
- the HDR page's exposure range;
- a main-app replacement (screen, sounds, LEDs, buttons);
- a battery controller.

## Plan of record: ACTIVE_PLAN.md (not published: `ACTIVE_PLAN.md`) — 2 November 2026

The user adopted a new plan after a whole-project review: **observe first,
then replay what works.** Every still from our own client has killed the
camera (eleven trials) and no trial captured what the RTOS said before it
died. The plan's steps, in order: (1) read the RTOS log and capture state with
Samsung's own `st cap log` / `st cap seq` / `st cap capmm show` commands;
(2) record every Linux↔RTOS message during a stock still; (3) control test:
stop the stock app, start it again, take a stock still; (4) make our client
replay the recorded sequence, then trim it; (5) test the NX
`IMAGEQUALITY_RAW*` setting. Static RTOS decompilation is paused unless an
observed log line points at a function, so the `0x8027510c` edge below is set
aside. The status of each step lives in the plan's table, and the newest
results are recorded in sections directly below this one.

The 2 November handoff prompt
(HANDOFF-MINIMAX-20261102.md (not published: `HANDOFF-MINIMAX-20261102.md`)) is superseded by
this plan.

## Trial F2 passed: two stills, two full RAW frames — 4 November 2026

After a power cycle, with the user present, from `master` (the MiniMax and
Codex frame-pause branches merged; build `af2b7f15…`):
result (not published: `../output/decompile/rea/pass-frameF2/RESULT.md`).
- Two normal stills in one session. Each copied the full frame of both
  lenses in the expansion callback, unchanged during the copy; the camera
  waited 688.7 and 619.5 ms.
- Capture-ready, complete JPEGs, eight give-backs; live view restarted
  after each still; no reboot.
- libcapture's log confirms our callback ran before the Beauty object's
  gyro answer.
- The frames' halves match the JPEG halves (0.71/0.68). A GRBG binned
  preview shows both lenses with natural colours.

Open: the hold in other recipes and modes; the frame's exact format, black
level and colour order (measured only by eye and by stored bits).

## F2 stopped at the starting-state guard — 4 November 2026

The user explicitly authorizes the prepared two-still test while away
("I am not around ... you can go ahead"). The presence requirement is
waived for this F2 test; no repeat permission request is needed. State/hash
checks and the ban on restarting the stock app still apply.

Preflight result (not published: `../output/decompile/rea/pass-frameF2/RESULT.md`): the
camera answers; ARM architecture and both library hashes match the local
firmware. At uptime 3117.75 s the stock service is inactive/dead, MainPID 0;
the process listing contains no camera app, capture client or recorder.
Boot time exactly matches MiniMax's successful F1 run, which stopped the
stock app and exited normally. So the camera has remained in that prior
run's post-capture state, without a fresh stock session.

The prepared runner requires an active stock session before its controlled
stop. It was not invoked past that guard: **zero stills, no staged helpers,
no new device files, no service restart or recovery attempt**. Reopening a
new hosted capture process in this historical post-capture state is not
validated by F1 or the same-process two-still host tests; it was not assumed
safe merely because the camera is idle.

**Next:** after a physical power cycle restores the normal stock shooting
session, recheck identity and run the already-authorized F2 command. The
user is away, so this pass stops at that concrete recovery dependency.
Fact `session.frame-f2-preflight-stopped-app-20261104` records only the
observed state; no RAW/completion claim advanced. Work remains on
`codex/frame-pause-continuation-20261104`; master and Claude's files are
unchanged. No workers/native sessions or camera cleanup remain.

## Codex continuation: reviewed F1, hardened the frame copy — 4 November 2026

Branch `codex/frame-pause-continuation-20261104`, worktree
`<repo>-codex-frame-pause-20261104`.
Based on all six MiniMax commits through `4eb99135`, above master
`be7cf3a1`. The six dirty source files in the original checkout match the
MiniMax tip byte for byte, so nothing was copied or changed there. The user
explicitly forbids merging into master; this pass commits on its branch
only, with no merge or push.

Reviewed checkpoint (not published: `../output/decompile/rea/pass-frame-review-20261104/RESULT.md`):
- Source: reject overflowing/oversize RAW layouts before arming or sending;
  validate descriptor size/readability and orientation mode 3, ignore
  duplicates, and recheck the physical mapping in the expansion callback.
- ASan/UBSan host suite and ARM build pass. Direct API tests hold the first
  frame through source reuse, a second capture and close, then free it.
- Corrected the offline grid checker. Rereading the saved F1 bytes gives
  same-half correlations 0.7438 / 0.6637 and full-file minimum 3072.
  Six cleared low bits are a stored-word observation, not ADC calibration.
- Fact `raw.frame-f1-offline-review-20261104` is **open**; no new native
  claim is accepted. Connected REA tools are not exposed in this session.

**Next:** [F2](CAPTURE_LIBRARY_PLAN.md#trial-f2-two-full-frame-stills-in-one-session--prepared-not-run),
two normal stills with `--frame`, full RTOS/IPCC recording and the user
present. Prepared but not run; no camera commands were sent in this pass.
The new descriptor/mapping guards also still need that target check.

The bounded MiniMax checker worker completed in a sanitized offline packet;
its source was reviewed and tests rerun. No worker or native session remains
owned, and no camera files need cleanup. Initial five-hour usage was 2%
used / 98% remaining; final usage/reset is in the checkpoint. No persistent
goal is attached to this chat; Phase2 remains unfinished.

## Trial F1 passed: both lenses in one still — 4 November 2026

Result (not published: `../output/decompile/rea/pass-frameF1/RESULT.md`). One normal still
with `g360-shot --frame`, on the camera, the user present.

- **The camera did not fault and the still completed** (`exit: all stills
  succeeded`, the four give-backs with the types 5, 7, 8, 9).
- **The copy happened in the window the trace predicted.** Our client
  registered before the Beauty object, the expansion callback arrived with
  the descriptor's size `0x3c8` (as fact `host.expansion-request-to-linux-client`
  says), and the copy took 651 ms. The IPC record shows the RTOS's gyro
  request at tick 8,534,000 and our reply at 9,216,400: it waited **682 ms**
  for us. The lens passes started 1.6 ms after the reply (the trace had
  measured 1.8 ms).
- **The frame did not change while it was copied** (`changed_during_copy: no`).
- **Both lenses are in it.** The frame at `0xa6000000` is the sensor's
  readout: the RTOS log shows the PP core reading exactly that buffer
  (`rdma0 addr=0xa6000000, stride=11648`, and the second lens at
  `0xa60016c0`, one row in). Each half of the frame matches the matching
  half of the JPEG (correlation 0.696 and 0.652, off-diagonal −0.109 and
  −0.129).
- **A Bayer mosaic, order GRBG** — the same order trial Q1 found for the PP
  core's corrected per-lens RAW. The values are multiples of 64: the
  sensor's 10-bit readout in the top of the 16-bit words, where Q1's
  corrected RAW had full 16-bit values. So this is the raw sensor data, not
  the PP core's output.

The check is reproducible with
`tools/observe/frame_check.py` (phase means, the half-to-half
correlation table and the four Bayer-order previews).

**Still open:** the pause was 682 ms — acceptable for this recipe, but a
recipe that waits on something else may not tolerate it. Two stills in one
session with the frame armed have not been tried. The frame is the sensor's
readout, so the PP core's corrections (lens shading, crosstalk, defect
correction) are not in it; a development step on the Mac would need them.

## Step R2 built: the frame copy in the expansion callback — 4 November 2026

Picking up the work another agent (Claude Opus 5.5) left uncommitted on
`master` (commit `be7cf3a1`, "the gyro hand-off holds the still with the full
RAW frame intact"), now continued on branch
`minimax/frame-pause-20261104` (worktree `.worktrees/minimax-frame-pause-20261104`).

**What that work was.** The uncommitted diff is step R2: the frame copy in the
expansion callback. `g360cap` gains `g360_open_options.frame_pause` (register
the callback client *before* the capture library's Beauty object, so our
callback runs first on the expansion topic), `g360_capture_options.frame` plus
`frame_layout`, a `g360_frame` result, `g360_maps_physical()` (find a physical
range inside a readable `/dev/mem` mapping in `/proc/self/maps`), and
`g360-shot --frame` / `--frame-layout`.

**Checked here, against the facts:**
- The expansion arm of libcapture's `OnCallback` calls the registered client
  as `(0x04010008, 0, descriptor, 0x3c8)` — the descriptor is the *third*
  argument, which is what the code uses
  (fact `host.expansion-request-to-linux-client`).
- The descriptor's word at `0x3c0` is the orientation discriminator
  (fact `host.gyro-expansion-request-and-auxiliary`); the code reads it as
  the "mode".
- The frame is copied from a mapping the camera's own `/dev/mem` mapping
  covers, and the copy is re-read afterwards; the source range is checked
  against the process's readable mappings before it is read, so a wrong
  address cannot fault the camera (the kernel reboots it on SIGSEGV).
- The client registration order is the one the pause trace relies on:
  `register_client()` now runs before `beauty_fn()` in `g360_open`.

**Added here:**
- `run_still_trial.py` gained `--g360-frame` and `--g360-frame-layout`, so
  trial F1 can be run with the standard runner; the summary now also reports
  a fetched `*-frame.bin`.
- A host test for the whole path (the expansion topic published by the fake
  capture library, the frame copied and saved, and the negative case).
- `g360cap/README.md` documents the new option and its contract.

**Built:** the ARM binary (sha256 `0a74f670d24853de41c0768645e37d1c282e02713fa5b69aa1cba9b131480ea4`).
The code pass is recorded in
pass-frame-copy-code-20261104 (not published: `../output/decompile/rea/pass-frame-copy-code-20261104/RESULT.md`).

**Still open:** trial F1 ran later the same day and passed — see the section
above.

## A pause with the full RAW frame intact — 4 November 2026

The user wants one normal still giving both lenses' RAW (or the combined
frame), without cross-still buffer tricks.
Trace (not published: `../output/decompile/rea/pass-expansion-pause-20261104/RESULT.md`):
- **The pause.** After the full frame is written at `0xa6000000`, the
  RTOS's SSIF stage publishes STL_EXPANSION and waits, with no timeout, for
  the gyro reply. The lens passes start 1.8 ms after the reply in every
  recorded still.
- **Who replies.** libcapture's Beauty client (`fd_alg`,
  `captureProcessA9`), in our process.
- **Order.** libcapture calls the expansion subscribers in subscription
  order, synchronously. If our client subscribes before the Beauty object
  is created, our callback runs first. While it copies the 33.7 MB frame,
  the still waits with the frame intact.

Proposed step R2 (built on 4 November — see the section above): reorder
start-up, find the frame through `/proc/self/maps`, copy in the expansion
callback; trial F1.

## The picture over lens 1 — 4 November 2026

Trace (not published: `../output/decompile/rea/pass-temp-picture-trace-20261104/RESULT.md`)
(static):
- `RecipeNormal` runs SSIF → lens PP ×2 → Ipc/Ldc/3dLut per lens →
  Stitch Main → **Resize** → Jpeg.
- The Resize stage creates a new 5792×2896 main image (the JPEG input),
  allocated first-fit from the bottom of the pool.
- The development stage (Ipc) releases its source lens image in a normal
  still (mode 1). Only HDR, smart and similar modes skip that release.
- The main size changes only with order-sheet flags or image size
  DUAL_SMALL, which libcapture refuses to set (API attribute 2 → −1).

So no normal-still setting keeps lens 1. Untested lever: what our process
still holds (unreturned buffers of a previous still) when the Resize image
is allocated. That needs two stills per session in quick-view hold mode.

## Trial Q2: the full frame is not kept; lens 2 repeatable — 4 November 2026

Result (not published: `../output/decompile/rea/pass-rawQ2/RESULT.md`): with the
quick-view hold, `0xa6000000` again holds the stitched YUV picture
(correlation 0.990 with the JPEG) plus the RAW frame's last rows. Lens 2's
RAW arrived intact again.

Comparing with R1, where live view took `0xAB134000` while the JPEG was
still encoding: the full-size picture seen in lens 1's memory is
temporary, written after the stitch and freed by the end of the QView
work. It fits exactly between lens 1's start and lens 2's start, so lens 1
is lost in every mode.

Next options:
- find what that temporary picture is and whether anything moves it
  (static);
- make the one-lens RAW usable: the library accepts no capture-ready in
  hold mode, and ends the quick-view state so the next still works.

## Trial Q1: one lens's RAW, natively — 4 November 2026

Result (not published: `../output/decompile/rea/pass-rawQ1/RESULT.md`): with quick view
HOLDZOOM (libcapture attribute 30) the RTOS did not restart live view.
**Lens 2's corrected RAW at `0xad134000` arrived intact:**
- it matches the JPEG's right half (correlation 0.95);
- GRBG mosaic, 16-bit, dark level about 520, saturating at 65535;
- 32 bytes of padding per row.

Lens 1's memory was reused within the same still:
- the RTOS placed the main JPEG at `0xAB134000`;
- a 5792-wide stitched brightness plane follows it.

No capture-ready came, so the library latched the session after 10 s.
Quick view was restored to 1SEC.

Next was trial Q2 (above).

## What could keep a still's RAW — 4 November 2026

Trace (not published: `../output/decompile/rea/pass-raw-keep-trace-20261104/RESULT.md`)
(static, REA on the RTOS, plus R1's log):
- the stitch step (`CMaterial_NX1_Still_Etc`) writes into its own stage's
  output buffer, which the capture memory manager hands out first-fit from
  one pool at `0xa6000000`, so the full RAW frame's blocks get reused; no
  setting found keeps them (`SetPreventRawClear` only guards a counter);
- the two lens images at `0xab134000` and `0xad134000` are corrected RAW,
  one per lens (lens shading, crosstalk, defect correction on the PP core;
  2896×2896, 2 bytes per pixel). The stitch does not touch them; only the
  live view restarted after the still does, and its first write comes about
  100 ms after the image set;
- `CDriveSingle::restartLiveview` does not restart live view when the
  quick-view time is HOLDZOOM (RTOS `0x330005`; libcapture attribute 30 =
  `0x1e0005`, wire `0x2b`), unless one of four checks applies
  (monitor-out, custom-WB mode, a smart mode, boot shot).

**Trial Q1 prepared (4 November):** `g360-shot --raw` now copies both
lens images (`-raw1.bin`, `-raw2.bin`, before the JPEGs) and `--quickview
holdzoom --restore-quickview NAME` sets the quick-view time around the
still; host-tested (four deliberate breakages caught), ARM build
`ff494ec2…`. The runner reads the camera's current quick-view value first
and sets it back, and reports each lens image against its half of the
JPEG. See CAPTURE_LIBRARY_PLAN "Trial Q1".

## Trial R1: the RAW frame is overwritten before the image set — 4 November 2026

Result (not published: `../output/decompile/rea/pass-rawR1/RESULT.md`): the still and its
JPEGs worked as in 4z-a, but what `--raw` copied from `0xa6000000` is the
still's finished YUV 4:2:2 picture (5792×2896, matches the JPEG), not RAW.
The RTOS's stitch step ("End of 1st/2nd image copy") writes it there about
120 ms after the RAW frame, before the image set is sent; live view then
takes the lens-image memory. Only the last 15 RAW rows survive (16-bit
words, top 10 bits used, dark level 4096). The library's range check
caught it and reported the still as failed. RAW quality (5a) runs the same
steps. The earlier note's "untouched until the next still" is corrected.
A native route needs the stitch step to write elsewhere or the RAW to be
kept; traced next (above).

## RAW copy in the library — 4 November 2026

g360cap (`g360_capture_options.raw`, `g360-shot --raw`) copies the still's RAW
frame from capture memory (physical `0xa6000000`, virtual address derived from
the image set's own physical/virtual pair) after the set arrives and before
the buffers go back. Checks: readable range, source spots re-read after the
copy, all samples within 12 bits. Saved as `-raw.bin` plus a description.
Host-tested (including two deliberate breakages); camera build ready. The
runner fetches the frame and reports statistics, Bayer-phase means and a
preview. Next: trial R1 (one normal still with `--raw`).

## Where a normal still's RAW frame lives — 4 November 2026

Note (not published: `../output/decompile/rea/pass-raw-frame-lifecycle-20261104/RESULT.md`):
in all nine recorded stills the RTOS wrote the sensor frame (both lenses) to
physical `0xa6000000`, 5824×2896, 12-bit, stride 11648, then processed its
two halves; the live view restarted afterwards uses `0xAB134000`. The frame is
in the capture memory our process already maps and is untouched until our
next still. No RTOS mechanism hands it over (image-set first image only for
RAW-capable recipes; maker note = calibration; `SetPreventRawClear` = a
counter flag). Proposed: g360cap copies the frame after each still, with an
overwrite check, and saves it.

## Trial E3: the long-exposure still never gets its frame — 4 November 2026

Result (not published: `../output/decompile/rea/pass-expoE3/RESULT.md`): with a 90 s wait
and nothing changed mid-capture, the 5 s RAW still still delivered nothing.
Long exposures use a dedicated capture readout (`CapInit eCapOption(1)`,
`capExposure 4967.431ms`, `CapFrameReadout`, zero-size processing windows)
whose frame never arrives; normal stills read from the running stream
(`VideoFrameReadout`). The long-exposure RAW route is blocked at 5 s. **The
user rules out debug routes (sequencer or front-end test commands): RAW must
be captured natively.** Next: trace a normal still's RAW frame (the three
"Raw Work" items) from sensor write to release, and the image-set topics that
come with every still, to find a supported hand-over.

## Trial E2: long exposure picks the RAW-capable recipe, then stalls — 4 November 2026

Result (not published: `../output/decompile/rea/pass-expoE2/RESULT.md`): with RAW_SUPERFINE,
LTNR on and TV −37, the RTOS chose the long-exposure recipe
(`CMaterial_PP_Core_Still_LTNR_Image`) and set `m_uiExposureTimeStill`
4 967 431 µs, reconfigured the sensors, and then logged nothing more for ~40 s
(no readout, no frame-end gyro request, no set). No reboot; settings restored.
Next options: repeat with a 90 s timeout; find the longest exposure that
completes; trace what the long still waits for.

## Trial E1: manual exposure works — 4 November 2026

Result (not published: `../output/decompile/rea/pass-expoE1/RESULT.md`): three stills at TV
160, 96 and 32 (ISO 100). The RTOS logged `SHOOTINGMODE_MANUAL`, `ISO_100` and
`m_uiExposureTimeStill` 976, 15 625 and 250 000 µs, exactly the requested
times; the images' mean levels rose 2 → 17 → 105. Settings restored; no
reboot. Next: E2 (TV −37, about 5 s, at RAW quality with LTNR on).

## Exposure control in the library — 4 November 2026

The user chose exposure control first. [Static
note](../output/decompile/rea/pass-exposure-control-20261104/RESULT.md): the
settings the September HDR scripts set from the camera shell (dial 4,
shooting mode MANUAL, ISO, `setvar TV`) are reachable from our process
through `mm_camera_set_usr_attributes` (ids 0, 1, 4, with libcapture's own
value shifts) and `mm_camera_set_var_attributes(0x101, TV)`. g360cap gains
`g360_set_manual_exposure`/`g360_set_auto_exposure` and `g360-shot --exposure
TV,ISO[:…]` (one still per entry), `--ltnr`, `--isonr`; host-tested. Trials:
E1 (1/1000, 1/64, 1/4 s) then E2 (5 s at RAW quality, the long-exposure
recipe). Caveat: September saw no brightening beyond about 1/5 s.

## Why the bracket did nothing; remaining RAW routes — 4 November 2026

Trace (not published: `../output/decompile/rea/pass-bracket-command-20261104/RESULT.md`): the
RTOS dispatches `CAPTURE_COMMAND_STILL_BRACKET` (id `0x27`) to the empty base
handler `CHandlerNX1`, whose methods return 0; no bracket command class exists
on this firmware. Brackets cannot run, so `RecipeBracket` is unreachable.
Remaining routes to a RAW-compression stage: the long-exposure (LTNR) recipe
(RAW quality, LTNR on, exposure ≥ ~4.9 s), or the sequencer debug command
`st cap seq order [OrderID] [recipe]`, which forces the next still's recipe.
Needs a user decision.

## Step 5d: the WB bracket command is accepted, then idle — 4 November 2026

Result (not published: `../output/decompile/rea/pass-step5d/RESULT.md`): with the bracket
words, the RTOS turned our request into `E_CAP_CMD_STL_WB_BRACKET` (drive
`DRIVE_WBBRK`) and then did nothing for 15 s; Linux got no notification. No
reboot. Likely waiting for a 3A lock (our working request locks 3A itself).
Next: trace what the bracket command waits for. `g360-shot` now sets
`DRIVE_SINGLE` back after a bracket capture type.

## Step 5c: the WB bracket request needs its bracket words — 4 November 2026

Result (not published: `../output/decompile/rea/pass-step5c/RESULT.md`): with capture type
`6` and zero words 11–14, `mm_camera_capture_still` returned −3 and sent
nothing. Static: a WB bracket requires word 13 = `0x1a0000`–`0x1a0005` (AB3 …
MG1) and word 14 = 1 or 2 (axis). The Gear 360 app never takes WB brackets.
g360cap now has `--bracket-words`. Next: trial 5d with `0,0,0x1a0000,1`.

## Step 5b: the still request resets the drive mode — 4 November 2026

Result (not published: `../output/decompile/rea/pass-step5b/RESULT.md`): the RTOS accepted
`DRIVE_WBBRK`, then our still request (capture type `0x15`, lock-3A-then-
capture) set `DRIVE_SINGLE` again, so a normal still ran and its RAW-quality
set was dropped as in 5a. The library failed fast and restored both settings;
no reboot. The bracket has its own capture type (mm `6` = WBB); g360cap now
has `--capture-type`. Next: trial 5c (RAW_SUPERFINE + capture type WBB).

## Drive mode → recipe, and multi-set stills — 4 November 2026

Drive → recipe (not published: `../output/decompile/rea/pass-drive-recipe-20261104/RESULT.md`)
(static): the drive setting becomes the sheet's capture type (`+0x30`), which
picks the recipe id. **`DRIVE_WBBRK` and `DRIVE_PWBRK` select
`RecipeBracket` (id 10), which adds the RAW-compression stage at a RAW
quality; `DRIVE_AEBRK` uses the normal recipe (no RAW).** The bracket runs one
RAW and sends one image set per frame. Our library can set the drive with
`mm_camera_set_usr_attributes(handle, 8, 0x80005)`.

g360cap now copies several image sets per still (`g360_capture_shot`), gives
back extra and late sets, fails fast (`G360_E_NO_SET`) when the camera
reports ready without a set, and sets the drive mode; host-tested, camera
build ready. Next: trial 5b (RAW_SUPERFINE + WB bracket), user present.

## Why step 5a had no RAW — 4 November 2026

Trace (not published: `../output/decompile/rea/pass-raw-compress-trace-20261104/RESULT.md`):
the RTOS's normal single-still recipe never builds the RAW-compression (LRC)
stage, so at a RAW quality its set is always dropped. The shared processing
page `RECIPE_PAGE_PP` builds it whenever the quality is RAW, and the bracket
(`RecipeBracket`), HDR and multi-exposure recipes use that page. The drive
mode (`DRIVE_AEBRK` = `0xa0008`) is the likely switch to a bracket recipe;
the dispatch is not traced yet. Next: trace it, then make g360cap accept
several image sets per request, then a bracket + RAW trial.

## Step 5a: RAW quality accepted, set dropped — 4 November 2026

Result (not published: `../output/decompile/rea/pass-step5a/RESULT.md`): g360cap set
RAW_SUPERFINE (the RTOS logged it), the still ran, and the RTOS's image-set
builder took the RAW branch but found no "Raw Compress" image, so it dropped
the set ("Raw Compress Address is NULL!!"). No set reached Linux; the library
timed out, set SUPERFINE back, and the camera stayed up. Next: trace
statically what produces the RAW-compression image for a still (the
builder's input `+0x4`).

## Step 4z-b: two stills in one session — 4 November 2026

Result (not published: `../output/decompile/rea/pass-step4z2/RESULT.md`): both stills
succeeded (six valid JPEGs, eight give-backs, buffers reused by the RTOS, no
fault). Step 4z is done. Next: step 5a, the RAW quality trial.

## Step 4z-a: the library takes a still and saves it — 4 November 2026

User-present trial
(result (not published: `../output/decompile/rea/pass-step4z1/RESULT.md`)): `g360-shot`
(g360cap) took one still with the stock app stopped, saved the main
(5792×2896), screen and thumbnail JPEGs (all decode), and returned the four
buffers with the same types, order and physical addresses as the stock app's
own still. No fault; the camera stayed up. The boot ID turned out to be
restored from the hibernation snapshot on every boot, so reboots are judged
by boot time and uptime. Next: 4z-b (two stills), then step 5a (RAW quality).

## Capture library plan and the stock buffer returns — 4 November 2026

The user asked for a library that captures a still and saves it (later, a
stock-app replacement on top of it). Plan:
[CAPTURE_LIBRARY_PLAN.md](CAPTURE_LIBRARY_PLAN.md), linked from
ACTIVE_PLAN as step 4z.

Static result
(stock release (not published: `../output/decompile/rea/pass-stock-release-20261104/RESULT.md`)):
- **`libmm-photo.so` reads and returns the stock still's images,** not the
  stock app's own code.
- **Its image-set callback copies only descriptors** and returns. A worker
  saves the file, then calls `ReleaseCaptureBuffer` with the virtual
  addresses: maker note 5 (`set + 0x2e0`), RAW 1 (only when present),
  main 7, screen 8, thumbnail 9.
- **Each JPEG buffer is complete** (`FF D8` … `FF D9`), and the size word is
  its exact length.

Facts: `session.stock-photo-library-releases-buffers-after-save-20261104`,
`session.still-jpeg-size-word-is-exact-length-20261104`.

**Library code (4 November):** [g360cap/](../src/lib/g360cap/README.md) is the
capture-and-save library: `g360_open`, `g360_capture` (blocking: still, copy,
give back, capture-ready), `g360_save_image`, plus the `g360-shot` program.
Host tests pass with a fake capture library; the camera build is
`output/g360cap-build/g360-shot`. Next: camera trial 4z-a (one still), then
4z-b (two stills), with the user present:
`run_still_trial.py run --tag step4z1 --probe output/g360cap-build/g360-shot --g360 1 --net only`.

**RAW quality (step 5), static (4 November):**
RAW quality route (not published: `../output/decompile/rea/pass-raw-quality-route-20261104/RESULT.md`).
g360cap sends `mm_camera_set_usr_attributes(handle, 3, 0x30004)`; the RTOS
logs it as `SetUserData`, converts it to `RAW_SUPERFINE`, and the order
sheet should then give set type 3 (first image plus JPEGs). The override key
is `0x134` (`VARIABLE_PROSUGGEST_SAMPLE_CAPTURE`), not decimal 134. Trial
`step5a` is prepared for after 4z-a.

## Step 4y: fixed — our client's still survives — 3 November 2026

User-approved trial:
- **The check:** the APEX gate in a no-still client next to the stock app
  found the magic, set our private copy and survived the max-aperture call.
- **The still:** with the stock app stopped, our probe
  (`output/stock-like-apex-build`) sent the still. It received the image
  set, "next shot ready" and "capture ready". The kernel logged no fault,
  and the camera stayed up.
- **Step 4 is done.**

**Next:** read the image data and return the buffers like the stock app; a
second still; then step 5 (RAW). Note
`output/decompile/rea/pass-step4y/RESULT.md`.

## Step 4v: cause found — our receiver segfaults on the image set; the kernel reboots — 3 November 2026

User-approved lethal trial with the recorders streamed over Wi-Fi (no SD
lag). The camera died and was power-cycled.
- **What the kernel log shows:**
  `MCB_Receiver: unhandled page fault (11) at 0x000006f0` in **our probe**
  (Thumb, `r2 = 0`), 0.3 ms after the RTOS sent the image set, then
  `CA9 fault occurs, reboot now!!!`. That reboot is the "death"; the RTOS
  was fine.
- **Where:** recovered under the only library shift that fits PC and LR:
  `CApex::GetMaxApertureValue` (`libapex` +`0x1d9e`,
  `ldr.w r3,[r2,#0x6f0]`), called from `Apex_Get_Max_Av_Value_For_Capture`
  (`libuser-spec`), called from the capture library's image-set handler
  `CCapImageSetCb::OnCallback`.
- **Why:** the pointer it reads is set only by `Apex_Initialize_A9()`. The
  stock app calls that at start-up; our probe does not.
- **Rejected:** "the JPEG encode never finishes in deaths". The SD reader
  stopped before the reboot.

**Fix built (not yet run):** an APEX gate in `one-still-probe.c`.
- **What it does:** it gives our process's APEX singleton a private copy
  of the user-spec block, after checking that the block is mapped and that
  its magic `0x330033` is present.
- **What it avoids:** `Apex_Initialize_A9()` would write pointers into
  shared memory.
- **Builds:** `output/stock-like-apex-build` and
  `output/stock-like-apex-suppress-build`; host tests pass.

**Next:** the gate in a no-still client next to the stock app, then the
test still. Note `output/decompile/rea/pass-step4v/RESULT.md`.

## Checks A and B: streaming works; IPC thread scheduling identical — 3 November 2026

Non-lethal:
- **A:** UDP streaming from the camera reaches the Mac.
- **B:** our probe's IPC threads run with exactly the stock app's
  scheduling (`udd_ipcc_fmt_in` SCHED_RR 60, MCB threads nice −10, CPU 0),
  so scheduling is ruled out and option E is dropped.

Note `output/decompile/rea/pass-sched2/RESULT.md`.

## Trial options prepared for the next camera session — 3 November 2026

Static preparation, no camera:
- **Streaming:** `rtoslog`, `regwatch` and `irqwatch` can stream over UDP to
  the Mac (`--net`).
- **New runner modes:** `sched` compares thread scheduling; `netcheck` and
  `jpegcheck` are idle tests.
- **New run options:** `--probe-sched` runs our probe at a chosen real-time
  priority, and `ptstop` can freeze the stock IPC threads after the gyro and
  the next adjust (`--freeze-mode after-gyro-adjust`).

The menu (risk and question per trial) is in `ACTIVE_PLAN.md`. Note
`output/decompile/rea/pass-trial-prep-20261103/RESULT.md`.

## What can reset the image engine during the restart — 3 November 2026

Static (REA), user away:
- **Ruled out:**
  - the chip-level reset of IP 5 in the liveview start. It runs only after
    a 2-second frame error, and no run logged one;
  - logged assertions. None appears in any record. An assertion would set
    the A7 fault flag at physical `0x85180334` and notify Linux.
- **Follow-up:** blocks 10/11 are the shared DMA muxes, and the JPEG
  encoder (own DMA) does not use them, so this lead is unlikely. The
  completion's first blocking point is the image-engine driver's lock 3.
  Nothing static found so far depends on the Linux side.

Note `output/decompile/rea/pass-rtos-epreset-20261103/RESULT.md`.

## The RTOS's image-engine interrupt dispatch — 3 November 2026

Static (REA):
- **The dispatcher:** the RTOS installs handlers on the same four
  image-engine lines as Linux (46, 47, 48, 50). Its acc dispatcher acts
  only on bits in its own shadow mask.
- **The error path:** the JPEG error callback leads to a retry (JPEG-block
  software reset, then a re-encode), which is not visible in the logs.
- **What 4s's count says:** Linux's normal count at the main end (+7) shows
  the RTOS took and cleared the event. The break is in the encode task.

**Next:** a lethal run with an image-engine reader and a pure heartbeat
side by side. Note `output/decompile/rea/pass-rtos-episr-20261103/RESULT.md`.

## Step 4u: a third death at the main JPEG encode's end — 3 November 2026

User-approved lethal trial with fewer recorders; the camera died and was
power-cycled.
- **The death:** between restart +31 and +40 ms, like 4s and 4t, at the
  main encode's end.
- **Together with 4g, 4i2 and 4j:** the RTOS handles the thumbnail and
  screen done events, but never completes the main encode while its other
  tasks keep running.

**Next (static):** the RTOS's image-engine acc interrupt dispatch, and the
liveview start's `registerISR`. Note
`output/decompile/rea/pass-step4u-reg/RESULT.md`.

## Step 4t: the register watch in a dying still — 3 November 2026

User-approved lethal trial; the camera died and was power-cycled.
- **Until the encodes:** the image-engine registers followed the
  survivor's sequence up to the JPEG slots switching on (about restart
  +5 ms).
- **The death:** at about restart +27 to +37 ms, around the main encode's
  end, before the marker (slots switched off) could show.
- **Recorder load:** six recorders syncing to the SD card left 4 to 16 ms
  sampler gaps.

**Next:** repeat with fewer recorders (`--irqwatch --regwatch`). Note
`output/decompile/rea/pass-step4t-reg/RESULT.md`.

## Image-engine top registers through a stock still — 3 November 2026

- **Tool:** the new `observe/regwatch.c` reads single image-engine top
  registers every ~1 ms (read-only; no JPEG-block register).
- **Stock still (non-lethal):** the RTOS switches the JPEG slots on (acc
  enable `0x6`) at restart +5 ms and off at +32 ms, just before
  "Enc Work is Ended". Clock gating doesn't change during the encodes.
- **What the lethal run will show:** whether the RTOS reaches its
  completion in a death (acc enable back to 0) or not (stays `0x6`). The
  trial runner has `--regwatch`.

Note `output/decompile/rea/pass-ep-regs-20261103/RESULT.md`.

## The JPEG-done path: one register read; the liveview start gates clocks — 3 November 2026

Static (REA, RTOS):
- **The RTOS handled the first two encodes in the death.** The still's
  three JPEG encodes are chained, and Linux's acc bursts match all three
  done events in both the survivor and the 4s death.
- **The break is after the main encode's done interrupt.** From there to
  "Enc Work is Ended" the RTOS touches hardware once: the stream-count
  register `0x2082a280`. The rest is semaphores, callbacks and logging.
- **The liveview start's `Execute`** rewrites the image engine's clock
  registers `0x20821020`/`24` (`EPClkGating`) without a lock, during the
  main encode. It does so in survivors too.

Next: a read-only register reader for these registers through a stock and
a dying still (needs a new tool and agreement), or static mapping of the
clock blocks. Note
`output/decompile/rea/pass-rtos-jpegdone-20261103/RESULT.md`.

## Step 4s: the image engine signals the JPEG end on time in a dying still — 3 November 2026

User-approved lethal trial (stock app stopped; the camera died and was
power-cycled):
- **The done interrupt fired on time.** Linux's image-engine line 50 fired
  at restart +32.2 ms, matching the encode end in a surviving still.
- **Linux didn't take it:** its masks did not change.
- **The camera died soon after.** Linux was alive then; the next RTOS-log
  heartbeat, due 15 to 45 ms later, never came.
- **So:** the encoder does not stall. The break is on the RTOS side,
  between the done interrupt and `Enc Work is Ended`.

**Next (static):** the RTOS's image-engine interrupt dispatch, the slot-1
callback and the code after the encode wait. Note
`output/decompile/rea/pass-step4s-irq/RESULT.md`.

## Image-engine interrupts on Linux: seen, not claimed — 3 November 2026

Camera, non-lethal:
- **Read-only check:** only the stock app has `/dev/drime5_ep` open.
  Linux's five image-engine masks are 0.
- **Stock still:** Linux's lines 47 and 50 fire during the still, line 50
  within 1 ms of the RTOS's JPEG end. Linux claims none, the mask page does
  not change, and the encode completes.
- **The kernel's claim-all switch** is the A7 fault flag (`a7 fault do not
  power off`), a recovery path.

**Next:** the same watch in a dying still (lethal, needs the user). Note
`output/decompile/rea/pass-ep-irq-20261103/RESULT.md`.

## The image engine's JPEG-done interrupt is shared with Linux — 3 November 2026

Static (REA: RTOS, kernel, `libudd5`, `libtint-util`):
- **The RTOS side:** the RTOS's still JPEG encode completes through image
  engine acc interrupt slot 1 (`ACC_JPEG_END`, bit 1 of `0x20821080`).
- **The kernel:** its `drime5_ep` driver installs its own acc handler at
  boot. The handler acknowledges and clears every status bit in Linux's
  mask, or all bits when a global flag is set. The kernel's enable/disable
  ioctls set or clear bits in the same shared enable register.
- **Linux user space:** the X server driver, `libmmfmodules` and
  `libtint-util` map and write these registers directly.
- **Ruled out:** our probe never opens the image engine. The stock app
  issues no image-engine ioctl during a stock still.
- **Next:** read the kernel's masks on the camera (read-only), then watch
  the `drime5_ep` interrupt lines through a stock still and a dying still.

Note `output/decompile/rea/pass-rtos-ep-20261103/RESULT.md`.

## The still's JPEG encode never finishes in dying runs — 3 November 2026

Re-reading every recorded RTOS log
(`analysis_helpers/restart_milestones.py`):
- **Survivors:** in every surviving still, the image engine's JPEG encode
  of the still starts +4.8 ms after `restartLiveview` and ends at about
  +32 ms (one slow survivor: +105 ms).
- **Deaths:** in each death that lasted past +34 ms (4g, 4i2, 4j: up to
  +76 ms), the encode started on time and never ended. Meanwhile the front
  end restarted the sensors up to `VideoFrameReadout`.
- **Code (REA):** `Jpeg_Encode_Start` (`FUN_801b822c`) programs the EP JPEG
  encoder (`0x2082a000`) and waits up to 2 s on a semaphore.
- **Gyro angles:** they do not track the outcome.

Next: whether the liveview start touches the EP block while the encoder
runs, and what completes the encode. Note
`output/decompile/rea/pass-rtos-encode-20261103/RESULT.md`.

## I2C interrupts through a stock still: the overlap is survivable — 3 November 2026

- **Tool:** `observe/irqwatch.c` (new) samples the Linux I2C interrupt
  counts every ~1 ms, with the IPCC ring cursors on the same clock.
- **Run:** a stock still (non-lethal).
- **Result:** Linux's bus-4 traffic overlapped the RTOS's rear-sensor reads,
  mode change and stream stop during the liveview restart (+679 to +698 ms
  against +684 to +700 ms). The camera survived.
- **So:** sharing the controller is not lethal by itself, and the I2C lead
  from the second RTOS pass is weak.

Note `output/decompile/rea/pass-i2c-stockstill-20261103/RESULT.md`.

## RTOS side, second pass: hardware locks and shared I2C — 3 November 2026

REA on the RTOS image, the kernel and the bridge library, plus read-only
sysfs and `/proc` on the camera:
- **Lock users:**
  - the kernel I2C driver takes hardware locks 26–28, for buses 0–2 only;
  - user space takes no lock;
  - lock 25 (the RTOS log) has no Linux user;
  - lock 28 guards the PMIC bus (Linux bus 2), which the RTOS's voltage
    control also uses.
- **Lock reset:** it runs only at a process's first `libudd5` open, seconds
  before the still. The RTOS takes no lock in the death window, so the reset
  cannot reach the restart path in these trials.
- **New:** the RTOS image sensors (front: I2C channel 3, rear: channel 4)
  use the same I2C controllers as Linux's buses 3 (audio codec, EEPROM) and
  4 (fuel gauge, USB-PD, EEPROM; about 130 interrupts per second). Same
  register addresses, same interrupt numbers, and **no lock on either
  side**. The RTOS reads and polls both sensors over I2C inside the death
  window.
- **Not explained:** stock stills survive with the same bus-4 traffic, and
  nothing yet ties a collision to the gyro-handler rule.
- **Fault notice:** the in-kernel RTOS raw handler is a fault notice, and no
  trial logged it.

Note `output/decompile/rea/pass-rtos-locks-20261103/RESULT.md`.

## RTOS side, first pass — 3 November 2026

REA on `rtos-p6.bin`:
- `CDriveBaseNx::restartLiveview` (`FUN_802748e8`) reads no Linux-provided
  input. It starts liveview with `FUN_800ffa24` and joins a task rendezvous
  (`FUN_8025b6c4`, participant 5).
- The gyro request's payload points into an RTOS task stack, with no Linux
  pointer.
- The Linux handler writes only three floats.

So no process-dependent RTOS input remains, and the cause lies outside the
paths examined. Note `output/decompile/rea/pass-rtos-restart-20261103/RESULT.md`.

## Step 4r: starting our probe like the stock app does not help — 3 November 2026

**SD card (3 November, after step 4r): repaired.**
- **The filesystem is FAT32, not exFAT.** On the Mac the trial folder
  `obs-step4r-parity` was empty. `diskutil verifyVolume` found:
  - "entries after end of directory" in that folder (today's power cut);
  - five zero-length files in `hdr-ev2-20170101-080515-935` still holding
    16,384 clusters each;
  - excess clusters on `gear360-emmc-20261013.img` (7.3 GB allocated for a
    3.5 GB file);
  - 6 orphaned clusters.
- **Repair:** with the user's go-ahead, `diskutil repairVolume`. The card
  now verifies clean and has about 4 GB more free space. `DCIM` is intact
  (1,513 entries), and the card's eMMC image still matches
  `dump/gear360-emmc-20261013.img` (SHA-256 `bcc71a35006e488e…`). The empty
  trial folder was removed.
- **Still advised:** FAT32 has no journal, so a power cut during the
  recorders' `fsync` writes can damage the card again. Consider recording
  lethal trials to a journaled ext4 partition on the eMMC.


Our probe started with working directory `/`, the stock app's exact
environment and its own session still killed the camera at the liveview
restart. A non-lethal check first confirmed the launch (`cwd=/`, ten
variables, `sid = pid`). Note
`output/decompile/rea/pass-step4r-parity/RESULT.md`. **Next:** RTOS-side
static analysis with REA.

## Step 4q: the still-expansion ring behaves the same — 3 November 2026

A focused microsecond watch of the still-expansion ring (576-byte RTOSPREF
slots from `0x85430e0c`, positions `0x85401c50`, flags `0x854342f8` and
`0x85401c4c`) showed the same updates before the death in a surviving and a
dying still. No Linux-visible difference remains. The remaining process
differences are the working directory (`/` vs `/tmp/obs-<tag>`), the systemd
cgroup and `DISPLAY`/`HIB`. Fact
`session.expansion-ring-same-in-survivor-and-death-20261103`; note
`output/decompile/rea/pass-step4q-surv/RESULT.md`.

## Step 4p (mirror trial 2): our gyro answer kills even when the stock app rejoins — 3 November 2026

- **What ran:** our library answered the gyro request. The stock IPC
  threads were released 24 ms after the reply and the stock library then
  handled the shutter-close notice and one adjust request. The camera died
  at the liveview restart.
- **Rule:** across every run, survival matches "the stock library handled
  both the gyro request and the first adjust request after it".
- **Kernel side** (REA and a read-only camera check): `d5_ipcc` keeps no
  per-file or per-process state, and our probe's start-up changes no kernel
  callback.
- **Open gap:** RTOSPREF words written around that handling (t5c2) were
  never compared reliably with a dying run.

Fact `session.stock-gyro-and-next-adjust-rule-20261103`; note
`output/decompile/rea/pass-step4p/RESULT.md`.

## Kernel queue sample: no hidden interrupt at the restart — 3 November 2026

- **Interrupts:** `pagewatch -u` on the kernel page holding the `d5_ipcc`
  queue (PA `0x8684f000`, confirmed by its callback table) saw 13 queue
  events during a stock still, one per ring A message, and none near
  `restartLiveview`.
- **Adjust handling:** REA shows `RunAdjustFunction` uses application hooks
  for some operations, but every logged adjust request in eight runs was a
  plain `FILE_OPEN` or `FILE_ACCESS`. Content and reply timing match between
  surviving and dying runs.
- **Result:** no Linux-visible input to the RTOS differs. Note
  `output/decompile/rea/pass-kq1-stockstill/RESULT.md`.

## Step 4o: the stock gyro answer is not enough — 3 November 2026

Mirror trial 1: the stock library answered the gyro request (dlog pid 267).
`ptstop --after-gyro-reply` then stopped the stock IPC threads within
37 ms, and our probe handled the rest. The camera died at the liveview
restart. Every surviving still had the stock IPC threads active after the
gyro answer, with the stock library receiving at least one later event;
every dying one did not. Leading hypothesis: an IPC interrupt around the
restart, on a line other than 0, must reach the stock app. Fact
`session.stock-ipc-needed-after-gyro-answer-20261103`; note
`output/decompile/rea/pass-step4o/RESULT.md`. **Next (non-lethal):**
sample the kernel interrupt queue during a stock still.

## Step 4n check: a running stock app does not protect the still — 3 November 2026

The stock app ran normally with all its threads, and two of our clients ran
beside it. Our idle client answered the `0x81` gyro / still-expansion
request and the camera died at the liveview restart. Across every run with a
dlog, survival exactly matches "the stock library handled that request"; the
adjust requests are split either way. The gyro reply frames are
byte-identical in all runs (`ff7f0e000300830010020400090000000000`), and the
handlers' system calls and page writes match. Fact
`session.gyro-request-handler-instance-separates-outcome-20261103`; note
`output/decompile/rea/pass-step4n-check/RESULT.md`. **Next:** mirror trials
with a ring-B trigger (stop or release the stock interrupt thread right
after the gyro reply).

## Static pass: IPC routing and the gyro handler — 3 November 2026

**REA connection:** the REA server is registered for Codex only. This
Claude Code session drove the same pinned `rea-agents@6.7.0` over stdio with
`analysis_helpers/rea_stdio_bridge.py` (Ghidra 12.1.3, JDK 21). The kernel
import needs `REA_GHIDRA_STARTUP_TIMEOUT_MS` above the default (analysis took
365 s).

**Findings:**
- **Routing:** `/dev/d5_ipcc` keeps one global queue of up to 10 interrupt
  records (wiped when full). Each record goes to whichever process pops it
  first, and `libudd5` runs only that process's callbacks, so flags without
  a local handler are dropped.
- **Handlers:** line 0 (bridge) only posts a semaphore. The gyro handler
  (mode 3) is stateless and identical in both processes.
- **Stock-only lines:** AAC (5) and HEVC (9), registered only when those
  codecs start.

No code-level reason was found for the answering process to matter. Facts
`session.kernel-ipcc-fmt-wait-global-single-consumer-queue-20261103`,
`session.udd5-interrupt-callbacks-are-process-local-20261103`,
`session.ipcc-interrupt-line-registrants-20261103`,
`session.gyro-mode3-handler-is-stateless-20261103`; note
`output/decompile/rea/pass-static-ipcc-routing-20261103/RESULT.md`.
**Next:** two of our clients with the stock app stopped (one lethal branch).

## Plan step 4m: the `0x81` request always goes to the stock library — 3 November 2026

With only the stock app's `MCB_Receiver` and `MCB_Sender` stopped (its
interrupt waiter active), our library answered the first adjust batch. The
RTOS's category-`0x81` still-expansion / gyro request went to the stock
library and waited 42 s until release. The stock library then answered and
the camera survived. Every surviving still had the stock library handle
`0x81`; every dying one had ours handle it. The routing ties this to the
stock waiter being active, so the camera cannot separate the two. Both
libraries map the gyro buffers uncached (`O_SYNC`). Fact
`session.stock-waiter-takes-0x81-request-late-answer-survives-20261103`;
note `output/decompile/rea/pass-step4m/RESULT.md`. **Next:** static work
with the REA server: `0x81` routing in `/dev/d5_ipcc` and
`libmulticore-bridge`, and the still-expansion handler's per-process state.

## Plan step 4l: the stock app's IPC threads protect the still — 3 November 2026

Control (non-lethal) passed: the stock app's `udd_ipcc_fmt_in`,
`MCB_Receiver` and `MCB_Sender` were held 15 s and released, and a stock
still right after worked. The trial (lethal, user-approved) used the same
three threads stopped while the stock app kept running and our client sent
the still. Our library answered every request and the camera died at the
liveview restart. So the rest of the running app does not protect the still;
its library IPC threads do, by answering or by waiting. Facts
`session.stock-ipc-threads-stop-and-release-harmless-20261103`,
`session.coexistence-still-dies-with-stock-ipc-threads-stopped-20261103`;
note `output/decompile/rea/pass-step4l/RESULT.md`. **Next:** stop only
`MCB_Receiver` and `MCB_Sender` (runner `--freeze-threads`), leaving the
stock interrupt waiter in the driver.

## Plan step 4k: shared memory, request split, DDR rate — 3 November 2026

Six runs, two lethal (user-approved, power-cycled).
- **Start-up:** our probe's start-up next to the stock app writes nothing to
  shared memory.
- **Answers:** a full memory watch of a surviving coexistence still and a
  dying still of ours shows the same writes before the death, apart from
  scene data and per-boot slot positions.
- **DDR:** the rate stays at 700 MHz through the app stop and our lethal
  still.
- **Request split:** in coexistence the adjust requests are split between
  the two libraries, but the stock app's library answers the gyro and
  still-expansion requests. In every dying still ours answered them.
- **Death timing:** varies from +5 to ~+130 ms after `restartLiveview`.

The shared windows come from `libprefman`. A counter at `0x85580200` moves
once per still, 22–91 ms after the restart. The REA server was not
connected, so the `libprefman` and plugin readings are leads, not claims.
Facts `session.probe-startup-writes-no-shared-memory-20261103`,
`session.dying-still-shared-writes-match-surviving-20261103`,
`session.coexistence-splits-requests-between-libraries-20261103`,
`session.ddr-rate-unchanged-through-lethal-still-20261103`,
`session.death-time-after-liveview-restart-varies-20261103`; note
`output/decompile/rea/pass-step4k-20261103/RESULT.md`.
**Next:** per-thread bisection of the running stock app (ptrace-stop its IPC
threads during a coexistence still). The tool `observe/ptstop.c` and the
runner option `--freeze-ipc` are built and tested on a dummy process; the
commands are in `ACTIVE_PLAN.md`. The non-lethal control on the stock app
passed: its three IPC threads were held for 15 s and released, and a stock
still right after worked (`pass-step4l-control-20261103/RESULT.md`, fact
`session.stock-ipc-threads-stop-and-release-harmless-20261103`).

## Steps 4i2 and 4j: the lethal step is the liveview start — 2 November 2026

No app-only writes in the stock app's small shared windows (page watch);
a verified busy CPU does not prevent the death; stopping liveview before the
still does not either, because the RTOS restarts liveview after every still
and the camera dies in that restart. With trial 08's death after a bare
liveview start, the lethal step is **a liveview start while the stock app is
not the running client**. Facts
`session.liveview-start-without-running-stock-app-is-lethal-20261102`; notes
`pass-step4i2/RESULT.md`, `pass-step4j/RESULT.md`. **Next:** what a running
stock app provides to a liveview start.

## Static analysis after 4h: bus hang + watchdog shut-down — 2 November 2026

No orderly shutdown message appears in any dying RTOS log, the bootloader has
`WDT Option -> shut-down`, and the kernel arms a 5 s `drime5_wdt`: the death
is most likely a bus/SoC hang during the post-still liveview restart followed
by a watchdog shut-down (inference). Display power changes and the capture
memory layout are ruled out from the recorded logs. A running stock app
differs from a frozen one only by execution (CPU activity, user-space writes
through its 17 `/dev/mem` mappings). Step 4i's first attempt was invalid (no
`yes` on the camera); `cpuhog` replaces it. Note
`output/decompile/rea/pass-plan-static-20261102b/RESULT.md`. **Next:** watch
the stock app's mapped pages (non-lethal), then the busy-CPU still.

## Plan step 4h: the coexistence still survives — 2 November 2026

Stock app running, our client sending the still: the camera survived; the
liveview restart and the JPEG image set completed. The stock app's library
thread handled the RTOS requests and saved `360_0189.JPG`; the app made no
per-shot actions. So the shutter sound, LEDs and OLED are not the factor; a
running stock app process is. Fact
`session.coexistence-still-survives-without-per-shot-actions-20261102`; note
`output/decompile/rea/pass-step4h/RESULT.md`. **Next:** step 4i
(busy-CPU test).

## Plan step 4g: our probe is idle in the death window — 2 November 2026

`strace` of our probe during the lethal still (streamed to the SD card): it
answers the adjust and gyro requests by +358 ms and then only sleeps until the
trace stops at +749 ms. Neither process, no ring message and the same RTOS
steps — yet the stock still survives. Remaining difference: the running stock
app's per-shot actions (OLED, LEDs, shutter sound) or its continuous
activity. Next: step 4h coexistence still (`--keep-app`). Note
`output/decompile/rea/pass-step4g/RESULT.md`.

## Plan step 4f: real gyro angles, camera still dies — 2 November 2026

The fixed probe (libm + libdevice-node linked) loaded the device plugin, the
responder read the motion history and the RTOS received real angles
(0.51, 0, 0). The camera still lost power at the same point, in the
post-still liveview restart after `VideoFrameReadout`, ≈ +0.7 s, with no
kernel message. The placeholder angles were a real bug but not the cause.
Fact `session.real-gyro-angles-do-not-prevent-power-cut-20261102`; note
`output/decompile/rea/pass-step4f/RESULT.md`. **Next:** step 4g — trace our
own probe during the still.

## Offline preparation while the camera is away — 2 November 2026

The plugin failure is a nested `dlopen` from `libdevice-node`'s constructor
during our `dlopen` of libcapture (glibc 2.13); no other start-up plugin
loader exists in the capture library's 140-library closure. The wakeup
ioctl `0x44` also calls `device_set_property(0x12, 7/0xd, 1)`, silently
skipped in every earlier client trial. The `0xaa06` adjust requests check
for factory trigger files `/mnt/mmc/active_align_check{,_rear}.chk`; the
same handler can dump RTOS buffers for `PRD_CAPTURE_RAW_SSIF/PP` (a RAW
lead). RAW quality values from the name tables. New one-command trial runner
`observe/run_still_trial.py` and `observe/fsynctee.c`. Note
`output/decompile/rea/pass-plan-offline-prep-20261102/RESULT.md`.

## Plan step 4e: root-cause candidate — placeholder gyro angles — 2 November 2026

Every dying still from our client delivered the gyro library's "no data"
angles `1, 2, -1` to the RTOS; both surviving stock stills delivered real
angles. Cause: in our process `libdevice-node` cannot load its plugin
`libslp_devman_plugin.so` (`undefined symbol: sqrt`, then
`device_get_property`; the camera logs `module_init(398) > dlopen() failed`
for each of our processes), so `device_get_property` silently returns
nothing and the gyro history looks empty. Linking `libm` and
`libdevice-node` into the executable (as the stock app does) fixes the
lookup — shown on the camera without a still. The probe now links both and
gates the still on a valid history. That the angles cause the power cut is
still a hypothesis; step 4f tests it. Facts
`session.client-devman-plugin-fails-to-load-20261102`,
`session.dying-stills-received-placeholder-gyro-angles-20261102`; note
`output/decompile/rea/pass-plan-step4e-gyro-20261102/RESULT.md`.

## Plan step 4d: everything the stock app does during a shot — 2 November 2026

`strace` of all stock-app threads during one stock still (user-approved,
detached cleanly). Before the request: LEDs off, status OLED redraw, a
read-only battery-offset check, `statfs`. After: the MCB receiver handles the
adjust requests (looks for a calibration file, `ENOENT`) and the gyro request
(`/dev/accel`) exactly as our library does; `ui_th_sound` plays
`shutter.wav` (+12 to +318 ms); the main thread blinks LEDs. No system call
by any thread from +324 to +788 ms. Linux has no cpuidle or cpufreq. The
remaining differences from our client: shutter sound, LEDs, OLED. Note
`output/decompile/rea/pass-plan-step4d-strace-20261102/RESULT.md`.
**Next:** step 4e mimic trial (ACTIVE_PLAN.md).

## Plan step 4c: the stock app is idle when our stills die — 2 November 2026

Read-only sampling of the stock app's threads (`/proc/254/task/*`, every
2 ms) during a stock still: main thread busy ~130 ms before the request,
shutter sound +19 to +290 ms, adjust/gyro handling around +250–320 ms, then
**nothing until +780 ms** (JPEG receive and save). Our stills die at about
+0.6–0.73 s, inside that idle window. No regulator changes during the still.
Note `output/decompile/rea/pass-plan-step4c-thread-sample-20261102/RESULT.md`.
**Next:** step 4d — `strace` the stock app during one stock still (needs the
user's OK).

## Plan step 4b: a frozen stock app does not save the still — 2 November 2026

With the stock app frozen by `SIGSTOP` (present, holding all its devices,
locks and mappings) our stock-like still died the same way, about +0.73 s
after the request, in the post-still liveview restart; a 15 s freeze without
a still was harmless and caused no ring traffic. The running stock app must
actively do something during the shot that is not on rings A/B. Fact
`session.frozen-stock-app-does-not-prevent-power-cut-20261102`; note
`output/decompile/rea/pass-plan-step4b-sigstop-20261102/RESULT.md`.
**Next:** step 4c in ACTIVE_PLAN.md (read-only sampling of the stock app's
threads during a stock still).

## Plan step 4, first part: the death is an abrupt power cut — 2 November 2026

Our client now sends the stock app's exact still request and its library
answers the adjust and gyro requests itself; the ring messages match the stock
still. The RTOS completes the still and its log ends right after
`DisableStream` in the post-still liveview restart (two trials). Linux and the
RTOS stop within ~45 ms, with no kernel message and no "Start Power Off" log:
an abrupt power cut, not the buffer-starvation shutdown claimed earlier. A
stock still in the same dark scene and memory layout survives, and stopping
the stock app changes no clock, regulator or GPIO. Facts
`session.client-still-dies-by-power-cut-at-liveview-restart-20261102`,
`session.app-stop-changes-no-clock-regulator-gpio-20261102`; note
`output/decompile/rea/pass-plan-step4-stock-like-still-20261102/RESULT.md`.

**Next:** step 4b in ACTIVE_PLAN.md — freeze the stock app with `SIGSTOP`
instead of stopping it, then the same still. **Camera state:** stock app
stopped; needs a power cycle. Side finding: the kernel reports EXT4 errors on
the root filesystem (`mmcblk0p9`, inode #7339, comm systemd).

## Plan steps 1-3 done on the camera — 2 November 2026

- **Step 1: the RTOS log is readable from Linux.** `st cap log` maps physical
  `0x85300000` (`0xC0000` bytes) through `/dev/mem`; the new read-only poller
  `observe/rtoslog.c` follows it every 10 ms to the SD card. The section
  below ("The RTOS's log is not in the Linux-visible shared window") is
  right about the preference window and **wrong in its consequence**: the log
  is readable, just not there.
- **Step 2: a working stock still, message by message.** 24 frames on the
  two live rings (A RTOS→Linux, B Linux→RTOS, 395 bytes each). Besides the
  48-byte request, Linux answers six `0x82/0xaa06` requests with
  `0x83/0xaa0b`, the gyro request `0x81/0x020b` with `0x83/0x0210` value 9,
  and returns four buffers with `0x83/0x020c`. The RTOS log narrates the
  still, including RAW written to `0xa6000000`, 5824×2896, stride 11648,
  12-bit.
- **Step 3: the stop/restart control changed shape.** A graceful stop sent
  nothing to the RTOS (it stayed in liveview), contradicting the trial-06
  power-save diagnosis. The restarted stock app went into `hib()` boot-snapshot
  creation, looped on "can't umount(/opt)", ignored the shutter and had to be
  killed by systemd. Never restart the stock app; power-cycle instead.

Facts `session.rtos-console-log-readable-via-devmem-20261102`,
`session.ipcc-live-rings-a-b-20261102`,
`session.stock-still-message-exchange-20261102`,
`session.graceful-stop-sends-nothing-to-rtos-20261102`,
`session.manual-app-start-enters-hibernation-20261102`. Notes: the three
`RESULT.md` files under `output/decompile/rea/pass-plan-step{1,2,3}-*-20261102/`.

**Camera state at the end:** alive, temporary files removed, stock app unit
`failed` with no process — it needs a power cycle to return to normal.

**Next: plan step 4** (see ACTIVE_PLAN.md "Results so far").

## The RTOS's log is not in the Linux-visible shared window — 2 November 2026

The camera-safe log-read probe ran on a healthy camera: the stock application
running, no application stop, no power cycle, no still, and the camera
unchanged before and after (same boot identity, the stock application still
on pid 254, the owned remote directory removed and its removal verified). The
known boot-type preference at offset `0x1268` read **1**, so the read path
works, and the sixteen words at the handoff's log offset `0x11c3c` read
**zero**.

**The zeros are the expected result — the handoff's offset was wrong.** A
second, still read-only probe dumped the whole `0x81000`-byte shared window
twice, half a second apart: 299,314 non-zero bytes of binary structure, **no
printable run of eight or more characters anywhere in the window**, and the two
dumps **byte-identical**. The `0xff` fill that begins at exactly offset
`0x34320` is the RTOS initializer's guarded clear at RTOS
`0x85400000+0x34320`, so the window's RTOS base is `0x85400000` — not the
`0x81b00000` that the offset `0x11c3c` assumed. The RTOS's log destination
`0x81c11c3c` is therefore `0x38EE3C4` **below** the window, not inside it.

Statically the reason is sharper still. `0x81c11c3c` is **not the record's
address**: it is an **event-flag control block** in the RTOS's own memory.
`LogBuffer_SendPrintEx` (`FUN_8051eec4`) receives that address and the length
`0x20`, rejects a null pointer or a null `*pointer`, and then calls
`FUN_80517eac(**0x81c11c3c, 0x20)` — a single `software_interrupt(6)`, so the
copy's source is implicit and the supervisor-call layer owns the destination.
The `LogTask` (`FUN_8004939c`) waits on the same event flag and answers it.
The record itself is `0xa4` bytes — an 11-byte tag at `+0x00`, the message at
`+0x0c` (at most `0x80` bytes), the message id at `+0x8c`, a timestamp at
`+0x90`/`+0x94`, the level at `+0x98`, a masked counter at `+0x9c` — in a
0x400-entry ring at `index*0xa4 + 0x8144b96c`, and every record is gated on a
per-thread log level that only the supervisor-call layer reports.

Facts `session.rtos-log-destination-is-outside-the-linux-shared-window-20261102`,
`session.rtos-log-record-layout-and-pointer-destination-20261102`;
rejected `rejected.rtos-log-is-readable-through-the-preference-window`;
note `pass-log-read-probe-20261102/LOG-READ-PROBE-RESULT.md`.

**Consequence:** the RTOS's log cannot be read from Linux, so the handoff's
still-trial poll variant is moot — there is nothing to poll. A still trial
also remains the one operation that has killed the camera eleven times and
would need the user present.

## The drive's post-STL_CAPTURE_START path, traced — 2 November 2026

The fallback trace is done, and it changes two things the handoff assumed.

**`SendCaptureMsgtoFrontEnd` is a trace label, not a message send.** The
literal (`0x8067d024`) has exactly one reference, inside
`CDriveBaseNx::StartCapture`, and no procedure in the image carries that
name. The call there is `FUN_800aaa60("SendCaptureMsgtoFrontEnd")` — a
timing-trace marker in the `CAP_CTRL` module whose result is discarded. The
named function was **inlined** into `StartCapture`; the section's body is
the tail of `StartCapture`. So the drive-to-front-end hand-off is **not an
observable call edge** — the label survives, the call it named does not.

**The sensor programming is `FUN_8023013c`.** The function that emits
`[SENSOR_DRV_LOG] [%s] PowerOnSeqByRegWrite: done` is a 30,252-byte routine
(`0x8023013c`–`0x80237767`) that performs the sensor power-on sequence by
register writes: a first phase of `FUN_80243928(obj,reg,value)` writes
(`0x6010`, `0x6214`, `0x6218`, `0x136`, `0x6028`, `0x602a` and a long `0x6f12`
run) and a second phase of `FUN_80243a04(obj,reg,value)` writes over
`0x3200`–`0x36xx`. It does not read the sensor back. Its whole
register-write body is skipped when the word at `0x85180000` is nonzero,
while the "done" log is still emitted. Four callers reach it, all in the
sensor-driver region: `FUN_80237894`, `FUN_8023fc94`, the driver's
`SetMode` (`FUN_80240400`) and the module's I2C core (`FUN_80245950`).

`StartCapture` reaches the already-traced shallow path **twice**, both
routes converging on `FUN_800bda58`: directly via `FUN_800ba588` when the
capture condition is not ready, and unconditionally later through
`FUN_800bd560`, which **forks** on `FUN_800bda58`'s return — zero gives
`FUN_803a771c`/`FUN_803a8644`, non-zero gives
`FUN_800bd600`/`FUN_800bd934`. Which branch runs is not established.

**The next edge is one address.** `StartCapture`'s last action is a virtual
call through vtable slot `+0xB0`. The `CDriveBaseNx` vtable sits at
`0x8067d240` (its `+0xBC` slot lands exactly on the known
`checkBufferFull` at `0x802756b0`, which corroborates the base), so slot
`+0xB0` holds **`0x8027510c`** — not yet decompiled. If it posts a message,
its receiver is the front-end handler the trace could not otherwise name.

Nothing traced here references `checkBufferFull`'s string or a
memory-manager entry point, so the sensor path and the SSIF-allocation path
are separate branches of the death window: the buffer-starvation hypothesis
is **neither confirmed nor refuted** by this trace. And nothing in the chain
is a panic, an assert or a blocking wait — **no cause is claimed**.

Facts `session.rtos-sendcapture-is-a-trace-label-not-a-message-send-20261102`,
`session.rtos-drive-startcapture-sensor-poweron-path-20261102`;
notes `pass-log-read-probe-20261102/worker-drive-trace.md` and
`pass-log-read-probe-20261102/PARENT-ACCEPTANCE-20261102.md`.

MiniMax branch `minimax/log-read-probe-20261102` (3 commits on `67a175bb`),
reviewed and fast-forward-merged into local `master`. The camera-safe probe
part was run on the live camera; the drive trace was delegated and then
re-read by the parent against the raw REA results (the `+0xB0` vtable slot,
the `FUN_800aaa60` marker recorder, the `FUN_800bd560` fork and the
`FUN_800ba588` gate were each confirmed independently). No still trial was
run and no application stop or power cycle was performed; the camera's boot
identity, the stock application's pid and the stock application's enablement
were unchanged before and after.

**Next:** decompile `0x8027510c`; then `FUN_8022dfa8`, the only delay-shaped
call in the sensor path (its argument is not established).

## Eleven deaths; the trigger pinned to the memory manager's +0x38 — 2 November 2026

Trials 12-16 all died with the identical signature (STL_CAPTURE_START, the
death ~0.5 s later): the three-second liveview window (12), the liveview-stop
flow (13), the Lock3a (14), and the explicit ten-megabyte buffer request
(16). The one safe trial (15, the still suppressed, the memory-info reads)
survived cleanly and showed the memory manager's pool is **populated** (416
MB total, the banks 313/218/94 MB) and that the requests **allocate
successfully** from the bare client. So the still's `checkBufferFull`
failure (the manager's vptr+0x38 returning other than 1, "Cannot alloc SSIF
buffer!") is a narrower state than the pool's emptiness, and an explicit
allocation does not change it. The manager is a statically-initialized
global; its class's vtable (and the +0x38's body) lives in the .data the
seeded carrier does not map.

**Next (static only):** recover the manager's vtable through the
static-initializer route and read the +0x38's condition — the state the
stock application's flow sets that the bare client does not. No further
still trials until that is answered.

## The death mechanism found: buffer starvation → power-off — 2 November 2026

The still's deaths are a **buffer-starvation power-off**, not a panic: the
capture starves for the SSIF buffers (`Capt::RawBufferStarved`,
`CCaptureMemoryManagerNX::checkRawBufferFull - Cannot alloc SSIF buffer!`) and
the capture controller's `turnOff` (`FUN_800efd00`) powers the camera off
("CaptureController : Start Power Off") ~0.5 s after STL_CAPTURE_START. The
buffer request is the command 0x1080029 (`CCapCmdIf::RequestMemory`, the NX
memory manager), reached from Linux through the library's exported
`RequestImageBufferMemory` (ELF 0x50948), which the bare client never calls.
The bare liveview's ~5 s death is plausibly the same starvation, slower.
Facts `session.still-death-buffer-starvation-poweroff-20261102`,
`session.rtos-requestmemory-command-20261102`;
note `next-edge/still-path/DEATH-MECHANISM-BUFFER-STARVATION.md`.

**Next:** pin the app's `RequestImageBufferMemory` caller and its arguments
(the size/bank), then add the buffer request to the probe — the trial-13
variable after the clean-handover (11) and Lock3a (12) trials.

## Trial 10 dead; the clean handover recovered — 1 November 2026

Trial 10 (the wakeup-modechange + still, hard stop) killed the camera again
(review committed): the wakeup ioctl was transport-accepted, STL_CAPTURE_START
arrived 3 ms after the still, the camera died ~0.5 s later. The mode-change
selection does not save the still.

**Clean-kill procedure recovered (the user's direction).** The app's
`UI_Mm_Camera_Set_Warm_Boot_Init` (0x11780c) calls the ioctl helper
`(0x43, 0, 0)` — the selector 0x43 → OP 0x1070006 → `FUN_80111a10`, which
writes the power state 1 and warm-re-inits (the 0x100 init, the front-end,
the saved-request clear, the Lock3A-flag clear, the mode set). This is the
Linux-reachable power-on that reverses a graceful stop's PowerSave — the
earlier boot-only conclusion is corrected. The app never calls
`UI_Mm_Camera_Destroy` (no callers). Facts
`session.rtos-warmbootinit-is-linux-poweron-20261101`,
`session.app-camera-destroy-never-called-20261101`.

The clean handover: **graceful stop + warm-boot-init (0x43) + wakeup (0x44)
+ still**. The one-still probe now issues both ioctls after the admission
gate (probe 1d07ae22…).

**Trial 11 (prepared, not run — the camera is unattended):** the graceful
stop + the warm-boot-init probe. No trials until the user can power-cycle.

**Trial 12 (designed):** if 11 dies, add the Lock3a — the app's
`UI_Mm_Camera_Start_Lock3a(1, 3)` = the ioctl selector 1 with the 0x10-byte
payload [3,0,0,0] (`CAPPProgramMode::sendCmdEvent` 0xf2a50). The still then
pends on the Lock3A flag and fires against a 3A-configured sensor. Note
`next-edge/still-path/APP-OPS-MAP.md`;
fact `session.app-lock3a-before-still-20261101`.

## Trials 07-09: the bare-client capture is lethal — 1 November 2026

Three camera trials pinned the death signature (reviews committed):

- **Trial 07** (hard stop + still): the liveview skip disappeared (no
  failure-branch VIDEOSTART_DONE — the power state stayed 1), but the still
  still killed the camera ~0.5 s after STL_CAPTURE_START (id 1) arrived
  3.6 ms after the still.
- **Trial 08** (hard stop + liveview + 10 s establishment wait): the camera
  died ~5 s into the wait, **before the still was ever issued** — the bare
  liveview alone is lethal (no app-side frame consumer).
- **Trial 09** (hard stop + still alone, no liveview): STL_CAPTURE_START
  3.6 ms after the still, the camera died ~0.5 s later. The still's capture
  processing is lethal on its own in power state 1.
- The user reports the camera was **on the charger for every death** — the
  battery-sag/power hypothesis is out (`POWER-SOURCE-NOTE.md`).

The 22 Oct run (the stock app running and handling the result) remains the
only surviving still. Two lethal bare-client paths: the still's capture
(~0.5 s) and the liveview's runtime (~5 s).

**Mode-change lead.** `E_CAP_CMD_OP_MODECHANGE` = OP 0x1070004 =
`CCapCmdIf::ChangeMode` (payload 0/1/2 → controller mode 3/2/5). The still's
`handlerBody` derives its drive mode from RTOS status 0x43; a bare client
never mode-changes, so its still always runs the boot-default 0x430000
branch (count 5), never the application's set-mode branches (count 2/4).
Fact `session.rtos-modechange-op-command-20261101`;
note `next-edge/still-path/MODE-CHANGE.md`.

**Next:** the app's wakeup is pinned: `UI_Mm_Camera_Set_Wake_Up` (0x117870)
→ `FUN_00113ef8(0x44, 0, 0)` = the ioctl selector 0x44, NULL/0 → the mapper →
0x1070004 → `CCapCmdIf::ChangeMode` with the mode word 0 → controller mode 3
(fire-and-forget). Fact `session.app-wakeup-is-modechange-ioctl44-20261101`.
The one-still probe now issues the same wakeup ioctl after the admission
gate (probe 422166a7…). Trial 10: the wakeup + still under the hard stop —
the still should then run the app's 0x430003 branch (drive mode 4) instead of
the boot-default branch (drive mode 0).

## Trial 06 death diagnosis: the RTOS power state — 1 November 2026

Trial 06 (corrected `SINGLE=1` still, id-7 subscribed) killed the camera again
(review committed, `293d0b7a`). Static post-mortem traced the death trigger:

- **Power state recovered.** RTOS `CCapCmdIf` singleton `0x814831d4`, field
  `+0x30` = power state: 1 on / 3 power-save / 0 off. `DoPowerSave`
  (`0x80111818`, OP `0x1070003`, reached from Linux via category 22) sets 3;
  `DoPowerOn` (`0x80111664`, OP `0x1070001`) sets 1 but is dispatched only
  from the RTOS's own boot (`init_capture_framework 0x800a985c`). The
  library's `OperateCapture` 0x1070001 arm is local-only (mapper + pref
  0x1268) — a Linux-side power-save is not reversible by command.
- **Liveview skip.** `StartLiveview` (`0x8010f4f0`) returns -1 in state 3/0
  ("Power off state"); `CCmdLiveview::start` publishes the request on
  `0x1000001` into the internal bus, and the publish's 0 (the internal
  handler's rejection) raises VIDEOSTART_DONE on the failure branch — the
  trial-03 arrival, 2 ms after the request.
- **Death chain.** The runner's `--stop-stock-app` lets the stock app run its
  shutdown (SIGTERM → the PowerOff-mode chain → `saveValueAndEndMMPkg` →
  PowerSave(1) → category 22) → state 3 → liveview skipped → the still fires
  into the power-saved pipeline → the camera dies. The 22 Oct run survived
  because the stock app was running (state 1).
- Facts `session.rtos-power-state-gates-liveview-and-still-20261101`,
  `session.rtos-poweron-only-at-boot-20261101`,
  `session.library-op-powersave-vs-local-poweron-20261101`;
  note `next-edge/still-path/POWER-STATE-CHAIN.md`.

**Trial 07 prepared: hard-stop + still.** New runner flag
`--stop-stock-app-hard`: instead of the graceful `systemctl stop`, it SIGKILLs
every `di-camera-app` process (systemd 208 has no `systemctl kill`) and
`reset-failed`s the unit, so the app's shutdown sequence (and its RTOS
power-save) cannot run. The probe is unchanged (trial-06 binary). If the
camera survives the still in state 1, the power-save was the trigger; if it
still dies, the death is inside the still's drive on a bare client.

## Static completion of the still path and RAW packing — 1 November 2026

Camera-free parent pass, working directly in master. The still path is now
statically mapped end to end and the RAW packing is characterized:

- **Still commands named**: the STL table gives 0x01010001 = STL_NORMAL (the
  plain still), 0x01010016 = STL_LOCK3A_NORMAL (mm capture type 21); the
  "device shutter" naming in still-request is corrected.
- **RAW encoding**: 10-bit in 16-bit words, confirmed three ways (path code →
  b=10; the measured raster's shift-6 samples; the firmware's own
  `ssif_*_10bit(16bit).raw` dump names).
- **RAW stage**: the first image is the LRC (Low Raw Compression, WDMA-assisted)
  output, named SRW by the library's own error text; Samsung3 (LibRaw
  compression 0x8005) is the candidate decoder, with a checkable prediction
  (depth nibble = 9).
- **Extraction layout**: the encoded set has type@+0, first image descriptor
  @+8 with virtual address@+0x5c (library-filled) and size@+0x7c, plus the
  shared descriptor geometry (stride@+0/+4, width@+8, height@+0xc).
- **Component**: a from-scratch Samsung3 decoder (`capture-engine/samsung3.c`)
  with round-trip host tests is committed; it is not yet validated against
  camera bytes.
- **Diagnostics**: both one-still probes now subscribe the still-sequence
  notification topics (id 7 included, fixing the trial-04 defect) and use
  SINGLE=1; host tests and ARM builds pass (new hashes supersede trial 04).

**Open:** runtime validation on the camera (still-suppressed control, a still
trial with the corrected probes, the first image's bytes against the Samsung3
header), the mode writer census, and the SRW container boundary.

## Static still-path completion and capture-type selection — 1 November 2026

No camera. Parent REA passes against the carrier, library and app closed two
static gaps left by the liveview-contract branch, working directly in master.

**Still-completion gate, completed.** The id-7 flag byte (publisher
`0x81c0d750+0x64`) is armed by `CDriveBaseNx::StartCapture` (`0x80274524`,
logs `SendCaptureMsgtoFrontEnd`) and cleared by the capture command handler
`0x800f09e8` (publishes `0x1000007`). `sendCaptureReady` (`0x800f8b80`) has
one caller, `CResLiveview::onStartFrame` (`0x800f8150`), and its readiness
gate is `isNeedtoSendCaptureReady` (`0x800f8e98`). id-7 therefore fires from
the liveview first-frame event, gated by the armed flag and a readiness
predicate — a real frame signal, not a transport ack.
Fact `session.rtos-capture-ready-armed-by-drive-start-20261101`.

**Capture-type selection, recovered.** `mm_camera_capture_still`'s third
argument is the mm capture type, decoded by `FUN_00042e18` into internal
command `0x1010001`–`0x101001d` (not an identity: `1`→`0x1010001` SINGLE,
`0x15`→`0x1010016` LOCK3A_CAPTURE). `startStillCapture(bool lock3a_complete)`
maps `lock3a_complete`→CAP_SINGLE(0) else CAP_LOCK3A_CAPTURE(0x18);
`Set_Capture_Common_Param` maps those to mm 1 and 0x15. The stock app's normal
still is mm capture type 1 (command `0x1010001`).
Fact `session.mm-capture-still-capture-type-argument-20261101`.

**Probe corrected.** `one-still-probe.c` and `one-still-liveview-probe.c`
passed 21 (0x15 = LOCK3A_CAPTURE) in that slot under the misleading name
`STILL_COMMAND_INDEX`. Renamed to `STILL_CAPTURE_TYPE` and set to 1 (SINGLE),
matching the stock app's normal still. Host tests pass; the trial-04 ARM hash
is superseded.

**Still command preconditions recovered.** `CCmdCaptureNormal`'s
`checkPreCondition` refuses the still unless `isRawWorkEnd` (RAW counter equals
target) holds and the Lock3A flag byte is clear, else the request is saved
"Pending"; `handlerBody` derives the capture type/count from RTOS mode status
`0x43`/`0x51`/`0x46`, and `setPostCondition` needs a `CCaptureCondition` object
and emits `0x1010003` (`STL_CAPTURING`). A bare still is refused or left
Pending unless the RAW pipeline and mode state were set up first.
Fact `session.still-command-preconditions-20261101`.

**Image-set delivery clarified.** `CCapImageSetCb::OnCallback` (`0x7f004`)
delivers the image set as a pointer to a stack-local `stCAPTURE_IMG_SHARED_SET`
descriptor (`(topic, &desc, 0, 0)`), carrying geometry and the pixel virtual
address — copy-only, no ownership transfer; the pixels themselves are reached
through the image object's buffer accessors, whose lifetime is still open.
Fact `session.image-set-delivery-is-stack-descriptor-20261101`.

**Status getter and 3A-lock clarification.** The mode/state words the still
`handlerBody` reads are live controller properties via a virtual getter
(`FUN_80084cfc` → slot `+0x20`), so mode status `0x43` is most plausibly set by
the liveview-start transition our client already performs. The mode is set via
`CLiveviewProcessStart::setMode` (`0x80181e50`), a command→mode dispatcher
(`0x1000005`→0xb, `0x1000007`→2, `0x1000008`→4, `0x100000a`→8, `0x100000d`→5,
`0x1000013`→10, `0x1000014`→9, `0x100001b`→0xc, with `0x1000001/2/9` routed to
a sub-dispatcher); the full writer census is deferred (broad catalogue). The 3A
lock is **not** a still prerequisite: `checkPreCondition` defers the still
("Pending") only when the Lock3A flag is *set*, and passes when it is clear
plus `isRawWorkEnd`. Pixel ownership is already the copy-then-release contract
(`ReleaseCaptureBuffer`, `raw.capture-release-address-conversion-branches`);
the open RAW question is provenance, now characterized as the LRC/SRW stage.

**RAW bit depth resolved to a route byte.** `judgePath` (`0x803212c4`) selects a
path code from the PP control-mode route bytes: path 3 (byte `+0x2c` zero) maps
to setup byte 2 → **b = 12**, path 5 (byte `+0x2c` nonzero) maps to setup 1 →
**b = 10**. The measured 18-Oct raster's multiples-of-64 samples and 65472
white match the 10-bit route (bits 6..15). The `image+0x60` stride derivation's
units stay unproven (7296 ≠ the measured 11648-byte row stride).
Fact `raw.path-code-selects-raw-bit-depth`.
[Bit depth](../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/RAW-BIT-DEPTH.md).

**RAW provenance sharpened: the first image is the LRC output.** The still
pipeline runs a hardware-assisted **Low Raw Compression** stage (`PP_LRC`,
`CMaterial_PP_LRC_Still`, "lrc & wdma done Compressed raw size=%d"); the first
image ("Raw Compress") is that stage's output with a tracked compressed size,
distinct from the SSIF 10-bit-in-16-bit sensor stream. Whether the delivered
buffer is compressed or not, and the LRC algorithm, are open.
Fact `raw.first-image-is-lrc-stage-output`.

**Open:** whether a bare client completes a still at all, and whether it needs
SINGLE (1) or LOCK3A_CAPTURE (0x15); what leaves the camera unresponsive
during a still hold; the sensor-RAW provenance of the still's first image.

## Reviewed liveview contract and still-path recovery — 1 November 2026

MiniMax branch `minimax/liveview-contract-20261030` (tip `e7b25562`, 19 commits
on `c1ea372b`) reviewed and fast-forward-merged into local `master`. Parent
acceptance: PARENT-ACCEPTANCE-20261101.md (not published: `../output/decompile/rea/pass-liveview-contract-20261030/PARENT-ACCEPTANCE-20261101.md`).
Four load-bearing RTOS claims were independently re-read through the REA/Ghidra
bridge against the pinned carrier and confirmed (see that note). No camera,
device or service action in this review.

**Milestone advance (two facts):**

- The liveview start edge now works from our own client. The transport's
  per-process destination-alive latch is the gate: with it zero, category
  `0x83` does not qualify for the `0x10` bypass, the frame is queued and the
  ioctl returns `0xFFFFFFFF`. One explicit empty category-`0x10` send raises
  the latch; the ioctl then returns 0 and `E_CAP_NOTIFY_LV_VIDEOSTART_DONE`
  arrives on handle id 8 (trial 03). First observed id-8 delivery.
- The still completion is handle callback **id 7** = `MM_CAMERA_CB_STL_CAPTURE_READY`
  on internal topic `0x1010007`, distinct from the RAW image-set channel
  `0x3010005` / public `0x4010005` (still-path recovery).

Trial 04 reproduced the stock liveview-first ordering and submitted a still
that the transport accepted (return 0), then the camera stopped answering
during the hold — the same signature as the 29 October still trial. After
physical recovery the card held no file from the still. The still does **not**
complete, and no RAW was owned.

**Two defects found by the worker, both correct:** (1) the diagnostic passed
the camera handle by pointer where the library validates it by value — fixed
and rebuilt; (2) the trial-04 probe subscribed only the image-set topic and
never `mm_camera_set_notification_id`, so it could not have received the id-7
completion even had the camera survived. The next diagnostic must subscribe
id 7 plus the still sequence 1, 3, 4, 5, 6, 7.

18 branch facts accepted, plus one parent-added fact
`session.rtos-capture-ready-is-gated-flag-20261101`: the id-7 still-completion
notification is gated by a publisher flag byte (`+0x64`) and by a
`sendCaptureReady` readiness/capture-type check, so it is not unconditional —
subscribing id 7 is necessary but not sufficient. The `rea-edge`
tool surface was expanded 12→25 read-only tools and is accepted.

**Open (unchanged):** why the camera stops answering during a still hold (a
still-suppressed control is the next bisect); whether a still completes from
our client and whether id-7 completion and the image-set RAW arrive with owned
lifetime; what `LV_VIDEOSTART_DONE` actually signals (RTOS raises it on the
failure branch and on first-frame paths); the RTOS attribute-`0x301`→handler
dispatch edge and the 28-byte payload field map.

The sections below this checkpoint are the worker's own, now superseded by
this parent review where they differ.

## Liveview-first still trial 04 — 1 November 2026

Pass output/decompile/rea/pass-liveview-contract-20261030, branch
minimax/liveview-contract-20261030;
trial 04 review (not published: `../output/decompile/rea/pass-liveview-contract-20261030/TRIAL-04-REVIEW.md`).
User-authorized camera trial on a fourth fresh boot.

**The still was submitted and the transport accepted it — the call returned 0
— and then the camera stopped responding during the observation hold.**

The probe reproduced the pinned stock ordering: the admission handshake first,
then one liveview start edge, then the one still request. The handshake raised
the destination-alive byte from 0 to 1 at 100 ms; the liveview ioctl returned
0; `mm_camera_capture_still` with the reviewed raw17 ABI, index 21,
Cap=Max=1, remaining 1 and the measured storage budget returned **0**. The
latch read 1 immediately before and after both requests.

**So the admission gate does not block the still.** That question is closed:
with the latch raised, the still frame is transmitted and accepted.

What is not established: capture completion, request association, any
callback, readiness, and any RAW. The camera stopped answering at the hold
marker — the execute call, the recovery call and the after-run identity check
all timed out, helper termination was unconfirmed, and the owned directory
`/tmp/gear360-load-06b6f42eff10` was **kept for diagnosis** because the final
guards could not be verified. The stock application had been stopped before
the trial and could not be restarted remotely.

**This is the same signature as the 29 October still trial**, which also
submitted a still and also lost the camera. Whether the still causes the
camera loss, or both events share an unrelated cause, is open.

**After physical recovery:** the retained directory was already gone, so the
helper's log tail past the hold marker was not recovered — a real loss of
evidence, recorded as such. **The SD card holds no file from the still**:
nothing on it was modified on 1 November, the newest file is the 18 October
bayer RAW, and `DCIM/104PHOTO` is dated 29 October. The camera was verified
clean: no residual, no helper process, the bridge hash matching the pin, and
the stock application active with MainPID 254.

**A defect the still-path recovery exposed.** The still completion is handle
callback **id 7**, `MM_CAMERA_CB_STL_CAPTURE_READY`, on internal topic
`0x1010007`: the library's id-to-public converter at `0x477bc` maps id 7 to
public `0x2010007`, the subscribe converter maps that to internal
`0x1010007`, and the RTOS's `CCapturePublisher::NotifyCaptureReady` at
`0x8006ed14` publishes on exactly that topic. The application's own id table
gives id 8 as `LV_VIDEOSTART_DONE`, which independently confirms the trial-03
reading. The trial-04 probe subscribed **only the image-set topic** and never
called `mm_camera_set_notification_id`, so it could not have received the
id-7 completion even had the camera survived. The next diagnostic must
subscribe id 7, and the still sequence ids 1, 3, 4, 5, 6 and 7 for a full
picture.

Sensor RAW travels on a separate channel — the image-set topic, internal
`0x3010005`, public `0x4010005`, delivered by `CCapImageSetCb::OnCallback`
at `0x7f004`.

**3A is a capture mode with a lock state machine, not a computation**:
`E_CAP_CMD_3A_LOCK` on the Linux side, `Control3A` at `0x55088`,
`E_CAP_CMD_STL_LOCK3A_NORMAL` and a `Lock3ACapture` state machine on the
RTOS. Whether a still requires the 3A lock before it completes is not
established.

A control would separate the two explanations about the camera: the same
helper with the still suppressed but the handshake and the liveview edge
performed. The reviewed helper already carries that bisect build.

## Liveview admission-handshake trial 03 — 31 October 2026

Pass output/decompile/rea/pass-liveview-contract-20261030, branch
minimax/liveview-contract-20261030;
trial 03 review (not published: `../output/decompile/rea/pass-liveview-contract-20261030/TRIAL-03-REVIEW.md`).
User-authorized camera trial on a third fresh boot.

**The liveview start edge now works from our own client.** One explicit empty
category-`0x10` send through the transport raised the destination-alive byte
from 0 to 1 at 50 ms — the same latency as the earlier accepted observation.
The ioctl then returned **0** instead of `0xFFFFFFFF`, and one notification
arrived on handle id 8.

The camera dlog shows that notification as
`E_CAP_NOTIFY_LV_VIDEOSTART_DONE` on a thread of **our** helper process,
emitted after our own id-8 subscription and after our liveview request, and
delivered to the app-facing `notiLogHandler`. That is the first observed
delivery of the RTOS completion notification through the id-8 route; in
trials 01 and 02 the only such line was the stock application's own boot-time
one, emitted before its subscription.

So the transport gate was the whole story for this edge:

> latch 0 → category `0x83` not exempt from the `0x10` bypass → frame queued
> instead of transmitted → `CSender::Send` returns -1 → ioctl returns
> `0xFFFFFFFF`

and with the byte raised:

> latch 1 → frame transmitted → `CSender::Send` returns 0 → ioctl returns 0 →
> the RTOS raises the completion notification → it arrives on id 8

**What the notification means is still unsettled.** The RTOS raises
`E_CAP_NOTIFY_LV_VIDEOSTART_DONE` on the **failure** branch of
`CCmdLiveview::start` and on the first-frame paths, and the dlog shows "Liveview
on end!" immediately before it. The arrival proves the route delivers; it does
not prove the stream started, and it does not prove it failed. Request
association, capture completion and readiness remain unestablished, and no
still was issued.

**Next edge:** the still path. The stock app's `startStillCapture` calls
`startLiveview` first and then waits on callback arguments (8, 5); with the
liveview edge and the id-8 route both working, the still arm behind it
becomes reachable for the first time. Alongside that, two narrower questions:
what the notification actually signals, and whether the client needs the
handshake once per process or once per request.

## Liveview latch-observation trial 02 — 31 October 2026

Pass output/decompile/rea/pass-liveview-contract-20261030, branch
minimax/liveview-contract-20261030;
trial 02 review (not published: `../output/decompile/rea/pass-liveview-contract-20261030/TRIAL-02-REVIEW.md`).
User-authorized camera trial on a fresh boot; the camera had to be
power-cycled because an earlier remote reboot took its access point down and
it did not return on its own.

**Decisive result: the destination-alive byte read 0 immediately before the
liveview request**, and the request failed with the library's `0xFFFFFFFF`
sentinel. The byte read 0 after the request and throughout the following
twenty-second window, and the callback count was zero.

The byte read is the byte the send path tests, not a proxy: the bridge
getter at `0x17364` and the gate inside `CSender::Send` at `0x179d8` resolve
the same GOT data slot `0x2256c`, which holds `0x22ad8`, the address of the
exported `_ZN7CSender12m_bDestAliveE` byte, and the probe's variable view
and getter view agreed at every sample.

So the chain is closed end to end: destination-alive byte 0, liveview
category `0x83` not exempt from the `0x10` bypass → frame queued instead of
transmitted → `CSender::Send` returns -1 → the library stores it in the
per-arm status word → ioctl returns `0xFFFFFFFF`.

The RTOS side is not the cause. Its handler `FUN_80063160`
(`controlLiveviewStart`) rejects only a length other than exactly 28 bytes,
the library sends exactly 28, and its frame-size check sits behind the RVF
test at payload word 3, where the library writes the literal 2 with bit 0
clear, so that check is not reached for our payload. And a queued frame is
not transmitted, so the RTOS never saw the command; the RTOS log stream is
not readable with the Linux dlog utility in any case, so its absence from
the capture was never independent evidence.

**Still open, and not claimed:** why the byte was zero, and whether it is
per-process or per-device. The byte lives in the bridge's `.bss` on a
private segment, which is consistent with a per-process latch; if it is, the
stock application's own successful liveview start implies it holds a
non-zero byte in its own process, and the difference is a per-process
admission step rather than a device condition. The prior accepted
observation recorded a helper raising the byte from 0 to 1 at 50 ms with one
explicit empty category-`0x10` call, so a way to raise it exists; it was
never combined with a liveview request.

Request association, capture completion and readiness remain unestablished
in every respect. A zero-byte latch is a transport admission fact, not a
statement about the sensor.

**Next edge:** whether the gate is the whole story — compare the byte with
the stock application running, or raise the byte explicitly and observe
whether the same liveview request then succeeds and whether id 8 then
delivers. Both are camera actions and need the user's authorization.

## Liveview next edge — 31 October 2026

Pass output/decompile/rea/pass-liveview-contract-20261030, branch
minimax/liveview-contract-20261030;
next edge (not published: `../output/decompile/rea/pass-liveview-contract-20261030/NEXT-EDGE.md`).
Offline static analysis only; no camera, device or network action.

**Closed:** the failure chain from the ioctl selector to the transport. The
LV_START arm calls the thunk 0x389bc, which the provider resolves as a thunk
to ControlLiveview 0x537dc. That arm sends attribute 0x301 with 28 bytes, and
CMCBAdapter::SendCommand 0x9abc8 forwards to CMulticoreBridge::Send with
category 0x83. In the pinned bridge, CSender::Send 0x179a8 admits a send only
when the category is 0x10 or the destination-alive byte m_bDestAlive is
nonzero. Category 0x83 does not qualify for the 0x10 bypass, so when that
byte is zero the frame is queued and the call returns -1, which the library's
per-arm status word converts into the 0xFFFFFFFF ioctl sentinel observed in
trial 01. The library logs "Liveview on start!" before the send, so that dlog
line does not show the command was transmitted.

**Latch writers:** the destination-alive byte is written only by an incoming
category-0x10 broadcast or by mcb_set_ignore_init_checking, which does
nothing but SetDestinationState(true). The capture library never writes it:
it has no ignore_init symbol, its only bridge imports are mcb_register_handler,
mcb_send, mcb_init and the two debug-name setters, and its bridge traffic
leaves through CMulticoreBridge::Send without setting the latch.

**Still open:** that the byte was zero at the moment of trial 01. The static
chain shows a path that produces the observed value; it does not show that
this path is the one taken.

**RTOS side, now located:** the handler is `FUN_80063160` =
`CCommandHandler::controlLiveviewStart`, whose own rejection is a
payload-length test — anything other than exactly 28 bytes returns -2 and
logs nothing. The library sends exactly 28 bytes, so **that gate passes**.
The executor `FUN_8004f95c` then dispatches to
`CCapCmdIf::StartLiveview` `0x8010f4f0`, whose frame-size check is **not
reachable for our payload**: it sits behind the RVF test, and the library
writes the literal 2 at the flag word, whose bit 0 is clear. The RTOS-side
rejection hypothesis therefore weakens: no RTOS-side check identified so far
would reject our request.

Two further RTOS findings change how the trial-01 dlog reads. First,
`E_CAP_NOTIFY_LV_VIDEOSTART_DONE` is raised on the **failure** branch of
`CCmdLiveview::start` (`FUN_800f01d8`), as well as on first-frame paths — the
name inverts the natural assumption. Second, the RTOS log stream is not
readable with the camera's Linux dlog utility: `dlogutil` reads
`/dev/log_%s` for the streams main, radio and system only, and no `CAP_CTRL`
line appears in any raw dlog capture this project retains. **The absence of
an RTOS liveview line in the trial-01 dlog is therefore not evidence that the
RTOS did not emit one** — the capture is silent about the RTOS side by
construction.

Net effect: the transport gate is now the only identified mechanism that
produces exactly the observed `0xFFFFFFFF` together with the matching
"Liveview on start!" log and the absent notification. That still does not
establish the latch value at the time, and it makes the latch observation the
decisive measurement.

**Next edge:** the live value of the destination-alive byte around a send. That
is a runtime observation and therefore a camera action; it needs the user's
authorization. Alternatively the RTOS-side E_CAP_CMD_LV_START handler and its
own rejection condition can be read further offline.

## Liveview diagnostic trial 01 —30 October2026

Pass output/decompile/rea/pass-liveview-contract-20261030, branch
minimax/liveview-contract-20261030;
trial C6 (not published: `../output/decompile/rea/pass-liveview-contract-20261030/trial-01-review.md`).
Camera executed at its own AP address192.168.43.1; pinned library, app and
bridge hashes matched exactly; fresh boot; stock app stopped once, verified
absent, restarted afterwards; owned directory removed and removal verified.

**Result:** an independent client registered the app-facing notification
callback and subscribed handle id 8 — all three calls returned0, handle428456.
The liveview start edge issued exactly one ioctl selector0x1b and was not
retried. The camera dlog shows it reaching ControlLiveview, StartLiveview and
the outgoing E_CAP_CMD_LV_START, with "Liveview on start!". The ioctl then
returned0xFFFFFFFF, the library's failure sentinel: the handle validator had
passed, so the failure came from the LV_START arm itself, and the stock app
classifies any nonzero ioctl return the same way. No
E_CAP_NOTIFY_LV_VIDEOSTART_DONE reached the id-8 subscription in 20 seconds
and the helper counted zero callbacks.

**Next edge:** what the LV_START arm's inner call returns on failure, and the
RTOS-side handler for E_CAP_CMD_LV_START — under what condition the RTOS
rejects or ignores it. The cause is NOT established; the dlog shows no error
line between the request and the helper's exit.

**Not established:** that the id-8 route can or cannot deliver. The only
LV_VIDEOSTART_DONE in the ring buffer came from the stock app at boot ~1.92s,
before the stock app subscribed id8 at ~14s, so that notification must have
arrived by another route. No completion, readiness, association or RAW claim.

## Reviewed liveview notification contract —30 October2026

Pass output/decompile/rea/pass-liveview-contract-20261030, branch
minimax/liveview-contract-20261030;
parent C6 (not published: `../output/decompile/rea/pass-liveview-contract-20261030/parent-review.md`),
library contract (not published: `../output/decompile/rea/pass-liveview-contract-20261030/LIBRARY-CONTRACT.md`),
diagnostic design (not published: `../output/decompile/rea/pass-liveview-contract-20261030/LIVEVIEW-DIAGNOSTIC.md`),
app side (not published: `../output/decompile/rea/pass-liveview-contract-20261030/app-side/APP-SIDE-FINDINGS.md`).
Offline only; no camera, device or network action.

**Closed:** handle callback id8 ↔ public topic0x02030001 ↔ internal topic
0x01030001, byte-verified against the pinned library in both directions. The
app-facing callback receives three raw words: small handle id, data pointer,
length. Registration must precede id subscription (the subscription reads the
client slot the registration writes). The pinned app dispatches on integer ids
and contains zero topic bytes, so word0 is an id, not a topic.

**Correction:** the camera handle is passed BY VALUE to
mm_camera_set_notification_callback, mm_camera_set_notification_id and
mm_camera_ioctl. The delegated diagnostic passed it by pointer and was
corrected, retested and rebuilt (sha256 d714db1b…125f, GNU_STACK RW). Its
host tests now assert the handle arrives as the create result.

**Still open:** no delivery, no request association, no readiness, no owned
RAW. The id→app-enum renumbering is only partly characterised; id8 maps to
8. The 0xa5 status-read parameter binding stays unqualified, so the
diagnostic omits that read. Trial 01 ran with the user present; the next edge
is the RTOS-side LV_START handler, not another client-side attempt.

**Camera access note:** the camera is reachable only at its own access-point
address192.168.43.1 after the user joins its AP; it is not present on the
local LAN. The host's AirPort join path is blocked for an agent process
(networksetup fails even for the currently associated network), so the
connection must be made by the user.

**Environment repair:** Ghidra12.1.3 is the Homebrew keg at
/opt/homebrew/opt/ghidra/libexec — the directory
~/Library/ghidra/ghidra_12.1.3_PUBLIC is the user-settings tree and fails
with executable_missing. JDK21 at /opt/homebrew/opt/openjdk@21, nvm node
v24.21.0, rea-agents6.7.0.

## Reviewed power-off integration — 30 October 2026

MiniMax198c7669 retained; curated source/Markdown integrated locally without
its generated-artifact ancestry. Parent C6 (not published: `../output/decompile/rea/pass-poweroff-integration-20261029/parent-review.md`)
triages all47 proposals; next stint (not published: `../output/decompile/rea/pass-poweroff-integration-20261029/NEXT_STINT.md`).
Bounded240second stop-alone and suppressed-still controls survived; failure cause,
request-associated completion, repeatable clean handover and owned RAW remain open.
Failed still observer retains actual return0; new outage is not independently
confirmed physical power-off. Stock photo worked according to user.

**Useful new edge:** stock starts liveview before selected still; its conditional
ioctl(handle,0x1b,0,0,NULL,NULL) sends28-byte[0,0,0,2,0,0,0] under01030001.
Do not substitute mm_camera_start. Query0xa5 is a shared-value boolean, not
freshness. Close callback8/status/failure contract using retained Terra receipts,
then prepare a bounded liveview-only diagnostic. Actual execution awaits physical
recovery availability. Index21 is valid on the selected stock branch; low16flags0
matches it. Stop redoing power-name/import/thermal/global-exclusion catalogues.

**User away:** no camera SDK/init/capture, service operations or settings/hardware
writes. Offline or documented bounded readonly only. Interrupted Terra/Luna
follow-ups are explicitly partial; native queues retain export/close receipts.
One-off30Oct04:05 was not activated (suggestion card only, tool scheduling failed).
Preserve2%weekly allowance, never push, preserve unrelated root.gitignore/AGENTS.md.

## Reviewed own-transport and first still attempts —29 October2026

Pass output/decompile/rea/pass-own-transport-20261029, base6771b6d;
parent C6 (not published: `../output/decompile/rea/pass-own-transport-20261029/parent-review.md`),
next edge (not published: `../output/decompile/rea/pass-own-transport-20261029/NEXT_STINT.md`).
Luna and6.1Terra implemented/reviewed bounded helper/orchestration. User authorized
camera use; parent owned all target actions. No end-to-end capture/RAW works.

**Advance:** with stock absent, own transport latch0→1 observed after empty send.
A separate pointer-free diagnostic then called the public still facade once on
each of two fresh boots, both returning0. First subscription used wrong ID domain
(parent packet error), now corrected using native public04010005→internal03010005.
First had zero callbacks over20s plus zero-address warning; corrected attempt's
final result was lost when camera/network became unavailable. User confirmscamera
actually off. No cause, sensor exposure, fresh completion or RAW is established.
GetVirtualAddr line342 is the checked zero-input arm returning0, not mmap failure;
actual caller/source remains open behind PLT39a60.

**Recovery verified:** after user powered camera on, readonly checks show new
boot1793258497, stock active/running/MainPID254, all3 owned dirs absent and no
listed helper. Prior trial03 normal termination remains unknown; recovery is a
separate observation. No further SDK, still, service change or device write ran.
53 offline init-runner cases, synthetic helper checks and ARM linking pass.
Current helper53d0aae6…e00b1; it records events only and reads no image pointers.
New interruption handling preserves uncertainty/path and stops further operations;
timeout adaptation/cancellation are retained together. Four native queues closed.

Changed facts: session.own-transport-notify-latch-observation-20261029,
submit.own-client-still-attempts-20261029,
completion.public-encoded-set-subscription-converts-to-internal,
session.virtual-mapper-zero-input-warning-returns-zero.
**Next primary:** actual power-off initiator and service SIGTERM→stock shutdown
sequence, or reviewed stop-without-still control. Do not redo OFF/IDLE or lock/MMU
catalogues. Zero mapper caller is secondary. Clean reusable handover and owned RAW
remain open. No push/schedule/reset credit; retain2% remaining allowance and
unrelated root.gitignore/AGENTS.md. Source/notes integrated locally before close.
Final meter97%five-hourused/3%remaining,76%weekly, reset1793270381; stopped preserving2%reserve. Source integrated in3f9e4e1.

## Reviewed startup verification — 29 October 2026

MiniMax candidate01dd652 (nine commits on230693c) reviewed with Luna and6.1Terra.
Parent C6 (not published: `../output/decompile/rea/pass-startup-integration-20261029/parent-review.md`)
triages all31 proposals. Reviewed source/Markdown is integrated through the parent
branch; original worker artifact history remains separate, and all retained raw
receipts are preserved locally. No camera action, upload, service operation or SDK
execution occurred in this review. User reports camera normal with stock app
holding the channel and no residue; historical trial state is not current state.

**Milestone advance:** retained trial-05 now verifies the existing bootstrap
helper at matching PID2784/starttick177370/executable and unchanged boot with
`d5_lock`, `d5_ipcc` descriptors and `udd_ipcc_fmt_in`, `MCB_Receiver`, `MCB_Sender`
tasks. SDK calls/fullhold/normalclose returned; run.json reports stock absence
before/after and cleanup. The cited events.jsonl is missing. This closes the
missing local snapshot, not packet receipt, fresh reply, readiness or owned RAW.

The new fixed-purpose runner was corrected for strict absence, one pinned stop,
complete scans, typed transport roles, bridge hash, unconditional final guards
and uncertain cleanup. Parent43/88/57 checks plus bootstrap/notify checks pass.
Maintained ARM link produces reviewed helper SHA
b29f42d445b93f3f7a93e87a6ea34c4ae5b111a1b632badd3acaf0d867f66c50;
not run on camera. Optional notify sends at most one explicit empty category10
message only from a zero baseline/working clock. Its latch is untagged: a rise
cannot prove a fresh response to this request. Writer exclusivity and complete
SDK-load closure claims are rejected. See
next diagnostic (not published: `../output/decompile/rea/pass-startup-verification-20261029/NEXT_TRIAL.md`).

**Next primary:** obtain a defined response associated with our request, then
one identified still completion and caller-owned sensor RAW. The prepared empty
notify observation alone cannot complete that milestone. New outbound sending
is outside consumed init-only approvals. Reusable clean handover/retirement and
RAW backing provenance/lifetime remain open; avoid broad teardown/writer catalogues
without an observed dependency. All nine worker sessions and the one parent
REA/Ghidra session have close/export receipts. No push, schedule or credit reset;
preserve2% remaining allowance and unrelated root.gitignore/AGENTS.md edits.

## Historical checkpoint — before MiniMax trial-05

## Verification checkpoint —29 October2026

Same explicit-bootstrap probe was repeated after user-confirmed power cycle,
with fresh boot/hash/unit/stock-absence guards and automatic observation.
SDK probe completed rc0/fullhold/normalclose; helper stopped, temporary files
removed/absence verified. Stock app remains stopped; power cycle restores
normal startup. No capture or persistent change. Main goal unfinished.

**Measurement failed:** camera CGI removed newlines from observer command;
missing identity fields are an observer transport error, not PID reuse proof.
Corrected observer now uses explicit semicolon separators. Live readonly
qualification PASSED against stable PID1 with matching tick/executable before/
after and complete metadata. This is transport proof, not receiver proof.
Review (not published: `../output/decompile/rea/pass-goal-reset-20261029/PARENT_REVIEW.md`),
journals (not published: `../output/decompile/rea/pass-goal-reset-20261029/live-verification/`).

**Next:** recovered authorized same-probe verification with corrected observer,
then one defined remote response. Do not return to lock/teardown catalogues.
Receiver entry, remote readiness, fresh still completion and owned RAW remain
open. Same SDK executable SHA07ad2df7ff60726cd0aa1a7927e90a5a4beda95c40851cc1fab8fb856857ea5f.
All workers/native sessions stopped; all three camera directories cleaned.
Last pre-save allowance93%used/61%weekly;2%reserve. User requested wind-down.
No credits/reset/push/schedule; preserve unrelated.gitignore/AGENTS.md.

## Active goal-focused continuation — 29 October2026

User requests concrete progress toward PROJECT_GOAL and supplies camera.
Pass output/decompile/rea/pass-goal-reset-20261029;
parent review (not published: `../output/decompile/rea/pass-goal-reset-20261029/PARENT_REVIEW.md`),
[goal milestones](CAPTURE_MILESTONES.md). Luna and6.1Terra used in bounded scopes.

**New target result:** explicit user-approved stop/init-only trial completed.
The stock service became inactive/MainPID0 and access stayed available on the
same boot. Our probe logged create0/client0 but, at a matching PID/tick sample,
had only the main thread and no IPCC/MCB receiver. Successful create therefore
still did not start the necessary local transport in this trial. The later
watcher aborted with guard-lost/midrun, failed predicate unknown. No readiness
pass or complete hold/normal SDK-close claim. Independent helper termination,
log retrieval and owned temporary-directory removal/absence verified.
Stock app remained stopped; user told they may power-cycle to restore normal
startup. Do not assume power-cycle/current camera identity from that instruction.
No still, settings, shared reset, firmware/persistent patch or service restart.

Accepted session.stock-absent-init-probe-20261029 and
session.init-probe-bounded-observation-window. Added held-probe CLI/stage logs,
ARM link and host/stub checks. No end-to-end capture or owned RAW enabled.

**Corrected second test:** user power-cycled and separately authorized the
opt-in bootstrap. Native Instance→Initialize(instance)→Beauty getter,
clientcreate and callback registration returned; full hold/normalclose completed
rc0. Direct FD/thread sampling was missed, so this is not receiver-readiness
proof. Logs and cleanup verified; stock service still inactive. Accepted
session.poweron-local-bootstrap-instance-call-sequence and
session.local-bootstrap-probe-calls-returned-20261029. No capture/owned RAW.

**Next primary:** automatic pinned process/thread/FD observation during a
recovered, explicitly authorized repeat of the same local-startup diagnostic.
Then choose a specific remote-response test. Do not repeat calls and label
zeros readiness. Local bootstrap source/ABI, focused tests and ARM build are
ready; shared1268 is not manually cleared. Full stock-thread retirement,
lock/MMU catalogues and optional processing are deferred unless an observed
startup failure requires them. RAW selected backing writer/provenance/lifetime
remains an independent gap; worker P→Core slot traces remain unreviewed.

Initial allowance2%five-hourused/46%weekly; preserve2%remaining. No persistent
tool goal, competing active stint, push, schedule or reset-credit use. Preserve
unrelated root.gitignore/AGENTS.md and historical untracked work. All owned
native sessions exported/closed except the disclosed interrupted second
signal-search server (terminated, no result used). All camera-owned temporary
files from both tests were removed. The readonly automatic observer is implemented, reviewed and committed
(8e29284); explicit bootstrap/tests/two target trials are committed inadc014a.
Physical power-cycle/readiness response for a same-binary verification repeat
is pending; no additional SDK trial is inferred from elapsed time. All workers
and owned native sessions are stopped/closed, except the separately disclosed
terminated signal-search server with no accepted result. Last usage86%used
(14%remaining),59%weekly, reset1793249117;2%reserve retained.

## Reviewed04:30 continuation — 29 October2026

Baseac8c88e; passoutput/decompile/rea/pass-lock-mapping-20261029, with
parent C6 (not published: `../output/decompile/rea/pass-lock-mapping-20261029/parent-review.md`)
and exact next edge (not published: `../output/decompile/rea/pass-lock-mapping-20261029/NEXT_STINT.md`).
Five accepted static facts bring the canonical store to452 unique claims:
Linux lock mapping, RTOS default ops/init masking, conditional peer-register
correspondence, IPCC signal-pending poll escape, and receiver status clearing
with a continuing loop. One existing event-wait fact is refined. No capture
capability or RAW ownership was enabled; clean stock-app handover is UNSOLVED.

**Next primary:** libdiosal _fx_thread_func cancellation/normal-exit cleanup
beyond the accepted dispatch prefix, then receiver Deactivate/destructor and
callback-holder ordering. A qualified owned exit acknowledgement/join/lifetime
contract remains missing. Default detached-request/cancel-without-join,
kernel poll escape and this+324==0 cannot certify retirement. Shared/local
reset and exclusive local receiver/subscription startup remain independent.
Do not infer hardware ownership from the now-qualified default address join.

Parent used Luna for audits/source comparison and6.1Terra for bounded native
tracing/review. Five owned REA/Ghidra queues exported/closed; drivers exited0.
All fresh decisive code/byte windows match originals, with one explicitly
incomplete source read and one verified initialized-copy alias. Repeated old
MMU/standby facts are corroboration, not new discoveries. Four-fact semantic
review plus parent fifth-body C6 and corrected22-ID mechanical audit are saved.

No camera/SDK/service/signal/device action, credit reset or remote push.
Camera is not needed for the next static cleanup-tail question. RAW backing
writer/ID continuity and lifetime remain secondary and unchanged.
Initial0% five-hour/30%weekly, reset1793226626; preserve2% remaining. Resource
closure is recorded below. One-off04:30 continuation was consumed and deleted;
no new schedule was created. Unrelated root.gitignore/AGENTS.md are preserved.

## Reviewed handover integration and lock-driver continuation — 29 October 2026

All five commits/175 files from MiniMax handover-reset tip57e6e2b are on local
master in d733f54, with parent C33-C50 triage and evidence preserved. Source
branch ancestry verified. Parent acceptance and continuation C6 (not published: `../output/decompile/rea/pass-handover-integration-20261028/parent-review.md`).
447 unique claims: the branch review added one accepted slot-specific leaf
contract and one rejected global reset-census proof; continuation adds six
accepted Linux mutex/lock-driver contracts. Prior OFF/provider/standby traces
were corroboration; their repeated investigation was stopped.

**Clean stock-app handover remains UNSOLVED.** New evidence narrows the blocker:

- Mutex termination has partial-error paths without compensation in those
  bodies, masks successful-path cleanup outcomes, and its interrupt cleanup
  only repeats a local address getter.
- The separately bound /dev/d5_lock open/release bodies return0 with no
  cleanup. Explicit unlock returns a payload status; syscall0 does not certify
  complete copying. No global process-death cleanup absence is inferred.
- Registered init writes ffffffff then0 to hardware VAf8600000 and reads
  f8600004; its result is discarded by the outer helper. The first write is
  missing from pseudocode but present in original-matched native instructions.
- Explicit free for user-validated IDs0..29 writes0 to f8600200+4*ID and tests
  one status bit. It does not certify peer quiescence or sensor drain.

**Next primary:** qualify hardware register mapping/ownership and RTOS peer
lock acquisition/release, then whole teardown ordering with an owned receiver
kept alive until remote shutdown/drain notification has been consumed. Copy
helper residual/NULL semantics remain an exact status-proof dependency.
Shared/local reset, retired callbacks and exclusive hosted initialization
are unproved. Do not treat close, init or killing the app as a solved protocol.
RAW selected backing writer/ID continuity and lifetime remain secondary.
A19-segment mapping carrier is prepared with all payloads original-matched;
Ghidra admission/native mapping proof is pending. See current-pass
MAPPING_CARRIER_READY.md and NEXT_STINT.md; previous receipts retain their
17-segment subject and must not be relabeled.

Parent used Luna for focused mechanics/review and6.1Terra for main native
research/review. Ten bridge, six UDD and twelve kernel REA byte windows match
originals; all17 kernel carrier PT_LOAD payloads match. The RTOS run's seven
windows and19 inspected instruction bytes also match. Parent native queues
are closed/exported; use each owned run's receipts and snapshot for resumption.
No product capability, camera/SDK/service/device operation or push occurred.
No camera is needed for the next static dependency; ask for it only once a
specific bounded observation and completion criteria will advance the proof.

User's current resource reserve is2% remaining, superseding old3/5% rules.
One-off04:30 Sydney continuation fired29 October2026
(2026-10-28T17:30Z), IDcontinue-gear-360-research-at-04-30; it was consumed
and deleted during the following reviewed stint.
Final closure/schedule (not published: `../output/decompile/rea/pass-handover-integration-20261028/SCHEDULE_AND_CLOSURE.md`).
Research checkpointc3a117e is on local master after full merge d733f54.
Preserve unrelated root.gitignore/AGENTS.md and historical local work.

## Reviewed continuation — resource checkpoint, 28 October2026

Base0b5319e; pass `output/decompile/rea/pass-selected-bootstrap-20261028/`.
Parent C6 and closure (not published: `../output/decompile/rea/pass-selected-bootstrap-20261028/parent-review.md`).
Five new accepted static facts: receiver interface/dispatch identity (two),
conditional Done notification320e, second-IO image-word stores, and PostProcess
caller/error qualification. Two rejected proofs: whole-record zero after one
plane clear; unmasked14001 selecting derived despite mask→14000.439 unique
claims. Local checkpoints102d514,1111dbc,811cbbc,ece366d,3058744,d1f998a;
all are onmaster, with the user's MiniMax receiver branch fully integrated.

Clean stock-app handover remains UNSOLVED. No qualified sensor-side drain,
retired callbacks, shared/local reset or exclusive hosted startup has been
demonstrated. Killing the app remains unqualified. User reaffirmed this as
main blocker. **Next primary:** resume MiniMax phase05 reset8025b7a0 invocation
on S81c0de64 for idle0102003f/mode1; requests040/041 completed, no new claim
accepted. Its parent resource checkpoint owns exact stopping point and inputs.
Do not re-prove reset-zero or infer fresh physical completion from1122.

RAW phase04 retains a precise gap: receiver P=load32[cell81952fc0] has no
established same-object/field-flow join to Core K=81952f94. Different layouts
do not prove inequality or no copy. FE+8 stores a constructor-time return;
later current-cell equality needs continuity. Future RAW edge is service-cell
writer/slot reader/Core handoff, without identifying derived O with base I.

At93% five-hour/14% weekly, parent wound down for user3% reserve. Phase05
exit130 reflects owned resource interruption, with partial evidence preserved.
All four native sessions exported/closed; all8 receipt sizes match. Latest RTOS
run082142 closed batch068,1680records; snapshot is ready for reuse. Final RTOS
audit:38 exact windows,2 incomplete BSS reads, no mismatch. No live BSS value
is claimed. Queue and relay are stopped; do not overlap a new active stint.
Camera is not needed for the next static reset-owner/order edge.

No C/C++ product change, camera/SDK/service/device action, schedule, credit
reset or push. Capture/retention and PROJECT_GOAL remain unfinished/false.
Unrelated root .gitignore/AGENTS.md and historical work remain preserved.

## Reviewed integration — MiniMax receiver-contract branch, 28 October 2026

Candidate `ae46c38` from `minimax/receiver-contract-20261028` (base `60697fa`)
is integrated locally with all30 commits. Parent review and all49 dispositions (not published: `../output/decompile/rea/pass-receiver-integration-20261028/parent-review.md`)
own acceptance. Worker report/claims/checkpoint remain attributed proposals,
with parent annotations and corrected critical instruction excerpts.

Two new accepted static records:

- `session.pthread-entry-dispatches-record-routine-and-context`: valid entry
  record+4 supplies the function and record+8 its argument.
- `session.fthread-threadrun-dispatches-vptr-slot-0c`: null returns; valid object
  dispatches vptr+0x0c with original object argument.

Concrete receiver construction, live entry/readiness and retirement remain
open. Existing image+0x84 getter/conditional clear and startup masking facts
are corroborated rather than rediscovered. No product capability is enabled.

Four rejected proofs cover universal readiness/round-trip claims, predicted
wrong payload or zero offset after clear, global never-waits/only-retirement
claims, and the alleged runtime frame builder. Original RTOS0x800c69bc spills
arguments then returns with bx lr. The carrier already maps0x81006000; source
VA0x807ef900 /flat0x7ef900. Do not expand the carrier for that false premise.
The worker's local-template and mode3-copy proposals do not close actual
emitted-frame contents, selected source lifetime or host address mapping.
Timeout argument10000 remains unit-unverified; a semaphore post alone does not
associate a response with a particular capture. Partial data remains explicit.

Validation reused92 exact original byte windows, leaving3 loader-resolved
windows unasserted and5 incomplete reads. All36 worker native closure/export
receipt sizes match retained artifacts. Parent rechecked only3 instructions
and4 original-byte windows (204bytes) in2 exported/closed Ghidra sessions.
Terra and MiniMax supplied focused reviews; their findings were source-checked.
Raw evidence is preserved both canonically and in the original worker checkout.

**Next primary:** qualify concrete receiver construction/entry/readiness and
partial-start retirement using recovered dispatch plus retained constructor/
vtable evidence. Reuse accepted guard/creation/cancel limits. **Next RAW:**
actual nonzero selected backing writer and lifetime, then concrete callback/
copy/export binding. Getter/clear, shell ack and local templates do not close it.

No C/C++ source, camera/SDK/service/device action, schedule, credit reset or
remote push. Capture/retention and PROJECT_GOAL remain unfinished/false.
Unrelated root .gitignore/AGENTS.md and historical work remain preserved.

## Reviewed continuation — local Linux bootstrap, 28 October 2026

01:55 one-time heartbeat, base `c7cd619`; pass
`output/decompile/rea/pass-local-bootstrap-20261028/`.
Parent acceptance and corrections (not published: `../output/decompile/rea/pass-local-bootstrap-20261028/parent-review.md`).
Six new accepted static records, two rejected overclaims;426 unique YAML IDs.
Existing FThread Destroy no-join fact refined with flag/self-ID gates.
Local master intermediate commits `261b845`, `4848fc0`; final review/facts and
closure committed together. No push. Project and capture/retention remain false.

New accepted IDs:
- session.local-adapter-singleton-publishes-constructed-allocation
- session.local-adapter-constructor-discards-event-create-results
- session.local-adapter-initialize-preserves-input-r0
- session.local-adapter-last-two-subscription-stages-are-stubs
- session.local-adapter-registration-body-has-no-failure-compensation
- session.fthread-create-boolean-does-not-certify-attribute-setup

The adapter getter lazily allocates0x30 bytes and publishes the preserved
allocation after construction; its pointer does not certify event resources or
subscriptions. Initialize preserves incoming r0 despite argument-free pseudocode.
DataManager/Publisher stages are literalzero stubs in this chain. MCB's13-call
aggregate has no normal-body compensation after partial nonzero returns;
preceding mcb_init and category0xb1 returns are discarded. The stock pref0x1268
branch skips local setup when nonzero, but direct caller lists do not prove
exported adapter entries unreachable. Missing imports do not imply clean -1.

FThread Create maps inspected primitive outcomes to0/1, ignores semaphore-get,
and can return1 after logged pthread attribute setup errors. This does not
certify receiver entry/readiness. Destroy cancellation/no-join was already known;
its zero flag/self-ID refusal and later clear are now qualified. No valid
private context, successful subscriptions, remote quiescence or RAW lifetime
is claimed. No C/C++ product source changed or camera capability enabled.

**Next primary:** receiver entry/readiness handshake through _fx_thread_func /
FThread::threadRun to bridge receiver, linking actual local readiness and
partial-start retirement to creation. Reuse session.bridge-startup-drops-thread-
and-driver-results, initialization guards and fthread-destroy-does-not-join.
Qualified exclusive handover and owned bootstrap remain open. Do not rescan
caller/bridge wrapper catalogs or treat outer0/active flag as readiness.
**Next RAW:** selected attached image's nonzero+0x84 backing writer and pointer/ID
continuity after factory clearing, before callback/driver/export use. Unchanged
this pass; do not repeat closed list/zero-hook surveys. Camera not needed yet.

Validation:25 complete byte windows match original ELF bytes or their3 explicitly
validated GOT relocations; one BSS read remains incomplete/non-live. All new IDs
resolve. Two successful native export/close receipts retain148+29 records and
snapshots. Known native/CLI PIDs absent, relay locks gone; Luna tasks finished.
MiniMax Flash selected in4 turns, high effort requested/unverified. Phases01/02
stopped for scope/denied-command retries; phases03/04 completed with file tools,
parent-rendered views and no shell execution. Full errata/audits retained.

No camera/SDK/service/device action, temporary camera files, push, reset credit
or new schedule. One-time timer removed after this run. No persistent goal
exposed. Initial five-hour0% used /weekly90%; final check42% /weekly97%, reset
1793130910. Weekly headroom now binds; preserve user3% reserve and stop after
saving. Unrelated root and historical untracked work preserved.

## Reviewed continuation — shared receiver and selected LRC, 27 October 2026

Base `5928675`; pass `pass-owner-retirement-20261027`. Eleven new static facts
accepted, four proof/inference records rejected; 418 unique YAML IDs parse.
Authoritative decisions and full source IDs are in the
parent review (not published: `../output/decompile/rea/pass-owner-retirement-20261027/parent-review.md`).
Intermediate local master commits: `14223ca`, `90ebebd`, `ee656d3`; final
review/facts/checkpoint are committed together. No remote push.

The receiver context is the word at R+0x1c, initialized to R+0x38 with vptr
0x8063c1d0. Its send/receive helpers load the same handle cell0x81011238 and
request12-byte transfers. A C++ context object is not a private command channel.
One0x5101 callback submit route and the conditional common mode-consumer path
are recovered. Descriptor0x0102003f selects0x3f (63 decimal); value1 requests
state0/OFF. R+0x20 trace branches do not gate message execution. Default handler
hooks return0; the adapter epilogue's r0 outcome is not an API success contract.
No exclusive consumer, all-writer serialization or callback retirement is proved.

IDLE exit's index1 uses separate ownerA0x81c0ded4/upper6, not mode owner
S0x81c0de64/upper9. A's actual nested callback requests0x01030003; S's
previously recovered callback sets0x1122. Getter cleanup registration and a
neighboring destructor slot are not event/entry bindings. The old CAPTURE
exit's indirect work and transitive effects remain bounded gaps.

RAW construction has conditional sourceS→selected type2 LRC M→later materialI
links through inverse +d8/+e0 insertion requests. A normal code4 LRC route can
call Finish(M,0); direct Cook and assistant routes preserve M/selector into Done
under their context and delivery gates. The separate stop slot emits selectors
1/2. Assistant M+f4 routing, A+4c/A+50 checks and recipient affinity limits are
explicit. These joins do not prove initial activation, live recipe selection,
nonzero plane installation, actual DMA, freshness or retained sensor backing.

**Next primary:** recover a valid Linux adapter instance and required local
receiver/subscription startup when shared preference1268 is already set; qualify
exclusive handover, failure handling and retirement. Use session.owned-initializer
and local adapter/bridge contracts. A mode word, shared flag, event or outer0
return remains insufficient. Do not assume a full RTOS reset is required or
that a new SDK object owns a private transport.

**Next RAW:** identify the selected attached image's nonzero backing writer and
pointer/ID continuity after its factory clears image+84, before pre-callback,
driver and export use. Reuse raw.selected-base-lrc-backing-binding plus the new
construction/Finish facts; do not repeat the closed zero-hook and graph surveys.

Validation:145 complete Ghidra byte windows match original RTOS, with both
initialized carrier segments verified. All new IDs resolve. Both owned native
runs have successful export/close receipts; recovery run2 closed after batch110
with1871 total retained records and snapshot. Old failed-run control is retained.
Owned driver/relay/CLI processes are absent; all Luna tasks finished. MiniMax
Flash/thinking completed two research turns and one text review, then hit provider
limit2056 before its next turn. No retry or credit purchase; effort high was
requested, hidden effort unverified. Parent and worker errors/corrections are
recorded in C6 rather than promoted as facts.

No camera contact, SDK load, service action, device write, temporary camera file,
push, schedule or reset credit. Camera is not needed for the next static edges;
notify user when a concrete guarded observation helps. Product goal and
capture/retention remain unfinished/false; no persistent goal is exposed.
Initial five-hour0% used, weekly75%, reset1793112425; native closure90% used,
weekly89%. At final checkpoint the last usage check was93% used /7%
remaining, weekly89%, with the same reset. The user's3% remaining reserve
remains the boundary. Unrelated root
and historical untracked work are preserved.

## Reviewed continuation — owner ordering and Cook dispatch, 27 October 2026

From master55c9e61, pass-mode-owner-ordering-20261027 accepted six bounded
static facts after parent C6: five `raw.cook-*` records and
`session.receiver-singleton-vptr-startup-hook`. See the
parent review (not published: `../output/decompile/rea/pass-mode-owner-ordering-20261027/parent-review.md`).

Cook Done producer initializes recordkind2. The checked baseCCook dispatcher
routes kind2 toslot34/802539ac and kind3 toslot38/80253ec4. The initial worker
window was interior to the vtable; its mistaken slot labels are corrected.
ActualDone selector0 calls PostProcess then scans rootM+e0 candidates and
conditionally attempts Start(P); nonzero PostProcess return does not suppress
this continuation. The distinctnotification selector0 scan applies a high-key
filter and attempts ReStart(P). Both can attempt multiple candidates. Numeric
material/dependency gating is recovered; none proves sensor DMA completion.

Receiver singletonR81c0d9a0 is constructed with vptr80630d10. Conditional
entry800a9fa0 dispatches slot14 to800ccb84 and3Asetup800cd588. This does not
bind scheduler creation or the separate mode-ownerS81c0de64 ordering/retirement.
One wrapper caller and the base constructors remain attributed worker leads.

Next primary: join actual mode-owner enrollment/completion execution contexts,
queue serialization and callback retirement for0102003f/value1. Qualified
shared/local handover and exclusive hosted initialization remain open. Do not
assume a per-descriptor reset or26-minus7 owner classification. Next RAW edge:
bind selected802b8104 material to the actualDone root/candidate graph and
then its backing producer/lifetime. Do not assume kind2 means notification
restart or treat a handler return as aggregate enqueue/capture success.

Validation:403 unique YAML claim IDs parse; all34 complete Ghidra bytewindows
match originalRTOS through pinned ELF PT_LOAD mapping. Native session closed
with successful export/close receipts, retained1050-record bundle and snapshot.
Owned native driver, relay andMiniMax processes are absent. Luna tasks finished.
MiniMax phase01 proposals are triaged/narrowed; phase02 failed API715(1000),
phase03 hit provider token-plan limit2056. No retry/creditpurchase or config
change; interpreter permission denials and attempted variants are recorded.

Six facts plus reviews/index are committed locally onmaster; no remote push.
No camera contact, SDKload, service action, devicewrite, temporarycamera file,
schedule or reset credit. Camera not needed for the remaining static edges;
notify user when a concrete reviewed observation helps. Capture/retention and
product goal remain unfinished/false. No persistent goal is exposed. Started
19% five-hour used/weekly63%, closure93% used at wind-down/weekly75%,
reset1793093421; retained user3% reserve preference. Unrelated local work kept.

## Reviewed MiniMax integration — 27 October 2026

Worker branch through `e881050` (four Markdown commits, basea74048d) is integrated
on local `master`. All13 proposals are triaged; six consolidated native session
facts are accepted and three overclaims rejected. Original worker reports remain
attributed proposals; the authoritative decisions are in the
parent review (not published: `../output/decompile/rea/pass-autonomous-review-20261027/parent-review.md`).

The0x7101 question is resolved statically under stated conditions: test mask2
aliases bit1, but default range0..9 rejects the clear, whose tail only loads
pending. With pending stable, dispatch selects a diagnostic or no-op, not the
empty callback. An unconditional no-signal result is not proved because the
outer completion reloads mutable pending state. Actual interference/serialization
and request freshness remain unjoined.

Both base and mode tables use8025bacc at+0x18; there is no proposed derived
logger override to explain. The constructing call is800fcbc0;800fcc60 is passed
separately as a cleanup-shaped callback. Two getter-backed owners are bound:
81c0de64 has upper9/vptr8063bb98, while81c0ddd4 has upper12/vptr8063b668.
The26 arm callers therefore must not be treated as one request's responder set.

New accepted IDs: `session.rendezvous-test-mask-normalizes-index-to-five-bits`,
`session.awb-init-range-rejected-clear-has-read-only-tail`,
`session.awb-init-stable-pending-selects-diagnostic-or-noop`,
`session.rendezvous-base-and-mode-share-slot18-return-stub`,
`session.rendezvous-base-upper-clamp-and-bit-mutator-branches`, and
`session.rendezvous-two-getters-return-distinct-static-addresses`.

Next primary edge: actual mode-owner enrollment/completion ordering for
0102003f/value1, upstream serialization and callback retirement, plus any
required reset transition. A per-descriptor reset is not assumed or required
merely because the generic helper exists. Qualified shared/local handover and
exclusive hosted initialization remain open. Selected RAW activation/backing
and retained lifetime were not advanced in this worker review.

Validation: all47 cited IDs resolve to consistent bodies (some have repeated
receipts across passes);29 complete worker byte windows and32 fresh parent
windows match original bytes. Two worker owner/guard reads were incomplete and
unmapped, not live observations. All28 fresh function listings are byte-covered.
Four normal worker sessions and the parent session have export/close receipts;
the duplicate worker session lacks lifecycle proof, though its reported PIDs
are currently absent. See lifecycle audit and corrective details in the review.

397 unique YAML IDs parse. Luna class/lifecycle reviews and a MiniMax text-only
review are finished. No C/C++ product/camera/SDK/service/IPC action, temporary
camera file, push or schedule. Goal and capture/retention remain unfinished/false.
No persistent goal exposed. Usage0% at intake,16% at closure, weekly63%,
reset1793093421; no reset credit. Worker integration is verified on master;
review/facts/checkpoint are committed together. Unrelated local work preserved.

## Reviewed extension — 27 October 2026, 13:06–13:19 AEDT

User requests continuation to about3% remaining, superseding the runbook5%
stop. From master1eed280, three more static session facts accepted:
`session.mode-completion-callbacks-forward-index-without-local-generation-check`,
`session.rendezvous-reset-entry-clears-pending-word`, and
`session.awb-init-forwards-command-word-to-mode-completion`.
Reviewed extension (not published: `../output/decompile/rea/pass-mode-dispatch-20261027/tail-review.md`).

Mode-owner callbacks complete fixed5/3 or a supplied argument without checking
a locally identified generation. The generic reset chain explicitly clears
pending+0x14; per-descriptor invocation on the mode owner remains unjoined.
The supplied argument's analyzed AWB INIT producer passes command0x7101, not a
proven valid participant index. Next exact edge:8056f6a4 bit-test/index behavior
and8025b734 reset validation for0x7101; do not transfer Before's bounds check
onto After. Then join reset/enrollment and callback retirement to this request.

188 original-byte windows match,172 procedure instruction records fully covered
except the separately limited giant Linux callback. Six owned native sessions
exported/closed; twelve MiniMax lanes completed and Luna reviews finished.
388 unique YAML IDs parse. No C/C++/camera/SDK/service/IPC action, temporary
camera file, schedule, push or reset credit. Goal and capture/retention stay
unfinished/false. Initial81% used; saving95% used/5% remaining, weekly59%,
reset1793073577 (14:59:37 AEDT). Research commit `ebfc678` is verified on local `master`. All owned resources
are closed and finished notes are committed; unrelated local work is preserved.
Final sample97% used/3% remaining, weekly60%, same reset. User reserve reached;
all owned sessions/workers closed, twelve new evidence references resolve, and
both added MiniMax streams confirm zero tool-call events. Extension saved on
local master through research ebfc678 and integration bb1de4b.

## Reviewed manual continuation — 27 October 2026, 10:00–11:14 AEDT

From local master `3de6e91`, parent owns
pass-mode-dispatch-20261027 (not published: `../output/decompile/rea/pass-mode-dispatch-20261027/parent-review.md`).
MiniMax leads bounded analysis; Luna supplies focused review/tool-helper work.
Parent owns REA/Ghidra, original-byte checks and acceptance. No active overlap,
camera action or schedule. Initial five-hour 1% used, weekly 45%, reset
1793073577; no persistent goal exposed. Capture/retention remain unfinished/false.

Accepted 15 static facts and one rejection: the 12-byte capture receive buffer and queue creation, receive
cycle gates, actual `0102003f` → handler63 → mode1 → **OFF state0** path, state
store before entry and masked errors, conditional participant1 arm/completion,
and FrontEnd standby's unchecked inner operation. `CAPTURE_STATE_IDLE` is state2.
The standby method reaches interface slot+0x54 and bounded MMIO bit writes; this
does not qualify hardware drain, complete/fresh participants or shared/local reset.
The interface now resolves through provider+4 to singleton81884450, whose
slot+0x54 selects8023fc04. Its stop sequence runs only for byte+0x17=1/2, then
marks state4. Command writes poll MMIO bit5 without an explicit deadline; GPIO
errors only log and the delay wrapper masks sleep failure. Next: qualify
request-associated responders, hardware poll semantics/freshness, callback
retirement and shared/local reset before exclusive hosted start. State0/state4,
category21 and app exit remain insufficient.

RAW: AddProcedure creates a new P and appends its pointer. GetFirst selects the
first node; a later LRC P can be activated later and need not equal initial Start P.
Selected activation, fresh receive, nonzero backing producer and retained lifetime
remain open. The encoded-set discriminator serializer is already accepted; avoid
reopening it. New ring facts show a marked slot is still overwritten by the writer,
and zero-ready fallback reads the previous slot without rechecking it. No actual
race, stale sample or physical pixel ownership is inferred. Next RAW work should
join the selected nonnull first image/plane to its producer and release boundary.

Validation: 142 original-byte windows match; 127 procedure instruction records
are fully covered, with the giant Linux callback separately limited to its
followed branch. Five owned native sessions exported/closed; ten MiniMax lanes
completed, Luna reviews finished, 385 unique YAML IDs parse. No C/C++ change,
camera/SDK/service/process action, temporary camera file, push or schedule.
See the audit (not published: `../output/decompile/rea/pass-mode-dispatch-20261027/evidence-audit.md`).
Closing sample: five-hour 80% used / 20% remaining, weekly57%, same reset
1793073577 (14:59:37 AEDT). No reset credit used. Local integration is
recorded after commit verification below.

Integration verified: research commit `bd11f2f` is contained in local `master`.
Only this pass's reviewed Markdown and fact/index changes were committed. Exact
worker inputs, copied instructions and ignored native artifacts remain local;
unrelated `.gitignore`, `AGENTS.md` and historical untracked research are preserved.
No push or further schedule. Goal/capture/retention remain unfinished/false.

## Reviewed 04:10 continuation — 27 October 2026

From local master `16a1584`; research is saved in the
parent review (not published: `../output/decompile/rea/pass-idle-ownership-20261027/parent-review.md`).
Eight native static facts and one external comparison accepted; two false
inferences rejected. The capture library remains unfinished and capture/retention
capability stays false. No camera contact or product source changes this pass.

- Shutdown wait: successful preclear removes `0x1122`; mode `0x10` requires all
  requested bits and clears the full event word on success. Positive timeout
  `1000` scales to `1000000` internally. Local submission drops lower status;
  the wait wrapper groups timeout/wait-disabled, and DoPowerSave still ignores
  mode-change failure. Facts: `session.idle-native-event-clear-and-all-bit-wait`,
  `session.idle-native-wait-positive-timeout-scaling`,
  `session.idle-controller-wrapper-drops-submit-status`,
  `session.idle-wait-wrapper-filters-error-status`.
- Completion source: default constructor bindings join wait and trigger to the
  same adapter member. Clearing the last currently armed rendezvous bit reaches
  its callback, which sets the entire `0x1122` mask in one call. Index bound nine
  does not itself prove nine enrollments. Facts:
  `session.idle-wait-and-trigger-share-adapter-member` and
  `session.mode-completion-rendezvous-empty-pending-callback`.
- Still scheduling: the assistant's 24-byte record carries P/Q/command;
  Start/ReStart publish their own P but drop send status. Receive copies scratch
  without checking the lower result, and the scheduler ignores that result.
  Facts: `raw.assistant-pointer-record-and-receive-gates`,
  `raw.procedure-start-publishes-self-and-restart-context`.
- Pinned official T-Kernel source matches native event ABI and structure:
  `external.tkernel-eventflag-primary-abi-source`. Native bytes independently
  establish the operations. No whole-build or live-vector identity is inferred.
  Rejections: `rejected.idle-mvn-one-makes-failure-unreachable` and
  `rejected.mode-rendezvous-nonempty-callback-gate`.

Next session edge: bind descriptor `0x0102003f`/mode 1 through the RTOS queue to
an actual idle transition and the complete participant cycle for that request.
Qualify shared/local reset, required responders and exclusive hosted startup
separately. Category 21, app exit and a preference write remain insufficient.
The lower messagebuffer copy/receive targets are now `0x8001b3b0`/`0x8001b6cc`;
their implementations are not accepted by this pass.

Next RAW edge: bind the selected builder's P to the book's chosen Start P and
fresh full-size receive. Book start uses `80258f9c(B)`, which dereferences
`8056edcc(B+0xa4)`; its relation to AddProcedure `80258eb4(B,0x10a00)` was open at this checkpoint;
that routine is now accepted as new-P creation, not lookup.
Selected image `+0x84` nonzero backing producer, transform, sensor provenance and
retained lifetime remain open. Do not infer identity from callback phase order.

Parent verified 108 original-byte windows, all matching; all 94 instruction
records are covered by those windows. All 29 owned native sessions exported and
closed; YAML parses with 369 unique claim IDs. MiniMax Flash supplied eight
completed inference sessions, including the initial/followup lane; requested
high effort, confirmed routing/variant only. Parent corrected arithmetic,
object-offset and predicate errors; one prohibited no-op `true` in the standard
followup is disclosed. Later MiniMax tasks used text-only mode with zero tools.
Luna supplied focused reviews; its ignore-aware inventory mistake was corrected
by exact reads. Reports, failed queries and C6 decisions are preserved.

Saving allowance: 88% used / 12% remaining, weekly 44%, reset
`2026-10-27T09:10:34+11:00`. No reset credit used. All workers finished; no
camera/SDK/service/process/IPC action, temporary camera files, firmware write or
push. Local integration and one-time timer closure are recorded below after
verification. Unrelated `.gitignore`, `AGENTS.md`, worktrees and research retained.

Integration verified: research commit `73caa44` is on local `master`; task paths
were clean after commit. Final closure sample 91% used / 9% remaining, weekly44%,
same reset. Completed 04:10 one-time automation deleted; configuration absent.
No further schedule created. Closure record (not published: `../output/decompile/rea/pass-idle-ownership-20261027/timer-closure.md`).

## Saved bounded continuation — 27 October 2026

From master86fe57e; MiniMax completed a text-only dependency review of current
LRC facts. No new native claim or product source change. Earliest unresolved
join remains selected material/image identity through pre-callback and SetParam
to export, before nonzero backing/transform/lifetime can be qualified. Follow
80254b5c→80255398→80257a48 with actual receiver/branch conditions. Do not infer
identity from phase order. Details and worker protocol correction:
parent review (not published: `../output/decompile/rea/pass-lrc-review-20261027/parent-review.md`).

Intake91% used/9% remaining; final95% used/5% remaining, weekly30%,
reset2026-10-27T04:05:29+11:00. Pass stopped at the configured reserve; no reset credit.
Worker exited0, no active owned resource or camera file. No native session,
camera/SDK/service/firmware action, C/C++ change or push. Goal remains unfinished,
capability false. No persistent goal exposed. Unrelated .gitignore/AGENTS.md and
untracked work preserved. Automation memory reconciled with the newer night pass.


## Reviewed 23:05 continuation — 26/27 October 2026

From master8541e3d; eight accepted static facts, saved in the
parent review (not published: `../output/decompile/rea/pass-night-host-20261026/parent-review.md`).
The capture library remains unfinished; capture/retention capability staysfalse.

Research integrated as local master `decb8ec`, target ancestry and clean task
paths verified. Final allowance90% used/10% remaining, weekly29%; all resources
closed. Completed23:05 one-time automation deleted;04:10 left untouched.

- `session.stock-shutdown-initiates-with-powersave-category22`: stock app calls
  PowerSave(1) before its PowerOff wait. Ioctl40→01070003 clears the local event
  and sends category22 with modeword1; ioctl41→01070002 remains wait-only.
- `session.rtos-category22-to-powersave-dispatch` binds the subscribed RTOS
  callback, default misc holder, copied member tables and four-byte handler
  to DoPowerSave80111818. Its NUL-aware copy preserves the stock small mode
  word; arbitrary four-byte inputs cannot be treated as a general memcpy.
- `session.rtos-powersave-notification-does-not-certify-idle`: duplicate state3
  emits the same21 notification, and ordinary mode-change errors are ignored.
  `session.rtos-poweroff-command-emits-systemdown-category` separately binds
  the logical controller flag clear and21 sender. Neither certifies quiescence.
- `session.incoming-frame-misc-systemdown-selection` and
  `session.incoming-callback-length-is-packet-size-minus-ten` bind the Linux
  category21 dispatch and callback N=L-10. Positive receive return does not
  independently certify the advertised byte count, freshness or ownership.
- `raw.pre-callback-executor-and-setparam-phases` recovers actual callback
  execution before conditional LRC SetParam, with status and error limits.
  `raw.lrc-creator-inserts-material-pointer-before-image-factory` joins creator
  output to the procedure list under successful insertion/same-instance gates.
  Later image-factory failure shows no explicit removal in that checked arm;
  no universal orphan/live queue claim is made. Image/backing continuity staysopen.

Next host edge: qualify the idle transition below8010b6f8→800cc324 and its1122
wait/notification, alongside the already accepted receiver/lifecycle failures.
Recover an exclusive clean start or qualified handover with required services,
shared/local startup state, outcome reporting and retirement. Do not use21,
unrealize alone, app exit or a1268 write as a recovered reset protocol.
Next RAW edge: nonzero selected image+84 producer/lifetime across this callback
and SetParam; scheduling frontier is80254b5c→80255398→80257a48. The middle wrapper
always returnsminus1, the last entry returns0 independently of phase results.
These constants are not capture outcomes. Do not repeat the closed+48 descriptor
or scalar-reload hypotheses.

Parent checked92 owned original-byte windows and4 reused windows; three relocated
tables match explicit14-entry REL models. One incomplete source-alias read is
retained and explained. All43 owned native sessions exported/closed; YAML parses
with358 unique claim IDs. No C/C++ changes or host/camera tests were needed.
No camera contact, SDK/capture/service/process/IPC action, firmware write or push.
MiniMax exited4 on provider Token Plan limit2056 before reading evidence; Luna
fallback supplied focused reviews. A Luna worker queued one prohibited file-panel
request; scope correction and exact result are retained in the review. No parent
CUA/browser navigation. All workers finished; no temporary camera files.

Saving sample: five-hour86% used/14% remaining, weekly29%, reset1793034329.
Final allowance/integration and one-time automation closure are in the review.
Unrelated .gitignore/AGENTS.md, worktrees and ignored research are preserved.

## Reviewed owned-start / LRC research — 26 October 2026

From master e37b53a, accepted three session facts and rejected a false RAW
interpretation. See the
parent review (not published: `../output/decompile/rea/pass-owned-start-20261026/parent-review.md`).
MiniMax Flash performed two bounded reviews; explicit Luna checked status flow.
Parent verified14 original-byte windows and18 exported/closed REA sessions.

- `session.unrealize-poweroff-wait-does-not-propagate-failure`: the checked
  power-off arm waits but sends no initiating shutdown. A failed wait can
  still produce API return0; semaphore wrappers supply no remote cleanup.
- `session.systemdown-handler-to-local-poweroff-event`: a conditional local
  SystemDown handler reaches the same adapter event member/literal. Incoming
  message selection, event freshness and remote quiescence remain unproved.
- `session.partner-status-setters-are-property-byte-stores`: status/init setters
  store property bytes and return0; they do not reset shared pref1268 or transport.
- `rejected.no-stack-reread-as-no-plane-consumption`: SetParam passes own-plane
  at aggregate+0x10 by address; the worker's missing-scalar-reload inference is
  incorrect. Existing control-store join remains accepted, backing remains open.

Next host edge is the actual initiating operation/remote notification and
qualified shared/local reset, through the misc handler's still-unbound member
tables. Next RAW edge is the still execution caller walking registered callbacks
and ordering SetParam; registration/global-map insertion is not that executor.
No camera, SDK, service/process, IPC or desktop operation occurred. All workers
finished; no temporary camera files. No capture capability enabled; phase2 open.
Reserve-aware saving began at88% five-hour usage; final sample/integration are
in the parent review. Initial MiniMax packet/scope failures are retained there.

## Reviewed local source comparison — 26 October 2026

The five principal public leads are locally inspected. See
[LOCAL_SOURCE_COMPARISON_20261026.md](LOCAL_SOURCE_COMPARISON_20261026.md), the
source inventory (not published: `../output/decompile/rea/pass-local-comparison-20261026/source-inventory.md`)
and parent review (not published: `../output/decompile/rea/pass-local-comparison-20261026/parent-review.md`).
The pass started from master `0517962`;98 pinned public text/source files,
seven retained SM-R210 plaintext files and manufacturer documentation were
compared. MiniMax Flash handled three bounded project reviews; explicit Luna
reviewed LibRaw. Parent checked hashes, decisive source and negative/causal limits.

Five accepted `external.*` records in `facts/external.yaml` describe source
contracts only. Four published NX systemd units and hib_tmp.sh exactly match
our retained files; main hibernation.sh has two path differences plus whitespace.
Later C200 bench reports qualify old injection recipes; nx500ex operates stock
control/debug paths. LibRaw supplies a real conditional decoder, without an
LRC-format binding. Manufacturer documents specify a PC USB SDK; its actual
source/API package was not acquired. No retrieved material closes our host.

`session.owned-initializer` and `raw.selected-base-lrc-backing-binding` gained
comparison pointers and stay open. Next acquisition work is a concrete
clean-start/orderly-handover protocol with one receiver, required services,
qualified readiness/failure and remote retirement, alongside the selected
nonzero backing-to-first-image producer join. Repeating known create-call
censuses, injecting into the app or obtaining another debug dump is insufficient.

No camera contact, desktop/browser automation, downloaded-program/SDK execution,
service operation, native instruction analysis, capture or public publishing
occurred. No product code or capability changed; phase2 remains unfinished.
Luna used explicitly allowed literal static-read commands; MiniMax used only
native text read/search/write tools. Integration/worker closure and final account
sample are recorded in the parent review.

## Public-source search checkpoint — 26 October 2026

Searched recovered internal names on the public web and GitHub, with a Luna
subreview. Found additional NX500 app-restart/hibernation research in Metol,
C200 app-kill/injection bench reports, recent nx500ex RTOS/LUT/debug-path notes,
and existing Samsung RAW decoding in LibRaw. No inspected material demonstrates
our app-absent SM-R210 RAW host or a complete ownership/handover procedure.
The official NX1 SDK is PC USB tethering, not the on-camera SDK we need.

See `reverse-engineering/PUBLIC_RESEARCH_20261026.md` for pinned source links,
search limits and distinctions. Five known script/unit files in our retained
extraction were read; related hibernation services exist and one unit matches
the published NX hash, but our boot script differs. The follow-up lifecycle
comparison is completed in the reviewed entry above. Third-party runtime and
native claims remain leads, not accepted SM-R210 facts.
No camera connection, SDK/third-party code execution, restart, injection,
firmware binary download or public posting occurred during this search.

## Read-only stock-app status — 26 October 2026, 18:07 AEDT

User asks about the stock-app interference/post-exit-state blocker and reports
the camera online. Revalidated pinned app/capture-library/bridge hashes and
stable boot/app identity. Stock app PID254 holds fd18 to /dev/d5_ipcc. This is
not sole receiver ownership, readiness or a safe handover. No SDK/context,
capture, IPC consumption/send, service/process action, device-memory operation
or camera file was used/created. New observation:
`session.stock-linux-peer-state-live-20261026`; review in
`output/decompile/rea/pass-app-ownership-status-20261026/review.md`.

`session.owned-initializer` remains a critical-path blocker alongside RAW
backing/completion/retirement. The prior overview underemphasized it. Shared
receive state and preference1268 are separate from process-local state; Linux
exit does not establish a remote reset/drain. New gyro service research
clarifies one dependency but does not solve takeover. Next ownership work is a
concrete clean-start or orderly handover with one receiver, required services,
qualified readiness/failure and remote cleanup. No takeover experiment started.

## Reviewed post-reset continuation — 26 October 2026, 14:18 AEDT

Completed another offline evidence stint with explicit Luna and MiniMax Flash
workers, starting from master26b0e2e. Seven new static claims are accepted;
phase2 remains unfinished. Retention capability/default refusal are unchanged.
No camera contact, SDK execution, service/capture/firmware action or remote push.

- Corrected the prior parent/worker +48 descriptor premise: typed int-pointer
  index0x48 is byte+0x120. Helper802950d4 unregisters the secondary interrupt
  listener, then clears byte+0x128 regardless of its result. It is not backing.
- Bound factory-return image arguments through attachment/wrapper construction,
  and callback conditional use. Attachment has count/null guards; an unchanged
  pointer/ID and invocation order across scheduling remain unproved.
- Recovered byte+0x49 writer801358c8 from native key10. The0xa0001/6/7 drive family
  produces values outside signed1/2, including its overrides; under quality
  byte+0x48>3 it can select the accepted LRC lookup arm if the composed instance
  reaches that builder unchanged. It is not a demonstrated RAW setting.
- Verified screen/thumb list selection, stateful release conversion and list
  removal even when manager reportszero. Local status/removal is not outcome.
  Library onWriteDone passes helper-derived type with address0; that method
  does not supply a matching address for selected native lists. No universal
  cleanup absence, five-live-resource or double-free claim is accepted.

Parent checked27 REA byte windows against original RTOS/library files; all match.
All24 owned native sessions exported/closed. Fact YAML parses with340 unique IDs;
no C/C++ source changed or host/camera test rerun. Review/evidence/dispositions:
`output/decompile/rea/pass-base-lrc-20261026/parent-review.md`. Input hashes and
visible tool calls audited. MiniMax scope deviations and initial consumer tool
block are recorded; parent ran scoped batches and returned records for a shell-free
analysis phase. No owned CLI stream remains open. Unrelated `.gitignore`,
`AGENTS.md`, worktrees and untracked/ignored research are preserved.

Next RAW edge: identify actual pre-callback invocation/order and a nonzero
installer/producer for selected image+0x84 before SetParam consumes it. ID0xb's
lookup skip remains conditional on its value at callback entry. Container helper
8057d3ec is an unreviewed registration dependency, not a presumed pixel producer.
Whole-result address-carrying cleanup, fresh association, sensor provenance,
byte extent/cache visibility and qualified host readiness remain separate gaps.

Allowance actually refreshed at startup0% (weekly61%). Saving sample five-hour38%
used/62% remaining, weekly67%, next reset Unix1792999845 (18:30:45 AEDT). Account
usage is shared; no reset credit, duplicate persistent goal or automation used.
Everything below is historical and does not override this checkpoint.

## Intake at resource boundary — 26 October 2026, 13:18 AEDT

User requested another stint with Luna. Startup usage sample is five-hour95%
used/5% remaining, weekly60%, reset2026-10-26T13:30:27+11:00
(Unix1792981827). The reset is more than two minutes away, so continuation
section7 requires no new substantive work and saving/stopping this intake.
No Luna worker, native session, camera action or automation was started; no
reset credit was consumed. Fact statuses and the unfinished goal are unchanged.
Next resumed stint should use Luna on the selected-base LRC backing edge
(manager8030f3f0 and its material+48 descriptor), with parent original-evidence
review; the saved partial retirement inventory remains unreviewed.

## Reviewed continuation — 26 October 2026

Phase2 remains unfinished. Source repair is on local master `ba3ac9d`; native notes/facts are in
`0c08b08`, with the final usage note saved locally alongside them. Capture capability remains
false and default capture refuses before SDK load. No camera contact, SDK
execution on camera, service operation, capture, firmware write or remote push.

- Fixed callback release overlapping adapter SDK calls: atomic admission/
  reservation, deferred first-image release, stable post-close errors/logging
  and no native client destruction during busy close. Copied bytes survive
  deferred release failure; uncertainty blocks reuse. Parent446/347 ASan/UBSan
  checks, ten TSan processes208 checks, C++/refusal/display checks and ARMv7 link
  pass. These are component/stub/build results, not camera qualification.
- Accepted local bootstrap/bridge predicates: singleton does not certify
  registration; initialization-byte latches precede setup; returned handler
  status survives forwarding. No readiness or safe retry follows.
- Accepted normal/device ABI word stores and bracket fallback-tail difference.
  Original C prototype/types remain unknown.
- Corrected `raw.encoded-first-image-conditional-lrc-source`: selectors1/2
  with null argument12 skip the setter and material lookup. Only other selector
  values enter LRC lookup under configuration>3. The old "otherwise" wording
  and the parent's follow-up premise were wrong.

Next raw edge: `raw.selected-base-lrc-backing-binding`, selected base helper
802950d4's material+48 descriptor into manager8030f3f0. No accepted nonzero
backing, transform, sensor provenance, fresh association or visibility proof.
Do not resume unbound derived MemToMem methods or caller-census absence proofs.
The partial RETSET26 output/release inventory is UNREVIEWED; review original
selectors/list/alias/lifetime evidence before implementing further releases.

Review/evidence: `output/decompile/rea/pass-lrc-export-20261026/parent-review.md`.
All65 owned native sessions exported/closed; all source/research workers stopped.
Input audit records worker Git-metadata/scope deviations and rejected malformed
edit/denied read attempts. Source was independently reviewed and validated.
Preserve unrelated `.gitignore`, `AGENTS.md`, untracked work and ignored evidence.
No persistent goal was exposed; PROJECT_GOAL remains the objective. No automation
was created and no reset credit consumed. Final saving sample: five-hour93% used/7% remaining,
weekly60%, reset 2026-10-26T13:30:27+11:00 (Unix1792981827); account usage is shared.

Everything below is historical and does not override this checkpoint.

## Saved offline continuation — 26 October 2026

Phase2 remains unfinished: our exclusive app-absent host, software trigger,
fresh sensor RAW, caller-controlled lifetime, supported settings and repeat
capture. Retention capability stays false; default capture refuses before SDK
load. This pass made no camera contact, SDK execution, service operation,
firmware write, capture or remote push. The independent Astra review guided it.

Reviewed local master commits:6e6e31e (explicit caller release), cc89e27
(initial host contract) and59842be (latched copy/native cleanup quarantine).
Final checks:446 core and317 parent adapter assertions plus isolated children,
C++ linkage/refusal/display checks; six targeted TSan cases139 assertions;
offline ARMv7 link. These are component/stub/build evidence, not camera success.

Accepted native facts now join the ordinary gyro request, library fd_alg
subscriber, inline auxiliary writeback and selector9/event2 return:
`host.gyro-expansion-request-and-auxiliary`,
`host.expansion-request-to-linux-client`, `host.gyro-return-selector-to-event`,
`host.ordinary-gyro-builtin-responder`,
`host.orientation-defaults-can-still-be-signalled` and
`session.poweron-gates-local-orientation-subscriber`. Power-on preference1268
can skip process-local initialization. A signalled reply may carry defaults;
it proves neither local readiness nor completed sensor samples. Do not add a
replacement responder, fake/delay a reply or reset the shared flag from these
static findings. Other mandatory requests and runtime qualification stay open.

`raw.encoded-first-image-conditional-lrc-source` now records argument12=null
at ordinary call802ac0ac; the alternate nonnull supplied-image branch cannot
apply at that selected static call. Other callers/modes remain open.
Next bounded offline edge: bind the selected LRC producer/transform to the
exported first-image buffer. Fresh association, visibility and whole-result
retirement remain separate missing contracts. See the required-contract table
in parent review (not published: `../output/decompile/rea/pass-host-services-20261026/parent-review.md`).

MiniMax Code was the main bounded source/research worker; parent accepted
critical claims and commits. All source workers are stopped.28 parent-owned
REA runs exported and closed successfully. One interrupted worker run has no
export/close receipts and supplies no accepted evidence; known worker/helper
PIDs are absent. The review records that lifecycle limitation and scope audit.
Raw evidence, logs, source snapshots and binaries remain local and ignored.

No persistent goal was exposed (tool returned null); retain PROJECT_GOAL without
creating a duplicate. Automation memory was absent at intake and is now saved.
Initial five-hour usage1%/99%; saving sample90% used/10% remaining, weekly45%,
reset 2026-10-26T08:25:42+11:00 (Unix1792963542). Account usage is shared; no reset credit consumed.
Preserve unrelated main-checkout .gitignore/AGENTS.md and untracked work.
Everything below is historical and does not override this checkpoint.

## Independent strategic review — 25 October 2026

Completed the requested offline independent review (not published: `ASTRA_INDEPENDENT_REVIEW_20261025.md`)
against master `2dcbdd4` and the integrated a177dc3 evidence. It recommends
closing one fixed-mode capture transaction: required host responders (including
the ordinary gyro request/reply), selected sensor-output provenance, fresh
association, private copying and full native result retirement. Source review
identifies native-release outcome, result replacement and API lifetime/output
gaps. These are review findings and proposals, not newly qualified hardware
capabilities; accepted fact statuses and the capture refusal remain unchanged.

The next proposed offline question is the exact host-service request/reply
contract of the selected ordinary graph. The review also prioritizes the
conditional encoded first-image path and its alternative supplied-image branch.
No camera operation, SDK load or fresh native session occurred; selected retained
REA records were reread with artifact-hash verification. No worker remains active.
This standalone review is complete; camera experiments remain paused and phase2
remains unfinished. No automatic continuation or remote push is requested.

## Reviewed integration — 25 October 2026; paused at user request

Candidate **a177dc3abf735407c93c02be5dff70263af522fe** from
`minimax/receive-contract-20261022` is integrated with its full committed
history. All seven new worker notes are retained with parent-review banners;
the bridge/shell target definitions and shell coordinates are imported.
Parent review: output/decompile/rea/pass-a177dc3-review-20261025/parent-review.md.
The dated26October worker filenames are preserved; this review occurred25October.

**Phase2 remains unfinished.** Own hosted context, stock app absent, software
still trigger and caller-owned sensor RAW remain the deliverable. Optional
camera JPEG is accepted. Retention capability stays false; default capture
refuses before SDK load. Facts/ remains the claim authority.

Accepted new edges:

- Cook0x802526c0 argument4 is a four-byte word at record+0x20. The0x2c-byte
  record is forwarded via0x802544cc to FMbox::setMsg0x8051e560. Copying that
  word requires a valid configured length covering it; successful queueing
  has additional gates. The live length and callback association remain open.
- shell_capture (ELF0x9dd4/Ghidra0x19dd4) permits local shell_exec success.
  Its MCB fallback prints the same refusal on send failure or wait failure.
  Timing does not distinguish them or prove RTOS arrival. The bridge ignore
  function calls SetDestinationState(true), which does not establish delivery.

Review corrections:

- Cook0x802526c0 returns1 on wrapper0, otherwise0. Enclosing
  Kitchen0x80255f94 normally returns0 regardless; these are distinct returns.
- Checked IQ maker-note/AE-AWB callers use the3A patch table; this is not an
  exhaustive writer proof or a positive exclusion of a still association.
  The encoded callback header word remains opaque, not a proved address.
- The existing boot-type label/guarded preference clear remain accepted
  within their fact limits. Synthetic BSS metadata does not prove memory
  reinitialization on every warm reset. No-callers lists do not exclude
  other cleanup routes; a settings write alone does not prove ownership.
- The worker's reboot/probe experiments are historical reports. Parent did
  not replay or qualify them as camera observations. Linux uptime cannot
  rule out an RTOS-only reset. The risk proposal is not an approved runbook.

Validation:50 helper tests (48 passed,2 optional inventory tests skipped),
three fresh hash-pinned REA/Ghidra sessions with successful exports/closures,
eight bounded queries, and selected RTOS code-byte binding to the original
image. Raw evidence and binaries remain ignored. No camera operation, SDK
load, service/reboot command, firmware write or remote push in this review.

**Paused. No research or camera task is running.** If resumed, the deferred
question is the ordinary Cook message consumer/opaque-header association;
the console item/ack route is a separate candidate, not a proved fix.
Preserve main checkout .gitignore/AGENTS.md and unrelated untracked work.

Everything below is the previous checkpoint, retained as history. Its old
allowance, camera status and next-task statements do not govern this pause.

## Previous checkpoint — 25 October 2026 (historical)

**Phase2 remains unfinished.** Own hosted context, stock app absent, software
still trigger and caller-owned sensor RAW are the deliverable. Optional camera
JPEG is accepted. Retention capability stays false; default capture refuses
before SDK load. Read PROJECT_GOAL.md and CONTINUE_INVESTIGATION_PROMPT.md;
facts/ owns accepted/open/rejected claims, currently312 unique records.

Reviewed candidate b024fcaca5ad6f01fbcf938321606deb964a1eee is reconciled into
master through two-parent merge4a1309c. Latest substantive checkpointb2f5d1a
is on local master, with candidate ancestry verified. Per-path dispositions:
output/decompile/rea/pass-b024fca-review-20261025/integration-decisions.md.
Review: same directory/parent-review.md. Task checkout:
/Users/<user>/.codex/worktrees/minimax-b024fca-integration/gear360_modding,
branch codex/minimax-b024fca-integration. Preserve main .gitignore/AGENTS.md
changes and unrelated untracked files. No remote push was requested or made.

## Results that should not be repeated

- Reviewed source components: latest-set diagnostic, display rectangle repair,
  bounded manual AP helper, pure producer reference model and portable REA
  helpers. Their component checks passed; no working capture backend is claimed.
- ID23→pool29 requests32MiB. Selected10700 material conditionally routes its
  image through six parameter entries to service+18/+3c. Byte+204 has direct
  constructor/debug writers; its live mode and supported setting remain open.
- Geometry manager initialization installs the selected providers. Provider
  template T=Q+10, not Q+4. Mode17 selects record8. Both static source variants
  and the guarded live metadata words give2912/2896. Template field copy and
  the field60 formula are recovered; final image binding and units remain open.
- Image+60 reaches an unscaled32-bit store at2080033c through the DMA wrapper,
  with enabled-channel alignment checks. Native logging calls it stride; the
  hardware unit contract, actual sample layout and freshness are not proven.
- Allocation reply conditionally copies all20payload bytes including word0;
  frame reserve32 stores28 at frame+c and emits a6-byte header before payload.
  Aliasing/delivery remain open and Linux SDK translation overwrites word0.
  This is not a nonce, sensor binding or retained lease.
- Actual normal-still route reaches80138a40→eight-argument Order80255f94.
  It can submit through802526c0 and normally return0; prepare ignores its
  virtual+70 result. Zero does not prove certainly unsent or completed. Arg4
  is logged CapNo and comes from8052060c, with units/uniqueness unresolved.
  The three-argument Order callback contract does not transfer automatically.

## Previously proposed next task (superseded by review above)

Track **802526c0 argument4 (CapNo)** into its Cook/message storage, then seek
an original-evidence join to the encoded callback's opaque header. Start from
completion.actual-single-order-zero-result-is-not-refusal. Do not repeat global
Cook caller/vtable censuses or constructor exit handling as a capture callback.
A selected old prefix1/liveview branch was not proof of this ordinary route;
a newly proven conditional join must be judged on its own evidence.

Use proper REA/Ghidra, exact byte offsets and small function budgets. Original
RTOS SHA cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7;
analysis-carrier SHA d6b247ec8b119657cd176528b7558b278ab41dcfbdda919c384883933bbb88d7
is a separate binding. No manual decoder. Missing procedure is not missing
mapped code. Use unique immutable query requests, never overwrite spool files.
MiniMax smart permissions were sufficient with local Write/read spool fallback.
No delegated process inspection/signals or outside-packet source/config reads.

## Camera, validation and cleanup

The user reconnected the camera. Read-only app/boot and pinned app, capture,
device-node and plugin guards passed. A new fixed RAM profile reads only
818828a4/a8 on ordinary page81882000, guarded by object vptr80670310 and
repeat reads. Two64-byte code anchors matched the pinned RTOS. Observation:
2912/2896. Helper and empty directory were removed; absence verified.
Journal: active pass/camera-profile8/1792916075085708000/resume/run.json.
The initial CGI-envelope parser and cleanup-name mistakes are retained in the
original journal and corrected in the resumed trial. No SDK load, capture,
service/radio/reboot, MMIO/device write or firmware change occurred.

New dependency guards passed88 parent offline tests. Fixed reader passed7
Apple-Clang ASan/UBSan suites, Linux ARM host-seam rejection and static ARM
build; target helper SHA f4bc1d2d9d2d45dbf2b2b6439f17e82200230c28d04ae4347a0129eb97fbd31c.
Homebrew host sanitizers hung on unchanged baseline code; Apple sanitized and
Homebrew plain baselines passed. Host tests now default to Apple Clang with
CC override; target LLVM/LLD is unchanged.

A MiniMax coding worker exceeded scope with broad host process-name cleanup
commands and a nonunique/tmp/orig-check directory. Parent stopped it, audited
zero camera commands/no remaining known test-path processes and independently
finished validation. Unrelated host process impact cannot be excluded by that
post-check. The nonunique outside directory was preserved. No broader coding
agent task continues. Native-only smart-permission tasks ended normally.

All owned CLIs ended. RTOS, device-node, library, AP-agent and plugin REA
sessions exported/closed with successful receipts. No pending native requests.
The final worker archive verifies4112 files/29366474bytes; its exact path and
hash/member manifest are in active pass/worker-preservation-manifest.json.
Original temporary packets remain; raw evidence/binaries/streams stay ignored.

Final allowance sample98% used (2% remaining), weekly15% used. The user stop
target has been reached; resource wind-down is complete. No reset credit was consumed; the earlier meter
refresh was not initiated by this agent. Final usage is saved in parent-review.
No fresh investigation/target trial starts during resource wind-down.

Historical floating-index text remains verbatim in
archive/CURRENT_INVESTIGATION_20261025_BEFORE_COMPACTION.md; obsolete camera,
allowance and next-task statements there do not govern current work.

## Final04:30 resource closure

Final recorded resource meter at2026-10-28T18:33Z:99% five-hour used,
46%weekly, reset1793226626. The final meter rose beyond the requested2%
reserve to1% remaining during wind-down. No further research is started;
all five owned sessions had already closed and final saving only remains.
This exceeds the reserve by one percentage point and must be reported plainly.
