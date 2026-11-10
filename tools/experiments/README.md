# Experiments for the next six steps

Prepared 4 November 2026, then validated on the connected camera under the
owner's authorization to test unattended, without a power cycle, in darkness.
**309/309 full RAW captures across 44 processes on one boot** passed, including
both 100-shot stress conditions and the complete HDR sweeps. Phase 2 remains
finished; these were the next steps of 4 November (not published: `../../PROJECT_GOAL.md`). Open work now lives in TODO.md (not published: `../../TODO.md`).
Results (not published: `../../output/experiments-live-20261104/RESULT.md`),
[compact evidence](../../reverse-engineering/unattended-20261104-evidence.json) and
archive/restore (not published: `../../retained-artifacts/unattended-experiments-20261104.md`)
record exact conditions and limits. The table defines coverage and acceptance;
it does not imply that full app/battery controllers are complete.

| Goal step | Prepared now | Later acceptance |
|---|---|---|
| 1 Optional JPEG if faster | Three output policies, balanced comparison cases, timings and summary tool | Keep omitting JPEG only if matched trials measurably improve end-to-end time |
| 2 No power cycles | Fresh-session control, then separately launched processes with the same boot-time guard | Second and third processes capture, return buffers and exit normally without reboot; then repeat |
| 3 Speed and stress | 10/100-still runs with zero added gap, discard storage controls, saved check after stress | Every still succeeds; stable frame hold, completion, memory and temperature; last saved frame correct |
| 4 Full HDR range | Exact 8/6-shot TV/ISO sequences from the canonical HDR runner; boundary probes; WB/Smart Range guards | Each requested exposure produces matching full RAW, complete JPEG, settings and RTOS exposure record |
| 5 Screen, sounds, LEDs, buttons | Platform inventory and passive button-observer build; source map and staged test protocol below | Test each control alone with stock absent, then during RAW capture; faults/input loss cannot hold capture |
| 6 Battery controller | Bounded read-only battery/status collector, unknown-state semantics and charging matrix below | Correlate provider values against stock indication and cable state; safe missing/stale-provider behavior |

Validated progress: Linux JPEG skip-copy/file omission saved 7.03% mean work
in three balanced RAW-saving blocks; process reentry, manual/auto stress and
every canonical HDR exposure passed. The RAW hold remains ~0.65 s. Screen
clear/center was physically confirmed before unattended work, passive input
polling passed without events, and named battery status was observed during
capture. Sounds/LED writes, button gestures, charge transitions and other
native drives remain later acceptance work.

## Build and prepare locally

These commands do not contact a camera. Build outputs and generated manifests
stay ignored under `output/`; do not commit generated binaries or logs.

```sh
CC=/usr/bin/clang sh src/lib/g360cap/tests/run.sh
sh src/lib/g360cap/build-arm.sh
sh tools/observe/build.sh
sh tools/experiments/build-buttons.sh
sh tools/experiments/build-display.sh
python3 -m unittest discover -s tools/experiments/tests
python3 tools/experiments/prepare.py plan
python3 tools/experiments/prepare.py bundle --out output/experiment-kit-validated-20261105 --wb-name WB_AUTO
```

The bundle contains the ARM probe, source-hashed manifest, session wrapper,
snapshot collector and a script per case. Existing destination directories
are refused. Bundling only writes local files. Stage it to a **new temporary
directory** later, verify its manifest hashes on the camera and use `sh` to
run scripts (FAT32 executable bits are unreliable). Keep the library hashes
in `run-session.sh` synchronized with the reviewed firmware contract.
For a kit staged under `/tmp`, set executable bits on `g360-shot`,
`gear360-display` and `button-observer` after verifying them; the wrapper refuses a non-executable
probe before stopping stock.

By default the shots go to `RUN_DIR/shots` on the card. `SHOTS_DIR=/opt/usr/g360-exp-NAME`
puts them on the internal flash's data partition instead (used by
[speed-run.py](speed-run.py)); the free-space check then reads `/opt/usr`, and
the session records stay in `RUN_DIR`. Any other `SHOTS_DIR`, or one that
already exists, is refused before stock is touched. The caller deletes it.

## Clear and label the screen after stopping stock

The existing `tools/experiments/gear360-display.c` renderer writes the 72×32
OLED through `/dev/ssd1309-ctrl`. Its drawing path needs no running stock
app. The new camera build excludes the optional stock-process bitmap reader
and links only the camera libc/runtime, with no capture SDK dependency.

