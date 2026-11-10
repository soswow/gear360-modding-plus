## HDR sweep: 2 EV steps on familiar speeds — 9 November 2026

The owner chose layout B: the sweep is now 1/6, 1/13, 1/50, 1/200, 1/800,
1/3200, 1/13000 at ISO 100 (TV 41, 59, 90, 122, 154, 186, 218): 2 EV steps
down from the fastest, 1.94 EV from 1/13 to 1/50, and 1.1 EV from 1/6 to
1/13. The TVs are those of the remote's shutter choices of the same names.

- Host tests: all of `src/app/tests/` pass; synthetic sweeps set `MANUAL 41
  100`, `MANUAL 59 100` … `MANUAL 218 100`.
- Camera (home network): installed with `install-app.sh --backup
  sweep-2ev-20261109 --restart` (sha256 `cb7d89e9…bb70`); same boot
  1794129402, the app reached `ready` with a 7-frame sweep. No sweep taken.

## Exif ExposureTime: the real time for a set shutter — 9 November 2026

Trial RANGE14 found that the RAW follows the set time with the 166 ms cap and
the ~5 us shortfall (0.013 EV RMS), while the firmware's ExposureTime, a
third-stop name, was up to 0.16 EV off. Sweep frames and remote-shutter
stills now write the real time (`g360_exif_exposure_from_tv` in
`g360_exif.c`, integer only) in the DNG, the JPEG and the sweep list.
Automatic stills keep the firmware's value; g360-shot is unchanged.

- Host tests: `test_exif.py` checks the helper against the float model for
  TV −200…399 (within 0.1 us, in lowest terms); `test_app.py` checks that
  manual stills and sweep frames use it and the lists carry it.
