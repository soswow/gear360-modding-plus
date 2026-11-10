# Status-display helper

`gear360-display.c` is an experimental status-line helper, with the b024fca
row changes repaired after source review. Build without contacting the camera:

```sh
sh archive/capture-session-display/build-display.sh
```

The helper accepts text, `--clear`, `--row N TEXT` (rows0..3), or `--render TEXT`.
Text and `--clear` use the second row; explicit rows select another band. The last mode prints
the twelve-column font on the workstation and never opens the device.
Missing render arguments and unknown options return usage status2.

The reviewed implementation sends and updates only the selected rectangle using one descriptor,
reports ioctl/close errors, and keeps its font in fixed arrays. Parent sanitizer
render checks and linking against the retained camera libraries pass. The ioctl
numbers/layouts originate in the candidate's native-analysis report; driver
semantics and the changed descriptor lifetime are not validated on hardware.
Golden host rectangle tests verify payload placement and selected-row updates
under the documented driver model; actual panel behavior remains unvalidated.
No device execution is established by these software checks. Do not treat this
helper as a capture service or as proof of app-free camera initialization.

The access-point shutdown test was not integrated here.


# Guarded diagnostic runner

`run_load_check.py` stages a hash-pinned helper and generated watchdog/recovery
scripts over FTP, verifies bytes and remote hashes, then launches a short CGI
command. App PID/start tick, boot identity, pinned app/library hashes and the
pinned native dependencies `/usr/lib/libdevice-node.so.0.1` and
`/usr/lib/libslp_devman_plugin.so` must match before staging, immediately
before execution and afterwards; a dependency that is absent, that changed or
whose digest cannot be read refuses the run, and its observed hash is kept in
the run record. The dependency pins are a reviewed subset of the library's
dependencies, not a complete loader closure, and they are not an authorization
to load the SDK. Signals are limited to the helper's exact executable/argv0 and
recorded PID/start tick. Unknown process state preserves the owned directory
and reports its path. These checks reduce process mistakes; they are not an
atomic PID-reuse guarantee.

The fixed argv is `--load-only --library /usr/lib/libcapture-fw-prod.so --log`
with a generated temporary progress path. With the real capture-shot helper,
loading the SDK can execute constructors and dependency/plugin hooks. Such a
load remains deferred pending that contract review. Passing runner tests does
not authorize an SDK load or prove initialization/capture readiness.

Offline validation:

```sh
python3 -W error::ResourceWarning src/lib/capture-session/camera/test_load_check.py
python3 src/lib/capture-session/camera/test_watchdog_payload.py
sh src/lib/capture-session/camera/build-watchdog-payload.sh output/capture-watchdog-build
```

`watchdog-payload.c` is a separate libc-only qualification helper. It validates
the fixed arguments, writes a marker inside the generated temporary directory,
and exits. `WATCHDOG_TEST_HANG=1` instead sleeps after writing; it never loads
the SDK or accesses a camera device. The build script produces both ARM variants
and checks dependencies/constructor sections. Binary outputs and full run records
remain ignored. Use an explicit binary hash and a new ignored output directory
for each parent-reviewed target trial. Runner exit2 records uncertainty; in the
sleeping qualification, a watchdog timeout with verified termination is expected.

The first on-camera qualification sent large shell bodies in CGI requests and
timed out. Post-run identity passed, no helper process remained, and exact owned
file/directory cleanup was subsequently verified. The revised transport stages
scripts as files; no specific CGI URL limit is claimed. Current trial results
and native load-hook boundaries are in the25October parent review/fact records.


25October target qualification: both libc-only payloads passed. Exit produced the
marker/rc0; deliberate sleep produced the marker and expected watchdog timeout,
rc143/terminated=yes. Both passed unchanged identity/hash guards and cleanup.
The SDK was not loaded. The target lost-connection recovery path remains covered
by offline tests rather than an induced camera failure.


## Stock-absent initialization diagnostics

`prepare_init_probe.py` generates local watchdog/recovery bytes for the fixed
init-only probe. It does no camera I/O. The default argv is a held `--probe`;
`--local-bootstrap` adds the explicit local service sequence. It checks the
owned temporary-path format and expected boot identity, and refuses generation
if the reviewed runner's fixed command structure drifts. The original load-only
runner still supports only its original mode. Runtime guard loss is a separate
failure outcome; independently confirm termination before cleanup.

`observe_init_probe.observe_init_probe` is a readonly monitor for a parent-owned
background thread during the execute call. Use its cached runner module's
`HttpFtpCamera` client so remaining-budget timeouts apply. It samples the owned
helper's descriptors/thread metadata with PID/starttick/executable and boot
guards; it executes no SDK call and interprets no thread name as readiness.
Use a new ignored JSON output for each observation. Synthetic checks are not
hardware proof; current target results and permission scopes are in
the parent review (not published: `../../../../output/decompile/rea/pass-goal-reset-20261029/PARENT_REVIEW.md`).

## Init-probe orchestration

`run_init_probe.py` is the fixed-purpose path for one init-only probe trial with
the observer already watching. It reuses the guarded runner's guards, staging,
watchdog, recovery and cleanup code; it does not add a mode to the runner and
the runner's CLI still stages and runs only the load-only helper.

    python3 src/lib/capture-session/camera/run_init_probe.py \
        --helper <capture-shot> --helper-sha256 <sha256> \
        --output <new-run-dir> [--previous-boot <uuid:btime>] \
        [--stop-stock-app]