```sh
# On the camera later, with stock stopped and no competing screen owner:
./gear360-display --write --clear
./gear360-display --write --center 'TEST MODE'
```

`--center` replaces the entire bitmap: it clears the old UI and centers one
line horizontally and vertically in a single draw/update. It accepts up to
12 characters from the existing uppercase/digit/space font (`/:-.` also
supported). Omit `--write` for a PBM preview with no device access. A full
clear is a blank framebuffer, not display power-off or driver reinitialization.

Every generated test case now pins the renderer hash. Before stopping stock,
the wrapper validates the message without writing the screen and takes the
existing screen-owner lock; known HDR screen/controllers cause a refusal.
After stock is confirmed absent, it writes the centered message, then starts
capture. The default is `TEST MODE`; override with, for example,
`DISPLAY_TEXT='RAW TEST' sh baseline.sh BTIME /opt/storage/sdcard/exp-UNIQUE`.
A failed draw/update ends the trial before any capture request. The label
remains after process exit; there is no stock restore or refresh process.
The stock app is still stopped gracefully using `systemctl --no-block stop`,
never forcibly killed or restarted. On 4 November the owner confirmed centered
TEST MODE with the old screen cleared. Every subsequent capture process's
preflight label write succeeded; physical visibility/no redraw during those
unattended captures was not observed.

For passive buttons, `sh tools/experiments/build-buttons.sh`
builds `button-observer.c` against the camera libc. It reads events without
grabbing or forwarding them, launches nothing and has a bounded observation
mode. Its host decoder accepts synthetic ARM32 evdev records. Never substitute `gear360-entry`:
that helper launches the old HDR application. The observer can later run as:

```sh
./button-observer --observe /dev/event0 30 > buttons.txt
```

Confirm the input device from `/proc/bus/input/devices` first. The old mapping
(Capture 126, Menu 127, Power 73) is a historical comparison, not a current
discovery. Observe short deliberate presses; test long-Power shutdown last.

## Common camera procedure, later

1. Have the user present for new trials. Check camera identity, firmware,
   free space, boot time (`awk '$1=="btime" {print $2}' /proc/stat`), normal
   stock shooting state, battery and temperature. A hibernation-restored
   boot ID does not identify a fresh boot. Record baseline settings and
   any existing HDR/button listeners; keep competing capture clients absent.
2. Start the existing `rtoslog follow` and `ipccrec record` observers using
   the [observation runner](../observe/run_still_trial.py) as the command
   reference. Save a continuous RTOS/ring trace spanning process exits and
   reentry, not just each probe's stdout. Record observer settings identically
   across timing comparisons. The old trial runner always requires active
   stock and caps the count at five; use the new session scripts for this kit.
3. Stage the kit and verify hashes. Run `baseline.sh BTIME
   /opt/storage/sdcard/exp-UNIQUE` once. It checks architecture, firmware and
   executable hashes, excludes known capture clients, stops the active stock
   service gracefully, takes two normal frame captures and exits normally.
   It does not restart the stock app. The supplied boot time is checked
   before stopping stock, after the stop and after capture.
4. Inspect all saved frame/JPEG pairs, capture-ready and the four give-backs
   per ordinary set. Check btime/uptime and health. Only then run `reentry-1.sh`
   and `reentry-2.sh`, each with the **same expected BTIME** and a unique
   output directory. They require inactive stock/MainPID 0/no stock process;
   they never stop/start the stock service. Each script is a new process,
   unlike `--count`, which tests multiple stills in one process.
5. A wrapper or probe failure ends that trial. Do not run the next case or
   retry an indeterminate request. Fetch its run directory and observer
   logs, classify it and review before continuing. A timeout may leave RTOS
   work in flight. Keep evidence if recovery needs a later power cycle.
   Never use a service restart or forced kill as recovery.

Case prerequisites are recorded in `manifest.json`; they are review
checkpoints, not an automatically executed pipeline. Scripts deliberately
do not chain cases or self-authorize a later stage. The wrapper checks known
client names and its own advisory lock; it cannot exclude unknown external
clients. Use the process inventory as well. No new native instruction claim
or unsupported drive mode is introduced by this kit.

## JPEG comparison and speed

`g360-shot --jpeg` supports:

