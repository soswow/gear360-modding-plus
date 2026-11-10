# g360wifi: join an existing Wi-Fi network

The camera normally opens its own access point (`192.168.43.1`). This joins
an existing 2.4 GHz network instead ("station mode"), for one boot, and goes
back to the camera's own access point if anything fails. TODO task
T11 (not published: `../../DONE-TODOS.md`).

## What the camera already has

- **Firmware:** the Broadcom BCM43455 chip has station firmware
  (`/lib/firmware/wlan_net_bcm43455.bin_blob`) beside the access point one
  (`wlan_apsta_…`). `/usr/bin/wlan.sh start` loads it; the stock software only
  ever calls `softap` and `p2p` (its log, `/opt/var/log/wlanplug_log`).
- **Programs:** `wpa_supplicant`/`wpa_cli` 2.3 (no WPA3), toybox `udhcpc`, `wl`,
  `ip`, `gdbus`.
- **Samsung's own test:** the stock app has a factory test
  (`Service/NETWifiTest/CUINETWifiTestMan.cpp`) that joins an open network
  named `wifi3` with a fixed address `192.168.1.111` and runs iperf. Found by
  strings and imports only.
- **Radio region:** `wl country` reads `US`, so 2.4 GHz channels 1 to 11 only.
  The chip is dual-band (`wl cap`), but 5 GHz is untried.

## How it works

`wifi-join.sh` runs on the camera from `/opt/storage/sdcard/mods/g360wifi/`.
It reads `/opt/storage/sdcard/wifi` as ordered name/password pairs. Empty
lines and lines beginning with `#` are ignored; names and passwords are never
logged or printed. For example:

```text
# Preferred network
Network one
password one

# Fallback network
Network two
password two
```

The first network has priority. If joining it or obtaining an address fails,
the next pair is tried; after all fail, the access point comes back. A single
name without a password retains the old open-network format. Since empty
lines are ignored, open networks cannot be mixed into a list of pairs. A line
starting with `#` cannot be used as a name or password. An incomplete list is
rejected before changing the radio.

`join` does, with each step in `wifi.log` beside the script:

1. **Checks** everything first and changes nothing if a check fails.
2. **Pauses g360-app** (SIGTERM, as `src/app/README.md` says) and takes
   its three locks.
3. **Turns the access point off** through its agent:
   `org.tizen.tethering.disable_wifi_ap`.
4. **Loads the station firmware** the way `wlan.sh start` does (two sysfs
   paths, `ifconfig wlan0 up`).
5. **Starts its own `wpa_supplicant`** without D-Bus, so ConnMan never sees
   it (see below). The config is in RAM (`/tmp/g360-wifi`, mode 700) and is
   deleted as soon as `wpa_supplicant` has read it.
6. **Gets an address** with `udhcpc` (hostname `gear360`), and shows it.
7. **Waits up to 5 minutes for the Mac** to find the camera and call
   `wifi-join.sh host-ok`. Then it stays on the network and starts
   g360-app again.

**Any failure means the way back:** it stops its programs, brings `wlan0` down
and asks the agent for the access point again with `enable_wifi_ap`. That's
the same request as `hdr/boot/autostart-services.sh`, with the name and
channel from the agent's `hostapd.conf` and the password from
`mods/ap-passphrase`. Then it starts g360-app again.

**The agent deletes its `hostapd.conf` while the access point is off.** So
the script also keeps the name and channel in `ap-settings` beside itself;
they aren't secret. If neither is there, it uses the device name (vconf
`db/setting/device_name`), which is what the stock app names the access
point after, with channel 6.

**A watchdog** (a second process) does the same if a run dies part way or
takes longer than 15 minutes.

Nothing it starts holds the locks, and nothing long-lived keeps a file open
on the card.

| Command | What it does |
|---|---|
| `check` | Read-only preflight; prints lengths, never values |
| `scan` | Radio scan while the access point runs; prints only the first configured network |
| `status` | Read-only: access point up or down, `wpa_state`, address, last log lines |
| `ap-cycle` | Access point off, then on again: tests the way back alone |
| `join` | The steps above |
| `host-ok` | Tells a waiting `join` that the Mac has found the camera |
| `revert` | Leave the network and bring the access point back (stops the keeper) |
| `apply off\|ap\|home` | Started by g360-app for its `WIFI` setting; see below |

## g360-app's WIFI setting

g360-app has a `WIFI` setting (hold Menu; the fourth setting):

| Option | What it does |
|---|---|
| `OFF` | Leaves any network, turns the access point off and takes the radio down |
| `AP` | The camera's own access point (leaves the home network first) |
| `HOME` | The first usable network in the card's `wifi` file, else the access point. Offered only when the file is there |

