# Fact store

7 November (camera, T20 done): **the remote page shows a live preview.**
g360-app runs the camera's movie preview while the page wants it, encodes
640×320 JPEGs (about 8 a second) and serves them as MJPEG on port 8090 and
through the page's CGI; it stops the preview for every capture and starts it
again afterwards (`app.live-preview-on-the-remote-page-20261107`,
[validation](../../src/app/VALIDATION.md)).

7 November (camera, T20 trial 1): **our own client runs the camera's movie
preview.** `MV_START` 1280×640 was acknowledged in 230 ms and 296 frames came
in 10 s (about 30/s) on the image set `0x4010007` only, NV12 and readable in
our process. A capture during it was refused; `MV_STOP` was acknowledged in
14 ms and a normal still followed; no fault. Software JPEG on the camera:
about 51 ms at 640×320 (`session.own-client-movie-preview-then-still-20261107`,
trial 1 (not published: `../../output/live-preview-T20/trial-01/RESULT.md`)).

7 November (static, T20): **the stock Wi-Fi preview is a movie recording
with no file, and Linux makes its JPEGs.** The iOS stream case sets save
type `WIFI_STREAM` and MJPEG; the RTOS sends YUV 4:2:0 frames to libcapture
client `"camerasrc"` on image topic `0x3030003` (or set `0x4010007`) after
`RecordMovie(0x1020001 MV_START, 28 bytes)` and ack `0x2030001`; stop is
`0x1020002`, ack `0x2030002`. libmmfmodules encodes on the image engine's
JPEG block and hands each JPEG to the DLNA server on port 7679
(`session.stock-live-stream-is-linux-mjpeg-of-movie-frames-20261107`,
`session.movie-frames-client-contract-20261107`). The RTOS's StartMovie
accepts 1280×640 among eight sizes and nine frame rates and writes user
values `0x36`/`0x37` (fresh REA on the carrier;
`session.rtos-startmovie-sizes-rates-and-stop-20261107`). The image block's
`+0x58` is the chroma plane
(`session.image-route-block-layout-from-stock-consumer-20261107`).
T20 result (not published: `../../output/live-preview-T20/RESULT.md`).

7 November: **the lens EEPROMs are decoded.** The stock factory table names
every field. The RTOS's four white points per lens are EEPROM 0x040 and
0x228–0x239, the same values as the stock JPEG's AWB record, and the lens
adjustment is a golden value over the first point
(`target.lens-eeprom-field-map-and-awb-points-20261107`, accepted). The
optical-axis block 0x0d0–0x16f is copied into every JPEG as MakerNote tag
0x0012 for an off-camera stitcher; its first six doubles fit two lens
centres and two radii (`target.lens-eeprom-optical-axis-block-20261107`,
open). [eeprom/README.md](../eeprom/README.md).

6 November (attended): **Wi-Fi is an app setting.** `WIFI: OFF / AP / HOME`
in g360-app's settings; the app starts `wifi-join.sh apply` in the
background. On the camera all three worked; one `HOME` stalled after
`wpa_supplicant`'s first scan and fell back to the access point, and a
second try joined in 1 s. The script now starts a fresh `wpa_supplicant`
after 12 s without a join (`app.wifi-setting-off-ap-home-20261106`).

6 November: **the screen shows Wi-Fi and battery.** g360-app's top-right
corner shows the home network, the camera's access point or no network, and
the battery with a charging bolt and percent. It reads the kernel's files
every 2 s; on the camera it showed the home network and a full battery on
the charger (`app.status-corner-wifi-battery-20261106`). The stock app
subscribes to deviced's `memory/sysman/battery_*` values and the tethering
callbacks instead (`app.stock-app-follows-battery-and-ap-by-subscription-20261106`).
Where `READY` was, it now shows the stills or sweeps left on the card. A
replug 4 s after an unplug wasn't recognised by the charger chip: 5 V at the
connector, no charger identified, no charging
(`target.quick-replug-not-recognised-by-charger-20261106`).

6 November (attended): **the camera joined the home Wi-Fi and came back.**
It left its own access point through mobileap-agent's `disable_wifi_ap`,
loaded the station firmware and joined a WPA2 network with the stock
`wpa_supplicant` (no D-Bus, so ConnMan saved nothing). It took a DHCP
address and answered the Mac there, then got its access point back with
`enable_wifi_ap` 5 s after the revert started, all on one boot. The agent
deletes its `hostapd.conf` while the access point is off
(`target.wifi-station-join-and-return-20261106`;
[g360wifi](../../src/wifi/README.md)).

6 November: **DNGs carry the shot's metadata.** Exposure time, ISO,
program, WB mode, date, f/2.2, 1.2 mm, the IQ MakerNote and a sweep
description; byte parity with `make_dng.build_full` on a camera DNG, and
rawtoaces now writes the exposure into its EXR
(`raw.dng-with-shot-metadata-20261106`).

6 November (unattended): **settings screen and sweep options.** Holding
Menu opens settings for mode (single/HDR), HDR shots (6/8) and saving
(flash first or straight to the card). Measured: 8 shots straight to the
card held still 26.4 s, flash first 16.7 s; 6 shots 19.3 s and 12.5 s
(`app.sweep-save-options-and-settings-20261106`). The buttons themselves
are still to be tried by the user.

5 November (attended; goals and tasks now in TODO.md (not published: `../../TODO.md`)):
- **HDR sweep through the built-in flash.** `/opt/usr` (eMMC p13, ext4,
  1.9 GB free) writes a frame in 0.8 s against the card's 2.1 s
  (`target.built-in-flash-opt-usr-for-frames-20261105`). Holding Capture
  now takes 8 exposures, each saved there before the next, then moves them
  to the card: held still 16.7 s instead of about 29 s, unattended on the
  camera (`app.hdr-sweep-via-flash-queue-20261105`); the button gesture is
  still to be tried by the user.
- **Power hold turns the camera off.** The stock sequence, traced with REA
  through the app, libdevice-node, the devman plugin, deviced and the kernel:
  `d5-keys/shutdown` = 1, deviced `PowerOff` (sync, forced card unmount),
  `d5-keys/shutdown` = 2. The kernel then calls the board power-off
  (`app.stock-poweroff-sequence-20261105`). Our app runs it from a copy in
  `/tmp`, and it worked twice on the camera
  (`app.power-hold-poweroff-20261105`).
- **The card.** It is FAT32 under the kernel's exfat driver, not FUSE.
  Renaming over an existing file leaves the old entry live, and the second
  FAT copy is not written
  (`target.card-rename-over-existing-leaves-old-entry-20261105`).
- **The camera's own Linux ABI.** Its `open()` flag numbers are not the
  generic ones: `O_DIRECTORY` is `040000`, `O_NOFOLLOW` is `0100000` and the
  generic `O_DIRECTORY` bit is the camera's `O_DIRECT`, which the card
  rejects with EINVAL. Measured on the camera, not assumed
  (`target.open-flag-numbers-are-not-the-generic-ones-20261105`, accepted).
  This was the root cause of the camera app's storage failure.
  [App validation](../../src/app/VALIDATION.md).
- **Controls without the stock app.**
  - Every button mapped: `experiments.button-events-recorded-20261105`,
    accepted. Menu 127 and hold 215; Power 73 and hold 116; Capture 126
    with real press and release times.
  - LEDs mapped and sound played: `experiments.leds-sound-without-stock-app-20261105`.
  - [g360ctl](../../src/lib/g360ctl/README.md); results in `output/controls-C*/RESULT.md`.
- **Colour pipeline for DNG.** The camera's MakerNote IQ records are
  decoded (`raw.stock-jpeg-iq-records-20261105`, open), and pair P2
  confirms they reproduce the JPEG from the RAW
  (`raw.dng-pair-p2-confirms-iq-pipeline-20261105`, accepted).
  [dng/README.md](../dng/README.md).

4–5 November unattended dark-room trials: **309 successful full RAW captures,
44 processes, one boot, 1,236 ordered give-backs**. Accepted bounded observations
in [experiments.yaml](contracts/experiments.yaml) cover same-boot reentry,
manual/auto 100-shot stress, full 8/6-shot HDR range, measured Linux JPEG
omission (7.03%), optional library logging (5.77% in a separate cohort), named
battery status and passive-input/ARM guard checks. RAW hold remains ~0.65 s;
native encoder bypass, other drives, physical controls and charge transitions
remain open. All owned camera files were removed after verified retention.
Results (not published: `../../output/experiments-live-20261104/RESULT.md`),
[compact evidence](../unattended-20261104-evidence.json),
archive index (not published: `../../retained-artifacts/unattended-experiments-20261104.md`).
The older entries below describe their state on their recorded dates.

4 November screen trial: accepted bounded observation
`experiments.screen-clear-center-driver-returns-20261104`: stock stopped,
clear/center invocations returned 0, no reboot, temporary files removed.
The owner confirmed centered text and the old interface cleared. Two EXT4 errors observed during
the stop remain unreviewed; no capture test followed. Result (not published: `../../output/screen-live-20261104/RESULT.md`).