| Policy | Host/library action | RTOS JPEG generation |
|---|---|---|
| `save` (default) | Copy, validate, save all JPEGs | Unchanged |
| `discard` | Copy and validate JPEGs, omit JPEG file writes | Unchanged |
| `skip-copy` | Do not read/copy JPEG payloads, still return every buffer | Unchanged |

`skip-copy` requires `--frame`. RAW copy, sampled source-change checks,
value validation, capture-ready and release failures still determine the
result. Skipped images carry `G360_IMAGE_SKIPPED`; success does not assert
that their JPEG payloads were valid. This is an experiment on Linux copying
and saving; **there is no implementation to bypass the RTOS encoder yet**.
If neither omission improves speed, retain the normal JPEG path. If the
RTOS dominates, trace a supported processing switch through REA before
adding an encoder-bypass experiment; do not equate a skipped write with
disabled encoding.

After reentry passes, run the six JPEG cases in their numbered ABCCBA order,
each in a new process. Repeat at least three balanced blocks with unchanged
scene, settings, RAW saving, observers, storage, power and thermal conditions.
If reentry fails, use fresh boots per matched case instead and record that
design change; do not silently disable its guard. For an actual decision,
repeat with fixed manual exposure using the reviewed CLI `--exposure` option
after HDR boundary validation; automatic exposure can change the timing.

The probe emits one `metric,...` row per capture call to stdout and its log:
capture API duration, saving/reporting duration, total per-shot work, first
image set and capture-ready latency, frame hold, frame size/flags, policies
and status. `cycle_us` is start-to-start time and includes the preceding
inter-shot gap, frees and metric-log sync; its first value is zero. Total
excludes metric output, freeing and post-run hold; it includes per-shot
setting changes. Startup/exit wall time is separately in `session.txt`.
All durations use the monotonic clock, in microseconds. Log fsyncs add
overhead, so keep them identical between cases. `--library-log off` uses the
existing C API's NULL log path, omitting synchronous library diagnostics while
keeping stdout metrics. Independent ring evidence is then required to review
give-backs. Default is `sync`; native/observer logs are separate. The summary
groups sync/off independently (older metric rows default to sync).

The three-block camera comparison measured mean work 4.433 s save, 4.341 s
discard and 4.122 s skip-copy (12 frames/policy, fixed TV96/ISO100, RAW saved).
RTOS encoding still runs. A separate matched sync/off in-memory comparison
measured 1.419/1.337 s (5.77% reduction). Do not add gains from separate cohorts.

Use one stream per session (usually `probe.log`), not both copies:

```sh
python3 tools/experiments/prepare.py summarize output/TRIAL1/probe.log output/TRIAL2/probe.log
```

The summary reports failures, valid full-frame counts, median, nearest-rank
p95, min/max and sample counts. Compare only matching settings/frame-output
groups. It does not declare a winner or prove no reboot/coherent sensor
content. A duplicate sequence in one stream is refused. Discarded frames
cannot be inspected afterward, so save and check the baseline and the
`after-stress` pair. Stress cases still perform the full copy and checks,
but discard both RAW and JPEG files to separate acquisition from card speed.
Run the snapshot collector alongside them; treat it as measurement overhead.
Use `--compact` for timing cohorts. The initial full-inventory collector forks
per node; preliminary JPEG cases using it were excluded from the comparison.
Only after measurements choose among pre-touching destination pages, buffer
reuse or a different copy implementation, each against the same content and
ownership tests. Do not tune mappings/cache attributes speculatively.

## HDR exposures and other drives

The planner extracts, without rounding, the canonical TV/ISO words in
`hdr/web/hdr-capture.sh` (not published: `../../hdr/web/hdr-capture.sh`):

| Sweep | Requested TV/ISO, brightest first |
|---|---|
| 2 EV | 48/1600, 48/400, 48/100, 80/100, 112/100, 144/100, 176/100, 208/100 |
| 3 EV | 48/1600, 48/400, 64/100, 112/100, 160/100, 208/100 |

These are separate software-triggered single stills with the frame copy,
not the firmware's bracket command (already observed to do nothing). Keep
normal SUPERFINE quality: RAW quality drops the ordinary set. The first
boundary case tests the shortest exposure, 1/8 s at ISO 100, then 1/8 s at
ISO 1600. Validate before either full sweep. Check exact applied RTOS
exposure/ISO and both RAW halves on **every** still; EXIF alone is not
physical exposure validation. Keep the scene still and use a neutral and
bright/dark target to detect stale frames, clipping and inconsistent settings.