Not chosen yet means `HOME` when the file is there, else `AP` (what the
launcher did before). `HOME` without the file is `AP`.

**The app starts `wifi-join.sh apply off|ap|home`** in the background: once
when it starts, and each time an option is selected, even the one already
stored. So selecting `HOME` again tries the network again after a fall back
to the access point. The script runs in its own session, with no descriptor
of the app's (locks, buttons, camera devices) and its working directory at
`/`; the app doesn't wait for it. It doesn't use the screen: the app has it,
and its [status corner](../app/README.md#the-status-corner-wi-fi-and-battery)
shows the change within 2 s. Everything goes to `wifi.log`.

**One run at a time.** `apply`, the keeper and the watchdog share
`$RUN/active`. An `apply` that has to wait (up to 3 minutes) goes ahead only if
it is still the newest choice; otherwise it logs `replaced by a newer
choice`.

- **`HOME`, the access point up (the usual case):** it scans the configured
  networks by name in order while the access point keeps running: about 5 s
  per scan, plus 5 s for one retry. A plain scan missed the network about one time in three on the
  camera; the named scan found it 5 times in 5. If none is found, the access point stays and nothing is interrupted.
  Otherwise the access point goes off and station firmware loads. Connection
  starts with the first network found by the scan, skipping earlier networks
  that were not found. If it fails, later networks are tried in file order
  without a pre-scan until one joins and obtains an address.
  If they all fail, the access point comes back. Already on a network: nothing
  changes.
- **A second `wpa_supplicant`.** A healthy join takes 1–2 s. If there's
  none after 12 s (`NOT FOUND` or `NO JOIN`), a fresh `wpa_supplicant`
  gets 30 s more. On 6 November one join stalled: the first scan after the
  station firmware loaded found 6 networks, then `wpa_supplicant` neither
  joined nor scanned again until the script gave up at 30 s; selecting
  `HOME` again joined in 1 s. A wrong password isn't tried again. The last
  attempt's debug log stays as `/tmp/g360-wifi/wpa.log.prev`.
- **`HOME` with Wi-Fi off:** it tries the network directly, and brings the
  access point up if that fails.
- **Early in boot** (a double-press right after the stock screen) the stock
  app's access point may not be up yet. `HOME`, `AP` and `OFF` all wait up to
  15 s for it first; otherwise it could come up after the change and undo it.
  They don't wait after `OFF` on the same boot (`$RUN/wifi-off`), or right
  after leaving a network.
- **After a join a keeper watches the link.** If the link is down for 2
  minutes (for example, the camera left home), it brings the access point
  back. The keeper runs from a copy in RAM and writes only the log.
- **A run that dies part way** (`HOME` or `AP`): its watchdog brings the
  access point back.

When this script itself starts the app again (after `join`, `ap-cycle` or
`revert`), it sets `G360_WIFI_SKIP_APPLY=1`, and the app then doesn't apply
its setting, so a `revert` isn't undone straight away.

Until 6 November (evening) `src/app/start.sh` ran the `HOME` steps as
`app-start`, before the app, with messages on the screen; that is gone.

## Why it can't lock you out for good

- **Nothing on the built-in flash changes:**
  - The network settings live in `/tmp`, which is RAM.
  - The chip has no flash of its own. Its firmware is loaded into its RAM at
    every `ifconfig up`, so a reboot loads the access point firmware again.
  - The boot scripts on the card are not touched.
  - The only flash writes are the ones every access point on/off already
    makes: the agent rewrites its `hostapd.conf`, and the driver log gets a
    line.
- **A reboot always gives the stock access point back.** If the camera can't
  be reached, hold Power in g360-app to turn it off, then turn it on. Every
  run ends by starting g360-app again, even when the access point fails, so
  Power-off is always there. Past kernel crashes rebooted the camera by
  themselves.

## The screen

The script no longer draws on the screen (7 November: the app owns it, and
its status corner shows the Wi-Fi state). The manual commands `join`,
`ap-cycle` and `revert` report only in `wifi.log`. Until then they showed
messages such as `WIFI JOIN` / `SEARCHING` and `WIFI OK` with the address
through the old `gear360-display` helper; that helper now lives in
`tools/experiments/` for the experiment kit. If the way back fails (`THE ACCESS POINT DID NOT
COME BACK` in the log) the app still starts, so holding Power turns the
camera off; then turn it on again.

Reasons (in the log): `WRONG PASS`, `NOT FOUND` (not in the scan), `NO JOIN` (seen, not
joined), `NO ADDRESS` (no DHCP), `NO HOST` (the Mac didn't call in), `LINK
LOST`, `NO FIRMWARE`, `WPA FAILED`, `AP OFF FAIL`, `WATCHDOG`, `REVERT`.

## Cautions

- **Root access on your home network:** the card's telnet (root, no password),
  FTP and web shell listen on every interface. Any device on that network can
  reach them while the camera is joined.
- **Password in plain text:** `/opt/storage/sdcard/wifi` holds the password in
  plain text on the card. Locally, `/wifi` in this repository is git-ignored.
- **WPA3:** a WPA3-only network won't work. WPA2 or mixed WPA2/WPA3 will.
- **Channels:** with the `US` region, a router on channel 12 or 13 isn't seen.

## The other way: ConnMan (not used)

Tizen's own path is the Wi-Fi API (`libcapi-network-wifi`: `wifi_activate`,
`wifi_scan`, `wifi_connect`), which goes through net-config and ConnMan.
ConnMan saves every network it joins as
`/opt/var/lib/connman/<service>/settings` on the built-in flash. That file
holds the name, `Favorite`, `AutoConnect`, the address settings and the
password in plain text, and it survives reboots. On a later boot, any time
station Wi-Fi is switched on (stock, or a future app), ConnMan would join it
by itself. That isn't harmful, but it lasts and isn't what the stock camera
expects. The direct path above leaves nothing behind.

## Tests

```sh
python3 -m unittest src/wifi/tests/test_wifi_join.py
```

These host-side tests use stand-ins for the camera's tools. They cover:
- success, then `revert`;
- each failure falling back;
- an access point that doesn't come back, and one that won't go off;
- `ap-cycle`;
- the watchdog, including a stale PID file that names someone else's process;
- `apply home`: in range, a missed scan, out of range, wrong password, no
  access point, an access point still starting, no network file, already
  joined, a hang stopped by the watchdog, and the keeper falling back and
  being stopped by `revert` (whose app restart skips the apply);
- `apply off` from the access point and from the home network, and waiting
  for an access point still starting; `apply ap` from the home network and
  when already up; no wait after `OFF`; a newer choice replacing a waiting
  one; an unknown choice refused;
- that no secret reaches the log or screen;
- that the long-lived helpers don't hold the locks.

## Trial log

7 November 2026: deployed the selection fix (script SHA-256
`a8d3fa3b2012f9b96a8ff45de3dec6ef91e4d26db7e507017c85cab4ba852368`)
and installed and restarted the rebuilt app. With three saved networks and
only the third available, an AP-to-HOME trial scanned the first two twice,
found the third, tried network 3 directly and got an address in 32 s (14:30:06
to 14:30:38). Before the fix, the same arrangement took 125 s (14:15:36 to
14:17:41), including roughly 88 s attempting the first two. These are single
camera observations, not a general timing guarantee.

| Step | Status |
|---|---|
| Deploy, `check`, `scan` (no switch) | done 6 November: hashes matched, `check` passed; the scan from the running access point saw 9 networks on channels 1, 3, 6, 11 and 5 GHz. The user's network is on channel 3 (WPA2-PSK/AES, −62 dBm), but the file's name differs in case at character 18 |
| `ap-cycle` (attended) | passed 6 November: off about 10 s |
| `join` (attended) | passed 6 November: joined in 1 s, address in 5 s, the Mac checked in |
| `revert` (attended) | passed after a fix: back in 5 s (result (not published: `../../output/wifi-station-20261106/RESULT.md`)) |
| `app-start` by double-Power (attended) | passed 6 November (reentry, at home): joined and the app was READY about 20 s after the press; keeper running |
| Keeper fallback (attended) | passed 6 November: link dropped on purpose; access point back after 2 min 11 s, with the app still running |
| `app-start` out of range (attended) | passed 6 November: made-up network name; access point kept (same `hostapd`), app started 11 s after the press |
| Fresh boot, double-press (attended) | passed 6 November: joined, app ready about 20 s after the press |
| Power hold while joined (attended) | passed 6 November: powered off; the next boot was normal |
| Early press, 3 s after the stock screen (attended) | failed with a plain scan (false `NOT IN RANGE`); passed with the named scan: joined, app ready 17 s after the run began |
| `WIFI` setting (`apply`), deployed | 6 November: the app's start applied `HOME` (not chosen yet); already on the network, nothing changed |
| `WIFI` setting chosen on the camera (attended) | 6 November, fresh boot: start joined; `OFF` left the network; `AP` came back in 6 s; the first `HOME` stalled (`NOT FOUND` after 30 s; access point back); `HOME` again joined in 1 s. Fix: a second `wpa_supplicant` after 12 s, deployed, not yet seen on the camera (result (not published: `../../output/wifi-setting-20261106/RESULT.md`)) |
