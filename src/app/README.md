# G360 camera app

A minimal C controller built directly with g360cap, g360ctl and g360display.
In single mode (the default) one Capture click takes one automatic-exposure
still, plays `shutter.wav`, shows `CAPTURING...`, and saves the selected
outputs (by default the full RAW as DNG plus the main JPEG, with Exif and
Samsung IQ metadata). It returns to `READY` after the files are synced, then
plays `preview_sound.wav`. Holding Capture for a second, then releasing it,
takes an HDR sweep. In HDR mode every Capture click takes the
sweep (see below). Pressing Menu opens the settings: mode, output, where
sweep frames are saved and more. `OUTPUT` selects `RAW`
(DNG only), `JPEG`, or `RAW+JPEG` (default), for both single photos and every
HDR frame. The screen is cleared before the initial `STARTING` message. Extra presses while busy, key repeats, orphan
releases and dropped input are not queued. Holding Power turns the camera
off (see below). A web page on the camera is a remote for the app
(settings, Capture, auto photos; see below). The top-right corner shows Wi-Fi, battery and charging,
and where `READY` used to be, the screen shows how many stills (or sweeps)
the card has room for (see below). This README still calls that state
`READY`.

## Timezone for photo timestamps

To use a local timezone, create the plain-text file `/opt/storage/sdcard/timezone`
(the `timezone` file at the root of the SD card), containing one IANA timezone
name, for example `Australia/Sydney`. Restart the app to apply it. The app uses
its bundled IANA 2026f timezone database, including daylight-saving rules,
even offline. Names are case-sensitive. If the file is absent, invalid, or
names an unavailable zone, the app keeps the camera's existing timezone and
logs the reason in `G360/state/app.log`. This affects the app and its photo
names and metadata, not the system timezone or the clock itself.

To rebuild the data for installation, run `sh src/app/tools/build-timezones.sh`.
It downloads the pinned IANA 2026f data, checks its SHA-256, and produces
`output/timezone-build/zoneinfo-2026f.tar`. Install its `zoneinfo-2026f`
directory alongside `start.sh` before deploying that launcher. On the camera,
extract it alongside `start.sh` with `tar --no-same-owner -xf
zoneinfo-2026f.tar`, only when `zoneinfo-2026f` does not already exist; never
extract over existing SD-card files. Its TZif files
are compiled in fat format for the camera's older libc. Timezone rules can
change; update the pinned release with future app releases.

## The status corner: Wi-Fi and battery

The top-right corner of every app screen shows, from left to right:

| Icon | Meaning |
|---|---|
| Two beamed notes (leftmost) | Sounds are ON (`SOUNDS` setting) |
| Capital R | RAW is ON (`RAW` setting) |
| Watch face at 3 o'clock | A self-timer is set (`DELAY` not `OFF`); left of the mode icon |
| Camera | Mode SINGLE: a Capture click takes one still (leftmost icon) |
| Camera with a second one behind it | Mode HDR: a click takes a sweep |
| Wi-Fi fan | On an existing network (the home network, [g360wifi](../wifi/README.md)) |
| Animated Wi-Fi fan | Searching for a saved network or connecting: dot, then the inner arc, then the full fan |
| `((•))` | The camera's own access point, with no connected clients |
| AP with a smaller filled centre | The camera's access point has at least one connected client |
| Wi-Fi fan with a × | A Wi-Fi operation failed and no network is available |
| No Wi-Fi icon | Wi-Fi is off or no network is active, with no reported failure |
| Battery, 4 x 7 px (rightmost) | Charge: the 2 x 5 px interior fills from the bottom, one row per 20 %: 0-20 % empty, 21-40, 41-60, 61-80, 81-99, 100 % full |
| Battery whose walls rise from the floor, over and over | Charging, or full with a cable in. The terminal on top and the floor stay lit; 6 frames, 0.3 s each |

The app's text moves below it: one line at row 17, two at rows 12 and 22
(at most two lines); a line of only digits (1 to 6) is drawn at double size. `ERROR` and `POWER OFF` are drawn without it.
`tools/status-preview.py OUT.png` renders the corner through the real
drawing code. Preview of four screens (not published: `../../output/status-corner-20261106/preview.png`)
(local only).

**Where the state comes from** ([status.c](status.c)): the kernel's own
files, no daemon and no D-Bus.
- Battery: `/sys/class/power_supply/battery/capacity` and `status`
  (`Charging`, `Discharging`, `Not charging`, `Full`), and `ac`, `usb` and
  `wireless` `online` for the cable.
- Home network: `wlan0`'s `operstate` is `up` and `/proc/net/route` has a
  default route on `wlan0`.
- Access point: a `hostapd` process and the access point's network
  (192.168.43.0) on `wlan0`. That's the check `wifi-join.sh` already uses.
- Searching and errors: the Wi-Fi script writes temporary state in
  `/tmp/g360-wifi`. Searching takes precedence over the AP, so the animation
  appears while the AP is broadcasting too. An AP restored after a failed
  connection is shown as an AP. Intentional Wi-Fi OFF shows no crossed icon.
- AP clients: the existing background `wl assoclist` reader supplies the
  client count; an unreadable count retains the previous known count.

**When the charging animation doesn't come back.** It follows the battery driver. On
6 November a cable unplugged and plugged back 4 s later wasn't recognised:
the charger chip saw 5 V (`/sys/devices/virtual/sec/switch/vbus_value`) but
identified no charger (`attached_dev` `No VPS`), so the driver kept
`Discharging` and the camera didn't charge. The screen was right. Unplug,
wait, and plug in again.