Choose WB while stock is still active (`st app set wb daylight` is the
existing source workflow); set Smart Range off using the existing
`st cap capdtm setusr 21 0x150000` path. Preserve/read back their previous
values and record the exact restore commands before changing them. The
generated HDR cases require exact `WB_DAYLIGHT` and `SMARTRANGE_OFF` rows,
both before and after the stock stop. For another of the page's five WB
choices, read its native WB symbol from `usrlist` and regenerate the kit
with `--wb-name WB_...`; do not infer the Linux numeric WB mapping from the
native USERDATA id. No `st app` command is used to trigger these captures.
The probe turns LTNR/ISO NR off for the sweeps and restores its documented
on/mid defaults; exposure returns to auto, not the page's ISO100/TV96 idle
state. Compare before/after settings and explicitly restore the recorded
baseline as needed. Do not restart stock to restore settings.

Other native drives need a separate reviewed request/recipe contract.
Existing `G360_CAPTURE_*` declarations or host multi-set tests do not prove
that a drive works on the camera. Leave WBB/PWB/depth/continuous/burst trials
pending native request/termination/completion review; do not blindly rerun
the known idle WB bracket or stalled 5-second RAW route. This kit prepares
the required HDR coverage with independent normal stills and records those
remaining drive-mode experiments rather than inventing their wire values.

## Platform controls and battery

`sh platform-snapshot.sh 30 > platform.txt` takes 30 one-second samples.
It records input-device and ALSA inventory, LED properties, thermal nodes,
uptime, boot time, memory and power-supply properties. It performs no writes
to device/sysfs nodes, no input grab and no battery/shutdown action. Missing
providers/properties are explicit unknowns; voltage/current/temperature and
charge/energy values remain in their reported units. If this firmware has
no power-supply class, recover its device-manager provider through REA and
prepare a reviewed read-only adapter. No absence or battery percentage is
inferred from missing sysfs files.

`sh platform-snapshot.sh --compact 600 > platform.csv` uses shell reads for
named battery, AC/USB online, memory and uptime, at approximately one sample
per second (bounded to 1–1800 samples). It avoids repeated device inventories
and preserves missing fields as `UNKNOWN`. The camera's `battery` provider
reported Full/100/Good, AC 1/USB 0 over 3,014 compact samples; the separate
`adc_battery/capacity=1370` must not be interpreted as percent. Temperature
400–420 and voltage are raw driver reports; transitions/freshness and units
still need validation. The canonical compact sampler also passed on-camera.

For the later app replacement, reuse the rendering, event-decoding and
gesture source in `archive/hdr-integration/` selectively. The old screen snapshot
restoration depends on a running stock process; design a dedicated screen
owner for stock-absent use. `gear360-display.c` is the source pointer for
the OLED driver ABI; `controls.h`, `gesture.h` and `entry.h` contain the
existing input logic. Recover sound/LED interfaces from their own library
contracts before any write trial. Do not run the old HDR launcher or
entry listener as the replacement app.

Record this control matrix later: screen draw/clear/repeated redraw; one
short sound and mute; each LED and final off/restored state; each button's
press/release/repeat and simultaneous input; then each action during one
RAW capture, safe cancellation after a completed still and loss of the UI
consumer. Inspect RAW equality/flags, ready, give-backs and button drop
events. Screen/audio failures must not block the capture callback or
invent camera completion. Shutdown-button behavior and persistent app
installation remain separate later work, not part of this offline kit.

Battery matrix: stock active vs absent, idle vs capture, cable unplugged vs
plugged, charging vs full, and a naturally observed low battery. Timestamp
each transition and compare reported status against the physical indicator
and stock display before takeover. Poll conservatively (one second for the
experiment; slower for a later UI), preserve missing/stale/error states,
and do not introduce battery cutoff thresholds, calibration, charging
control or deliberate deep discharge. The controller remains a future
implementation until its provider, units and error behavior are validated.

After review, stop only the observers owned by the trial and remove only
the staged kit and its unique temporary outputs once the evidence is
fetched. Preserve compact logs/findings and hashes; captured RAW pixels
need not be retained under the repository's cleanup policy.
