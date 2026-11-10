# Gear 360 modding plus

Research and an open replacement camera app for the **Samsung Gear 360 (2017),
model SM-R210**: the small two-lens 360° camera. The app runs from the SD card,
takes both lenses at full sensor resolution as RAW (DNG), and adds exposure
sweeps, a Wi-Fi remote page and a status display. Nothing is flashed: the
camera's firmware is not modified.

> **Status.** The source, tests and research are published. A packaged SD-card
> installer is still being prepared; until it exists, installing means the
> developer route in the [app README](src/app/README.md#installing-a-build-on-the-camera).
> Read [Before you start](#before-you-start) and
> [Security](#security-the-cameras-network-services-have-no-password) first.

## What the app does

`g360-app` replaces the stock camera software while it runs. It is a small C
program that talks to the camera's own libraries.

- **Single photo:** one Capture click saves the full RAW of both lenses as a DNG,
  a JPEG, or both, with Exif, the camera's own colour data and the camera's pose
  (from its accelerometer) in every file.
- **Exposure sweep (HDR):** seven stills at fixed manual exposures covering the
  camera's whole usable range, about 1/6 s down to about 1/13,000 s at ISO 100.
- **Buttons and screen:** a settings menu on the camera's 72 × 32 pixel OLED, a
  status corner (Wi-Fi, battery and charging, how many photos fit on the card),
  the side LED, and the camera's own sounds.
- **Remote page:** a web page served by the camera for Capture, settings,
  shutter and ISO, timed automatic photos and a live preview.
- **Wi-Fi setting:** off, the camera's own access point, or an existing network.
- **Power:** holding Power turns the camera off.

Details, every setting and the file layout on the card:
[src/app/README.md](src/app/README.md). What has been checked on a real camera:
[src/app/VALIDATION.md](src/app/VALIDATION.md).

## Before you start

- **Only the SM-R210 has been used.** One camera, one firmware. Other Gear 360
  models are different cameras; do not try this on them.
- **Use at your own risk.** The app does not flash or patch firmware, and the
  camera boots into its stock software. Even so, software that drives a camera's
  hardware can misbehave, and no warranty is given (see the [licence](#licence)).
- **The app starts when you double-press Power** after the camera has booted. It
  then stops the stock camera application and takes over the screen, buttons and
  sensors until you turn the camera off.
- **A microSD card is needed.** The app, its settings and the photos live there.
- **No Samsung code, sounds or images are included.** The app plays the camera's
  own sounds from the camera's own files while it runs.

## Security: the camera's network services have no password

The SD-card mod that this project builds on starts three services that ask for no
password, and they stay on while it runs:

| Service | Port | What anyone who can reach it can do |
|---|---|---|
| Telnet | 23 | Open a command line on the camera as `root` |
| FTP | 21 | Read and overwrite any file on the camera and the card |
| Web shell (`/cgi-bin/execute`) | 8888 | Run any command as `root` from a web address |

On the camera's **own access point** only devices that know its Wi-Fi password
(shown on the camera's screen) can reach them. If you use the app's WIFI setting to
make the camera join your **home or any other network**, every device on that
network can reach them. Anyone there could delete or overwrite files on the camera
and the card, change its settings, or stop it from working. Do not join a network
you do not trust (shared, public, office, hotel), and keep the camera on its own
access point when other people's devices are around.

## Getting the source

```sh
git clone https://github.com/soswow/gear360-modding-plus.git
```

This repository is published from a private working repository, with a fresh
history. Earlier private history is not part of it.

## Building from source

The app is C11, cross-compiled for the camera's 32-bit ARM Linux with
`clang` and `lld` (the paths are at the top of
[src/app/build-arm.sh](src/app/build-arm.sh)). The build links against the
camera's own shared libraries (`libdevice-node` and the C library among them).
Those belong to Samsung and are **not** in this repository: copy the camera's
`/lib` and `/usr/lib` from your own camera (for example over FTP) into a folder
of your choice, so that the folder holds `lib/` and `usr/lib/`, then point the
build at it:

```sh
GEAR360_DUMP=/path/to/that/folder sh src/app/build-arm.sh
```

The host-side tests need no camera. The commands are in the app README under
[Build and checks](src/app/README.md#build-and-checks).

## Where things are

| Path | What it holds |
|---|---|
| [src/app/](src/app/README.md) | The camera app: main loop, settings, status corner, power, storage, launcher, web page, host tests |
| [src/lib/](src/lib/g360cap/README.md) | Its libraries: [g360cap](src/lib/g360cap/README.md) (capture, RAW, DNG, Exif), [g360ctl](src/lib/g360ctl/README.md) (buttons, LEDs, sound), [g360display](src/lib/g360display/g360display.h) (OLED), [capture-session](src/lib/capture-session/README.md) |
| [src/wifi/](src/wifi/README.md) | The Wi-Fi setting: join a network or run the access point |
| [src/experimental/](src/experimental) | Models of the stock camera's internal protocol (research, not a working backend) |
| [tools/](tools/experiments/README.md) | Host tools: camera shell, trials, experiment kit, read-only probes |
| [reverse-engineering/](reverse-engineering/facts/README.md) | The research: facts, the investigation log, DNG and lens-EEPROM notes |

Reaching a camera that has joined your network: copy
[camera-hosts.example](camera-hosts.example) to `camera-hosts` and list its
addresses; the host tools read that file. `camera-hosts` is ignored by git.

## The research

The findings are in [reverse-engineering/facts/](reverse-engineering/facts/README.md)
(accepted, open and rejected claims, each with its evidence) and in the dated
[investigation log](reverse-engineering/CURRENT_INVESTIGATION.md). The
colour pipeline is in [reverse-engineering/dng/](reverse-engineering/dng/README.md) and the
lens modules' calibration data in [reverse-engineering/eeprom/](reverse-engineering/eeprom/README.md).

Much of this work was done by disassembling the camera's own software. **The
disassembly itself, decompiled code, firmware images, memory dumps, captures and
photographs are not published**, because they are Samsung's or private. Where a
document cites such a file, the citation is kept and marked
"(not published: `path`)". Please do not post firmware, dumps or decompiled
Samsung code in issues or pull requests either.

## Credits and third-party files

This project builds on the SD-card mod by **ottokiksmaler**,
[github.com/ottokiksmaler/gear360_modding](https://github.com/ottokiksmaler/gear360_modding):
the `nx_cs.adj` / `info.tg` trick that makes the Gear 360 (2017) run a script from
the card, and a small web shell, FTP and telnet server. That repository has no
licence, so **we do not copy or redistribute any of its files**. Our installer (in
preparation; until it is released, fetch the files yourself from their repository)
downloads `gear360_mods_SD.zip` from commit `0e3af0f` of that repository, checks
its SHA-256 (`a7a437cb…19b5`), and copies only these files to your card: `info.tg`,
`nx_cs.adj`, `mods/telnetd`, `mods/ftpd`, `mods/httpd`, `mods/tcpsvd`,
`mods/www/cgi-bin/execute` and `mods/www/cgi-bin/gallery`. They stay their author's
work. The four network tools are one BusyBox 1.18.4 binary (GPLv2).

Our own boot hook (`mod.sh`) is written from scratch; it starts the same way as
theirs, with the same `st led` blink, and credits their original. They also
documented that almost all of the camera's `st` shell commands work as on the
Samsung NX1 and NX500; see their
[ST commands notes](https://github.com/ottokiksmaler/nx500_nx1_modding/blob/master/ST%20Commands.md).

Time zone data is the IANA time zone database (public domain).

## Trademarks

Samsung and Gear 360 are trademarks of Samsung. This project is not affiliated
with or endorsed by Samsung.

## Contributing

Issues are welcome, especially reports from a real SM-R210: what you did, what the
camera did, and the app's log (`G360/state/app.log` on the card). Pull requests are
read and, if accepted, merged by hand, because this repository is generated from a
private one. Keep to the rule above: no Samsung firmware, dumps or decompiled code.

## Licence

Copyright (C) 2026 the authors of this repository.

This program is free software: you can redistribute it and/or modify it under the
terms of the **GNU General Public License, version 3** (see [LICENSE](LICENSE)).
It is distributed in the hope that it will be useful, but **without any warranty**;
without even the implied warranty of merchantability or fitness for a particular
purpose.

Files that belong to someone else (see Credits) keep their own licences and are not
covered by this one. Binaries published with a release are built from the source of
the tagged commit in this repository.