**How it stays current: polling.** The UI loop already wakes every 50 ms
for the buttons. Every 2 s it reads those files (a few small reads) and
redraws only when something changed. A plug-in shows within about 2 s. The
charging animation needs no extra thread: while charging, the same loop
takes the frame from the clock (`now / 0.3 s mod 6`, `charge_frame` in the
status) and redraws when it differs from what the screen shows; when not
charging the frame stays 0, so an idle screen is never redrawn for it. The
Wi-Fi search animation similarly advances every 0.4 s, and is static when the
camera is not searching. The
app log gets one line per change, for example `APP STATUS Wi-Fi home
network, battery 100%, charging`.

**How the stock app does it: listening.** deviced turns the kernel's battery
events into `memory/sysman/*` values (`battery_capacity`,
`battery_charging`, `battery_charge_full` and others). The stock app
subscribes to them with `vconf_notify_key_changed`, and to the access point
with the tethering service's callbacks (`tethering_set_enabled_cb`,
`…_disabled_cb`, `…_connection_state_changed_cb`). Found from its imports and
strings only. g360-app polls instead: our home network join
(`wifi-join.sh`) doesn't go through the tethering service or ConnMan, so
there's nothing to subscribe to for it, and one 2 s read covers all three
states.

## Sounds for battery, charger and Wi-Fi

Besides the status corner, the app plays the stock app's own tones (the files
in `/usr/apps/com.samsung.di-camera-app/res/sounds/camera/`, see
stock sounds (not published: `../../reverse-engineering/stock-sounds/index.html`)) when these
change. A separate thread reads what the UI thread publishes every 2 s, so
no sound ever holds up the screen or the buttons. The rules are in
`alerts.c` (pure logic, tested without a camera):

| When | Sound |
|---|---|
| Battery reaches 20% or lower | `low_battery.wav`, once |
| Battery reaches 10% or lower | `13_N_NoBattery.wav`, three times in a row |
| A charger is connected | `charger_connect.wav` |
| The camera joins the home network, or a station connects to the camera's own access point | `BT_connected.wav` |
| A station leaves the access point, or the home network is lost | `BT_disconnected.wav` |

- The battery warnings sound when the level gets worse, not on every
  reading, and are silent while a charger is connected. Unplugging at a low
  level warns again. At 10% only the three-times warning plays. An app start
  that finds the battery already low warns once.
- A charger already in at app start is silent (only a new cable plays).
- The access point with nobody on it is not a link. Each station that joins
  or leaves is a sound. Switching the Wi-Fi setting between modes plays
  "disconnected" for the old mode's links and "connected" for the new mode's.
  The home network joined during or just before the app's start is announced.
- **Stations** on the access point are counted by running the Broadcom tool
  `wl assoclist` (`/usr/bin/wl`) once a second while the access point is up
  (`wifi.c`). If it fails, the last count stands.
  **Not yet seen on the camera:** the home-network and charger/battery
  signals use the files the status corner already reads, but the station
  count was only tested against a stand-in `wl`; whether `wl assoclist`
  answers on the camera's access point needs an attended trial (T15).
- Maintenance runs (`--once`, `--sweep-once`) play none of these.
- The log gets `APP ALERT <file>` for each sound.

## Ready: stills or sweeps left

When the app is ready, the screen shows how many more captures fit on the
card as a bare number at double size (10 x 14 px digits): `245` shots in
single mode, `48` sweeps in HDR mode, `1` for the last one, and the words
`CARD FULL` when none fits. The mode icon in the status corner says whether
the number counts shots or sweeps; the R icon says the output includes RAW, the notes icon
that sounds are on (both absent when off). The old second line (`HDR 6
FLASH`, `JPEG ONLY`) is gone. On 6 November the card had 10.07 GB free: 245 stills, 48 sweeps of 6 or
36 of 8.

The count uses the same budget and checks as the shots themselves
([main.c](main.c), `left()`):
- **Budget:** the card's free space when the app started, less 5 MiB, less
  what each shot has written since. A 128 MiB reserve is always kept.
- **RAW** reserves and counts the 33.8 MB DNG for each photo or sweep frame.
- **JPEG** reserves a JPEG allowance of up to 96 MiB and counts about 6.3 MB
  per photo. A sweep reserves one full allowance plus a counted JPEG for each
  remaining frame (backed by the 128 MiB reserve).
- **RAW+JPEG** combines those budgets: about 40 MB per frame. The count
  follows the selected output for single shots and sweeps, whether frames
  go to the flash first or straight to the card. Actual JPEG sizes vary.

At `CARD FULL` a Capture press still ends in `ERROR` (no space), as before.
A flash wear warning (below) replaces the count until the next press.

## Settings: press Menu

Press Menu at `READY` (a plain short press; no hold needed). The
screen shows the settings list, one setting at a time:

```
SETTINGS 1/6      OUTPUT 2/6      SETTINGS 3/6
MODE: SINGLE      RAW+JPEG        SAVE: FLASH

SETTINGS 4/6      SETTINGS 5/6      SETTINGS 6/6
WIFI: HOME        DELAY: OFF        SOUNDS: ON
```

| Button | In the list | Inside a setting |
|---|---|---|
| Menu (short press) | next setting | next option |
| Capture | open the setting | select the option (stored at once) |
| Power (short press) | leave the settings | leave the settings (the option shown is not selected) |
| Power (hold) | turns the camera off | turns the camera off |

Inside a setting the screen shows its name and the option between dashes,
for example `SAVE TO` / `- CARD -`.

| Setting | Options | What it does |
|---|---|---|
| `MODE` | `SINGLE` (default), `HDR` | What a Capture click takes. In single mode a one-second hold still takes a sweep. |
| `OUTPUT` | `RAW`, `JPEG`, `RAW+JPEG` (default) | Applies to single photos and every HDR sweep frame. RAW saves only the DNG; JPEG saves only the processed JPEG; RAW+JPEG saves both from the same exposure with matching filename stems. JPEG-only skips the RAW copy; RAW-only skips JPEG copies. The R icon appears for either RAW option. |
| `SAVE` | `FLASH` (default), `CARD` | Sweep frames go to the built-in flash first and are moved to the card afterwards, or straight to the card. With `OUTPUT: JPEG` the frames always go straight to the card. |
| `WIFI` | `OFF`, `AP`, `HOME` | Wi-Fi off; the camera's own access point; or the network in the card's `wifi` file when it's in range, else the access point. `HOME` is offered only when that file is there. Not chosen yet: `HOME` with the file, else `AP`. |

