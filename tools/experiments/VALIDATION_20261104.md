# Offline experiment preparation — validation, 4 November 2026

Scope: the owner requested preparation/builds for all six next steps while
the camera was unavailable. No network/camera contact, native analysis,
service operation, sensor acquisition or device file was involved. Phase 2
stays finished; the six new experiment facts stay open.

Implemented and reviewed:

- `g360cap` optionally skips JPEG payload copies only when a full frame was
  requested. It retains descriptor decoding, ordered give-backs, completion,
  frame validation and failure reporting. Default capture behavior is kept.
- The CLI can copy/validate JPEGs without saving, skip their copies with
  `--frame`, or discard a validated frame after measuring it. Per-shot rows
  report capture, saving/reporting, cycle, ready/set and frame timing/status.
- Offline planner/bundler generates guarded baseline/reentry, balanced JPEG,
  10/100-still stress, saved post-stress check and HDR boundary/8/6-shot cases.
  HDR words come directly from the versioned canonical runner; preset labels
  are not reinterpreted as new TV settings.
- A passive button observer decodes ARM32 evdev records, reports SYN_DROPPED
  and observes with a bounded poll loop. The platform collector reads raw
  status/sysfs properties with explicit unknowns, never writes them.

Validation commands and results:

| Check | Result |
|---|---|
| `CC=/usr/bin/clang sh src/lib/g360cap/tests/run.sh` | Passed: ASan/UBSan pure checks, C++ linkage, fake-SDK capture/CLI/API suite, including new policies and failure/ownership cases |
| `python3 -m unittest discover -s tools/experiments/tests -v` | 10 tests passed: canonical HDR, manifests/script syntax, metrics/refusals, synthetic platform/input and shell lifecycle fixtures |
| `sh src/lib/g360cap/build-arm.sh` | Passed, ARM EXEC, existing camera libc/gyro/runtime dependencies |
| `sh tools/experiments/build-buttons.sh` | Passed, ARM executable linked to camera libc/clock runtime; no capture SDK |
| `sh tools/observe/build.sh` | Existing observation helpers rebuilt successfully, no execution |
| `python3 -m unittest discover -s tools/observe/tests` | 26 existing observation/layout/frame-check tests passed |
| Offline kit generation, file hashes and shell parsing | Passed; source/binary-pinned manifest, no transport/auto-run |

The shell lifecycle tests replace `/proc`, storage and command providers with
isolated fixtures. They exercise a stop once on fresh entry; no stop on
reentry; refusals for wrong boot, wrong probe hash, active stock, another
capture client and mismatched WB; and failure propagation without retry.
Those results are shell control-flow checks, not camera readiness evidence.

Build SHA256:

- `output/g360cap-build/g360-shot`:
  `c08394caf8b3237cac2b19a571027fa051c3e4925f5e5b3c424634681a76e1a1`.
- `output/experiment-buttons-build/button-observer`:
  `bc8603b84294fdbee71576d25b48c92fc484e4680186d99706f336bc549923b1`.

Generated kit: `output/experiment-kit-20261104/` (ignored; locally
reproducible). Its `manifest.json` pins source, target binary and staged script
hashes. Only canonical source/tests/notes/facts are committed.

Remaining: all camera runs; a supported RTOS encoder bypass if measurements
justify it; native contracts for other drives, screen/sound/LED ownership
and the firmware battery provider; actual app/battery controller integration.
First hardware edge is the saved-frame baseline followed by a new process
in the same boot. The runbook specifies evidence, checkpoints and cleanup.

No workers or native sessions were created. No persistent goal was present
or created. Source is committed/integrated into local master under the
standing worktree agreement; no remote push is requested. Codex five-hour
usage was 72% used/28% remaining at startup and 81%/19% at the preparation
checkpoint (reset 4 November 2026, 23:18:09 Australia/Sydney); weekly usage
was 13% then. Final usage is recorded in the current index at wind-down.
