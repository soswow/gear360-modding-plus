# Phase 2 handoff — 4 November 2026

Phase 2 is finished by the owner's decision after trial F2. Its definition is
PROJECT_GOAL.md (not published: `../../PROJECT_GOAL.md`) (retired 5 November 2026); goals and
open tasks are now in TODO.md (not published: `../../TODO.md`). The repository
cleanup did not start a next product step or change a hardware contract.

**Follow-up, 5 November:** the owner subsequently started all six experiment
preparations and authorized unattended camera testing. Results (not published: `../../output/experiments-live-20261104/RESULT.md`):
309 full RAW captures, 44 processes, one boot; manual/auto 100-shot stress and
both HDR sweeps passed. Linux JPEG omission reduced mean work by 7.03% in its
matched cohort. See current status (not published: `../ACTIVE_PLAN.md`), which supersedes the
historical limits below where those tests provide new evidence.

## Current implementation

[g360cap](../../src/lib/g360cap/README.md) is the C API, usable from C++, with `g360-shot` as
its CLI. It hosts the camera's capture library in our own process while the stock
application is stopped. The full sensor frame is copied while the RTOS waits
for our process's gyro reply, before the camera reuses that backing.

The library's owned copy is both lenses side by side: 5824×2896, stride 11648,
33,732,608 bytes, physical source `0xa6000000` in this firmware's fixed layout.
The caller owns the copy until it frees it. The geometry and lifetime are from
the existing goal/facts, not a new claim from this cleanup.

Start with:

- [Public API and build instructions](../../src/lib/g360cap/README.md),
  [header](../../src/lib/g360cap/g360cap.h), and [ARM build](../../src/lib/g360cap/build-arm.sh).
- F2 result (not published: `../../output/decompile/rea/pass-frameF2/RESULT.md`): two normal
  stills in one session, both complete with full frames, JPEGs and give-backs.
- Frame review (not published: `../../output/decompile/rea/pass-frame-review-20261104/RESULT.md`)
  and pause trace (not published: `../../output/decompile/rea/pass-expansion-pause-20261104/RESULT.md`).
- [Pinned firmware](../facts/coordinates.yaml) and
  artifact restoration (not published: `../../retained-artifacts/README.md`).

Offline checks do not operate the camera:

```sh
sh src/lib/g360cap/tests/run.sh
sh src/lib/g360cap/build-arm.sh
python3 archive/hdr-integration/tests/test_offline.py
```

## Limits to carry forward

The original Phase 2 trials saved JPEGs and began after fresh boots. Subsequent
[experiments](../../tools/experiments/README.md) validated JPEG discard/skip-copy,
same-boot process reentry and normal-still manual exposure through the full
HDR range. These modes do not bypass RTOS encoding. The frame copy still adds
a roughly 0.6–0.7 s hold. Other native drives remain unvalidated. Cleared low bits,
estimated dark level and a visually judged GRBG order are not calibrated sensor
precision, black level or colour measurements.

Never restart the stock application with `systemctl start`: the observed
restart route enters boot-snapshot creation. New-process reentry after our
client exits passed the 44-process follow-up. Follow the current runbook and explicit
camera authorization rather than replaying an old script from an archive.

## Next work, when started by the owner

The owner started offline preparation later on 4 November, while the camera
was unavailable. The [experiment kit](../../tools/experiments/README.md) is now the
entry point for prepared cases and remaining hardware/native checks.

The goals file records optional JPEG only if faster, stable sessions without
power cycles, capture speed/stress, the HDR program's full exposure range, a
limited main-app replacement (screen, sounds, LEDs, buttons), and battery status.
The existing HDR integration (not published: `../../archive/hdr-integration/README.md`) and
web controls (not published: `../../hdr/web/README.md`) provide source for that work.

The owner does not need the historical captured RAW files. Their deletion
inventory preserves hashes and descriptions; source, logs and findings remain.
Restore firmware/evidence on demand rather than reacquiring samples or treating
an absent ignored file as proof that the investigation never retained it.