| `DELAY` | `OFF` (default), `2 S`, `5 S`, `10 S` | Self-timer: after a Capture click (or the sweep hold) the screen counts down (`CAPTURE IN` / `SWEEP IN`, then the seconds left) and the capture starts at 0. Sounds: with 5 S or 10 S, `timer.wav` plays on the click and on every second after it, until the last two seconds, which play `timer_last2sec.wav` (with 2 S that is the whole countdown, from the click); a cancel stops the sound. A Capture, Menu or short Power press during the countdown cancels it; a Power hold still turns the camera off. While a delay is set, the status corner shows a watch face at 3 o'clock left of the mode icon. Maintenance runs (`--once`, `--sweep-once`) ignore it. |
| `SOUNDS` | `ON` (default), `OFF` | `OFF`: the app plays no sound at all: no shutter, sweep start/end or sweep-done sound, no self-timer ticks, no power-off sound, no battery, charger or Wi-Fi alert sounds (those are dropped, not queued for later). The self-timer and everything else work as before. |

**Wi-Fi is applied in the background** ([wifi.c](wifi.c)): at start and each
time a `WIFI` option is selected (even the stored one, so `HOME` retries the
network), the app starts `wifi-join.sh apply off|ap|home` in its own session
with none of the app's descriptors, and doesn't wait. The status corner
shows the change. The app log gets `APP WIFI start|chosen: apply … started`.
When the camera joins a home network, its IPv4 address appears across two
lines for four seconds once the app is idle. It also appears when the app
starts already connected, and again after a reconnection. Camera activity or
opening settings dismisses it.
Details: [g360wifi](../wifi/README.md#g360-apps-wifi-setting).
Maintenance runs (`--once`, `--sweep-once`) don't touch Wi-Fi.

The ready screen shows the remaining capture count; the mode and R icons
show whether it counts sweeps and whether output includes RAW. The settings
are kept on the card in
`state/settings.0` and `state/settings.1`
([settings.c](settings.c)): two 20-byte checksummed slots written in place,
like the counter, so a torn write falls back to the other slot and two bad
slots fall back to the defaults. The Wi-Fi choice is byte 15, which was always
0 before: older slots read as "not chosen". The delay shares that byte: Wi-Fi
in the low two bits, the delay's index (0 off, 1 2 s, 2 5 s, 3 10 s) in
bits 4-5. Bit 6 means sounds OFF. Output uses bit 2 for RAW-only, bit 7 for
JPEG-only, and both clear for RAW+JPEG (both set is invalid). Existing
RAW ON slots migrate to RAW+JPEG; RAW OFF slots migrate to JPEG. Older
builds reject RAW-only slots and can fall back to the other slot or defaults.
The app log gets
`APP SETTINGS loaded|default|saved: …` lines. Settings can be opened only
at `READY`; a Menu press during a capture or sweep does nothing.

All capture API operations run on one worker thread, with one session kept
open. The UI thread continues reading buttons while capture and saving block.
Output uses owned buffers after capture; a later batch API can add a bounded
output queue without moving IO into the RAW callback. This version accepts
one shot at a time and stays busy until saving completes. On capture/storage/
sound/display failure it reports `ERROR` and does not retry an uncertain shot.

**Memory is tight.** Of the camera's 378 MB of RAM, about 270 MB belongs to
the camera driver; programs get roughly 80 MB. The app holds a shot's RAW and
JPEG (about 42 MB) while it saves, so files go to the card in 2 MiB pieces,
each `fsync`ed before the next (sweep moves too). Written in one go, a DNG
waited in RAM for the card, and on 6 November the kernel killed the app
mid-save ([validation](VALIDATION.md)).

## The HDR sweep

Start it with a Capture click in HDR mode, or in single mode by holding
Capture: after a second the screen shows `HDR SWEEP`; release it. The app
plays `hdr_start.wav`, then takes 7 stills (each shot starts `07_N_AF.wav`,
which plays during the shot instead of holding it up) at fixed manual
exposures, all at ISO 100, covering the camera's whole usable shutter range:
familiar speeds 2 EV apart down from the fastest, with the last step to the
slowest (1/13 to 1/6) about 1.1 EV:

| Frame | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| Speed | 1/6 | 1/13 | 1/50 | 1/200 | 1/800 | 1/3200 | 1/13000 |
| TV | 41 | 59 | 90 | 122 | 154 | 186 | 218 |
| Step from previous | | 1.1 EV | 1.94 EV | 2 EV | 2 EV | 2 EV | 2 EV |
| Exif ExposureTime (real) | 1/6.0 | 1/12.9 | 1/49.4 | 1/197.6 | 1/792.7 | 1/3209 | 1/13,486 |
| ISO | 100 | 100 | 100 | 100 | 100 | 100 | 100 |

The exposure is 2^(-TV/16) s. Trials L1, F1, ENDS1 and ENDS2 (8 November,
`output/decompile/rea/pass-longexp-static-20261108/RESULT.md`) found the
ends: no still exposes longer than about 166 ms, so TV 41 (1/5.9 s
requested) is really 1/6 s; TV 218 is at the shortest real exposure, and the
fastest frames come out about 5 us short (0.09 EV at 1/12,634). Trial
RANGE14 (9 November) confirmed over the whole range that the RAW follows
these times (0.013 EV RMS), so since 9 November the Exif ExposureTime is the
real time (`g360_exif_exposure_from_tv`: capped at 166 ms, 5 us short), not
the firmware's third-stop name (1/6, 1/20, 1/80, 1/250, 1/1000, 1/3200,
1/13000), which was up to 0.16 EV off. Until 8
November 2026 the sweep was 6 or 8 frames from 1/8 to 1/8192 s, the 8-shot
and the old 6-shot sets starting at ISO 1600 and 400; old stored settings
still load.