The optional `--diagnostic-still --local-bootstrap --notify --free-bytes DECIMAL`
mode stages a separately built and pinned standalone one-still helper under the
same owned `capture-shot` filename. `DECIMAL` must contain only ASCII digits
and fit an unsigned 64-bit value. The generated argv is fixed to
`--diagnostic-one-still --library /usr/lib/libcapture-fw-prod.so --log` the
owned progress path, and `--free-bytes` the validated decimal; it does not pass
the init-only helper's `--probe` options. The caller must provide the helper's
exact SHA-256. The standalone helper's intended operation is one device-index
21 still (Cap=Max=1, remaining=1), pointer-free event logging, no retry, at most
20 seconds of waiting, and no image handling, setting, destroy or release.
Those helper properties require independent source and binary review; the runner
only fixes the argv and checks the supplied digest. No camera execution follows
from this documentation or from passing host tests.

The result's `diagnostic_mode` identifies this mode. The legacy `sdk_returned`
field means only that the helper process returned zero. Even when `ok` is true,
`still_completion`, `request_association`, `raw_ownership`, and
`remote_readiness` remain `not-established`; `ok` combines local process and
metadata checks, not a completed still. The existing observer requires the
`capture-shot: before probe hold` marker for immediate sampling, otherwise it
samples after its 8-second partial-stall threshold; a helper that finishes
before a guarded snapshot produces a non-pass. The same watchdog, guards,
recovery and cleanup rules apply.

The optional `--diagnostic-liveview --local-bootstrap --notify --free-bytes
DECIMAL` mode stages the separately built and pinned liveview-only helper under
the same owned `capture-shot` filename. It is exclusive with
`--diagnostic-still`; giving both is refused before anything is staged. The
generated argv is fixed to `--diagnostic-liveview-only --library
/usr/lib/libcapture-fw-prod.so --log` the owned progress path, and
`--free-bytes` the validated decimal, because the helper's own CLI is strict
about that argument even though it never allocates a RAW buffer. The helper
issues exactly one liveview start edge, registers the app-facing notification
callback, subscribes handle id 8, observes for a bounded window, and never
issues a still command or dereferences a data pointer. A callback arrival is
not request-associated completion and is not capture readiness. Those helper
properties require independent source and binary review; the runner only fixes
the argv and checks the supplied digest. No camera execution follows from this
documentation or from passing host tests.

The stock application and device-channel owners must be verifiably absent on
every execution path. Without `--stop-stock-app`, the runner refuses before
staging if that precondition is not already true. With the flag, it rechecks
the pinned process and unit identity, sends one `systemctl --no-block stop`
request, never retries an uncertain request, and then verifies absence. A
zombie or stopped stock process is still a refusal.

What it guarantees that driving the pieces by hand does not:

* the observer starts **before** the probe is executed, so the guarded sample
  is taken while the helper is still alive;
* the probe's stdout, stderr, progress, status and owner files, the staged
  script bytes and the observer's own record are kept whether the trial
  succeeds or fails;
* the generated directory is kept when the helper's termination could not be
  confirmed, because it still holds the owner and status files needed to
  finish the job;
* `sdk_returned`, `worker_observed`, `local_transport_observed`, and
  `remote_readiness` are reported separately. `worker_observed` remains the
  guarded helper-process snapshot. Only a complete snapshot can contribute to
  `local_transport_observed`, which requires numeric descriptor rows for
  `d5_lock` and `d5_ipcc`, plus numeric thread rows for `udd_ipcc_fmt_in`,
  `MCB_Receiver`, and `MCB_Sender`; missing roles are listed in
  `local_transport_missing`. This bounded `/proc` sample is not atomic and
  does not prove exclusive hardware ownership or service readiness.
* The run also pins `/usr/lib/libmulticore-bridge.so` against its reviewed ABI
  hash at collection, immediately before execution, and afterward. The run
  record retains each observed bridge hash. This is a narrow ABI guard, not a
  claim that every loader dependency is pinned.

If the host runner is interrupted during execution, evidence retrieval, guards,
or cleanup, it records `interrupted: true`, `outcome: interrupted`, the known
remote path and termination state in `run.json` and console. It does not issue
further camera calls, repeat a stop or SDK command, request recovery, or remove
the owned directory after interruption. A stop already attempted may have had
an effect, and helper termination is unknown unless independently confirmed.
The observer is signalled to stop and cannot start another camera call; a call
already in flight cannot be cancelled by the wrapper and may outlive its bounded
join. Preserve the directory and use a separately reviewed recovery procedure.
Before remote ownership, interruption reports a local cancellation with no
claimed device cleanup.

`--previous-boot` is the boot identity of the last completed trial. The current
one must differ from it: the trial needs a fresh boot, so a repeated boot
identity is refused before anything is staged.

The observer's remaining-timeout rebuild now also accepts a camera whose class
came from a second import of the runner file, and the observer takes an
optional stop predicate so it ends when the probe is over instead of waiting
out its deadline.

## One-still bisect build switch

`build-one-still-probe.sh` accepts `SUPPRESS_THE_STILL=0` (default) or `1`.
Any other value is rejected before the compiler runs. The suppressed build
performs the same local setup, public encoded-set subscription, and category
`0x10` notify request, then omits the `mm_camera_capture_still` call. Its log
states that the still command was omitted and that the earlier notify request
was sent; it does not print synthetic still-call entry or return records.
This switch only selects a compile-time variant: the build script does not
execute the binary or contact a camera.

Offline validation:

    python3 -W error::ResourceWarning src/lib/capture-session/camera/test_init_probe.py

Passing these tests is not a hardware result and not an authorization to load
the SDK, capture, or run against a camera.