4 November screen follow-up (offline): `experiments.main-app-controls-with-capture-20261104`
remains open. The existing direct OLED renderer now has clear/center commands
and a stock-independent ARM build; the prepared harness labels the screen
after confirming stock exit. Host layout/lifecycle tests pass. No new native
claim or camera result; see [screen preparation](../../tools/experiments/README.md#clear-and-label-the-screen-after-stopping-stock).

4 November (offline preparation, camera unavailable): the owner started
preparing all six post-Phase-2 experiments. [Open experiment questions](contracts/experiments.yaml)
record JPEG timing, process reentry, speed/stress, full HDR range, app controls
and battery status. [Tooling and protocols](../../tools/experiments/README.md) are built
and host-tested; no new native/runtime claim is accepted and all six hardware
questions remain open. [Validation](../../tools/experiments/VALIDATION_20261104.md).

4 November (camera, trial F2): **two normal stills in one session, each with
the full RAW frame of both lenses**, copied during the camera's gyro wait
(688.7 and 619.5 ms holds), unchanged during the copy; both stills completed
with capture-ready, JPEGs and give-backs; no reboot. Run from `master` after
merging the MiniMax and Codex frame-pause branches
(`raw.frame-f2-two-full-frame-stills-pass-20261104`,
result (not published: `../../output/decompile/rea/pass-frameF2/RESULT.md`)).

F2 follow-up, 4 November: the user permits the prepared test unattended.
`session.frame-f2-preflight-stopped-app-20261104` accepts only the literal
preflight observation: camera online, library hashes match, same boot as
F1, stock app already inactive. The starting-state guard stops the trial
before any still. New-process reentry remains unvalidated; the prepared
fresh-session trial awaits a physical power cycle, without asking again
for F2 authorization. Result (not published: `../../output/decompile/rea/pass-frameF2/RESULT.md`).


4 November Codex continuation: `raw.frame-f1-offline-review-20261104` stays
**open** as an offline review of MiniMax's F1 bytes. The corrected checker
accumulates every sampled row and compares matching grid areas; same-half
correlations are 0.7438 / 0.6637. Full-file minimum is 3072, not the sampled
3264, and cleared low bits do not establish ADC precision. Source guards,
ASan/UBSan tests and the ARM build are complete; repeat frame capture F2
and new native revalidation remain pending.
Review (not published: `../../output/decompile/rea/pass-frame-review-20261104/RESULT.md`).


4 November (static + logs): **a native pause with the full RAW frame
intact.** After writing the full frame (both lenses) the RTOS publishes the
expansion topic and waits, with no timeout, for libcapture's gyro reply
before the lens passes run. libcapture calls subscribers in subscription
order, so a client subscribed before the Beauty object runs first and can
copy the frame while the still waits
(`raw.gyro-handoff-pauses-still-with-full-frame-intact-20261104`).

4 November (static): the picture over lens 1 is the Resize stage's
5792×2896 main image (the JPEG input), placed first-fit; both lens images
are released after development in a normal still; the image size cannot
be set from Linux. No normal-still setting keeps lens 1
(`raw.resize-main-image-reuses-lens1-ipc-releases-sources-20261104`).

4 November (camera, trial Q2): with the quick-view hold, `0xa6000000` is
still the stitched picture (not the full RAW frame), and lens 2's RAW
survived again. Lens 1's RAW is overwritten in every mode by a temporary
full-size picture that fits exactly between lens 1 and lens 2
(`raw.lens1-raw-overwritten-by-temporary-picture-20261104`).

4 November (camera, trial Q1): with quick view HOLDZOOM live view stayed
off and **lens 2's corrected RAW arrived intact** (`0xad134000`, GRBG,
16-bit, dark level about 520; matches the JPEG's right half). Lens 1's
memory was reused by the same still's main JPEG and a stitched picture. No
capture-ready came in this mode
(`raw.quickview-holdzoom-lens2-raw-intact-lens1-reused-20261104`).

4 November (static + R1 log): no setting keeps the full RAW frame (the
stitch writes into its stage's buffer from the shared first-fit pool), but
the two lens images at `0xab134000`/`0xad134000` are corrected RAW per lens
(lens shading, crosstalk, defect correction) and only the restarted live
view overwrites them; with the quick-view time HOLDZOOM (libcapture
attribute 30 = `0x1e0005`, wire `0x2b`) a single still should not restart
live view
(`raw.stitch-uses-pool-buffer-lens-images-are-corrected-raw-20261104`,
`settings.quickview-holdzoom-skips-single-still-liveview-restart-20261104`).

4 November (camera, trial R1): the RAW frame does not survive to the image
set: about 120 ms after it is written, the still's own stitch step writes
the finished 5792×2896 YUV 4:2:2 picture over `0xa6000000`; only the last 15
RAW rows remain (16-bit words, top 10 bits used, dark level 4096). RAW
quality (5a) runs the same steps
(`raw.normal-still-raw-frame-overwritten-by-stitched-picture-20261104`).

4 November (logs + static): every normal still writes its RAW frame to
physical `0xa6000000` (5824×2896, 12-bit, stride 11648), inside the capture
memory our process maps; the post-still live view uses `0xAB134000`, and no
RTOS mechanism hands this frame over
(`raw.normal-still-raw-frame-at-a6000000-not-handed-over-20261104`).

4 November (camera, trial E3): with a 90 s wait the 5 s still still never
delivers: long exposures use a dedicated capture readout whose frame never
arrives, unlike normal stills read from the running stream
(`settings.long-exposure-capture-path-never-delivers-20261104`).

4 November (camera, trial E2): a ~5 s exposure with LTNR on at RAW quality
selects the long-exposure recipe and the RTOS accepts 4.97 s, but the
sensor frame never completes within ~40 s
(`settings.five-second-exposure-selects-ltnr-recipe-then-stalls-20261104`).

4 November (camera, trial E1): manual exposure from g360cap works; the RTOS
applied exactly 1/1024, 1/64 and 1/4 s at ISO 100 and the images brightened
(`settings.manual-exposure-from-g360cap-observed-20261104`).

4 November (static), exposure: dial mode, shooting mode, ISO, noise
reductions and the shutter (variable `0x101`, TV = 16·log2(1/s)) can be set
from our process through the capture library
(`settings.exposure-attributes-through-libcapture-20261104`).

4 November (static): the bracket command (id `0x27`) has an empty handler on
this firmware, so brackets cannot run
(`session.bracket-command-has-empty-handler-20261104`). Remaining routes to a
RAW-compression stage: the long-exposure (LTNR) recipe for exposures of about
4.9 s or more with LTNR on, or the debug command `st cap seq order … <recipe>`
(`session.ltnr-recipe-for-long-exposure-and-seq-recipe-override-20261104`).

4 November (camera, step 5d): with the bracket words, the RTOS accepts the WB
bracket (`E_CAP_CMD_STL_WB_BRACKET`, `DRIVE_WBBRK`) and then does nothing; no
notification reaches Linux (`session.wb-bracket-command-accepted-then-idle-20261104`).

4 November (camera + static, step 5c): a WB-bracket request (capture type 6)
needs words 13/14 (step `0x1a000N`, axis 1/2); with zeros libcapture refuses
it (−3) before sending (`submit.capture-still-bracket-words-20261104`).

4 November (camera, step 5b): the still request's capture type resets the
drive mode (our `0x15` → `DRIVE_SINGLE`), so setting `DRIVE_WBBRK` beforehand
does nothing; a bracket must be requested as its own capture type (mm `6` =
WBB) (`session.still-request-resets-drive-mode-20261104`).

4 November (static, REA), drive → recipe: `DRIVE_WBBRK`/`DRIVE_PWBRK` select
`RecipeBracket` (RAW compression at a RAW quality); `DRIVE_AEBRK` the normal
recipe (`settings.drive-selects-capture-type-and-recipe-20261104`). Linux
attribute 8 sets the drive (`settings.drive-mode-linux-attribute-8-20261104`).
The bracket runs one RAW and one back-end image set per frame
(`raw.bracket-recipe-one-raw-many-backend-sets-20261104`).

4 November (static, REA), RAW compression: the normal single-still recipe
never builds the RAW-compression (LRC) material
(`raw.normal-still-recipe-has-no-raw-compress-stage-20261104`); the shared
processing page does, at a RAW quality, and it is used by the bracket, HDR,
multi-exposure and other recipes
(`raw.recipe-page-pp-creates-lrc-at-raw-quality-20261104`). Drive mode
(`DRIVE_AEBRK`) is the leading, untraced switch.

4 November (camera, step 5a): RAW_SUPERFINE set by g360cap reaches the RTOS
and its order sheet, but the RAW-compression image is missing, so the RTOS
drops the whole image set ("Raw Compress Address is NULL")
(`session.raw-superfine-set-dropped-raw-compress-null-20261104`,
`session.rtos-set-builder-drops-raw-quality-set-without-raw-compress-20261104`).

4 November (camera, step 4z-b): two stills in one g360cap session both
succeeded, and the RTOS reused the returned buffers
(`session.g360cap-two-stills-one-session-20261104`).

4 November (camera, step 4z-a): **g360cap took a still, saved its three
JPEGs and returned the buffers exactly like the stock app** (same types,
order and physical addresses on ring B)
(`session.g360cap-still-saved-and-buffers-returned-like-stock-20261104`).
The boot id is restored from the hibernation snapshot and is the same on
every boot; use the boot time or uptime to detect a reboot
(`session.boot-id-restored-by-hibernation-not-per-boot-20261104`).

4 November (static, REA), kernel: the camera reboots when the kernel raises
SIGSEGV/SIGBUS/SIGFPE/SIGABRT/forced SIGKILL on any process (before any
handler runs) or when a process dies of SIGABRT/SIGFPE
(`session.kernel-reboots-on-forced-fault-signal-or-abort-exit-20261104`).

4 November (static, REA), RAW quality route: our library can send image
quality with `mm_camera_set_usr_attributes(handle, 3, 0x3000N)` (wire
`0x1103`, logged by the RTOS as `SetUserData`), which the stock app does not
use for quality (`settings.stock-app-sets-quality-through-user-spec-20261104`).
The order-sheet override key is `0x134`, the variable
`VARIABLE_PROSUGGEST_SAMPLE_CAPTURE`, so RAW_SUPERFINE should give set type 3
(`settings.raw-quality-override-is-prosuggest-sample-capture-20261104`).

4 November (static, REA): the stock still's images are read and returned
by `libmm-photo.so`. Its callback copies only descriptors; after saving, it
returns the buffers with `ReleaseCaptureBuffer` types 5 (maker note), 1 (RAW,
when present), 7, 8, 9, using virtual addresses
(`session.stock-photo-library-releases-buffers-after-save-20261104`). Each
still JPEG buffer is complete and its size word is exact
(`session.still-jpeg-size-word-is-exact-length-20261104`).

2 November plan steps 1-3 (camera observations, ACTIVE_PLAN.md (not published: `../ACTIVE_PLAN.md`)):
the RTOS console log is readable through `/dev/mem` at physical `0x85300000`
(`session.rtos-console-log-readable-via-devmem-20261102`); the two live IPCC
rings are A (RTOS→Linux) and B (Linux→RTOS)
(`session.ipcc-live-rings-a-b-20261102`); a stock still is 24 frames,
including Linux replies to `0x82/0xaa06`, the gyro request and four buffer
returns (`session.stock-still-message-exchange-20261102`); a graceful stop
sends nothing to the RTOS (`session.graceful-stop-sends-nothing-to-rtos-20261102`);
a restarted stock app enters boot-snapshot creation
(`session.manual-app-start-enters-hibernation-20261102`).

3 November plan step 4k (camera observations, two lethal runs):
- **Start-up:** our probe's start-up writes nothing to shared memory
  (`session.probe-startup-writes-no-shared-memory-20261103`).
- **Answers:** a dying still's shared-memory writes before the death match a
  surviving coexistence still's
  (`session.dying-still-shared-writes-match-surviving-20261103`).
- **Request split:** in coexistence the stock app's library answers the gyro
  and still-expansion requests, while adjust requests are split
  (`session.coexistence-splits-requests-between-libraries-20261103`).
- **DDR:** the rate stays at 700 MHz through a lethal still
  (`session.ddr-rate-unchanged-through-lethal-still-20261103`).
- **Death timing:** varies from +5 to ~+130 ms after `restartLiveview`
  (`session.death-time-after-liveview-restart-varies-20261103`).
- **4l:** a coexistence still dies when only the stock app's IPC threads
  are stopped, so the rest of the running app does not protect it
  (`session.coexistence-still-dies-with-stock-ipc-threads-stopped-20261103`).
- **4m:** with the stock interrupt waiter active, the RTOS's `0x81`
  gyro/expansion request goes only to the stock library; a 42-second-late
  stock answer still survives
  (`session.stock-waiter-takes-0x81-request-late-answer-survives-20261103`).
- **Static (REA):**
  - `/dev/d5_ipcc` gives each interrupt record to one process from a global
    queue (`session.kernel-ipcc-fmt-wait-global-single-consumer-queue-20261103`);
  - user-space callbacks are process-local
    (`session.udd5-interrupt-callbacks-are-process-local-20261103`);
  - line registrants: bridge line 0, AAC line 5, HEVC line 9
    (`session.ipcc-interrupt-line-registrants-20261103`);
  - the gyro handler is stateless
    (`session.gyro-mode3-handler-is-stateless-20261103`).
- **4n-check:** a running stock app does not protect the still. The
  outcome follows which library instance answered the gyro request
  (`session.gyro-request-handler-instance-separates-outcome-20261103`).
- **4o:** the stock gyro answer is not enough; the stock IPC threads must
  stay active through the liveview restart
  (`session.stock-ipc-needed-after-gyro-answer-20261103`).
- **4p:** survival matches the stock library handling the gyro request and
  the first adjust request after it
  (`session.stock-gyro-and-next-adjust-rule-20261103`).
- **4q:** the still-expansion ring and its flags behave the same in a
  surviving and a dying still
  (`session.expansion-ring-same-in-survivor-and-death-20261103`).
- **RTOS side, second pass (static plus read-only sysfs):**
  - the kernel I2C driver takes hardware locks 26–28 for buses 0–2 only
    (`session.kernel-i2c-locks-buses-0-2-with-ids-26-28-20261103`);
  - no user-space code takes a hardware lock; lock 25 has no Linux user
    (`session.no-user-space-hardware-lock-taker-20261103`);
  - lock 28 guards the PMIC bus (Linux bus 2) shared with the RTOS's DVFS
    (`session.lock-28-guards-shared-pmic-bus-2-20261103`);
  - the RTOS image sensors use I2C controllers 3 and 4, the same hardware
    as Linux's codec bus and fuel-gauge / USB-PD bus, with no lock on
    either side
    (`session.rtos-sensor-i2c-shares-linux-buses-3-4-unlocked-20261103`);
  - the in-kernel raw handler is an RTOS fault notice, never logged in any
    trial (`session.kernel-raw-slot0-is-rtos-fault-notice-never-logged-20261103`).
- **I2C interrupts through a stock still:** Linux's bus-4 traffic overlapped
  the RTOS's rear-sensor access during the liveview restart, and the camera
  survived (`session.linux-bus4-traffic-overlaps-surviving-restart-20261103`).
- **The still's JPEG encode** runs on the image engine's JPEG encoder,
  with a 2-second wait
  (`session.still-jpeg-encode-start-waits-2s-on-ep-encoder-20261103`). The
  claim that it never ends in deaths was **rejected** in step 4v: the SD
  reader stopped before the reboot
  (`rejected.still-jpeg-encode-never-finishes-in-dying-runs-20261103`).
- **The image engine is shared:**
  - the RTOS learns the encode is done through acc interrupt slot 1
    (`ACC_JPEG_END`, enable register `0x20821080`)
    (`session.rtos-jpeg-done-via-ep-acc-slot1-20261103`);
  - Linux maps the same registers into user space, and the kernel's
    `drime5_ep` driver handles the same acc interrupt for the bits in its
    mask. Its enable/disable ioctls change the shared register
    (`session.linux-shares-ep-acc-interrupt-with-rtos-20261103`).
- **On the camera:** Linux's image-engine masks are 0, and only the stock
  app has the engine open. Linux sees the engine's interrupts during a
  stock still, including one at the JPEG end, but claims none
  (`session.linux-sees-ep-interrupts-claims-none-20261103`). The kernel's
  claim-every-bit switch is the A7 fault flag, a recovery path
  (`session.kernel-ep-all-bits-switch-is-a7-fault-flag-20261103`).
- **4s:** in a dying still the image engine raises its JPEG-end interrupt
  on time (restart +32 ms, like a survivor), and Linux does not take it.
  The camera dies soon after
  (`session.dying-still-ep-signals-jpeg-end-on-time-20261103`).
- **The JPEG-done path:** the three encodes are chained, and in the death
  the RTOS handled the first two. After the main encode's done interrupt
  the only hardware access is the stream-count read `0x2082a280`. The
  concurrent liveview start rewrites image-engine clock registers without
  a lock (`session.jpeg-done-path-one-register-read-20261103`).
- **Image-engine top registers through a stock still:** the RTOS switches
  the JPEG slots on (acc enable `0x6`) at restart +5 ms and off at +32 ms,
  just before "Enc Work is Ended". The clocks don't change during the
  encodes (`session.ep-top-registers-through-stock-still-20261103`).
- **4t:** the register watch in a dying still. The register sequence
  matches the survivor up to the JPEG slots, and the camera died around
  the main encode's end, too early to read the marker
  (`session.step4t-death-at-main-encode-end-20261103`).
- **4u:** a third death at the main JPEG encode's end, which is when the
  image set is sent to Linux
  (`session.deaths-cluster-at-main-encode-end-20261103`).
- **The RTOS's image-engine interrupt dispatch:** it runs on the same four
  lines as Linux and handles only bits in its own shadow mask. The JPEG
  error path does a JPEG-block software reset and re-encodes
  (`session.rtos-ep-acc-dispatch-and-jpeg-retry-20261103`). The image-engine
  passes are correct as static descriptions but are not the cause (4v).
- **What can reset the image engine during the restart:** the chip-level
  reset in the liveview start needs a 2-second frame error, which no run
  had. Backend DMA-path teardowns reset shared blocks 10/11 when a group
  empties. That one is open
  (`session.rtos-ep-resets-and-rmu-reset-ruled-out-20261103`).
- **Blocks 10/11 are the shared DMA muxes,** which the JPEG encoder (own
  DMA) does not use. The JPEG completion's first blocking point is the
  image-engine driver's lock 3
  (`session.ep-dma-mux-blocks-not-jpeg-and-lock3-20261103`).
- **Thread scheduling is identical:** the stock app's and our probe's IPC
  threads have the same policy and priority. Wi-Fi streaming from the
  camera works (`session.ipc-thread-scheduling-identical-20261103`).
- **4v — the cause of the deaths:** with the recorders streamed over Wi-Fi,
  the kernel log shows **our probe's `MCB_Receiver` thread segfaulting** at
  address `0x6f0` as it handles the image set. The kernel then logs `CA9
  fault occurs, reboot now!!!` and reboots the camera
  (`session.dying-still-is-receiver-segfault-and-kernel-reboot-20261103`).
  The image-set handler `CCapImageSetCb::OnCallback` calls
  `Apex_Get_Max_Av_Value_For_Capture` → `CApex::GetMaxApertureValue`, which
  reads through a pointer that only `Apex_Initialize_A9()` sets. The stock
  app calls it at start-up; our probe does not
  (`session.image-set-callback-needs-apex-initialize-a9-20261103`).
- **4y — fixed:** with the APEX gate (a private copy of the user-spec block
  for the APEX singleton, no shared writes), our client's still survives.
  It receives the image set and the still-completion notices, and the
  kernel logs no fault (`session.apex-gate-still-survives-20261103`).

2 November drive trace: the drive's post-`STL_CAPTURE_START` path is traced
as far as the evidence goes, and two handoff assumptions change.
**`SendCaptureMsgtoFrontEnd` is a trace label, not a message send** - the
literal has one reference, inside `CDriveBaseNx::StartCapture`, and
`FUN_800aaa60` at that site is a `CAP_CTRL` timing-marker recorder whose
result is discarded, so the named function was inlined and the front-end
hand-off is not an observable call edge. **The sensor programming is
`FUN_8023013c`** - a 30,252-byte routine that emits
`[SENSOR_DRV_LOG] [%s] PowerOnSeqByRegWrite: done` and performs the power-on
sequence by register writes (`FUN_80243928` then `FUN_80243a04`), with its
whole write body skipped when the word at `0x85180000` is nonzero. The
shallow path is reached twice, both routes converging on `FUN_800bda58`, and
`FUN_800bd560` **forks** on that function's return. The next edge is a
single address: `0x8027510c`, the target of `StartCapture`'s last virtual
call through vtable slot `+0xB0` (the `CDriveBaseNx` vtable at `0x8067d240`;
its `+0xBC` slot lands on the known `checkBufferFull`). Nothing traced
references `checkBufferFull` or a memory-manager entry point, so the sensor
path and the SSIF-allocation path are separate branches - the
buffer-starvation hypothesis is neither confirmed nor refuted, and **no cause
is claimed**. Facts
`session.rtos-sendcapture-is-a-trace-label-not-a-message-send-20261102` and
`session.rtos-drive-startcapture-sensor-poweron-path-20261102`; notes
worker drive trace (not published: `../../output/decompile/rea/pass-log-read-probe-20261102/worker-drive-trace.md`)
and parent acceptance (not published: `../../output/decompile/rea/pass-log-read-probe-20261102/PARENT-ACCEPTANCE-20261102.md`).

2 November log window: the RTOS's log destination is **not** in the
Linux-visible shared window. The camera-safe log-read probe read the
preference window through libuser-spec's `pref_rtos_get_long` on a healthy
camera (the stock application running, no stop, no power cycle, no still):
the known boot-type preference at offset `0x1268` read **1**, and the
sixteen words at the handoff's offset `0x11c3c` read **zero**. A second
probe dumped the whole `0x81000`-byte window twice, half a second apart:
299,314 non-zero bytes of binary structure, **no printable run of eight or
more characters anywhere**, and the two dumps **byte-identical**. The
`0xff` fill that begins at exactly offset `0x34320` is the RTOS
initializer's guarded clear at RTOS `0x85400000+0x34320`, so the window's
RTOS base is `0x85400000` - not the `0x81b00000` the handoff's offset
assumed - and the log destination `0x81c11c3c` is `0x38EE3C4` **below** the
window. The claim `session.rtos-log-ring-and-shared-send-20261102` is
superseded and `rejected.rtos-log-is-readable-through-the-preference-window`
records the disproved inference. Fact
`session.rtos-log-destination-is-outside-the-linux-shared-window-20261102`.
Log read result (not published: `../../output/decompile/rea/pass-log-read-probe-20261102/LOG-READ-PROBE-RESULT.md`).

2 November +0x38 decoded: the still's death check is a **64 MB page
allocation probe** — `checkBufferFull` → the manager's vptr+0x38 (0x800ad300)
→ the +0x34 → `FUN_800afb80(manager, 1024, '0', 3, …)` → a first-fit scan
for 1024 consecutive free 16 KB pages in the bank-3 range
`manager[0x1a031]..manager[0x1a172]`, committed on success, "Cannot alloc
SSIF buffer!" on exhaustion. The manager (created by `FUN_800a1258` via the
ctor `FUN_800ab41c`, vtable 0x8062b790) starts with a fully-free 26,624-page
table — so the bare-context failure is either the range endpoint or the page
state, both now directly observable. Facts
`session.rtos-memory-manager-object-graph-20261102`,
`session.rtos-still-check-is-64mb-page-alloc-20261102`.
+0x38 decoded (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/PLUS38-CHECK-DECODED.md`).

2 November death mechanism: the still's capture starves for the SSIF buffers
(`Capt::RawBufferStarved`) and the capture controller's `turnOff` powers the
camera off ("CaptureController : Start Power Off"); the buffer request is the
command 0x1080029 (`CCapCmdIf::RequestMemory`, the NX memory manager), reached
from Linux through the library's `RequestImageBufferMemory` API, which the
bare client never calls. Facts
`session.still-death-buffer-starvation-poweroff-20261102`,
`session.rtos-requestmemory-command-20261102`.
Death mechanism (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/DEATH-MECHANISM-BUFFER-STARVATION.md`).

1 November app ops map: the app's `UI_Mm_Camera_Start_Lock3a` (0x119008) is
the ioctl selector 1 (the 3A_LOCK) with a 0x10-byte payload — the app issues
the still only after the Lock3a, so the still pends and fires on the lock
completion against a 3A-configured sensor; the bare client skips it. The full
wrapper/selector/command map is recorded in APP-OPS-MAP.md. Fact
`session.app-lock3a-before-still-20261101`.
App ops map (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/APP-OPS-MAP.md`).

1 November warm-boot init: OP 0x1070006 (`FUN_80111a10`) writes the power
state 1 and warm-re-inits — the Linux-reachable power-on (the ioctl selector
0x43, the app's `UI_Mm_Camera_Set_Warm_Boot_Init`), reversing a graceful
stop's PowerSave. The app's `UI_Mm_Camera_Destroy` has no callers — the app
never destroys its camera handle. Facts
`session.rtos-warmbootinit-is-linux-poweron-20261101`,
`session.app-camera-destroy-never-called-20261101`.
Power-state chain (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/POWER-STATE-CHAIN.md`).

1 November mode change: `E_CAP_CMD_OP_MODECHANGE` = OP command 0x1070004 =
`CCapCmdIf::ChangeMode` (payload 0/1/2 → controller mode 3/2/5 via the
MessageAdapter's route-0x1122 send); the still's `handlerBody` derives its
drive mode from RTOS status 0x43, and a bare client never mode-changes, so
its still always runs the boot-default 0x430000 branch. Facts
`session.rtos-modechange-op-command-20261101`,
`session.app-wakeup-is-modechange-ioctl44-20261101`.
Mode change (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/MODE-CHANGE.md`).

1 November power-state chain: the RTOS `CCapCmdIf+0x30` power state (1 on /
3 power-save / 0 off) skips the liveview in state 3 or 0; `DoPowerSave`
(category 22) sets 3, `DoPowerOn` (0x1070001) sets 1 but is dispatched only
from the RTOS's own boot (`init_capture_framework`), and the library's
`OperateCapture` 0x1070001 arm is local-only — a Linux-side power-save is not
reversible by any recovered command. Facts
`session.rtos-power-state-gates-liveview-and-still-20261101`,
`session.rtos-poweron-only-at-boot-20261101`,
`session.library-op-powersave-vs-local-poweron-20261101`.
Power-state chain (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/POWER-STATE-CHAIN.md`).

1 November image-set extraction: the encoded-still set's layout is type@+0
(1/2/3), first image descriptor@+8 with virtual address@+0x5c (filled by the
library before the callback) and size@+0x7c; the first image's own error text
names it **SRW** ("srw address convert error."). The client copies `size` bytes
from the virtual address and releases. Fact
`raw.image-set-extraction-layout-and-srw-naming`.
Extraction (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/IMAGE-SET-EXTRACTION.md`).

1 November RAW provenance: the still pipeline runs a **Low Raw Compression**
(`PP_LRC`, WDMA-assisted) stage; the first image ("Raw Compress") is the LRC
output with a tracked compressed size, distinct from the SSIF 10-bit-in-16-bit
sensor stream. Fact `raw.first-image-is-lrc-stage-output`.
LRC stage (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/RAW-PROVENANCE-LRC.md`).

1 November RAW bit depth: `judgePath` selects a path code from PP route bytes —
path 3 (+0x2c zero) → setup 2 → **b = 12**, path 5 (+0x2c nonzero) → setup 1 →
**b = 10**. The measured 18-Oct raster matches the 10-bit route (bits 6..15).
The stride derivation's units stay unproven. Fact
`raw.path-code-selects-raw-bit-depth`.
Bit depth (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/RAW-BIT-DEPTH.md`).

1 November image-set delivery: `CCapImageSetCb::OnCallback` (`0x7f004`) delivers
the image set as a pointer to a stack-local `stCAPTURE_IMG_SHARED_SET`
descriptor (geometry + pixel virtual address), copy-only with no ownership
transfer; the pixel buffer lifetime stays open. Fact
`session.image-set-delivery-is-stack-descriptor-20261101`.
Delivery (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/IMAGE-SET-DELIVERY.md`).

1 November still-command preconditions: `CCmdCaptureNormal` refuses a still
unless `isRawWorkEnd` (RAW counter == target) holds and the Lock3A flag byte is
clear, else saves it Pending; `handlerBody` derives capture type/count from
RTOS mode status `0x43`/`0x51`/`0x46`, and `setPostCondition` needs a
`CCaptureCondition` and emits `0x1010003` (STL_CAPTURING). A bare still is
refused or Pending without prior RAW/mode setup. Fact
`session.still-command-preconditions-20261101`.
Preconditions (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/STILL-COMMAND-PRECONDITIONS.md`).

1 November still-path static follow-up: the id-7 completion gate is armed by
`CDriveBaseNx::StartCapture` and gated by `isNeedtoSendCaptureReady`, firing
from `onStartFrame`; and `mm_camera_capture_still`'s third argument is the mm
capture type (1=SINGLE→command 0x1010001, 0x15=LOCK3A_CAPTURE→0x1010016),
selected by the app from `lock3a_complete`. The diagnostic had passed 21
(0x15) in that slot; corrected to 1. Facts
`session.rtos-capture-ready-armed-by-drive-start-20261101` and
`session.mm-capture-still-capture-type-argument-20261101`.
Capture type (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/CAPTURE-TYPE-SELECTION.md`),
gate (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/CAPTURE-READY-GATE.md`).

1 November parent acceptance: MiniMax liveview-contract branch (e7b25562)
fast-forward-merged into master. Four load-bearing RTOS claims independently
re-verified through the REA/Ghidra bridge against the pinned carrier; all 15
new session facts accepted, and the two worker-found defects (handle by-value,
and the trial-04 missing id-7 subscription) confirmed. The still completion is
id 7 / topic 0x1010007; the admission gate does not block the still, but no
still has completed and the camera stops answering during the hold.
Acceptance (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/PARENT-ACCEPTANCE-20261101.md`).

1 November still-path static recovery: **the still completion is handle
callback id 7, `MM_CAMERA_CB_STL_CAPTURE_READY`, on internal topic
0x1010007.** The library's id-to-public converter at 0x477bc maps id 7 to
public topic 0x2010007, the subscribe converter maps that to internal
0x1010007, and the RTOS's `CCapturePublisher::NotifyCaptureReady` at
0x8006ed14 publishes on exactly that topic. The application's own id table
gives id 8 as `MM_CAMERA_CB_LV_VIDEOSTART_DONE`, independently confirming
the trial-03 liveview reading. Sensor RAW travels on a separate channel —
the image-set topic 0x3010005 internal, 0x4010005 public — via
`CCapImageSetCb::OnCallback` at 0x7f004. This exposes a defect in the
trial-04 diagnostic: it subscribed only the image-set topic and never called
`mm_camera_set_notification_id`, so it could not have received the id-7
completion even had the camera survived. The next diagnostic must subscribe
id 7, and the still sequence ids 1, 3, 4, 5, 6 and 7 for a full picture.

Also recovered: **3A is a capture mode with a lock state machine, not a
computation** — `E_CAP_CMD_3A_LOCK` on the Linux side, `Control3A` at
0x55088, `E_CAP_CMD_STL_LOCK3A_NORMAL` and a `Lock3ACapture` state machine
on the RTOS.
Still path (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/STILL-PATH.md`).

1 November liveview-first still trial: **the still was submitted and the
transport accepted it — the call returned 0 — and then the camera stopped
responding during the observation hold.** The probe performed the same
category-0x10 handshake that unblocked the liveview edge, which raised the
destination-alive byte from 0 to 1 at 100 ms; the liveview ioctl then
returned 0, and mm_camera_capture_still with the reviewed raw17 ABI returned
0 as well. The latch read 1 immediately before and after both requests, so
**the admission gate does not block the still**. The camera then stopped
answering: the execute call, the recovery call and the after-run identity
check all timed out, helper termination was unconfirmed, and the owned
directory was kept for diagnosis because the final guards could not be
verified. This is the same signature as the 29 October still trial.

After physical recovery the retained directory was already gone, so the
helper's log tail was not recovered, and **the SD card holds no file from the
still**. The camera was verified clean.
Trial 04 (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/TRIAL-04-REVIEW.md`).

31 October liveview trial 03: **the liveview start edge now works from our own
client.** One explicit empty category-0x10 send through the transport raised
the destination-alive byte from 0 to 1 at 50 ms, the same latency as the
earlier accepted observation; the ioctl then returned 0 instead of
0xFFFFFFFF, and one notification arrived on handle id 8. The camera dlog
shows that notification as E_CAP_NOTIFY_LV_VIDEOSTART_DONE on a thread of our
own helper process, emitted after our subscription and delivered to the
app-facing handler — the first observed delivery of the RTOS completion
notification through the id-8 route.

What the notification *means* is still unsettled: the RTOS raises it on the
failure branch of CCmdLiveview::start as well as on first-frame paths, so
the arrival proves the route delivers, not that the stream started. Request
association, capture completion and readiness remain unestablished, and no
still was issued.
Trial 03 (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/TRIAL-03-REVIEW.md`).

31 October liveview trial 02: **the destination-alive byte read 0 immediately
before the liveview request**, and the request failed with the library's
0xFFFFFFFF sentinel. The byte is the one the send path tests — the bridge
getter at 0x17364 and the gate inside CSender::Send at 0x179d8 resolve the
same GOT slot 0x2256c, which holds the exported
_ZN7CSender12m_bDestAliveE byte at 0x22ad8 — and both views agreed at every
sample. Since the liveview category 0x83 does not qualify for the 0x10
bypass, a zero byte makes CSender::Send queue the frame instead of
transmitting it and return -1, which is the value the library turns into the
sentinel. The RTOS side is not the cause: its handler rejects only a length
other than exactly 28 bytes, the library sends exactly 28, and its
frame-size check sits behind an RVF test the library's payload does not
reach. Why the byte was zero, and whether it is per-process or per-device,
remain open.
Trial 02 (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/TRIAL-02-REVIEW.md`).

31 October next edge: the liveview start failure chain is now closed statically.
The LV_START ioctl arm calls the thunk 0x389bc, which the provider resolves as a
thunk to ControlLiveview 0x537dc; its LV_START arm sends attribute 0x301 with
28 bytes, and CMCBAdapter::SendCommand 0x9abc8 forwards to
CMulticoreBridge::Send with category 0x83. In the bridge, CSender::Send 0x179a8
admits a send only when the category is 0x10 or the destination-alive byte is
nonzero; 0x83 does not qualify, so with that byte zero the frame is queued and
the call returns -1, the value the library turns into the 0xFFFFFFFF ioctl
sentinel. The library logs "Liveview on start!" before the send, so that line
does not show the command was transmitted. The destination-alive byte is written
only by an incoming category-0x10 broadcast or by mcb_set_ignore_init_checking;
the capture library never writes it. On the RTOS side, the one liveview
precondition found, check_boot_liveview_enable, is a boot-time display and
boot-shot gate rather than a command-path gate, and the E_CAP_CMD_LV_START
handler itself was not located. The RTOS producer of the completion
notification is located: NotifyVideoStartDone at 0x8006ef5c sends on topic
0x1030001, numerically the internal topic 0x01030001 behind public topic
0x02030001 and handle id 8, confirming the id-8 contract from the producer
side. Whether the latch byte was zero at trial time is still open.
Next edge (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/NEXT-EDGE.md`).

30 October liveview trial 01: an independent client registered the app-facing
notification callback and subscribed handle id 8 (all three calls returned 0);
the liveview start edge reached ControlLiveview/StartLiveview and the outgoing
E_CAP_CMD_LV_START, but the ioctl returned the library's failure sentinel
0xFFFFFFFF and no notification arrived on id 8 in 20 seconds. Cleanup verified,
stock app restarted, camera clean. The failure is inside the LV_START arm; its
cause is open, as is any delivery property of the id-8 route.
Parent review (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/trial-01-review.md`).

30 October liveview contract: handle callback id8 ↔ public topic0x02030001 ↔
internal topic0x01030001 byte-verified in the pinned library, with the
app-facing callback receiving the small id, data pointer and length. The
camera handle is passed BY VALUE to both notification entry points and to
ioctl; a delegated diagnostic that passed it by pointer was corrected and
rebuilt. Registration must precede id subscription. No camera action; no
delivery, association or readiness claim.
Parent review (not published: `../../output/decompile/rea/pass-liveview-contract-20261030/parent-review.md`).

30 October power-off C6: MiniMax47 proposals triaged; bounded stop/suppression
survival accepted, global shutdown/causal exclusions rejected. Stock index21/1
and zero selected flags qualified. Main next edge is stock's conditional liveview
ioctl0x1b and callback8 contract, distinct from mm_camera_start; shared status
boolean is not freshness. Source bisect logs/build switch corrected, host and ARM
checks passed. User away: offline/readonly only. No owned capture/RAW yet.
Parent C6 (not published: `../../output/decompile/rea/pass-poweroff-integration-20261029/parent-review.md`).

29 October first own-client still attempts: two facade calls returned0; the first
had an invalid subscription domain, now corrected; the corrected attempt lost
its final result as camera powered off (user confirmed). Cause is open. Physical
recovery now verifies normal stock app, no helper or owned directories. Public
subscription04010005→internal03010005 and the mapper zero-input/return0 warning
are accepted static contracts. No fresh still or owned RAW.
Parent C6 (not published: `../../output/decompile/rea/pass-own-transport-20261029/parent-review.md`).

29 October own-transport target trial: with stock absent, our guarded helper
logged local category10-alive latch0→1 at50ms after one explicit empty send;
full hold/normalclose and cleanup passed. Packet/request association and capture
readiness remain open. Next is a concrete diagnostic still path.
Parent C6 (not published: `../../output/decompile/rea/pass-own-transport-20261029/trial-01-review.md`).

29 October startup-verification C6: the retained guarded trial-05 snapshot now
shows our own helper with d5_lock/d5_ipcc descriptors and IPC/receiver/sender
tasks while run.json reports the stock app absent. This closes the local-process
observation gap; a fresh associated reply, remote readiness, reusable clean
handover and owned RAW remain open. All31 proposals triaged; global flag-writer/
freshness and SDK-load-closure proofs rejected. Runner and optional diagnostic
corrected, host checks and ARM link passed; no new camera action.
Parent C6 (not published: `../../output/decompile/rea/pass-startup-integration-20261029/parent-review.md`).
Earlier checkpoints below are historical.

29 October verification repeat: SDK calls/hold/close completed, but automatic
sampling failed due to CGI newline removal. Observer corrected and live
transport-only metadata qualification passed against PID1. No receiver or RAW
claim promoted. See current checkpoint and goal-reset parent review.

29 October goal-focused target test: stock service stopped with access intact;
our init-only client returned0 but had no observed local transport/receiver at
the pinned sample. Later guard abort is explicit; helper termination and
temporary-file cleanup verified. Added held-probe diagnostic/stage logs. The
next milestone is explicit local startup plus a defined remote response. Full
retirement/lock catalogues are deferred. See [milestones](../CAPTURE_MILESTONES.md)
and parent review (not published: `../../output/decompile/rea/pass-goal-reset-20261029/PARENT_REVIEW.md`).
The corrected explicit-bootstrap probe completed its calls/hold/close normally,
but the manual receiver snapshot was missed. Automatic observation is now ready;
a physical-recovery confirmation for verification remains pending. No receiver
readiness or owned RAW is inferred from the successful returns.

29 October04:30 C6 adds five static contracts (452 unique claims): receiver
status-byte clearing with a continuing loop, the IPCC
signal-pending escape, default
RTOS ops publication/init masking, conditional peer-register correspondence,
and the Linux lock register mapping:
virtualf8600000 uses PFN6000<<12, physical06000000, length1000. Live MMU,
RTOS peer ownership and clean handover remain open.
Parent C6 (not published: `../../output/decompile/rea/pass-lock-mapping-20261029/parent-review.md`).

29 October Linux lock C6: six bounded contracts accepted;447 unique claims.
Mutex cleanup/partial-error limits, d5_lock no-op open/release, separate payload
status, ordered init writes with masked result, and explicit free bit-check
are recovered. Clean stock-app handover remains unsolved. Next is hardware
mapping/RTOS peer ownership and whole teardown ordering, not a camera trial.
Parent C6 (not published: `../../output/decompile/rea/pass-handover-integration-20261028/parent-review.md`).

28 October handover-branch review: all five commits preserved with C33-C50
triage. A narrow mode slot+0x0c leaf contract is accepted; the global no-reset/
only-bit-clear proof is rejected. Same-owner sites do not establish an ordered
per-request cycle. Standby/provider/sensor traces corroborate existing facts.
Parent review (not published: `../../output/decompile/rea/pass-handover-integration-20261028/parent-review.md`).

28 October resource checkpoint:439 unique claims; five static facts and two
rejected proofs added this stint. Stock-app handover remains unsolved. The
next main edge is actual reset invocation on the mode owner, from preserved
MiniMax phase05 partial evidence. All native sessions exported/closed.

28 October PostProcess C6 joins its normal Done-list arguments and confirms
that a nonzero material-hook result does not suppress callbacks on the checked
status arms. This qualifies execution/cleanup behavior, not lifetime ownership.

28 October second-consumer C6 accepts conditional image-word stores into
receiver slots. A pointer-slot write establishes neither pixel direction nor
producer/retention. See raw.fe-second-io-stage-copies-conditional-image-word-slots
and the parent review (not published: `../../output/decompile/rea/pass-selected-bootstrap-20261028/parent-review.md`).

28 October callback C6 accepts the conditional Done-wrapper slot8 to material
notification0x320e, with repeated-getter and indirect-effect limits. The selected-
derived proof is rejected because factory masking maps14001 to14000. The
second FE consumer80322598 remains under investigation.

28 October backing review rejects whole-record zeroing inferred from the
first16-byte plane clear. The second copied block is not certified zero;
finite caller scans do not close subsequent writer/identity continuity.
Parent C6 (not published: `../../output/decompile/rea/pass-selected-bootstrap-20261028/parent-review.md`).

28 October continuation: concrete receiver constructor/getter and dispatch
slots accepted conditionally. Secondary interface+0x40 is distinct from
listener storage+0x44; primary+c binds t_Main and secondary+8 subtracts0x40
before OnInterrupt. This proves static pointer/slot identity, not readiness
or retirement. Parent review (not published: `../../output/decompile/rea/pass-selected-bootstrap-20261028/parent-review.md`).

28 October receiver-branch integration: two narrow native dispatch contracts
accepted, four overclaims rejected; all49 worker proposals are triaged. Entry
record+4/+8 and threadRun vptr+0x0c are recovered. Readiness, fresh capture and
owned backing remain open. RTOS0x800c69bc is a return stub, contradicting the
proposed runtime frame builder. Existing mapped carrier data needs no such
remapping. Parent dispositions (not published: `../../output/decompile/rea/pass-receiver-integration-20261028/parent-review.md`).

28 October reviewed continuation (six new static facts, two rejected proofs): local adapter singleton acquisition and constructor
event-create masking are accepted static contracts. The getter publishes the
preserved allocator pointer after constructor return; neither the pointer nor
that return certifies resources/subscriptions. Original ELF relocation is
separate from runtime pointer identity. Initialize's incoming r0 is preserved through subscribe; its argument-free
decompiler signature is incomplete. The actual DataManager/Publisher subscription
stages are literalzero stubs in this build, not additional registrations in that
chain. MCB and separate orientation startup remain to qualify. FThread Create's0/1 result can mask pthread attribute setup errors, while the
already known Destroy/no-join contract is narrowed by its flag/self-ID gates.
Direct caller lists do not prove bootstrap impossible; unresolved imports do
not prove clean -1 returns. Hosted startup remains open.
Parent review (not published: `../../output/decompile/rea/pass-local-bootstrap-20261028/parent-review.md`).

27 October MiniMax branch review: six consolidated static session contracts
accepted and three overclaims rejected after fresh Ghidra/original-byte checks.
0x7101 tests bit1 but its default-owner clear is range-rejected; only stable-word
no-signal cases are accepted. Base/mode slot+0x18 is the same no-op, not an override.
Two owners have distinct bounds9/12; the26 arm callers are not one request’s
responder set. Safe handover, exclusive hosted startup and retained RAW remain
open. Parent acceptance and all13 dispositions (not published: `../../output/decompile/rea/pass-autonomous-review-20261027/parent-review.md`).

27 October extension: mode-owner callbacks and explicit pending-word reset are
recovered, but per-request reset/retirement remains unjoined. The analyzed AWB
INIT completion forwards command0x7101; its bit-test/reset behavior is the next
native edge. No valid-index or fresh-completion inference is accepted from it.
Extension review (not published: `../../output/decompile/rea/pass-mode-dispatch-20261027/tail-review.md`).

27 October manual continuation: the actual mode1 descriptor selects handler63
and requests **OFF state0**; IDLE is state2. State is stored before entry, and
transition/standby results are masked before conditional participant1 completion.
The concrete interface is bound: its state1/2 stop sequence polls a hardware
bit without a deadline, logs GPIO errors and masks sleep failure before state4.
Queue creation dimensions are not participant identity. New-P creation/tail
insertion and first-list selection clarify the RAW activation question. Encoded
ring marker checks do not enforce writer exclusion; the reader can fall back
without rechecking readiness. These bounded static facts establish neither
physical drain/exclusive hosted startup nor retained sensor RAW. See the
parent review (not published: `../../output/decompile/rea/pass-mode-dispatch-20261027/parent-review.md`).

27 October04:10 continuation: native SVC6 recovery qualifies the idle event
operation as keep-mask clear followed by an ALL-bit wait for0x1122 with
whole-word clear on success. Local submission drops the lower return; the wait
wrapper treats timeout/wait-disabled together, and DoPowerSave ignores the
mode-change outcome. These recover more of the shutdown protocol without
establishing request freshness, quiescence, reset or exclusive owned startup.
The recovered rendezvous callback sets the entire0x1122 mask at once after its
current pending word becomeszero; its index bound9 is not an enrollment proof.
The still assistant's24-byte pointer record and its receive failure gates are
also recovered. Start/ReStart publish their supplied procedure P and context
word while dropping send status; selected construction-to-Start identity,
fresh receive and RAW pointee retention remainopen.
Primary T-Kernel source is a pinned ABI comparison, separately verified against
the native service bodies; no whole-build/live identity is inferred. See the
parent review (not published: `../../output/decompile/rea/pass-idle-ownership-20261027/parent-review.md`).

26 October23:05 continuation (finished after midnight27 October): the stock
shutdown initiator is the preceding powersave category22 send; the later
poweroff/unrealize operation waits. Bound the RTOS category22 handler and
zero-body21 notification to Linux SystemDown. Duplicate RTOS state and ignored
idle-transition errors prevent treating that notification as a qualified reset.
Recovered actual pre-callback execution and conditional ordering before LRC
SetParam. Selected backing, exclusive hosted initialization and phase2 remain
open; capture capability staysfalse. Review, byte/table binding and provider
failure details are in the parent review (not published: `../../output/decompile/rea/pass-night-host-20261026/parent-review.md`).

26 October owned-start review adds three accepted session facts: unrealize
can mask its power-off wait failure; the conditional SystemDown handler signals
the local wait event; partner-status setters only store property bytes. None is
a qualified remote reset or handover. The RAW stack-aggregate no-consumption
inference is rejected; selected backing remains open. See the
review (not published: `../../output/decompile/rea/pass-owned-start-20261026/parent-review.md`).

26 October local source comparison is complete. [external.yaml](external.yaml)
records five bounded public-source findings: verified NX500/SM-R210 plaintext
overlap, C200 helper/report drift, nx500ex control/debug-file limits, the actual
LibRaw Samsung3 input contract and the NX1 SDK documentation boundary. These
do not transfer third-party native/runtime claims to SM-R210. The
[comparison](../LOCAL_SOURCE_COMPARISON_20261026.md) and its pinned local
inventory distinguish reusable code from ownership/format gaps.
`session.owned-initializer` and selected LRC backing remain open; capture
capability stays false.

26 October public-source search adds an external-leads pointer to the open
owned-initializer question. Metol, C200 and nx500ex contain related experiments;
their claims are not promoted to SM-R210 hardware facts. See the
[public research review](../PUBLIC_RESEARCH_20261026.md).

26 October post-reset review: accepted native drive-to-first-image selection,
conditional image-pointer construction joins, screen/thumb list selection and
release conversion/outcome limits. The prior +48 descriptor lead is corrected
to the byte+120 interrupt-listener unregister edge. Library write-done sends
addresszero and does not close selected address-list cleanup. Seven new static
claims,27 original-byte windows and24 exported/closed native sessions are in the
parent review (not published: `../../output/decompile/rea/pass-base-lrc-20261026/parent-review.md`).
Selected RAW backing, fresh association, whole-result retirement and phase2
remain open; capture capability stays false.

26 October resumed continuation: `completion.adapter-defers-release-during-native-call`
closes the reviewed setter/submission versus callback-release overlap in the
host component. `host.orientation-singleton-is-not-subscription-status` and
`session.local-adapter-initialize-propagates-only-subscription-chain` narrow
bootstrap success signals. Parent source/sanitizer/build review is recorded in
the current pass (not published: `../../output/decompile/rea/pass-lrc-export-20261026/parent-review.md`).
The common submission ABI word stores and bracket fallback-tail difference are
accepted. The LRC selector/null gate is corrected: null does not itself cause
material fallback. Selected-base backing and the partial whole-result retirement
inventory remain open/unreviewed; no camera capability is enabled.

26 October offline continuation is saved. [Host-service contracts](contracts/host-services.yaml)
join the ordinary gyro request to the library's builtin fd_alg writeback and
selector9/event2 return. A reply can carry defaults; runtime activation,
properties/mapping and the inventory of other required responders stay open.
`session.poweron-gates-local-orientation-subscriber` separates shared startup
state from local subscriber construction. The selected ordinary encoded-builder
call passes null argument12; LRC provenance/transform remains the next image edge.
`completion.explicit-caller-result-release` and
`completion.adapter-latched-copy-and-retirement-state` record validated software
lifetime/cleanup repairs. No camera capability is enabled; phase2 remains open.
See the parent review (not published: `../../output/decompile/rea/pass-host-services-20261026/parent-review.md`)
for tests, exact evidence, integration and interrupted-worker lifecycle limits.

25 October independent strategic review:
ASTRA_INDEPENDENT_REVIEW_20261025.md (not published: `../ASTRA_INDEPENDENT_REVIEW_20261025.md`)
challenges investigation priorities and documents source lifecycle/API gaps,
with proposed tests for host services, encoded output and complete retirement.
It does not promote conceptual recommendations to accepted native facts or
resume camera work. Existing claim statuses remain unchanged.

Latest25October integration: MiniMax candidate a177dc3 is reviewed in full.
Accepted the Cook argument4 record offset and conditional mailbox copy,
and the shell local/send/wait predicate. Refusal timing does not prove
MCB delivery; BSS metadata and direct-call inventories do not establish
reset behavior or exclusivity. Phase2, fresh association and owned sensor
RAW remain unfinished. Work is paused at the user's request after local
integration. See the parent review (not published: `../../output/decompile/rea/pass-a177dc3-review-20261025/parent-review.md`).

Previous checkpoint (historical):

Latest25October checkpointb2f5d1a: reviewed b024fca history is integrated;
profile8 configuration words2912/2896 were observed under code/object guards
with verified target cleanup. The fixed reader/dependency guards passed parent
checks. Actual single-still eight-argument Order can submit and return0; its
CapNo-to-callback association remains the next edge. Phase2 and retained sensor
RAW stay unfinished. See CURRENT_INVESTIGATION.md and the
active parent review (not published: `../../output/decompile/rea/pass-b024fca-review-20261025/parent-review.md`).


Latest25October integration: accepted conditional material-image parameter
routing, exported adapter entry ABI and distinct generic-allocation arguments.
The plugin getter performs readonly sysfs node probes. Native live sensor
provenance, fresh completion and caller-owned retention remain open. That pass
was offline; the latest integration is paused after review. Parent review owns
acceptance; worker
proposals remain proposals. Candidateb024fca is a merged ancestor of master.


This directory is the current claim authority for the SM-R210 capture library. `CURRENT_INVESTIGATION.md` says which phase is open. A record here says what is accepted, still open, or rejected.

25 October closing continuation: SDK-free exit/deadline/cleanup target checks
passed; the SDK remains unloaded. Conditional output programming, pre-programming
wait failure behavior and diagnostic allocation-return forwarding are accepted
within their limits. Finalizers/configuration composers are rejected as completion
proof. Ordinary recipe pre-callback backing acquisition is the next bounded edge;
request association and retained sensor RAW remain open. Native sessions closed.

25 October owned-RAW continuation: accepted conditional intermediate material
hooks, FrontEnd selector/wrapper/nonzero service creation, and Order callback
argument joins. Concrete pixel producer, callback implementation, fresh completion
and retained ownership remain open. Read-only health passed; no SDK load.
Current review (not published: `../../output/decompile/rea/pass-owned-raw-20261025/parent-review.md`).

25 October takeover of MiniMax `f666fbe`: accepted exact LRC listener,
opaque MMIO result, first callback forwarding, conditional field copy/selected
clear, own-plane getter and creator-context joins. Bridge guards and masked
startup failures clarify initialization limits. See the
takeover review (not published: `../../output/decompile/rea/pass-f666fbe-takeover-20261025/parent-review.md`).
Experimental display source is integrated with offline checks. Native RAW
binding, concrete callback, full hosted readiness and retained sensor ownership
remain open. No camera operation; retention support remains false.

24 October review of MiniMax `0f976d1`: numeric setting/name corrections and
release reentry fixes are integrated, followed by uncertain-still-send
quarantine. Accepted `session.imageset-unsubscribe-shares-delivery-gate`
identifies a conditional cleanup boundary, while
`completion.library-encoded-callback-preserves-opaque-header` leaves request
identity open. Copied descriptors and equal geometry are rejected as sensor
provenance. Camera contact is paused until the user can power cycle it.

24 October MiniMax-led continuation: accepted
`settings.userdata-wire-native-translation` closes the checked API-id to native-id
conversion and excludes RAW122..126 from this particular11xx path. It does not
exclude other interfaces. Producer, sheet-writer and source repairs are active.

Accepted `session.platform-self-check-live-20261024` verifies the reviewed
SDK-free startup, argv,32-bit widths and clock on the pinned camera. Identity
and cleanup checks passed. Capture SDK initialization remains unverified.

24 October branch review through `ffa0598`: accepted
`raw.encoded-set-geometry-diagnostic` and imported its pure metadata decoder
extension. Stride units remain unproved. The candidate backend remains open
under `completion.ffa0598-library-backend-review`; the type3/two-exposure proof
is rejected under `rejected.type-three-as-two-exposure-proof`. See the
review (not published: `../../output/decompile/rea/pass-ffa0598-review-20261024/parent-review.md`).

24 October phase2 continuation: the user confirms that only our own hosted
capture context, with stock app absent and caller-owned sensor RAW, meets the
goal. Accepted `completion.portable-session-core` is a tested software
component only. Native command-index/layout, shared preference, startup order, bridge gate/
destruction-order and stock JPEG byte-count paths are accepted within their
limits. The LRC plane reaches a concrete control store; its runtime binding
and retained sensor ownership remain open. A runnable backend is unfinished. See the phase2 integration review linked by those fact records.

Reviewed source additions from the4d2deb0 pass: an offline observer-trace decoder
and experimental raw-grab argument/error fixes pass host checks and ARM build.
The production LRC attachment helper is identified; its sensor backing/lifetime
is still open. These components do not complete the capture API. Parent camera
work is limited to read-only identity/health checks.

Active4d2deb0 review: accepted `submit.stock-control-frame-historical-20261023`
as a bounded log observation. Rejected exact half-frame arithmetic, the late
image-request timeline, and still-only timeout as a demonstrated trigger/reboot
isolation. The raw grabber remains experimental; source/build binding and RAW
ownership are unresolved. See review (not published: `../../output/decompile/rea/pass-4d2deb0-review-20261023/parent-review.md`).

Latest8315d77 review: two pure C packet/metadata components and a reviewed
fixed manager-vptr reader are integrated on master. Guarded camera observations
and native checks resolve the SSIF allocation shape, encoded descriptor writers,
callback mapping limits, command16 receiver and concrete capture-buffer release
route. A generic grant is not a sensor binding; metadata copying does not pin
pixels. Main goal remains unfinished. All owned workers/native sessions are
closed; partial writer/queue tasks remain unaccepted. See the
selective review (not published: `../../output/decompile/rea/pass-8315d77-review-20261023/parent-review.md`).

23 October delegated continuation: parent source checks accept the actual
twenty-byte allocation request producer, Linux request/translation mechanics
and the conditional four-byte release dispatch. The former no-op table belongs
to category0x81 inbound handling; category0x80 has the real allocation producer.
Concrete manager lifetime, sensor binding, owned receive and capture association
remain open. See the contract review (not published: `../../output/decompile/rea/pass-contract-workers-20261023/parent-review.md`).
The conditional manager query is largest contiguous free bytes in16KiB units;
readiness encodes0x82/0x020a with an empty body. Two producer turns completed;
other worker evidence is partial and parent-reviewed only where accepted. All
owned workers/native sessions are closed; no camera action.

Latest follow-up, 23 October: reviewed worker snapshot `a18b965` plus the
`6aa5d98` capture-available supplement. Accepted source-bound corrections for
the attribute-byte dispatch and scalar user-data conversion, the scoped
initial-table stubs, a diagnostic NEXTSHOT_READY marker and the separate
conditional internal event `0x01010006`. Its wire route and RAW-completion
meaning remain open. Rejected the enum-as-pool-producer proof, category/table
confusion, string-based SSIF provenance and readiness-as-DMA-completion proof.
Integrated corrected offline decoders; camera helpers remain deferred. See the
follow-up review (not published: `../../output/decompile/rea/pass-minimax-followup-review-20261023/parent-review.md`).

First 23 October MiniMax review: accepted the corrected Linux write-block order,
the second still-command wire path and its compatible pure encoder extension.
The retained software-submit output is accepted as a bounded historical
observation, with its free-space arithmetic corrected. The passive observer
does not close owned completion, and the media effector's address bookkeeping
does not prove a sensor RAW handoff. Those two proofs are explicitly rejected;
the lease API and new visibility work remain leads. See the
parent review (not published: `../../output/decompile/rea/pass-minimax-review-20261023/parent-review.md`).

An essay in `../archive/` can explain a record. It does not outrank the record. Open an archive or `output/decompile/rea/` file only when a record's `detail` field names that file for the question being answered.

## Status and kind

- `accepted` — parent review accepted the statement, with the limits in `not_proven`.
- `open` — the statement is the question, not a result.
- `rejected` — reviewed evidence closes this as a product route or as a proof.
- `superseded` — a newer record replaces it. Follow `superseded_by`.
- `kind`: `observation` (measured bytes or a live read), `static` (instructions in a named build), `runtime` (observed on a running camera), or `inference`.

A static result is not a runtime fact. Decompiler names, frame counters, and agreement between agents are not sensor provenance.

## Addresses

[coordinates.yaml](coordinates.yaml) is the address-space key. Quote every address in a record as hex with `0x`. Say which space it belongs to. Decimal is written only when a field's decimal and hex identities have already been confused.

## Files

| File | What it owns |
|---|---|
| [contracts/experiments.yaml](contracts/experiments.yaml) | Open post-Phase-2 experiment questions; offline preparation is not camera evidence |
| [coordinates.yaml](coordinates.yaml) | Artifact hashes and address spaces |
| [contracts/session.yaml](contracts/session.yaml) | Init, shutdown, shared-handle limits |
| [contracts/registry.yaml](contracts/registry.yaml) | The camera command registry and the control-register reader |
| [contracts/settings.yaml](contracts/settings.yaml) | Exposure, ISO, shutter, native RAW flags |
| [contracts/submit.yaml](contracts/submit.yaml) | The 48-byte still request and its RTOS continuation |
| [contracts/completion.yaml](contracts/completion.yaml) | What would count as this still being finished |
| [contracts/raw-buffer.yaml](contracts/raw-buffer.yaml) | Measured raster and the missing ownership handoff |
| [routes/type0-still.yaml](routes/type0-still.yaml) | The normal 360 still graph |
| [routes/preview-movie-work.yaml](routes/preview-movie-work.yaml) | Diagnostic preview buffer. Not the product |
| [rejected.yaml](rejected.yaml) | Routes and proofs that stay closed |

## Review rule

`status` is the review decision. `kind` is how the statement was established. An accepted static claim is not a runtime fact.

A record that names an address sets `address_space` to an id in [coordinates.yaml](coordinates.yaml). The artifact hash for that space lives there.

Accepting or rejecting a claim means re-reading the cited instruction or bytes through the connected REA session, then through the `detail` path. For the RTOS image that session is Ghidra. The archive prose, a decompiler name, a hand-decoded `llvm-objdump` listing, and agreement between agents do not close the claim.

## Updating

Change the record in the same commit as `CURRENT_INVESTIGATION.md`. Set `status` and add the evidence path. Do not copy the whole essay into the record. One statement per claim. Leave `unknown` when the parent review left it open.

Latest MiniMax pass: `session.rtos-sc-control-read-branch` is static evidence
of a control-register read, with host access still unproved.
`session.fail-closed-success-contract` defines the success/refusal boundary;
`rejected.cp15-census-as-complete-interface-proof` prevents treating bounded
search coverage as interface absence. The pure packet component validates
sparse selectors on requests and results; it is not a working transport.

Latest reviewed status, 21 October 2026: the parent review of the
`codex/sc-host-access-20261021` worker pass accepted three records, each narrowed
to the evidence it actually checked, and rejected one as a proof.
`session.rtos-sc-gate-prefix-compare` is accepted as static for the checked n=2
call with the literal `sc`; the helper returns zero without examining token[2]
when token[0] and token[1] both match, so the gate is a 2-byte prefix comparison
at that call, and the earlier wording that a longer token does not enter is
corrected. `session.rtos-sc-branch-console-only` is accepted as static, narrowed
to the directly checked console-loop call `0x80012270` to dispatcher `0x800136e8`
with one resolved reference; that is resolved-reference coverage only, and the
claim id is retained for historical reference rather than as exclusive
accessibility. `registry.shell-command-map-is-single-caller` is accepted as
static, narrowed to executor `0x8004a848`'s own instructions; no single-caller,
registry-membership or path-safety claim is made, and no caller is assigned to
`0x80540b14`. `registry.control-register-has-one-reader` is rejected as an
inference: the original fixed-`r1` mask is not a complete reader proof, and the
corrected finite eight-site candidate count still cannot establish the absence of
other readers or of command-reachable readers, so the count stands as a worker
lead rather than an accepted exhaustive result. `session.rtos-sc-control-read-branch`
keeps its accepted static status with the prefix-semantics limit removed; its
remaining limits are unchanged.

Every other record from that pass stays `status: open` and is explicitly a worker
finding awaiting parent review: `session.rtos-console-task-initialization`,
`session.rtos-console-command-table`, `session.rtos-console-ref-command-surface`,
`session.rtos-console-token-residency`, `session.rtos-control-register-reader-census`,
`session.linux-console-uart-nodes`, `session.rtos-sc-host-reachability-boundary`,
`registry.sc-handler-is-console-only` and `registry.node-name-block-layout`. The two
live camera records are `kind: observation` and `status: open`: prose-only
historical reports whose raw responses, device identity, guards and timestamps were
not retained. They no longer carry the hand-created node inference or a
no-host-reader conclusion, and the shipped Bluetooth script naming `/dev/ttyAMA1`
is recorded as naming that node rather than as proving runtime ownership of it.

The accepted baseline facts above remain the authority for what they cover. The
native questions about console reachability, reader exhaustiveness and the console's
physical UART are not settled by these records and are not part of the accepted set.

Bounded follow-up accepted: `registry.ref-key-object-dispatch-chain` connects a
specific ref key/constructor/vtable slot conditionally to execute0x800e8718;
`registry.proxy-add-result-is-not-membership-proof` shows why the add return
cannot certify membership. Both leave runtime activation and command safety
open. `rejected.literal-and-xref-census-as-console-exclusivity` records the
withdrawn exclusivity proof. A separate offline MiniMax follow-up is checking
the bound ref handler's selected register/state item; no camera trial is implied.

`registry.ref-reg-formatted-context` corrects the selected-item result: the
caller formats general and context register labels, including UATB, before
additional stack bounds. `rejected.ref-reg-stack-only-and-tid-parser` rejects
the first worker summary. The syscall producer and active-register meaning
remain open; the corrected context-producer trace is active offline.

Latest accepted diagnostic: `session.svc6-dispatch-state-profile` and
`session.svc6-dispatch-cell-live-20261021`. The fixed word matched the static
SVC6 target; active CPU state and the selected syscall producer stay open.

22 October mapping review: the worker's interval/decoder/cache-invalidation
and barrier-elision interpretations are corrected. Accepted records are
`session.rtos-map-record-endpoints`,
`session.rtos-control-setter-preserves-tre`,
`session.linux-hardware-pte-type-requires-remap-state`, and
`session.linux-remap-block-index4-device`.
`rejected.mapping-audit-cache-and-barrier-inference` keeps the withdrawn
inference closed. Active translations, packet publication/reader ordering,
strict admission and sole receiver ownership remain open.

The subsequent diagnostic trace accepts
`registry.ref-reg-context-service-conditional` and
`registry.ref-reg-uatb-word-source`. Its ordinary-entry/default-hook path
selects the negative-key service and copies a stored record word into the
UATB-labelled output. This does not identify an active hardware register.

The 22 October console-source follow-up accepts two input arms, the gated
RTOS-internal input/output ring services, the construction-time mode choice
and a conditional default MMIO reader. See the four `session.console-*`
records in the session contract and their source review. Current activation,
object lifetime, safe invocation and a Linux/host message carrier remain
open. The name/scheduler-based provider proof is rejected separately.

The 22 October continuation adds `session.strict-ring-admission-component`:
a pure outgoing extent planner with strict slack and overflow-safe arithmetic.
It does no I/O and does not repair the RTOS producer or close owned receive.

`session.console-local-selector-domain` and
`session.console-gate-wrapper-arguments` separate local constructor/control
selectors and checked SWI argument wiring from the still-unproved host carrier.

22 October wind-down: `session.owned-receive-transport` stays open. The
status handoff (not published: `../STATUS_HANDOFF_20261022.md`) separates committed reviewed
components from deferred worker models; no new native or runtime claim was
accepted during resource-boundary saving.

24 October producer join: `raw.encoded-first-image-conditional-lrc-source`
links the first image to LRC slot0 only on the checked lookup branch.
Sheet selector1/2 instead accepts a caller image; live binding remains open.

`settings.order-sheet-configuration-writer` records the concrete key4-to-sheet+48
map and preserves the134 lookup-failure override-to3. Runtime input and RAW
effect remain open.

27 October owner-ordering pass: three bounded RAW static facts recover the
Cook Done local record, selector0 cross-key candidate restart scan, and the
procedure/member/dependency numeric gate. Multiple candidates can be restarted;
this does not bind selected LRC material to that graph or prove Done reception,
sensor completion or retained backing. See `raw.cook-*` in the RAW contract.

`session.receiver-singleton-vptr-startup-hook` binds the constructed receiver
at0x81c0d9a0 to its conditional startup dispatch. Scheduler invocation and the
separate mode-owner0x81c0de64 ordering/retirement contract remain open.

`raw.cook-dispatch-separates-done-and-notification` corrects the candidate
activation model: kind2→Done atslot34; kind3→notification/restart atslot38
for the checked base CCook table. Selected material binding stays open.

`raw.cook-done-selector0-candidate-starts-after-postprocess` follows the actual
kind2 handler: PostProcess then conditional candidate Start attempts. A reported
PostProcess error does not suppress those attempts; handler return is not their
aggregate outcome. Selected LRC membership and physical backing remain open.

27 October receiver-context continuation: `raw.material-link-requests-bidirectional-insertion`
and `raw.selected-lrc-type2-source-downstream-graph` recover conditional
source→selected LRC→later material construction via the inverse +0xe0 writer.
NewP creation differs from firstP selection. The actual Done root and backing
remain open; insertion requests are not successful live execution.

The same pass accepts the conditional LRC code4→Finish0 route and material
pointer/selector preservation through assistant kind3 and Cook kind2 messages.
The stop-slot-as-normal-Done0 proof is rejected. Invocation, matching delivery,
procedure affinity, physical backing and retention remain open.

Receiver context C is the loaded word at R+0x1c, initialized to R+0x38;
its vptr slots bind submit/wait/execute. Notification0x5101 conditionally submits
through that context. Member-call-as-thread-switch and only-consumer proofs
are rejected; full dispatch, copy, retirement and ownership remain separate.

IDLE exit's index1 targets a third rendezvous ownerA0x81c0ded4/upper6,
separate from modeS0x81c0de64/upper9. Its nested callback requests0x01030003;
this differs from S's0x1122 setter. Cleanup-callback and neighboring-vtable-slot
misidentifications are rejected. Neither bit nor event certifies physical drain.

Final same-pass C6 identifies the shared static command-buffer handle cell,
the default post-hook's outgoing-r0 outcome (not an API status contract), and the conditional
queued0x3f consumer route into the already accepted OFF chain. Decimal63 and
input value1 must not become hex0x63 or target state1. No exclusive consumer,
request generation, callback retirement or physical handover is certified.