The screen counts `SWEEP 1/7` … `SWEEP 7/7`. As soon as the last frame has been
captured (it is in memory; before it is written out), `hdr_end.wav` plays:
**the camera can move from then on.** With `SAVE:
FLASH` the app then shows `SAVING 1/7` … `SAVING 7/7` while it moves the
frames to the card, and returns to `READY`. When everything is done (moved to the
card, flash life read), `shutter_continous.wav` plays.

**Flash first or straight to the card** (setting `SAVE`). Saving a 33.7 MB
DNG to the card takes 2.1 s; the camera's own flash (`/opt/usr`, ext4,
about 1.9 GB free) takes 0.77 s. Measured on the camera (boot 1793857325):

| Sweep | Camera held still | Last frame saved | Moved to the card | All on the card |
|---|---:|---:|---:|---:|
| 6 shots, `FLASH` | 12.5 s | 13.3 s | 14.8 s | ~28 s |
| 6 shots, `CARD` | 19.3 s | 21.5 s | — | 21.5 s |

These were measured with the earlier 6-shot set; a 7-frame sweep should
take about one frame longer (roughly 2 s held still with `FLASH`, 3 s with
`CARD`). Not yet measured.

"Held still" runs from the first capture's start to the last capture's end.
`FLASH` frees the camera sooner; `CARD` finishes the whole job sooner and
doesn't wear the built-in flash. These timings are for RAW-only sweeps; RAW+JPEG adds JPEG copies and writes. Earlier measurements:
output/speed-20261105/RESULT.md (not published: `../../output/speed-20261105/RESULT.md`).

**What each frame is.** The selected `OUTPUT`: DNG only (`RAW`), JPEG only
(`JPEG`), or a same-stem DNG and JPEG pair (`RAW+JPEG`). Names use the sweep's
start time and one counter number per exposure, like
`photos/YYYYMMDD-HHMMSS-NNNNN.dng` and `.jpg`. JPEG and DNG in a pair come
from the same exposure and carry the same Exif and pose XMP.
Each DNG carries the frame's own Exif: the real exposure time (above),
the ISO as the camera recorded it, exposure program, white-balance mode,
f/2.2, 1.2 mm, the sweep's start time and the description
`HDR SWEEP YYYYMMDD-HHMMSS FRAME k/n`, plus the camera's IQ MakerNote (see
below). Each sweep also gets `photos/YYYYMMDD-HHMMSS-sweep.csv`:

```
file,frame,tv,iso,exposure_time,actual_iso
20261105-192750-00001.dng,1,48,1600,1/8,1600
...
```

`tv` and `iso` are what was asked for (TV is 16 × log2(1/s));
`exposure_time` is the Exif's real time as a fraction of seconds (for
example `33199/200000`, 165.995 ms) and `actual_iso` comes from the still's
own record. Before 9 November `exposure_time` was the firmware's
third-stop name. Automatic exposure is set back after the sweep. With
`SAVE: CARD` each frame's names are reserved, its selected files written
and their list rows appended before the next capture.

