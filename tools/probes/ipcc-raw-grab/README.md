# Experimental raw-grab diagnostic

Imported selectively from MiniMax snapshot `4d2deb0`, with reviewed argument
handling fixes. **This is an experimental diagnostic, and it has no successful
end-to-end RAW capture demonstration.** The product remains the callable C/C++
API described in PROJECT_GOAL.md (not published: `../../../PROJECT_GOAL.md`).

The host runner now builds numeric, mode-specific commands: ordinary still
commands omit bench options and a zero log-dump default. The actual C entry
point reports invalid arguments through a pre-initialization fallback, accepts
zero optional values, and rejects a bench with no address before device I/O.
Host syscall injection verifies these paths without touching a device.

Build and check locally:

```sh
sh tools/probes/ipcc-raw-grab/tests/run.sh
sh tools/probes/ipcc-raw-grab/build.sh
```

The ARM build does not execute anything. Target execution needs a reviewed
capture/session and address contract; the earlier timeout runs do not establish
that their helper reached the still request. The send-free preflight has run on the camera; capture/acquisition modes
have not been exercised by this review. See review (not published: `../../../output/decompile/rea/pass-4d2deb0-review-20261023/parent-review.md`).

The retained prototype still has unresolved acquisition-critical behavior:
untagged allocation matching/retry, passive ring sampling,
capture-context interpretation, sensor producer/packing, backing retention,
cache visibility and result association. Copy extent is a candidate, not a
proved sensor raster. Its low-word-bit statistic cannot validate sensor RAW.
The setting helper attempts an enable/disable cycle; it does not establish the
original setting value or a successful acknowledged restore. Its release/error
paths are not a complete owned-session lifecycle.

No live command is recommended by this component's host test result. The
[offline trace decoder](../../../reverse-engineering/observer-trace/) is separate from the target helper.
Source and Markdown are local; binaries, captures and generated output stay
ignored. No push is part of this work.

The watcher now makes bounded contiguous copies of wrapped frames, validates
incoming envelopes, and reports observed malformed/discarded data. Host tests
cover15 synthetic ring cases. Its scratch buffer lasts only for a synchronous
callback; the change grants no request association, cache coherence or RAW
ownership. Full producer laps remain undetectable from modulo cursors.

`--dry-run` builds and prints the still request, then exits before memory
mapping, device access, settings, logs or capture. It accepts the two named
still-command presets; other commands and modes without a still request are
refused. Host C-entry tests check zero device calls and an independent golden
frame. On23 October2026 the ARM build printed the expected56-byte frame and
exited0 on the camera. The stock app retained its process start identity and
the uploaded helper was removed. This verifies preflight execution only.
