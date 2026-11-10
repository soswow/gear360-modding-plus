# g360ctl — buttons, LEDs and sound without the stock app

Part of task T4 in TODO.md (not published: `../../../DONE-TODOS.md`)
(formerly next step 5 in `PROJECT_GOAL.md`): a limited replacement for the main app. The screen part already works
(screen trial (not published: `../../../output/screen-live-20261104/RESULT.md`)). This component
covers the other three controls with the stock app stopped.

## What is known (5 November 2026)

| Control | Interface | Known | Not known yet |
|---|---|---|---|
| Buttons | `/dev/input/event0`, "Drime5 GPIO Keyboard" (Linux evdev); Xorg and the September `gear360-entry` listener also have it open | Mapped in C2/C2e: Menu short 127 and Menu hold 215; Power short 73 and Power hold 116. Short presses arrive as a pulse at release. A hold sends only its own code, pressed after about 1.5 s (Menu) and released at let-go. Capture 126 has real press/release times, so holds and doubles are timed by the app. Two buttons arrive independently. No auto-repeat | Exact hold thresholds; who acts on 116 when the stock app runs |
| LEDs | Seven kernel LED-class devices, `/sys/class/leds/led.0`–`led.6`: `brightness` (max 255), `trigger` (none, timer, battery and charger triggers…), and `delay_on`/`delay_off` under the timer trigger | Mapped in C1b: 0 side orange, 1 side green, 2 side blue, 3 front green, 4 back green, 5 front orange, 6 back orange ("front" = screen side). The stock app calls `device_set_property`; the devman plugin exports `OEM_sys_set_led_0..7_brightness/_delay_on/_delay_off` (strings only, not yet traced in REA) | Whether the stock setters use the same nodes |
| Sound | ALSA card `rt5659`; PulseAudio (pid 193) and Samsung's `sound_server` (pid 198) run without the stock app; `paplay`, `aplay`, `amixer` are installed; the app's sounds are WAV files in `/usr/apps/com.samsung.di-camera-app/res/sounds/camera/` | The stock app uses `mm_sound_nx_play_sound` (libmmfsound → sound_server) | Whether `paplay` or `aplay` plays with the stock app absent; volume; latency |

Sources: the unattended run's platform inventory
(`output/experiments-live-20261104/segment1-platform.txt`), its process
listings, the firmware dump, and the September HDR integration
(archive/hdr-integration (not published: `../../../archive/hdr-integration/README.md`)).

## Plan

1. **Library and tool (offline, done here).** `g360ctl.h` / `g360ctl.c` and
   the `g360-ctl` CLI:
   - **buttons:** open read-only and non-blocking, without a grab; decode
     the 16-byte ARM32 `input_event`; name keys 73/126/127; report presses,
     releases, repeats and kernel-dropped events;
   - **LEDs:** read every `led.N` (brightness, max, active trigger, delays);
     set one (trigger `none`, then brightness); blink one (trigger `timer`,
     delays, brightness); save all to a file and restore them;
   - **sound:** run a player program (`paplay` or `aplay`) on a WAV file and
     wait for its exit status.

   Host tests run against a fake sysfs tree, a recorded event file and a fake
   player. ARM build as for the other camera tools.
2. **Trial C1 (attended: needs eyes, ears and hands).** Stock stopped
   gracefully (never restarted), then:
   - each LED alone, 3 s each, with the OLED naming it, so the user can say
     which LED is which;
   - one LED blinking;
   - all LED states restored from the saved file;
   - two stock sounds with `paplay` (and `aplay` if that fails);
   - 45 s of passive button recording while the user presses Power once,
     Capture and Menu, holds Capture and Menu about 2 s, and double-clicks
     Capture.

   No double-Power press: the September SD-card listener may still start
   its HDR mode on that gesture. No Power hold: the system may shut down.
3. **After C1:**
   - name the LEDs and record the button timing;
   - trace the devman LED setters in REA to confirm they use the same nodes;
   - add gestures (click, double, hold) on the recorded timings;
   - trial C2: controls during a RAW capture, checking that capture
     completion, frame flags and give-backs are unchanged.

Out of scope here: the app itself, persistent installation, input grabbing
(only needed if another consumer must be kept from seeing keys), the
battery controller (next step 6).

## Rules for the camera

- Stop the stock app only with `systemctl --no-block stop` and wait for it
  to be inactive; never start or restart it. Work continues in that state
  across processes on one boot; no power cycle is needed between sessions.