**With `OUTPUT: JPEG`** each frame is the camera's JPEG instead, with Exif
(the real exposure time, ISO as the camera recorded it, the sweep's start time)
and the IQ MakerNote, named like any photo (`photos/…-NNNNN.jpg`). The frames
go straight to the card whatever `SAVE` says (a JPEG of 1–5 MB takes
0.1–0.3 s there; RAW+JPEG pairs use the flash queue together), and the list names the `.jpg`
files. The JPEGs carry no `HDR SWEEP …` description; the list says which
frame each one is. Measured on the camera (7 November, 6 shots, night
indoors): held still 6.0 s, last frame saved 6.2 s, JPEGs 1.3–4.9 MB. The
same sweep with RAW-only takes 12.5 s (`FLASH`) or 19–21 s (`CARD`).

**The queue** ([queue.c](queue.c)) is `/opt/usr/g360-app/queue`:
`STAMP-K.dng` for frame K, plus `STAMP-K.jpg` when output includes JPEG,
and `STAMP.csv` with the exposures: each frame's
line is appended and fsynced as soon as it is captured, before its selected outputs are
written, so a crash keeps the exposure of every captured frame (on 7
November a sweep cut off in frame 6 lost them all). Each frame is fsynced
before the next capture. Moving a
frame: reserve one card stem for its selected files, copy and fsync every
file, append each output's exposure-list row (fsynced), then delete the flash
sources. A crash can duplicate a frame on the card but cannot lose its only
complete copy. A DNG of the wrong size or JPEG missing its SOI/EOI markers is
an interrupted write: it is dropped and logged; a complete partner still
moves. Old DNG-only queues remain recoverable. Both output files appear as
separate rows with the same frame number in the sweep CSV.

**Stops and failures.** A Power hold or SIGTERM during a sweep finishes the
frame in progress and skips the rest. Frames already taken stay on the
flash, and so does a failed sweep's. At every start the app first moves
whatever is queued (`SAVING`), then opens the capture session. If
`/opt/usr` cannot be used or lacks room for the selected sweep plus a 64 MiB
margin (including a full JPEG allowance per frame for RAW+JPEG), the app logs
`APP FLASH QUEUE unavailable` and sweeps go straight to the card, whatever
`SAVE` says.

**Its log.** `state/app.log` gets one line per frame with the camera's
exposure and the time since the first capture, then
`APP SWEEP CAPTURED … held still N ms` (first capture's start to last
capture's end), `APP SWEEP SAVED … moved to the card in N ms` and the
flash's wear estimate (below).

**Wear.** A RAW-only sweep with `SAVE: FLASH` writes 236 MB to the camera's
own flash; RAW+JPEG adds the JPEG sizes.
The flash cannot be
replaced (it is soldered to the board and also holds the camera's system).
Its life estimate read 1 of 10 (0–10% used) on 5 November. The chip's
endurance isn't published; at a conservative 1,000 erase cycles it is
about 30,000 sweeps.

**The wear check** ([wear.c](wear.c)). After every sweep the app reads the
chip's own estimate: the eMMC register EXT_CSD, which the kernel shows at
`/sys/kernel/debug/mmc0/mmc0:0001/ext_csd` (reading it is one read command
to the chip). It logs `APP FLASH LIFE A a B b pre-EOL p`. Life values come
in 10% steps: 1 is 0–10% used, 4 is 30–40%, 10 is 90–100%, 11 is past the
estimated life. **From 4 on** (either value), the screen shows
`FLASH WEAR 4` … `FLASH WEAR 9`, or `FLASH WORN` for 10 and 11, instead of
`READY`, until the next Capture press. It only warns: stills and sweeps
still work. If the register can't be read, the app logs
`APP FLASH LIFE unreadable` and shows `READY`.

## What a DNG carries (since 6 November)

Single shots and sweep frames are written with `g360_save_dng_full`
([g360cap](../lib/g360cap/README.md)), matching the Python
reference `make_dng.build_full` byte for byte:
- **Exif:** exposure time, ISO, exposure program and white-balance mode from
  the still's own record, except that a set shutter (sweep frames, remote
  Shutter) writes the real time from its TV instead of the firmware's
  third-stop name (`g360_exif_exposure_from_tv`, trial RANGE14); automatic
  exposure keeps the firmware's value; date and time (the name's timestamp); f/2.2,
  1.2 mm (6 mm full-frame equivalent); orientation; lens info.
- **Description** (sweeps): `HDR SWEEP YYYYMMDD-HHMMSS FRAME k/n`.
- **The camera's IQ MakerNote** (Samsung Type 2: AE, AWB twice, colour),
  as in our JPEGs: white points, per-lens calibration, tone curve.
- **Colour:** AsShotNeutral from the shot's AWB gains, ForwardMatrix1 and
  ColorMatrix1 from the shot's colour matrix
  ([dng/README.md](../../reverse-engineering/dng/README.md)).

LibRaw (and so rawtoaces, darktable and others) reads the exposure, ISO,
F-number, focal length and date. The header is 40,960 bytes, so a DNG is
33,773,568 bytes.

## The camera's pose in every photo (T19, on the camera since 7 November)

Around each capture the app reads the kernel's motion history
([motion.c](motion.c): 8 MB of RAM at physical 0x9cd00000 mapped through
`/dev/mem`, write position by ioctl on `/dev/accel`; layout and axes in
`target.motion-history-sample-layout-and-axes-20261106`). The window runs
from the capture call's start to its return and is at least 0.25 s (50
samples), at most 5 s; reads stay 50 ms behind the write position and records
whose timestamps are out of step (last lap's) are skipped. The mean
accelerometer gives the direction of "up" in the camera's frame
([g360_pose.c](../lib/g360cap/g360_pose.c)). It goes, as one XMP packet, into
the JPEG (a second APP1 after the Exif one) and the DNG (TIFF tag 700):
- `GPano:PosePitchDegrees` and `GPano:PoseRollDegrees`, for any tool: pitch
  of the front lens axis above the horizon, roll clockwise as seen looking out
  of that lens. No heading (no magnetometer) and no `ProjectionType`: the
  image is two fisheyes, not an equirectangular.
- `g360:UpX/UpY/UpZ`: the calibrated accelerometer in g, +1 on the axis
  pointing up (an upright camera reads 0, 0, -1). Axes: +X away from the
  buttons, +Y away from the front lens, +Z down out of the grip. The
  stitcher rotates the sphere until this vector points down; it stays
  defined with a lens straight up or down, where roll is not.
- `g360:RawAccelX/Y/Z` (counts), `g360:Samples`, `g360:AccelStdG`,
  `g360:GyroRmsDps` (is the camera still?) and `g360:Calibration`
  (`driver-calib_xyz`: the history already has the driver's `calib_xyz`
  subtracted and the app adds nothing, only 8197 counts per g). Hand-held
  face readings still left about +50 (Y) and +240 (Z) counts, up to 1.7°,
  uncorrected; a later calibration or sign fix can redo old photos from the
  raw means.
- Namespace `http://gear360-modding.invalid/ns/pose/1.0/`, prefix `g360`.

Checked on the camera (7 November, stock stopped, 170-190 samples a shot):
upright on a table pitch -1.4 roll -0.9; buttons side tipped up about 30°
roll +31.8 pitch -3.8; screen-side lens tipped up about 30° pitch +32.9 roll
-0.9. The up vector's length was 0.98 g at rest (scale within 2 %).

If the history can't be read, the shot is saved without XMP and the log says
`APP POSE none: ...`; otherwise `APP POSE n samples pitch .. roll .. up ..
gyro .. dps`. Tests: `--motion-history FILE --motion-position FILE` point the
reader at plain files.

## Photos and counter

Production root: `/opt/storage/sdcard/G360` (the same SD card as `/sdcard`).

```
G360/
  photos/20261105-123456-00001.jpg
  photos/20261105-123456-00001.dng
  state/counter.0
  state/counter.1
  state/settings.0
  state/settings.1
  state/storage.lock
  state/app.log
  state/capture.log
```

Sweep frames (the selected DNG, JPEG or pair) and each sweep's `-sweep.csv` go into `photos/` too.

The timestamp uses the camera's local wall clock at filename reservation;
the JPEG's date fields use that same time. It is not a host-time correction.
The global sequence starts at00001, persists across app launches/reboots and
clock changes, and is shared by both files. It stops at99999 instead of
wrapping. Two versioned/checksummed counter slots are updated in place and
fsynced before use; startup scans photo stems once to recover stale/missing
state. Output files are reserved with `O_EXCL` before capture, so existing
photos are never overwritten. Interrupted/failed shots can leave gaps and
empty reserved files. Two corrupt counter slots fail closed.

The launcher reads card space at startup; the app conservatively budgets
successful writes and retains128MiB plus a maximum-shot allowance before
accepting another shot. It does not assume target statvfs structure layouts.
A full card or IO error produces `ERROR`; concurrent external card writes are
ultimately checked by actual write/fsync results.

## Starting from a normal boot

Duplicate root `mod.sh` entries once made an older hook run at boot (7 November
2026). They were cleaned up that evening; the readiness cue below still needs
its attended boot check, see [validation](VALIDATION.md).

The intended SD boot hook starts the passive `gear360-entry`
listener outside the stock service's cgroup. Once the listener has opened its
input device and is listening, the boot hook plays `BT_connected.wav` as a
readiness cue (independent of the app's `SOUNDS` setting). If audio is not
ready it retries for up to a minute without blocking the listener; it stops
trying if our app has already started. Double-Power calls
`/sdcard/mods/hdr-ui-v2/start-hdr.sh`. The deployment changes that SD launcher
into a small trampoline to this app's `start.sh`, preserving the previous
launcher as `start-hdr.sh.g360-before-20261105`. The SD boot and network scripts
are left intact. Code is installed in `/opt/storage/sdcard/mods/g360-app-v1-20261105`.

`start.sh` pins the executable/camera-library hashes, checks free space and
competing clients, and holds the existing controls/screen/experiment locks.
It leaves the stock service cgroup before requesting
`systemctl --no-block stop di-camera-app.service`, only when stock is active.
On a double press it attempts to show `LOADING...` while stock is still
running; stock can redraw over it. It redraws the message once stock is
inactive and its process is absent, then launches our app. The first draw
while stock is running has not yet been verified on the camera. If stock
is already stopped, it reenters on the same boot. Wi-Fi is the app's own
`WIFI` setting (below); the launcher doesn't touch it. It never starts/restarts
stock or power-cycles the camera. The existing listener suppresses another
launch while our controls lock is held.

Double-Power changes from entering the older HDR UI to entering this app.
For an explicit launch, run `sh /opt/storage/sdcard/mods/g360-app-v1-20261105/start.sh`.
To stop remotely, send SIGTERM to the verified g360-app PID; an in-flight
capture/save finishes before exit. Stock stays stopped. The maintenance
binary options `--once` (one automatic shot) and `--sweep-once` (one sweep)
run and exit. Legacy `--raw on|off` and remote `raw=on|off` remain aliases
for RAW+JPEG/JPEG. `--sweep-save flash|card`, `--output raw|jpeg|raw+jpeg`
and `--sounds on|off` override the stored settings for that run only. Normal
launcher operation never passes them. `--store-settings` (with any of those
overrides) writes them to the stored settings and exits without touching the
camera, screen or Wi-Fi; it is refused while the app runs (it holds
`state/storage.lock`). For example, to mute the app before unattended work:
stop it, run `g360-app --store-settings --sounds off`, start it again. The launcher creates `/opt/usr/g360-app` and passes `--queue
/opt/usr/g360-app/queue --queue-free-bytes N` (from `df`); without them the
app still runs, and sweeps go straight to the card.
[tools/sweep-trial.py](tools/sweep-trial.py) runs one unattended
`--sweep-once` (or, with `--still`, `--once`) against trial folders and checks
the files (`--save`, `--output`, `--sounds`; pass `--sounds off` when
nobody should hear the camera).

## Remote control: the web page

`http://<camera>:8888/remote.html` (on the access point
`http://192.168.43.1:8888/remote.html`; the old `/hdr.html` redirects there)
is a remote for the running app. It shows what the camera's screen shows
(the same text, plus the status corner as words), the state, and the
settings, and it can do what the buttons do:

- **Capture** is a Capture click: with `DELAY` set the camera counts down
  and ticks, then shows `CAPTURING...` and plays the same sounds. In single
  mode **HDR sweep** is the one-second hold; in HDR mode Capture is the
  sweep. **Cancel countdown** stops a countdown, like a button press during
  one.
- **Settings** are five of the Menu's six. A choice is stored on the card
  at once and the camera shows it (status corner, ready count). Wi-Fi is
  shown but changed only on the camera: a change would cut the page off
  from it (the remote refuses `wifi=` as a bad request).
- **Shutter and ISO** (dropdowns, this page only: nothing on the camera's
  screen or Menu, nothing stored on the card) are for single photos.
  Shutter is Auto or one of the 34 familiar third-stop speeds over the
  camera's whole range, 1/6, 1/8, 1/10 … 1/8000, 1/10000, 1/13000 s, named as
  the firmware names them and sent as the TV nearest each name (the photo's
  Exif has the real time, within 2% of the name, 0.09 EV at 1/13000)
  (`G360_SHUTTER_TVS` in `remote.h`, 41-218; `cmd=set&shutter=auto|TV`, other
  numbers are refused; the state's `exposure.shutter` is the TV, 0 for Auto);
  Auto shutter means Auto for both: the ISO dropdown is then disabled. With a
  set shutter the ISO is 100/200/400/800/1600 (`iso=`; refused as `set a
  shutter first` otherwise), starting at 100, the September-verified value,
  and kept when another shutter is chosen. Manual exposure with the ISO is
  `g360_set_manual_exposure`, as the sweeps use it. The worker applies the
  choice just before each still and puts the automatic exposure back right
  after it. A sweep (the **HDR sweep** button or HDR mode, also chosen on the
  camera) puts both back to Auto; going back to single keeps Auto. They are
  refused in HDR mode (`single mode only`), and a restart of the app means
  Auto.
- **Auto photos** press Capture every N seconds (1 to 86400), inside the
  app, so they continue with the page closed, until **Stop**, a short
  Power press on the camera, a full card or the app's end. Between shots
  the screen counts down to the next press, `AUTO IN` / `7` (`4:59` from a
  minute, `1:00:00` from an hour) instead of the count left; with a delay
  set, `CAPTURE IN` then counts the rest. With `DELAY: OFF`,
  `timer_last2sec.wav` plays 2 s before each capture (intervals over 2 s;
  not with `SOUNDS: OFF`; Power stops it). The self-timer counts **inside** the
  interval: every 10 s with `DELAY: 5 S`, each press comes 5 s after the
  previous capture started and the countdown fills the other 5 s, so a
  capture starts every 10 s. When the delay plus the capture takes longer
  than the interval, the next press comes as soon as the camera is ready (a
  delay longer than the interval wins). The first press is at once. Auto
  presses wait while the settings are open on the camera; a Capture click
  meanwhile is an extra photo; a Capture or Menu press during an auto
  countdown cancels that one photo, Power stops auto photos.

Requests are refused (and the page says why) while the camera is busy,
counting down, has its settings open, or (a capture) when the card is full:
a remote or auto press never ends in `ERROR` for a full card.

**How it works** ([remote.h](remote.h)). The app listens on the Unix socket
`/tmp/g360-app.sock` (RAM, not the card). The page polls
`/cgi-bin/g360?cmd=state` every 0.7 s and sends commands the same way
(`cmd=set&output=jpeg` for mode, output, save, delay and sounds;
use `output=raw%2Bjpeg` for both outputs; `cmd=capture`, `cmd=capture&sweep=1`, `cmd=cancel`,
`cmd=auto&every=10`, `cmd=auto&every=0`). The CGI is `g360-remote`
([remote_cgi.c](remote_cgi.c), 9 KB), a separate program: `start.sh`
refuses to launch while any process is named `g360-app`, and
`install-app.sh` stops every one. It sends the query and prints the app's
one-line JSON reply, or `{"app":"off"}` when no app answers. In the app a
thread accepts one connection at a time (1 s to send its line) and hands
the request to the UI thread, which answers it within one 50 ms loop with
the same code the buttons use (`press_capture`, `choose_setting`), so the
remote can't do anything the buttons can't, and the settings live in one
place. A request takes about 40 ms on the camera. The log gets
`APP REMOTE <query>` for each command that acts, `APP AUTO …` lines and
`APP DELAY cancelled (remote)`. Maintenance runs don't listen.

**Live preview** ([preview.h](preview.h), T20). The page's Live preview
card shows both lenses as the camera sees them, about 8 pictures a second at
640×320. **Start** sends `cmd=preview&on=1`. The capture worker (the only
thread that calls the capture library) then starts the camera's movie
preview (`g360_preview_start`, 1280×640 YUV frames at about 30 a second).
A frame is copied, half size, only when the encoder wants one. The encoder
thread turns it into a JPEG with the camera's `/usr/lib/libturbojpeg.so`
(about 51 ms a picture), and only while someone watches. The app serves the
JPEGs as multipart MJPEG on its own TCP port 8090
(`http://<camera>:8090/preview.mjpg`, at most 4 viewers, all sharing the
same pictures). The page shows them through its CGI (`cgi-bin/g360?cmd=stream`
relays that port), so it needs only the camera's one address.

Before every still or sweep the worker stops the camera's preview; the
library refuses a capture while one runs. It starts again when the app is
READY, and the picture resumes on the same connection. The page shows
"Paused for the capture" meanwhile. The preview keeps running during a
self-timer countdown and with the settings open. **Stop**
(`cmd=preview&on=0`) ends it and closes the streams. It also stops by itself
20 s after the last viewer leaves (or 20 s after Start when nobody
connects). A camera start that fails turns the preview off, and the state
gives the reason (`"preview":{"state":"off|starting|on|paused",…,"error":…}`).
The log gets `APP PREVIEW …` lines.

Deploy the page, the CGI and the redirect with
`sh src/app/web/deploy.sh` (after `build-arm.sh`; `GEAR360_CGI_BASE` for
the home network). Anyone on the camera's network can use the page, as with
the existing `/cgi-bin/execute` shell.

## Power hold: turning the camera off

Holding Power for about a second makes the key driver send code 116
(`KEY_POWER`, g360ctl's `POWER_HOLD`). On that press, in any state, the app:

1. stops taking presses and shows `POWER OFF`;
2. lets the worker finish a shot already in progress (it is saved as
   usual), then close the capture session and storage;
3. plays the stock `powerOFF.wav`;
4. copies its own executable to `/tmp/g360-app-poweroff` and becomes that
   copy, run as `--power-off` ([power_relaunch.c](power_relaunch.c)). The
   copy sends its output to `/tmp/g360-app-poweroff.log`, closes every other
   file and moves to `/`, so nothing of it is on the card;
5. runs the stock app's own power-off steps ([power.c](power.c)):
   `sync`, then `device_set_property(4, 0x15, 1)` (writes 1 to
   `/sys/devices/platform/d5-keys/shutdown`), deviced's D-Bus `PowerOff`
   (deviced syncs and unmounts the card, turns USB off), and
   `device_set_property(4, 4, 0)` (writes 2 to the same file). In the
   kernel, 2 calls the board's power-off function, which switches the
   power chip off; 1 only stores the value.

Step 4 is needed because deviced unmounts the card in step 5. If the card
is busy, deviced runs `fuser -m -k` on it: SIGTERM, then SIGKILL 5 s later,
to every process with anything on the card. That includes a process whose
executable is on the card, as ours is. In the stock app this doesn't arise:
it runs from the root filesystem.

The steps and their order come from the stock app's
`CAPPPowerOffMode::powerOffSystemShutdown`, traced with REA/Ghidra; the
evidence is in [VALIDATION.md](VALIDATION.md). `libdevice-node` and
`libdeviced` are opened with `dlopen` only at that point. If the copy can't
be made or started, or the first step fails, nothing else runs and the app
shows `ERROR`. If deviced refuses (it
accepts `PowerOff` only after "booting done", once per boot), the files are
still synced and the last step still runs. If the camera is still on 10 s
after the last step, the app shows `ERROR` and exits.

The stock app also sends the RTOS a PowerSave request (capture-library
ioctl 40) just before these steps. Our app doesn't, for two reasons:
- **The power cut comes from Linux.** It is made by the key driver's
  `shutdown` file, not by the RTOS.
- **It has probably already happened.** The launcher's
  `systemctl stop` most likely sent the stock app down that same
  shutdown path, so the RTOS would answer a second request as a duplicate.
  That is a working hypothesis, not proven.

The stock LED animation and its 1.5 s pause are also left out.

## The camera's open() flags

`storage_platform.h` carries the camera's own flag numbers, not the generic
Linux ones. The camera's `O_DIRECTORY` is `040000` and its `O_NOFOLLOW` is
`0100000`; the generic numbers put `O_DIRECTORY` at `0200000`, which on this
camera is `O_DIRECT`. The card (FAT32, served by the kernel's exfat driver)
rejects that bit, so a
directory opened with that bit fails with `EINVAL`. The numbers were measured
on the camera with [tools/open_flags_probe.c](tools/open_flags_probe.c); the
fact record is
`target.open-flag-numbers-are-not-the-generic-ones-20261105`.

## The side LED shows the charge

The side LEDs follow the same battery reading as the screen: **green**
(`led.1`) when the battery is `Full` with a cable in, **yellow** while it is
`Charging`, **off** otherwise. There is no yellow LED, so yellow is the side
orange one (`led.0`) alone. `G360_YELLOW_GREEN` (0-255, default 0) mixes some
green into it to warm it toward yellow; no mix has been judged by eye. The LEDs
are written only when the state changes (`APP LED side green (full)`,
`yellow (charging)`, `off` in the app log) and are switched off when the app
ends. Blue is not touched. The shot flash puts back only the lens LEDs it
changed, so it can't undo the charge light.

## Installing a build on the camera

```
sh src/app/tools/install-app.sh --backup NAME-YYYYMMDD --restart output/camera-app-build/g360-app
```

`start.sh` starts `g360-app` only when its SHA-256 matches `g360-app.sha256`,
so the two must never be out of step. [tools/install-app.sh](tools/install-app.sh):

1. uploads the build into camera RAM over FTP, checks it, copies it to the
   card as `g360-app.new` and checks it again; writes `g360-app.sha256.new`;
2. with `--backup`, copies the current pair to `g360-app.g360-before-NAME…`
   and `g360-app.sha256.g360-before-NAME…` (refused if they exist);
3. with `--restart`, stops the running app with SIGTERM (an in-flight shot or
   sweep finishes first). Without it, a running app is refused: the file it
   runs from is never deleted under it;
4. replaces both files in one camera command (delete, then rename), and
   checks both;
5. with `--restart`, runs `start.sh` (the Double-Power path) and waits for
   `g360-app` to appear.

A cut-off upload (Wi-Fi drop, reboot, a stopped script) leaves the old app
working. On 6 November an upload written straight over `g360-app` was cut
at 11 KB of 98 KB by a reboot; the launcher refused the file and Double-Power
did nothing. Its refusals go to the listener, not `app.log`.

Other files (`start.sh`, scripts) go through
[tools/deploy-over-web.sh](tools/deploy-over-web.sh) the same way: RAM, check,
`NAME.new`, check, delete and rename, check. It refuses `g360-app`. Both
default to the access point address; set `GEAR360_CGI_BASE` for the home
network, e.g. `GEAR360_CGI_BASE=http://<home-network-ip>:8888/cgi-bin`, or for
another Wi-Fi network (last seen: `http://<hotspot-ip>:8888/cgi-bin`; see
AGENTS.md "Reaching the camera" for how to find it).

**Speed.** The bytes go over FTP (port 21, the BusyBox `ftpd` the SD boot
hook starts, user `root`); the checks and the swap go through the HTTP shell.
A 98 KB file deploys in about 2 s. If FTP doesn't answer, the bytes go
through the HTTP shell in 48-byte requests instead (it drops longer
commands): about 2.7 s per KB, 4.5 minutes for the app. The script says
which path it took.

## Build and checks

```
sh src/app/build-arm.sh
python3 src/app/tests/test_target_flags.py
python3 src/app/tests/test_storage.py
python3 src/app/tests/test_queue.py
python3 src/app/tests/test_wear.py
python3 src/app/tests/test_settings.py
python3 src/app/tests/test_status.py
python3 src/app/tests/test_alerts.py
python3 src/app/tests/test_remote.py
sh src/lib/g360display/tests/run.sh
python3 src/app/tests/test_launcher.py
python3 src/app/tests/test_app.py
python3 src/app/tests/test_power.py
python3 src/app/tests/test_deploy.py
```

The app is C11 (`-std=c11`, on the camera and in the host tests). The values
its threads share are `<stdatomic.h>` atomics: the compiler's own header, so
the camera's missing C headers don't matter, and a `_Static_assert` requires
them to be lock-free (the signal handler writes one). Other `_Static_assert`s
check the glibc struct layouts that `storage_platform.h` declares by hand.

`test_target_flags.py` compares the camera's flag numbers in
`storage_platform.h` with the measured table. The host build uses the host's
own headers, so without that check the wrong numbers would only show up on
the camera.

The new display library uses the previously validated full-screen72×32
OLED draw/update ABI; it performs no display init/power/contrast changes or
stock-process mirroring. Text is up to three centred lines of 12 characters
(a single line is drawn exactly as before), or two below the status corner. Capture, metadata and DNG modules retain their
existing component validation. Hardware/installation results: [VALIDATION.md](VALIDATION.md).