- Camera (home network): installed with `install-app.sh --backup
  exif-real-time-20261109 --restart` (sha256 `ae3b27c0…d5bd`). One remote
  still at TV 79 (shutter 1/30, ISO 100), `G360/photos/20261109-093024-00277.dng`:
  Exif ExposureTime 0.0326286 s (1/30.65; the firmware's name would be 1/30),
  ExposureProgram 1 (manual). Shutter set back to Auto.

## Remote shutter: 34 third-stop speeds — 9 November 2026

The remote's shutter dropdown now offers the familiar third-stop speeds over
the whole range, 1/6 … 1/13000 (34 choices), each sent as the TV nearest
its name (`G360_SHUTTER_TVS`, within 2%). The sweep keeps its own table
(`G360_SWEEP_TVS`, unchanged).

- Sweep 20261109-083034 (frames 00270-00276, the user's): Exif names the
  seven frames 1/6, 1/20, 1/80, 1/250, 1/1000, 1/3200 and 1/13000, ISO 100.
  The RAW signal ratio of neighbouring frames (`tools/experiments/exposure_ends.py`,
  pixels at 2-85% in both) is 0.278, 0.286, 0.273, 0.283 and 0.267 against
  0.273-0.286 expected; the last pair had too few lit pixels.
- Host tests: all of `src/app/tests/` pass; `test_remote.py` checks that the
  page's list equals `remote.h`'s and that every listed TV is accepted.

## HDR sweep and remote shutter: the full range in 7 steps — 8 November 2026

Trials ENDS1/ENDS2 confirmed the camera's usable shutter range (about 1/6 s
to 1/12,634 s). The sweep is now 7 frames at ISO 100 over that range, TV 41,
71, 100, 130, 159, 189, 218 (about 1.84 EV apart), and the remote's shutter
dropdown offers the same seven times (sent as the TV,
`cmd=set&shutter=41` … `218`; powers of two 8-8192 are now refused). One
table, `G360_EXPOSURE_TVS` in `remote.h`, feeds both.

- Host tests: all of `src/app/tests/` pass; synthetic sweeps set `MANUAL 41
  100` … `MANUAL 218 100` and show `SWEEP 1/7` … `7/7`; remote stills log
  `APP EXPOSURE TV 159 (1/981 s) ISO 800`.
- Camera (home network): installed with `install-app.sh --backup
  hdr-seven-full-range-20261108 --restart` (sha256 `c3a0b7ec…7242`), page
  and CGI with `web/deploy.sh`. The app reached `READY`; the remote refused
  `shutter=512` (`bad request`), took `shutter=130` (exposure `{shutter:
  130, iso: 100}`) and went back to Auto. No sweep or manual still taken
  yet, so the in-between times' Exif names are unseen.

## One HDR sweep: 6 frames at ISO 100 — 8 November 2026

The `HDR SHOTS` setting (6 or 8) is gone from the Menu, the remote
(`shots=` is now a bad request; the state has no `shots`) and the
maintenance options (`--sweep-shots`). Every sweep is 6 frames at ISO 100,
2 EV apart: 1/8, 1/32, 1/125, 1/500, 1/2000, 1/8000 (TV 48, 80, 112, 144,
176, 208). Settings slots keep byte 13 (written as 6; slots with 8 still
load).

- Host tests: all of `src/app/tests/` pass; the synthetic sweeps set
  `MANUAL 48 100` … `MANUAL 208 100` and show `SWEEP 1/6` … `6/6`.
- Camera (home network, `<home-network-ip>`): installed with
  `install-app.sh --backup hdr-six-iso100-20261108 --restart` (sha256
  `0e041f36…c79`), page and CGI with `web/deploy.sh`. The app started,
  loaded the slot the previous build wrote (`APP SETTINGS loaded: mode
  SINGLE, output RAW+JPEG, save flash first, …`), reached `READY`, and
  refused `cmd=set&shots=8` as `bad request`. No sweep was taken on the
  camera yet.

## Sweep: exposures kept after a crash, shot sound in the background — 7 November 2026 (T24)

Two fixes after the sweep that died in frame 6 (TODO T24):

- **Exposures survive a crash.** Each frame's line goes to the queue's
  `STAMP.csv` (appended, fsynced) as soon as it is captured, before its DNG
  is written. Before, the file was written only after the last capture, so
  the recovered `20261107-160837-sweep.csv` had no exposures.
- **The shot sound no longer holds up each frame.** `07_N_AF.wav` starts in
  the background (`g360ctl_sound_start`) and is reaped before the next
  frame's sound and after the last capture. Waiting for `paplay` had cost
  0.25–0.92 s per frame between setting the exposure and arming the frame.

**Host checks** (all pass): `test_queue.py` (appended lines, refusals, a
line cut off by a crash: earlier frames keep their exposures),
`test_app.py` (each background shot sound reaped before the next and before
the sweep ends), and the other app tests.

**Installed** with `install-app.sh --backup sweep-sound-20261107 --restart`:
`g360-app` `d546115d6392209c9e6d10db76d3baaf6bf2f788f4e9e5b200104f3a58639647`,
PID 1514. Previous files kept as `*.g360-before-sweep-sound-20261107`.

**Checked on the camera** (sweep `20261107-165513`, HDR 6, RAW, flash first,
sounds on): arming 0.05–0.10 s (0.25–0.48 s with sounds on before); last
frame saved at 16.7 s (17.9–18.2 s before, 15.8–15.9 s with sounds off);
all six exposures in the card's list; queue empty and no `paplay` left
running afterwards. Not yet confirmed by ear that each frame's sound plays.

## Live preview: camera runs passed — 7 November 2026 (T20)

**What changed** (app `258ef76477850b1e8743c37e25619269100fe431d09c9b0b02bc5f01c6fb825e`,
CGI `g360-remote` `71b8d75168a5e0bc1c19046d1647e2cc9105be61c0c849b058745e94731cf080`,
`remote.html` `a8de45837e70029f1d349111a20436a5334939fda239049a1af72df87323771c`):
the Live preview card, `cmd=preview&on=1|0`, `cmd=stream`, the app's MJPEG
port 8090 and g360cap's `g360_preview_*` (README, "Remote control: the web
page"). Installed with `install-app.sh --backup live-preview-relay-20261107
--restart` on the hotspot network (`<hotspot-ip>`), page deployed with
`src/app/web/deploy.sh`. Same boot as the day's earlier work
(`139ef0e1…`), stock stopped, no power cycle.

**On the camera:**
- `curl` from the Mac on port 8090: 46 JPEGs in about 6 s (about 7.7 a
  second), 640×320, about 14.7 KB each, a correct picture of both lenses.
- In the browser, Start showed the picture through `cgi-bin/g360?cmd=stream`
  (`On · 1 watching`). The built-in browser pane blocked the direct port
  (`ERR_BLOCKED_BY_CLIENT`), which is why the page uses the CGI relay.
- A Capture with the preview on (mode set to Single for the test, then back
  to HDR; delay 5 s): the preview ran through the countdown, then
  `APP PREVIEW paused for the capture: ok`, JPEG and DNG saved
  (`20261107-154615-00105`), `APP PREVIEW camera start: ok`. The page went
  on → paused → on and the picture resumed on the same connection.
- Stop: the camera preview stopped (`camera stop: ok`), the page's stream
  closed, state `off`.
- Start, then leave the page: `stopped (no viewer for 20 s)` after about
  20 s, the camera preview stopped; no pictures were encoded while nobody
  watched.
- The kernel log shows no fault; the app kept running.

**Host tests:** all 12 app test files pass (the preview scenario: refusal
before Start, the MJPEG stream, the CGI relay, pause for a capture, Stop,
the idle stop), and g360cap's host tests with the preview probe.

**Not checked:** a sweep with the preview on (it uses the same pause), a
phone browser, more than two viewers, hours-long runs.

## Boot hook regression (7 November 2026)

After reboot the root of the card exposed three live `mod.sh` entries; the
camera ran the older `hdr-ui-20260926` listener instead of the installed hook.
There was no readiness sound, and double-Power launched the old HDR controller.
The old listener and screen were stopped, and the v2 listener was started in
the mod cgroup. The owner confirmed that double-Power showed `LOADING...` and
started the app. The visible root `mod.sh` was restored from the pre-change
backup, but `ls` still showed three entries. The correct hook and sound are
therefore **not verified for the next boot**. After the app stopped stock, the
home address stopped answering on ports 21, 23 and 8888. Do not replace or
rename the root hook again until the raw FAT root is inspected and the duplicate
entries are safely resolved. See `target.card-rename-over-existing-leaves-old-entry-20261105`.

## Boot hook duplicates removed (7 November 2026, 21:00)

The card is FAT32 (the kernel mounts it with its "exfat" driver). A raw read of the root directory showed two live
`MOD.SH` entries before the repair: the old 255-byte launcher (first in directory
order, so the one boot used) and the 1472-byte hook. Both were removed; the new
`hdr/boot/mod.sh` (2333 bytes) was staged in `/tmp`, checked identical, then
written once. A second raw read shows one live `MOD.SH` at cluster 200456, data
identical to the repo file. Until the next boot the kernel's cache lists the name
but cannot look it up in lowercase. **Not yet verified:** the sound at boot.

## Boot listener and loading indication (7 November 2026)

The launcher now attempts `LOADING...` before requesting the stock stop and
redraws it after stock exits. The isolated launcher lifecycle tests check the
lock and ordering. The boot hook watches for the listener's own readiness log
and plays `BT_connected.wav` without holding up input. ARM build and deployment
are checked separately; an attended fresh-boot check is still needed to confirm
both the sound and the early display while stock is running.

# Minimal app validation — 5 November 2026

## Remote control page: camera runs passed — 7 November 2026

**What changed** (app `1ff44fecd197f4b5e88a465a1d419ebd52e50e88bf8b9bb4d7fac815d460bd00`,
CGI `g360-remote` `9010c4872be08fff208aa5b180ccd7c916a93bc2e2a867b5659cc7e514f5f214`):
the app answers the web page over `/tmp/g360-app.sock`; `remote.html` and
`cgi-bin/g360` replace the stock-app HDR page (README, "Remote control: the
web page"). Installed with `install-app.sh --backup remote-20261107
--restart` on the home network (<home-network-ip>), page deployed with
`src/app/web/deploy.sh`.

**On the camera** (same boot, stock stopped):
- `cmd=state` took 0.04 s (0.12 s the first time).
- `set mode=single`, `set delay=2` stored and shown; `set raw=maybe`
  refused as a bad request.
- `capture` with a 2 s delay: `CAPTURE IN 2`; a second `capture` refused
  (countdown running); `cancel` back to the count; `capture` again counted
  down and captured.
- Auto photos, every 6 s, delay 2 s, RAW on: captures 7.0 and 7.3 s apart.
  A RAW still keeps the camera busy about 5 s, so delay + still (7 s) is
  longer than the interval and the press waits for READY, as designed.
- Auto photos, every 5 s, delay 2 s, RAW off: countdowns at 0.2, 5.2,
  10.2, 15.1 s; captures 5.05, 4.92 and 5.06 s apart (0.1 s polling): the
  countdown fills the interval. `auto every=0` stopped them (`APP AUTO
  stopped (remote) after 4 presses`).
- `capture&sweep=1` in single mode, RAW off: `HDR SWEEP`, `SWEEP 1/6` …
  `6/6`, ready again after 10.9 s.
- The settings were put back (HDR, 6 shots, flash, delay 5 s, RAW on).
- The page in a browser, desktop and 375 px wide: live screen text, status
  line, settings highlighted; no sideways scroll.

**Attended, the same day:** auto photos every 10 s, delay 5 s, single
mode, RAW off: countdowns started at 0.2, 10.2, 20.2 … 70.4 s; the user's
short Power press during the 8th countdown stopped them and cancelled it
(`APP AUTO stopped (Power pressed) after 8 presses`, `APP DELAY cancelled
(auto photos stopped)`).

**Wi-Fi made status-only** (the user's choice, same day; app
`a6fb10c645b30cd9d0c7d55911fd29d72e4b9842e00a7695cd40fd7934fc6164`, CGI
`3be5cf8554a0943609b12ed76229de0ff09294d76c02995cfd36b64f8c18a076`): the
page shows the Wi-Fi setting without buttons; `cmd=set&wifi=ap` on the
camera answered `bad request`.

**Auto countdown and 2 s warning** (follow-up the same day, app
`a97f72a0fac05ed9c92f0e624541cb2f1ca8bf967b764116e1ff48f09c81c528`, backup
`g360-app.g360-before-auto-countdown-20261107`). Between auto shots the
screen counts down to the next press instead of `AUTO N S`; with no delay
`timer_last2sec.wav` plays 2 s before each capture. On the camera, single,
RAW off: Delay OFF, every 6 s: `AUTO IN 4` … `1`, captures, and the log's
`APP AUTO 2 s warning` before presses 2 and 3. Delay 5 s, every 8 s:
`AUTO IN 2`, `1`, then `CAPTURE IN 5` … `1`, no warning. Settings put
back. The sound itself was not checked by ear.

**Settings open on the camera** (attended): with `SETTINGS 1/7 / MODE:
HDR` on the screen, `set delay=2` and `capture` were refused with
"settings open on the camera"; the delay stayed 5 s, nothing was
captured. After a Power press the page showed the camera ready.

**The old HDR files on the card.** `www/cgi-bin/hdr-start`, `hdr-stop`,
`hdr-status` and `mods/hdr-capture.sh` were deleted. The `cgi-bin`
directory then listed six entries each for `hdr-start`, `hdr-stop` (they no
longer resolve: 404) and `hdr-status` (six listings of one older
2017-01-14 copy, inode 3136, which only prints `/tmp/gear360-hdr-status`).
They are stale directory entries from earlier deploys, the card defect in
`target.card-rename-over-existing-leaves-old-entry-20261105`; deleting the
live copy uncovered them. They were left alone: a check of the card's file
system should clear them.

## RAW setting (JPEG only): unattended camera runs passed — 7 November 2026

**What changed** (build `a822b06a88b6459bf86c59739039b985e7d6c4d12c6f8e7cf863c679c093707d`):
a `RAW` setting, second in the list (`ON` default, `OFF`). With `OFF` a still
is the JPEG with Exif alone (no frame copy, no DNG name reserved) and sweep
frames are JPEGs saved straight to the card, whatever `SAVE` says. The ready
count and the space checks use the JPEG allowance; the second line says
`JPEG ONLY` or `HDR 6 JPEG`. Stored as bit 7 of the slots' byte 15. New
maintenance switches: `--raw on|off`, `--sounds on|off` (this run only) and
`--store-settings` (store and exit; refused while the app holds the storage
lock).

**Camera** (boot `139ef0e1…`, home network, no power cycle; muted first, at
the user's request: stored sounds `OFF` and `--sounds off` on every run).
Records (not published: `../../output/raw-setting-20261107/RESULT.md`):

| Trial | Result |
|---|---|
| JPEG still (`--once --raw off`) | one 5.0 MB JPEG, no DNG; capture to saved 1.40 s (RAW: 4.29 s) |
| JPEG sweep, 6 shots, `SAVE` flash | six JPEGs straight to the card, list names `.jpg`, Exif exposures match; held still 6.0 s |
| RAW still | DNG + JPEG as before |
| RAW sweep, 6 shots, card | six DNGs, list names `.dng`; held still 21.4 s |

**Host checks** (all pass): `test_settings.py` (bit 7, older slots read as
RAW `ON`), `test_storage.py` (a JPEG reserved alone, on the same sequence),
`test_app.py` (RAW in the menu, a JPEG still, a JPEG sweep with `SAVE: FLASH`
that never touches the queue, JPEG counts and `CARD FULL`, the start-up space
check with RAW `OFF`, `--raw`/`--sounds`, `--store-settings` and its refusal
under the lock), and the rest of the README's list; ARM build clean.

**Deployed** with `install-app.sh --backup raw-setting-20261107` while the app
was stopped, then started with `start.sh`; previous build kept as
`g360-app.g360-before-raw-setting-20261107`. Running with the user's settings
and sounds `OFF`.

**Not yet tried on the camera:** the setting by button, the new screen text
on the OLED, a JPEG sweep started by a button.

## Saves in 2 MiB synced pieces: no more out-of-memory kill — 6 November 2026

**What broke.** A Capture click froze the camera at `CAPTURING...`
(boot `139ef0e1…`). The capture finished, then the kernel's out-of-memory
killer killed `g360-app` inside the DNG `write()` on the FAT32 card:
`20261106-185117-00023.dng` stopped at 24 MiB of 33.8 MB and its JPEG was
empty (both left on the card). At the kill: 2.4 MB free, 21 MB of the file
in writeback, the app 42 MB (one RAW plus one JPEG) and the only large
process. About 270 MB of the 378 MB RAM is the camera driver's, so roughly
80 MB is left for programs. A second press during the shot was dropped (the
log has one `APP CAPTURE`), so it played no part.

**Fix.** `save_parts` ([g360_output.c](../lib/g360cap/g360_output.c))
writes at most 2 MiB, then `fsync`s, so no more than 2 MiB of a file waits
for the card. The sweep move to the card (`copy_file`, [queue.c](queue.c))
syncs every 2 MiB the same way. The bytes written are unchanged.

- **Host:** all `g360cap/tests` and `src/app/tests` pass;
  `test_output.py` still matches a full-size DNG byte for byte.
  `test_app.py` had failed to link since the LED commit (no LED stubs);
  stubs added.
- **Camera** (same boot, build `fd8a36d8…`, installed with
  `install-app.sh --backup chunked-save-20261106 --restart`): two Capture
  clicks, `00024` and `00025`, each 33,773,568 B DNG plus a 6.1–6.2 MB
  JPEG, `APP SAVED` both times. Sampled every ~2.5 s: writeback peaked at
  2,052 kB (21 MB at the kill) and the app went back to 7.8 MB after each
  save. Press to saved about 5 s by file times, like `00021` and `00022`
  before the change. A sweep with the new move was not run.

## Safe install: app and hash swapped together — 6 November 2026

**What broke.** A deploy moved the running build aside, then uploaded the
new one straight onto `g360-app`. The camera rebooted at 19:11 with 11,280 of
97,812 bytes written. `g360-app.sha256` still named the old build
(`6defd9bd…`), so `start.sh` refused the file (`hash mismatch`) and
Double-Power did nothing; the refusal went to the listener, not `app.log`.
Fixed by copying the kept `g360-app.g360-before-flashearly-20261106` back.

**New install path** ([README](README.md#installing-a-build-on-the-camera)):
`tools/install-app.sh` stages the build and its hash file as `.new`, then
replaces both in one camera command; `tools/deploy-over-web.sh` uploads to
camera RAM first and swaps only after the hash checks.

- **Host:** `tests/test_deploy.py` against a fake CGI: cut-off, lost piece,
  interrupted upload and backup collisions leave the old pair; `--restart`
  stops, swaps, then launches.
- **Camera** (boot 1793952688, stock active): a test file deployed new and
  over itself (`ls` lists it once), then removed. HEAD rebuilt to the same
  `6defd9bd…` and was reinstalled without `--restart`: one entry each for
  `g360-app` and `g360-app.sha256`, both naming `6defd9bd…`, nothing left in
  `/tmp` or as `.new`. 4 min 26 s.
- **Seen by chance:** a later upload with the new script was stopped at
  88 KB. The live pair stayed `6defd9bd…`; only its RAM copy was left (the
  cleanup on interruption was added after).
- **FTP for the bytes** (the user's request: 4.5 minutes was too slow).
  A 98 KB test file deployed to the card in 2.2 s on the home network
  (FTP 0.2 s, then the same checks, copy and swap), and a 3 KB one over it in
  1.0 s; `ls` listed the name once, `/tmp` was clean, and the file was
  removed. `install-app.sh` without `--restart` refused the running app
  (PID 3392) and changed nothing.
- **`--restart` and `--backup`** first ran on the camera with the chunked
  save build (6 November, above): backup made, app stopped, pair swapped,
  app started (PID 873).

## Status corner and shots left — 6 November 2026

The top-right corner shows the capture mode (one camera for single, a camera with a second behind it for HDR sweeps), the Wi-Fi state (home network, access point or
none), the charge in percent (a bolt in place of the % sign while charging),
read every 2 s ([README](README.md#the-status-corner-wi-fi-and-battery)).

- **Host:** display pixels and refusals, the status reader against fake
  `/sys` and `/proc` trees, and the app redrawing and logging on each change
  (all camera-app tests pass).
- **Deployed** `g360-app` `c0a7349e492359df86894ded3470979dcf224da1254f04f23a637e0a0b7f063e`
  on boot 1793936180; previous build kept as
  `g360-app.g360-before-status-20261106` (`b00f97ac…`); `ls` lists the name
  once. Relaunched through the double-Power entry script (already on the
  home network, so `wifi-join.sh` left it alone): `APP STATUS Wi-Fi home
  network, battery 100%, charging`, then `READY`.
- **On the camera, with the user:** unplugging removed the bolt. A replug
  4 s later wasn't recognised by the charger chip (5 V present, no charger
  identified), so the camera didn't charge and the bolt correctly stayed
  off.
- **Second build** `f3144cfa915bb8f1f4113c23be014875988aeedee6b537b610eac78250b934d9`
  (the user's request): stills or sweeps left instead of `READY`. Deployed
  on the same boot (previous build kept as
  `g360-app.g360-before-left-20261106`); it showed `48 SWEEPS` / `HDR 6
  FLASH`.
- **Not yet seen:** a recognised replug, the Wi-Fi icon changes, the count
  after a shot. Record (not published: `../../output/status-corner-20261106/RESULT.md`).

## DNGs with the shot's metadata — 6 November 2026

Single shots and sweep frames now carry the still's Exif (exposure time,
ISO, program, white-balance mode, date, f/2.2, 1.2 mm), sweep frames a
description (`HDR SWEEP … FRAME k/n`), and every DNG the camera's IQ
MakerNote. Header 40,960 bytes (`g360_save_dng_full`).

- **Camera:** `capture-pair.py --tag DNG3 --frame-output dng` with
  `g360-shot` `479e6bae…` (silent; boot 1793857325, reenter, no reboot). The
  DNG equals `make_dng.build_full` byte for byte, has ExposureTime 1/125,
  ISO 100, Program AE, WB auto like the JPEGs, and ExifTool validates it.
  rawtoaces 2.2.0 put ExposureTime 0.008, ISO 100, program, WB and focal
  length into the EXR header.
- **Host:** C/Python full-header parity for four metadata variants, the
  MakerNote records unchanged, refusals (bad dates and descriptions, no
  info, small capacity, bad frame), sanitizers, ExifTool; `g360_save_dng_full`
  file parity; the queue accepts both frame sizes; the app passes the date
  to every DNG and `HDR SWEEP … FRAME k/8` to each sweep frame.
- **Deployed** `g360-app` `b00f97ac2a6e777aa325e4c137f6722b845b762c15e6080db51e53aa75698c4c`;
  previous build kept as `g360-app.g360-before-dngmeta-20261106`
  (`72e552d1…`); one live entry per name on the raw card. Relaunched:
  `APP SETTINGS loaded: mode HDR, HDR shots 6, save flash first`, `READY`.
- **Not yet seen:** an app-written DNG with these tags on the camera (the
  app wasn't run, to keep the camera silent); the writer is the one
  checked above.

## Settings screen and sweep options: unattended camera runs passed — 6 November 2026

**What changed** (build `72e552d1c593401a4e5f3ab6580089903142e96311a8d99819db0fce9653223f`):
a settings screen opened by pressing Menu (mode single/HDR, HDR shots 6/8,
save flash first/straight to the card), stored in `state/settings.0/1`;
HDR mode takes a sweep on a Capture click; 6-shot sweeps; card-direct
sweeps; three-line screen text.

**Camera runs** (boot 1793857325, stock app inactive, no service started, no
power cycle; `tools/sweep-trial.py --no-fetch`, maintenance overrides
`--sweep-shots`/`--sweep-save`, trial folders, DNGs deleted afterwards):

| Trial | Sweep | Held still | Last frame saved | Moved to the card |
|---|---|---:|---:|---:|
| `20261106a` | 8 shots, straight to the card | 26,418 ms | 28,520 ms | — |
| `20261106b` | 6 shots, straight to the card | 19,343 ms | 21,514 ms | — |
| `20261106c` | 6 shots, flash first | 12,548 ms | 13,343 ms | 14,760 ms |

Each exited 0 with its frames and `-sweep.csv` (8 or 6 lines) on the card,
the flash queue empty, and `APP FLASH LIFE A 1 B 1 pre-EOL 1`. The 8-shot
card-only time had been estimated at ~26.4 s from the flash run; measured
26.4 s.

**Host checks** (all pass): `test_settings.py` (defaults, alternating slots,
torn and damaged slots, refusals, no symlinks); `test_app.py` with a
scripted button driver: Menu press opens the list, Menu moves, Capture opens
and selects, Power leaves without selecting, settings are stored and
reloaded at the next launch, HDR mode sweeps on a click with 6 shots, `CARD`
saves straight to `photos/`, single mode again takes one still, a Menu press
during a capture opens nothing, a Power hold inside the settings powers
off; a hold with no flash space now sweeps straight to the card. Display
tests: two and three lines at rows 7/17 and 2/12/22, one line unchanged.

**Deployed** `g360-app` `72e552d1…` (hash file updated; `start.sh` unchanged,
`eefff93c…`); previous build kept as `g360-app.g360-before-settings-20261106`
(`b447d6e6…`). The raw directory has one live entry per name. Relaunched
through the double-Power trampoline: `APP SETTINGS default: mode SINGLE,
HDR shots 8, save flash first`, `READY`, queue arguments present. Records:
`output/sweep-deploy-20261106/settings/`.

**Not yet tried on the camera:** the settings screen itself (real Menu,
Capture and Power presses, the three-line text on the OLED), HDR mode by
button, and the stored settings across a reboot.

## Flash wear warning after a sweep: unattended camera run passed — 5 November 2026

After each sweep the app reads the flash's own wear estimate (EXT_CSD
bytes 267–269 through `/sys/kernel/debug/mmc0/mmc0:0001/ext_csd`). From
life value 4 (30–40% used) on, the screen shows `FLASH WEAR n` or
`FLASH WORN` instead of `READY` until the next Capture press.

- **Camera run** (boot 1793857325, `sweep-trial.py --tag 20261105b
  --no-fetch`, build `b447d6e6…`): held still 16,674 ms, moved in
  19,627 ms, then `APP FLASH LIFE A 1 B 1 pre-EOL 1` and `READY`. That
  is the real chip read after a sweep; with value 1 there is no warning.
  The trial's DNGs were deleted afterwards.
- **Host:** `test_wear.py` (parsing, refusals of short, non-hex and
  out-of-range files, and that every warning text renders on the 72×32
  screen); `test_app.py` (value 1: logged, no warning; values 4 and 11:
  `FLASH WEAR 4` / `FLASH WORN` after `SAVING 8/8`; a Capture press brings
  `READY` back; no debugfs: logged as unreadable).
- **Deployed** `g360-app` `b447d6e6701a06c85ef3dba51a87a6371dd9d58d46bd3e84cc542ec5a097876d`
  (hash file updated; `start.sh` unchanged, `eefff93c…`); the previous
  build is kept as `g360-app.g360-before-wear-20261105` (`517ad99b…`). The
  old file was deleted before the rename; the raw directory has one live
  entry per name. Relaunched through the double-Power trampoline:
  `launch mode=reenter`, `READY`, queue arguments present. Records:
  `output/sweep-deploy-20261105/wear/`.
- **Not seen on the camera:** the warning itself (the chip reads 1).

## HDR sweep (hold Capture): unattended camera run passed — 5 November 2026

**Status:** the sweep itself is validated on the camera without the user,
through the maintenance option `--sweep-once`. The physical gesture
(hold Capture, release) is still to be tried by the user.

**The run.** Boot 1793857325, stock app inactive since the session start,
no service started, no power cycle, no firmware change. Build
`517ad99bdb70ce28…` staged in `/tmp`, run by
[tools/sweep-trial.py](tools/sweep-trial.py) `--tag 20261105a` with the
launcher's three locks, a trial root on the card
(`/opt/storage/sdcard/exp-sweep-20261105a/G360`) and a trial queue on the
flash (`/opt/usr/g360-exp-sweep-20261105a/queue`). Its log (times are uptime):

```
13544.80 APP SWEEP 20261105-192750
13547.61 APP SWEEP FRAME 1 TV 48 ISO 1600 exposure 1/8 ISO 1600 at 2472 ms
13549.89 APP SWEEP FRAME 2 TV 48 ISO 400 exposure 1/8 ISO 400 at 4767 ms
13552.15 APP SWEEP FRAME 3 TV 48 ISO 100 exposure 1/8 ISO 100 at 7010 ms
13554.28 APP SWEEP FRAME 4 TV 80 ISO 100 exposure 1/30 ISO 100 at 9141 ms
13556.38 APP SWEEP FRAME 5 TV 112 ISO 100 exposure 1/125 ISO 100 at 11255 ms
13558.47 APP SWEEP FRAME 6 TV 144 ISO 100 exposure 1/500 ISO 100 at 13337 ms
13560.54 APP SWEEP FRAME 7 TV 176 ISO 100 exposure 1/2000 ISO 100 at 15412 ms
13562.59 APP SWEEP FRAME 8 TV 208 ISO 100 exposure 1/8000 ISO 100 at 17461 ms
13562.62 APP SWEEP CAPTURED 20261105-192750: 8 frame(s), held still 16702 ms, saved to the flash 17500 ms
13582.36 APP SWEEP MOVED 20261105-192750: 8 frame(s) to the card, 0 incomplete dropped, status ok
13582.37 APP SWEEP SAVED 20261105-192750: moved to the card in 19426 ms
13582.42 APP UI READY
13582.49 app_exit=0
```

- **Held still 16.7 s**, the same as the speed test's 16.8 s
  (`output/speed-20261105/RESULT.md`, S5e); the move to the card took
  19.4 s (2.3–2.5 s per frame). Previously, 8 single shots took about 29 s.
- **The camera applied every exposure.** Each still's own Exif record (the
  `exposure` values above) matches the request: 1/8 s at ISO 1600, 400,
  100, then 1/30 … 1/8000 s at ISO 100.
- **Files.** Eight DNGs of 33,733,196 bytes,
  `20261105-192750-00001.dng` … `-00008.dng`, and
  `20261105-192750-sweep.csv` with the eight lines; the flash queue was
  empty afterwards and `/opt/usr` usage returned to 96,608 KB.
- **The frames differ as they should.** Frames 1, 4 and 8 were fetched and
  read as 5824 × 2896 16-bit samples. The scene was dark (median 4096, the
  black level), but its brightest points fall with the exposure: maximum
  65472 (clipped), 17536 and 8128; 99.99th percentile 8640, 4416, 4416.
- The trial's DNGs were then deleted from the card; its records stay in
  `exp-sweep-20261105a/G360/state/` and `photos/*-sweep.csv`. Local records:
  `output/sweep-trial-20261105a/`.

**Host checks** (all pass): `test_queue.py` (frames, exposure lists,
ordered and resumable moves, dropped partial frames, refusals),
`test_storage.py` (DNG-only reservation on the same counter),
`test_app.py` under ASan/UBSan (sweep by hold, `--sweep-once`, a capture
failure in frame 3, a Power hold during a sweep, recovery of a queue left
by a crash, no flash space), `test_launcher.py` (queue arguments; the app
still starts when `/opt/usr` can't be used), `test_target_flags.py` (the
new `O_WRONLY`, `O_TRUNC`, `O_APPEND` numbers against the measured table).

**Deployed** to `/opt/storage/sdcard/mods/g360-app-v1-20261105/`:
`g360-app` `517ad99bdb70ce28be069da4d763e5f6f8aa3682ac99cb406cf27afa3b4f99b2`
(hash file updated) and `start.sh`
`eefff93cc63daa3c4d4313a0d4c73e23e63050c76da6f8617d9104ed885a87ce`.
The previous ones are kept as `*.g360-before-sweep-20261105`
(`15a28f82…`, `6cb0ef94…`). Each old file was deleted before its new one was
renamed in. The raw directory then had one live entry per name; the
renamed launcher's old long name is marked deleted and its short entry
reads `start.sh`. Records: `output/sweep-deploy-20261105/`.

**Launched** through the double-Power trampoline (`start-hdr.sh` →
`start.sh`): `launch mode=reenter`, `APP UI READY`, process arguments
`… --queue /opt/usr/g360-app/queue --queue-free-bytes 1935904768`, and no
`SWEEP unavailable` line. The app was left running at READY, the camera on
its charger at 100%.

**Not yet tried:** the hold gesture and the `HDR SWEEP` hint on the real
buttons and screen; the sounds' timing as heard; a Power hold during a
sweep and the move at the next start (host-tested only).

## Power hold turns the camera off: validated — 5 November 2026

**It works, twice.** The user ran the trial and repeated it. Each time,
they booted the camera normally, double-pressed Power, waited for READY
and held Power. The camera showed POWER OFF and switched itself off, then
booted normally when turned on. The app log has both runs:

```
launch mode=fresh btime=1793856869 executable=15a28f826ecbc3b4…
APP UI STARTING
APP UI READY
APP POWER HOLD
APP POWER OFF from /tmp/g360-app-poweroff, log /tmp/g360-app-poweroff.log
launch mode=fresh btime=1793857076 executable=15a28f826ecbc3b4…
APP UI STARTING
APP UI READY
APP POWER HOLD
APP POWER OFF from /tmp/g360-app-poweroff, log /tmp/g360-app-poweroff.log
```

Checked read-only on the next boot (1793857325, stock app running as
normal):
- **The capture session closed cleanly before each power-off.** In
  `capture.log`: `open: ready` at 124.1 s, `close` at 127.6 s; then
  `open: ready` at 63.5 s, `close` at 267.1 s.
- **No error line followed.** "APP ERROR power off" is printed only if the
  camera stays on.
- **The card is intact.**
  - The card mounted with no error in the kernel log.
  - Both FAT copies mark it cleanly shut down (`0x0fffffff`).
  - The raw directories `G360/state`, `G360/photos` and the app folder
    have one live entry per name.
  - Both counter slots hold 4, so the next shot is 00005.
  - Shots 00002–00004 (JPEG and DNG) still match the copies fetched before
    the power-offs, byte for byte.
- **The second FAT copy still holds none of today's files**, even after
  deviced's unmount and two power-offs. The camera's driver evidently
  writes only the first copy. This predates the app.

**Not observed:** deviced's own log, which went with the power-off (it's in
RAM), and the time from the hold to power-off. A Power hold during a
capture was tested on the host only.

Evidence: `output/poweroff-20261105/after/` (local).
Fact: `app.power-hold-poweroff-20261105`.

### How it was built and deployed

**What the stock app does** (REA 6.7.0, Ghidra 12.1.3, fresh sessions
through the repository's stdio bridge; responses in local
`output/poweroff-20261105/`). Pinned binaries and their SHA-256:

| Binary | SHA-256 |
|---|---|
| `di-camera-app` | `5937b3ab…` (`linux-app-elf`) |
| `libdevice-node.so.0.1` | `ff7a00ff…` (`linux-device-node-elf`) |
| `libslp_devman_plugin.so` | `973e5e09…` |
| `libdeviced.so.0.1.0` | `821430ca…` |
| `/usr/bin/deviced` | `c043f4ab…` |
| kernel | `ebfd2c32…` (`linux-kernel`, raw, entry `0xc0008000`) |

1. **The order.** `CAPPPowerOffMode::onEnter` (`0xbddd4`) masks the
   keys, then calls `managePowerOff`. That runs the screen and LED and a
   1.5 s delay, then reaches `powerOffSystemShutdownMode` (`0xbbcd0`).
   That calls `saveValueAndEndMMPkg` (`0xb8a58`): prefs, `UI_Mm_Photo_End`,
   then RTOS PowerSave(1) and the PowerOff wait unless there's a lens error.
   It then calls `powerOffSystemShutdown` (`0xba534`).
2. **`powerOffSystemShutdown`** calls `setDevicePowerShutdown` with
   `r1 = 1` (`0xbacf4`). It then calls `deviceRequestPowerOff`
   (`0xbaee8`) and starts the power-off LED. `setDevicePowerSuspend`
   comes last, either directly (`0xbb868`) or at the end of
   `powerOffLedByThread` (`0xb9b7c`, about 1.3 s of LED blinking).
   - `setDevicePowerShutdown(1)` (`0x77ed4`) is
     `device_set_property(4, 0x15, 1)`.
   - `setDevicePowerSuspend` (`0x77ebc`) is
     `device_set_property(4, 4, 0)`.
   - `deviceRequestPowerOff` (`0x77eb0`) is `deviced_request_power_off()`.
   Evidence: `ev_fb02639c…`, `ev_e9c2d4d0…`, `ev_efcb8380…`,
   `ev_1a6cb4f5…`, `ev_d0c70f37…`.
3. **`libdevice-node`.** Device 4 is the entry "power" at `0x1f11c`. Its
   setter at `0x14384` (Thumb, outside any Ghidra function) switches
   through a jump table on property − 4. Property 4 calls plugin slot
   `+0x15c` and property `0x15` calls slot `+0x460`, each with the value
   (`ev_b6794cda…`, walk in `node-walk-*.txt`). The plugin's interface
   table is at `0x2a260`: `+0x15c` is `OEM_sys_set_power_state` and
   `+0x460` is `OEM_sys_set_power_shutdown` (`ev_83adb968…`).
4. **The plugin.** `OEM_sys_set_power_shutdown(v)` writes `v` to
   `/sys/devices/platform/d5-keys/shutdown` (`ev_a992e44f…`).
   `OEM_sys_set_power_state(0)` writes `2` to the same file
   (`ev_38d34487…`).
5. **deviced.** `deviced_request_power_off` makes a blocking D-Bus call
   to `PowerOff` on the system bus (`org.tizen.system.deviced`,
   `/Org/Tizen/System/DeviceD/Core`, interface
   `org.tizen.system.deviced.core`), with no arguments and an int32 reply
   (`ev_91dacc25…`, `ev_109dac92…`).
   - **When it accepts:** deviced's handler (`0xdf84`) acts only in state
     1, which "booting done" sets (`0xde48`). It then sets state 2, so it
     acts once per boot. It returns -1 otherwise (`ev_829c550d…`,
     `ev_18118074…`).
   - **What it runs:** the type-1 notifiers. Those stop uevent control,
     stop card uevents, `sync`, and unmount the card. They also turn
     USB off and stop the battery timers (`ev_2c7c97c7…`, `ev_7fcdd748…`,
     `ev_5c9af3da…`, `ev_f1166f33…`).
   - **The unmount:** it is forced. If the card is busy, deviced waits
     0.5 s, then runs `/sbin/fuser -m -k -SIGTERM /opt/storage/sdcard`.
     Five seconds later it runs `-SIGKILL`, then detaches the card lazily
     (`fs_unmount` `0x1690c`, `ev_4e0e4c52…`; `0x12860`, `ev_0f8e3b60…`,
     arguments `ev_94a888a0…`). Nothing in deviced cuts the
     power.
6. **Kernel.** The `d5-keys` `shutdown` store (`0xc0392240`) parses the
   value into `g_shutdown`. On `2` it calls the pointer at `0xc07b9d8c`
   (`ev_349bbf76…`). Board init sets that pointer to `0xc001e048`
   (`ev_f7547d28…`), which is the board's power-off. It
   reboots instead when the `1shot` value is 1 or 9, which only a write
   to `d5-keys/1shot` sets (`0xc039230c`, `ev_dcee8688…`). Otherwise it switches the
   power-management chip off, with no orderly kernel shutdown
   (`ev_b1cced38…`). `1` only stores the value: the show and store
   functions are the only readers in the static references.

**Camera state, read only** (boot 1793849521): deviced logged
`request booting done` at 1.7 s, and `d5-keys/shutdown` read 0.

**What our app does.** On key 116 (press) in any state, the app:
1. shows `POWER OFF`;
2. lets the worker save a shot in progress, then close the capture
   session and storage;
3. plays `powerOFF.wav`;
4. copies its executable to `/tmp` and re-executes it as `--power-off`,
   with no file on the card;
5. runs `sync`, `device_set_property(4, 0x15, 1)`,
   `deviced_request_power_off()`, `sync` and
   `device_set_property(4, 4, 0)`.

Step 4 exists because deviced's forced unmount would otherwise kill the
app, which runs from the card, before the last step. The RTOS PowerSave,
the LED animation and the 1.5 s delay are left out
([README](README.md#power-hold-turning-the-camera-off)). Host tests:
`tests/test_power.py` covers the step order and each failure. The Power
scenarios in `tests/test_app.py` cover a hold at READY and a hold during
a capture, whose shot is saved first. They run the real copy, re-exec
and detach: the sequence runs from `/` with no inherited descriptors.
ARM build passes, with no new `NEEDED` libraries.

**Deployed** on boot 1793849521, with the previous app stopped by SIGTERM
after boot, PID and executable checks. Executable
`15a28f826ecbc3b473af6f3ade12dd7a7b0fc4ac1e28d32565c7baf9d9426b67`; the
previous build is kept as `g360-app.g360-before-poweroff-20261105`
(`ab627b29…`).

**A card problem found and fixed while deploying.** A first deploy (build
`e62849a1…`) renamed the upload over the existing `g360-app`. On this
card, the camera's FAT driver leaves the old directory entry live in that
case. Reading the raw FAT32 directory showed two live `g360-app` entries:
the old one at position 2 and the new one at position 15. Each still had
its own intact cluster chain in FAT 1; nothing was cross-linked. Fixed
with normal file operations, checking hashes between each step:
1. renamed the current entry away;
2. confirmed the remaining one was the old build;
3. deleted it;
4. renamed the new build back into the free name.

The raw directory then showed one live entry and the old chain freed.
The final deploy deletes the old file before renaming. The card root
already has three live `mod.sh` entries (1472, 454 and 255 bytes) from
earlier work; they were left alone. The second FAT copy lags for every
file written on this boot. Records are in `output/poweroff-20261105/deploy/`.

## Full flow from a fresh boot: validated — 5 November 2026

**The whole path works from a normal boot.** The user rebooted the camera,
double-pressed Power, waited for READY and pressed Capture twice. Checked
read-only on the camera afterwards (boot 1793849521, uptime about 90 s):
- **Launch:** `launch mode=fresh btime=1793849521 executable=ab627b29…`.
  The launcher stopped the stock app itself; it was inactive with MainPID 0
  afterwards, and `g360-app` (PID 447) was still running in READY.
- **Library:** `open: ready` at 42.4 s after boot.
- **Two stills, 5.5 s apart:** RAW holds of 625 ms and 578 ms, both back to
  READY:
  - `20261105-133248-00003.jpg` / `.dng`;
  - `20261105-133253-00004.jpg` / `.dng`.
- **Counter:** it carried across the reboot, from 00002 to 00003.

The user's second shot on the earlier boot is also there:
`20261105-132211-00002`, taken at 13:22 with the same build.

**All three new shots, checked on the host.** Each file was fetched and
matched its on-camera SHA-256.
- **DNG:** each passes `exiftool -validate` with no warning, and each is
  byte-for-byte identical to `make_dng.py`'s rebuild from its own samples
  and the IQ records in its own JPEG (`tools/verify-shot.py`).
- **JPEG Exif:** each carries its own capture time and exposure:

  | Shot | Time | Exposure | ISO |
  |---|---|---|---|
  | 00002 | 13:22:11 | 1/100 s | 100 |
  | 00003 | 13:32:48 | 1/100 s | 100 |
  | 00004 | 13:32:53 | 1/125 s | 100 |

  All three show Program AE and Auto white balance.

**Small observation.** For shot 00003, `APP CAPTURE …` is logged before
`APP UI CAPTURING...`. The worker thread's log line beat the UI thread's.
The screen and sound worked as before, so this looks like log ordering
only; it was not investigated.

Local evidence: `output/camera-app-20261105/shot-002`–`shot-004/`
(files and `verification.json`) and
`output/camera-app-20261105/fresh-boot-20261105-1332/camera-logs.txt`
(app log, capture log, hashes).

## Root cause found and fixed; first camera capture — 5 November 2026

**The fresh-boot storage failure is resolved.** The app now opens its own
storage, reaches READY and takes a still on the camera.

**Root cause.** `storage_platform.h` carried the *generic* Linux numbers for
`O_DIRECTORY` and `O_NOFOLLOW`. On the camera those bits are one octal digit
lower, and the bit the generic headers use for `O_DIRECTORY` is the camera's
`O_DIRECT`. The card rejects that bit (it was described here as FUSE exfat;
it is FAT32 under the kernel's exfat driver), so the app's
first `open()` of its own photo directory was rejected with `EINVAL` and
storage initialization failed before any capture-library call. The trace
shows it exactly:

```
mkdir("/opt/storage/sdcard/G360", 0755) = -1 EEXIST (File exists)
open("/opt/storage/sdcard/G360", O_RDONLY|O_DIRECT|O_LARGEFILE|O_CLOEXEC) = -1 EINVAL (Invalid argument)
```

**How the numbers were established.** An ARM probe
([tools/open_flags_probe.c](tools/open_flags_probe.c)) measured the camera's
flag bits directly on boot 1793844742, over the card, the root filesystem and
tmpfs, with a regular file and a symbolic link as the reference cases:
`O_DIRECTORY` is `040000` (a regular file gives ENOTDIR), `O_NOFOLLOW` is
`0100000` (a symbolic link gives ELOOP), `O_CLOEXEC` is `02000000` (the only
bit that sets FD_CLOEXEC) and `0200000` is `O_DIRECT`. Directory fsync works
on the card, so the counter slots' durability needed no change, and
`O_CREAT` with `O_EXCL` creates a new file and refuses an existing one with
EEXIST. The fix changes the two numbers; a new host test
(`tests/test_target_flags.py`) compares them with the measured table, because
the host build uses the host's own headers and would not catch this.
Fact record: `target.open-flag-numbers-are-not-the-generic-ones-20261105`.

**Camera run.** The fixed build was deployed to
`/opt/storage/sdcard/mods/g360-app-v1-20261105/g360-app`, SHA256
`ab627b290487b9070f5d74ef4a674c4cb236912ccbc105b1f791bf849c12c10a`, with the
hash file updated to match; the previous build is kept on the card as
`g360-app.g360-before-20261105` (SHA256 `b6c19d42...`). Started on the same
boot 1793844742 with the stock app already inactive (`launch mode=reenter`):
no service was started, no firmware was written, no power cycle. Storage
initialized — `photos/`, `state/counter.0`, `state/counter.1` (20 bytes
each), `state/storage.lock` and the capture library's own `state/capture.log`
were all created, and the screen showed `READY`. The library then reported
`mm_camera_create` succeeded, `bridge alive` and `open: ready`.

**Attended capture.** The user pressed Capture once. The app showed
`CAPTURING...`, played the shutter sound, and the owner confirmed seeing
`CAPTURING...` and `READY` on the screen. The log reads:

```
APP UI CAPTURING...
APP CAPTURE 20261105-125537-00001
APP SAVED 20261105-125537-00001.jpg 20261105-125537-00001.dng
APP UI READY
```

**Outputs, checked on the host.** `20261105-125537-00001.dng` is 33,733,196
bytes (588-byte header plus 33,732,608 unchanged samples) and passes
`exiftool -validate` with no warning: SAMSUNG/SM-R210, 5824×2896, 16-bit,
GRBG, ColorMatrix1, AsShotNeutral, WhiteLevel 65472, BlackLevel 4106.
`20261105-125537-00001.jpg` is 4,729,708 bytes with Exif (SAMSUNG/SM-R210,
software g360cap, 1/100 s, ISO 125, Program AE, F2.2, sRGB, Auto white
balance, date 2026:11:05 12:55:37 matching the file name) and the Samsung
Type 2 MakerNote with AWB, AE and colour records, which `stock_iq.py` decodes
from the file. Rebuilding the DNG with the Python reference from the RAW
samples and this shot's own IQ gives a **byte-for-byte identical** file
(`tools/verify-shot.py`, report in `output/camera-app-20261105/shot-001/`).

**Still open.** The counter slot advanced to 1, so the sequence persists;
a second shot has not been taken. The fresh-boot path itself (a real boot
with the stock app active) was not repeated — no power cycle was needed or
made, and the run used the same-boot reentry path. The app was left running
in READY.

Local evidence: `output/camera-app-20261105/` (probe output, trace excerpt,
app and capture logs, the shot and its verification report).

## Earlier state of this file

Host checks passed: storage restart/clock reversal/torn-state/no-overwrite/
locking/exhaustion/IO checks under ASan/UBSan; OLED renderer and exact device
ABI/failure checks;29 launcher lifecycle/lock/hash/cgroup checks; application
startup, owned dated outputs, two-shot same-session operation, busy/repeat/
dropped-event filtering, single-worker affinity and capture/save/sound/display
failure tests under ASan/UBSan. Freestanding ARM build passed.

Installed ARM SHA256:
`b6c19d429cb18f324a9a59338c11b09edbf6d57fb3c1b2a5d304529d8960ef3f`.
Runtime folder `/opt/storage/sdcard/mods/g360-app-v1-20261105`; all three
staged executable/launcher/hash-file digests matched. Existing double-Power
listener PID337 runs in gear360-mod cgroup on boot1793781685. Only its SD
`hdr-ui-v2/start-hdr.sh` target was replaced with a trampoline; original
SHA256 `a044e993d2cf8c96522131a87ff75b4e2a8ebb83b40b3ed8f128dba5b4022920`
was backed up and verified as `start-hdr.sh.g360-before-20261105` before
in-place replacement. Boot/network scripts were unchanged. No firmware
patch, service restart or power cycle.

User confirmed readiness and was asked to double-Power, then Capture once.
Physical trial is pending its observed result; do not equate host mocks or a
staged executable with a validated camera capture. Fresh-boot invocation has
not been physically retested (no power cycle permitted for a responding
camera). Retained local evidence: `output/camera-app-20261105/` including
preflight, deployment hashes/backup and app-status records.

At final account check, five-hour usage reached95% (5% remaining), reset
1793856676; no new substantive work/trial starts beyond that boundary. Owned
coding workers finished. Remaining work: observe the requested attended trial,
validate actual JPEG/DNG/counter, and record its result; then fresh-boot path
can be checked on a later normal boot when available.

## First real fresh-boot attempt: storage failure

User rebooted normally, reconnected, double-Power launched the app, saw
STARTING then ERROR. Read-only log on boot1793844742 confirms:
`launch mode=fresh`, stock inactive/MainPID0, and
`APP ERROR storage open: storage IO failure`. No photos directory or capture
log was created: failure occurred before capture-library initialization.
Failed app PID457 was terminated gracefully after exact boot/comm/exe guards;
stock remains stopped. Evidence: local `fresh-boot-error.txt` and
`failed-app-cleanup.txt`. Fresh-boot shortcut/stock stop is now observed;
actual capture remains unvalidated. Check ARM-specific open flag constants
in storage_platform.h first (the directory/no-follow flags need target ABI
verification); no root cause is accepted yet. Five-hour usage97%, beyond the
95% stop threshold; only error retention/cleanup performed, no new trial/fix.

## Shot LEDs (6 November 2026)

Each capture (single still, every sweep frame) turns the front and back upper
LEDs from green (`led.3`, `led.4`) to orange (`led.5`, `led.6`) for at least
500 ms, then restores the saved LED states. LED write errors never fail a
capture. Deployed `g360-app`
`6defd9bdbd3993b4450db023393c2bcb96ae4b666152ea6f263e97f5fa328814` (hash file
updated; previous files kept as `*.g360-before-leds-20261106`), relaunched
through `start.sh` (PID 1346). Superseded below.

### Earlier, shorter flash (6 November 2026)

The flash now starts at the start of the shot, before the shutter (or sweep
start) sound, instead of after it, and lasts 100 ms. A thread waits, so the
shot is not held up; each sweep frame flashes at its start. Installed with
`install-app.sh --backup flash100ms-20261106 --restart`: `g360-app`
`1d1cb42f6a6aad3c03c0f45a906564d2a429b4fdfceff24348a2cebfa23a29a5`, PID 2187.
Not yet watched by the user on a real shot.
# Home Wi-Fi address (7 November 2026)

Built and installed `g360-app` SHA-256 `b7fea047019229e648564d56bc4430fa8cc22716d54825e7f2d82176a8aeec93` on the camera over the home network. After restart, the app log showed `192.168.` / `0.192`, then the normal remaining-shot count. The status-reader, app orchestration and display tests passed on the host. The display was not visually observed, and a fresh Wi-Fi reconnection was not exercised.

## Timezone configuration (7 November 2026)

Built IANA 2026f TZif in fat format and installed it beside the launcher.
Tar comparison found no content differences; ownership, mode and timestamps
differ on the SD filesystem. On camera, the bundled Australia/Sydney data
produced AEDT in November and AEST for 1 July 2027. The isolated launcher
tests passed 37 cases, including valid, missing and invalid timezone settings.
Built and installed `g360-app` SHA-256
`b7fea047019229e648564d56bc4430fa8cc22716d54825e7f2d82176a8aeec93`
with the paired installer, restarted it, and verified its log and environment
select Australia/Sydney. The system clock and system timezone were not changed;
no new capture was taken.


## Output RAW / JPEG / RAW+JPEG (8 November 2026, T26)

Replaced RAW ON/OFF with one OUTPUT selection for single shots and every
HDR frame, in both the OLED menu and remote page. RAW saves DNG only;
RAW+JPEG saves same-exposure files under one stem, including flash-first
queue moves and recovery. Old RAW ON migrates to RAW+JPEG, OFF to JPEG.

Host checks passed: app orchestration (ASan/UBSan), settings, remote,
storage, queue, OLED rendering/ABI (including the new plus glyph and both
OUTPUT screens), target flags, installer/deployment and launcher, ARM build.
The queue tests include complete partners of interrupted outputs and failure
before exposure-list durability; app tests include recovery of a large JPEG
pair when the current output is RAW and the card reserve must be protected.

On one camera boot, all nine combinations (RAW/JPEG/RAW+JPEG × single/card/
flash) passed: 39 exposures. A final six-frame RAW+JPEG flash-first sweep
also passed with the recovery-budget revision: matching stems and exposures,
complete 33,773,568-byte DNGs and JPEGs, 12 CSV rows, empty queue, no dropped
frames. A sampled JPEG's exposure, ISO, date/time and pose match its DNG
metadata header. RAW-only persisted across a real app restart; all three
remote output choices saved successfully. No OLED screen was observed by a
person. Acquisition summary (not published: `../../output/output-selection-20261108/RESULT.md`).

Installed final app SHA-256
`b8e2d08966c90f923858423fff08bd7a3d118ea7ad5f22d4a2f98be44fb66c4d`
with the paired installer, backups `output-20261108` and
`output-guard-20261108`. Updated web page and CGI (CGI SHA-256
`21948ce9ff25422b85d134931be4b9bbcda4ca4cd66e10403bed4f11b7b1b435`).
App running (PID 2195), state ready; original HDR/JPEG/8/card/home/2 s/sounds
settings restored. Stock inactive, boot unchanged. Temporary trial photos
cleaned; logs, paths and final photo hashes retained. No full sensor pixels
were downloaded.