- LED writes go only to `trigger`, `brightness`, `delay_on` and `delay_off`
  under `/sys/class/leds/led.N`, after saving every LED's state; the trial
  restores them on exit, including on failure.
- Buttons are read passively: no `EVIOCGRAB`, no injected events.
- Sounds use the firmware's own WAV files and players at their default
  volume.

## Status (5 November 2026)

- **Library and CLI built** (`g360ctl.h`, `g360ctl.c`, `g360-ctl.c`).
- **Host tests pass**, covering a fake sysfs tree, recorded input events
  and fake players; five deliberate breakages were caught.
- **ARM build** `g360-ctl` sha256 `7eaf2c26…` (names the hold codes); it
  links only `libc`, `librt` and `libgcc_s` from the camera.
- **Trial C1 ran (5 November)** (result (not published: `../../../output/controls-C1/RESULT.md`)):
  - all seven LEDs were set, blinked and restored exactly;
  - both stock sounds played through `paplay` with the stock app stopped;
  - labels longer than 12 characters were refused by the display helper,
    so the button prompt never showed and no presses were recorded.

  Labels are now fixed and all checked first.
- **Trial C1b ran (5 November)** (result (not published: `../../../output/controls-C1b/RESULT.md`)):
  - the user mapped all seven LEDs; the library and CLI name them;
  - both sounds played again;
  - 16 button events were recorded, including the unseen codes 116 and 215,
    which are not yet matched to the presses.
- **Trials C2 and C2e ran (5 November)** (C2 (not published: `../../../output/controls-C2/RESULT.md`),
  C2b–C2e (not published: `../../../output/controls-C2e/RESULT.md`)): guided prompts mapped
  every button (see the table above). 215 is the Menu hold and 116 the
  Power hold. A 2 s Power hold is safe with the stock app stopped.

## Build and test

```sh
sh src/lib/g360ctl/tests/run.sh      # host tests
sh src/lib/g360ctl/build-arm.sh      # output/g360ctl-build/g360-ctl
sh tools/experiments/build-display.sh   # the OLED helper the trial uses
```

## Trial C1 (user present)

```sh
python3 archive/g360ctl-trials/run-trial.py --tag C1
```

- **Before:** nothing; no power cycle. The mode follows the camera's
  state:
  - stock app stopped (the usual state; same-boot reentry was validated
    with 44 processes) → `reenter`;
  - stock app running → `fresh`, which stops it once and never restarts it.
- **What it does:** checks and stages `g360-ctl`, the display helper
  (`14043a98…`, the screen-trial build) and
  controls-session.sh (not published: `../../../archive/g360ctl-trials/controls-session.sh`), then prints what to watch,
  listen for and press.
- **Records:** `output/controls-C1/` (ignored except Markdown):
  - `session.txt`: the timeline, sound statuses and the other processes
    reading the button device;
  - LED states before, saved and after;
  - `buttons.txt`.

**Pass:**
- every LED matched to a number;
- LED states after equal those before;
- whether each sound played and with which player;
- each pressed button recorded with its code, presses and releases;
- no reboot.

## Trial C2: guided buttons (user present)

```sh
python3 archive/g360ctl-trials/run-trial.py --tag C2 --session buttons
```

C1b recorded unseen codes 116 and 215, but the presses weren't noted in
order. buttons-session.sh (not published: `../../../archive/g360ctl-trials/buttons-session.sh`) shows one prompt at a time
(8 s each, "WAIT" between) while a passive recorder runs:
- MENU SHORT, MENU HOLD 2S;
- CAPT SHORT, CAPT HOLD 2S, CAPT DOUBLE;
- POWER SHORT.

No LED or sound changes. The runner lists each prompt with the events
recorded under it. The session logs the kernel's button interrupt counts
before and after, so a run with no events still shows whether the camera
registered presses.

`--prompts power` (trial C2e) asks for:
- PWR HOLD 2S, POWER SHORT, PWR HOLD 2S;
- MENU CAPT (both together);
- MENU HOLD 4S.

A 2 s Power hold is safe with the stock app stopped: the user reports that
only a 5–10 s hold then switches the camera off. With the stock app
running, 2 s does. Labels are at most 12 characters of A–Z, 0–9 and space
(the helper refuses "+"). Same no-power-cycle rule as C1: the mode follows
the stock app's state.

