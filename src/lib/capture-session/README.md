# Portable capture-session core

**Status: partial phase2 component, not a working camera backend.** Reviewed
from MiniMax0ad5f7c with source repairs. The API is usable from C++, and manages
submission, result borrowing, timeout and explicit release through a backend
seam.
The selected goal remains our process hosting the camera library with the
stock application absent, sensor RAW under caller ownership and optional
stock JPEG from that same still. See PROJECT_GOAL.md (not published: `../../../PROJECT_GOAL.md`).

This core does no camera I/O. Its tests supply a fake backend; their bytes
are synthetic and prove neither sensor provenance nor hardware completion.
The library adapter and CLI are imported as experimental source. Stub tests
cover their local behavior; initialization, request association, actual sample
identity, native bounds and teardown remain unproved. See the experimental
section below and the integration review (not published: `../../../output/decompile/rea/pass-minimax-forward-20261024/parent-review.md`).

The core permits one request at a time and requires the backend to supply
only completions associated with that request. It cannot infer association,
fresh DMA, sensor packing or geometry from pointers. Backend close must clean
all its resources, including partial opens and buffers whose release failed.
Those are remaining API/backend integration work.

The ownership of a result is the caller's, and it is explicit: the session
hands out the bytes of a completion and keeps holding them until the caller
releases them with `capture_session_release_image`. A new capture does not
take them back. While the caller still holds a result, the next trigger is
refused with `CAPTURE_SESSION_BAD_STATE` before anything is released and
before anything is submitted, so the held bytes and the metadata committed
with them stay valid and unchanged. Releasing explicitly lets the next
trigger through.

A backend collects a completion through exactly one collector. The older
`collect_completion` carries the bytes of the still and no geometry, so the
geometry of such a completion is unknown and is reported as zero - zero
means unknown, not zero-sized. The optional extended collector
`collect_image` carries the bytes *and* the geometry of the samples in one
call, so the two belong to the same completion; the session latches the
pointers, the sizes and the geometry together, and the notification and a
later blocking collection then see the same metadata. The backend is not
asked about the geometry again, neither for a later look at the completion
nor for the release. A backend that gives both collectors, or neither, is
refused when the session is opened - before the transport is opened at all.

The geometry a backend states is its assertion, not a verified property of
the sensor. The session does not check it against the bytes it delivered.
`width`, `height` and `stride_bytes` are what the backend says about those
samples; `bits_per_sample` is the number of significant bits in a sample
and says nothing about how the samples are stored - not the packing, not the
bit order, not the padding. No native packing layout is assumed anywhere in
this core, and no mask is reinterpreted.

The backend structure is copied whole into the session and every field of it
is read, so a backend has to zero the structure before it fills the fields
in: a field left unset would otherwise be whatever the storage happened to
hold.

A finite wait returns TIMEOUT without cancelling the request; late completion
may still be collected. A failed clock before submission sends nothing. A
mid-wait clock failure returns CLOCK_ERROR with the request still outstanding.
Zero timeout submits and polls once. Settings cannot change during a request.
A malformed completion latches the session until close; there is no automatic
resynchronization or retry. Callback reentry cannot destroy borrowed bytes;
a close requested from the callback is deferred until it returns. A trigger
while the caller still holds a result is refused with BAD_STATE and changes
nothing: no release, no submission, no change of the completed count, and no
change of the held bytes or of their metadata.

Each accepted output is released separately, except exact pointer/size aliases,
which are released once. Partially overlapping/interior output pointers are
unsupported by the backend contract. A refused release is per buffer: an
output that was given back successfully is gone, and the buffer the backend
refused stays with the session, so the next trigger stays refused with
`CAPTURE_SESSION_BAD_STATE` until that buffer has been released. Close is
the final cleanup path. These are software contracts that a real backend
still has to implement against the actual native resource lifecycle.

```sh
sh src/lib/capture-session/tests/run.sh
sh src/lib/capture-session/build-core.sh
```

Host checks use ASan/UBSan and C++ linkage. The ARM command compiles objects
only; it neither links a complete program nor executes camera code. The
platform header uses SDK declarations on macOS and minimal target declarations
for the camera; clock_gettime is supplied by the camera's librt when linked.

The standalone platform self-check builds an ARM executable that prints its
arguments, type widths and monotonic clock. It links only libc and librt;
it does not load the capture SDK or create a camera context. Its startup
preserves the stock application's argc/argv and runtime-finalizer handover.
The build rejects executable constructors because this minimal startup does
not run them. Host execution and ARM linking are checked. On24 October the SDK-free
self-check also passed on the pinned camera; boot/app identity stayed unchanged
and its temporary files were removed. This does not validate SDK hosting.

```sh
sh src/lib/capture-session/build-platform-self-check.sh
```

## Experimental library adapter

Everything above is the reviewed core. This section describes the component
sitting on top of it - the adapter that hosts the camera's own capture
library - and it is **experimental**: it is host-tested against a stub of that
library. This reviewed revision has no accepted end-to-end camera capture;
historical candidate runs do not close the initialization and RAW contracts.

The 25 October takeover review (not published: `../../../output/decompile/rea/pass-f666fbe-takeover-20261025/parent-review.md`)
clarifies two remaining implementation dependencies. The shared preference can
skip per-process adapter initialization. Calling the C++ initializer requires a
valid instance, and bridge startup can mask resource/thread failures; a client
handle or a repeated zero return is insufficient readiness evidence. No such
workaround is enabled here.

The selected LRC image constructor clears the first plane block at image+0x84.
The hardware configuration getter later reads that same block. Its actual
binding, sensor provenance and retained lifetime still need proof. Copying the
image metadata or selecting a quality mode does not establish sensor retention.
The adapter therefore continues to report that capability as unsupported.

**Files.** The adapter is `capture_session_library.c` / `.h` (the host, the
backend seam and the topic handling) with `capture-shot.c` as the command-line
program. Its host checks are `tests/test_capture_library.c` against
`tests/stub_capture_library.c`; the whole suite is run by `tests/run.sh`.
`build-core.sh` and `build-arm.sh` compile objects only - the latter for
`armv7a-linux-gnueabi`, against the camera's own declarations, with no dump
and no link. `build-library-host.sh` additionally links the experimental program against
the pinned dump, without executing it.

**The library's path is required.** A null or empty path is refused with a
reason (`capture_library_host_last_error()`). The linked-library case - the
library the program was linked against - is not supported and is not promised:
the entry points are resolved on the handle the open obtains, and a
process-wide lookup would bind to whichever object in the process's global
scope exports the name, which on the camera may be a different build of it.

**A close that abandoned a request fails closed.** Closing a session with a
still request still outstanding frees the adapter's own bytes - the result
that arrived and was never collected - and clears the state that says a
request is outstanding. It does **not** cancel the native request: nothing
here shows the camera that the request is off, and the native route cannot be
asked which request a later delivery belongs to. So that host is not reused,
it takes no further request, and the process opens no further host either -
the library's registrations survive, so a late delivery would land in the
next one. A fresh process clears only this local eligibility latch; it does not establish
remote quiescence or camera recovery. The tests that abandon a
request run in a child of their own for that reason.

**The first result is latched, and an extra set is refused.** The native route
hands out no request id, so a set that arrived after the submission cannot be
associated with the request - it may be a duplicate publication of the same
still, a second still, or a late delivery of older work. The first result is
therefore the one the adapter keeps: the extra set is refused before its
payload is read, the latched copy is not replaced or freed, and the library's
plane is not released a second time. What the retirement of that extra plane
is, is left unestablished. The host is then marked as needing recovery - it
takes no further setting, no capture mode and no request - and
`capture_library_host_read_health()` says so. This process opens no further
host either, because an unattributable set leaves the native obligations
unresolved.

**A native release that failed leaves its outcome unresolved.** The release of
the still's first image is the library's own call, and its status says
nothing about what the camera did with the plane - a nonzero status is not a
statement that the plane was released. So a later call would release the same
address twice, and the host takes no further request, no further setting and
no further capture mode; the process opens no further host either. The
release is not retried. The bytes that were copied are not affected: they
stay the caller's, and they are collected and released as they are. A zero
status is a local completion, never proof of retirement - nothing here waits
for an acknowledgement the interface does not offer.

**A request made while the native release runs is refused.** The release is a
call into the library, made with the callback lock dropped because the
library may deliver a callback from inside it. While that call is in flight
the adapter is inside the library, so the host takes no request, no setting
and no capture mode in that window.

**Closing while the native release runs forbids the process reuse too.** The
release's outcome was then never established, and the outstanding-request
rule does not cover it - the collection may already have cleared the
request. The quarantine is set before the release returns, and a later
successful return does not take it back. Nothing here certifies a recovery:
the process exit is not one.

**A mutating call of this adapter is reserved, and the release a callback
owes is queued behind it.** The release of the still's first image is a call
into the library, and so are this adapter's own mutating calls - the still's
submission, a setting, the capture mode. A release made from inside one of
those calls would be two calls into the library at the same time. Each of
those calls is therefore **reserved** under the adapter's lock in the same
step in which it is admitted, so no callback can slip between the check and
the call. A callback that arrives while the reservation is held copies the
samples at once and **queues** the release instead of making it; the thread
that owns the call claims that release when the call returns and makes it
with the lock dropped. The plane is released exactly once, and never while
one of this adapter's own calls is running. The copied bytes are
unaffected: they are the caller's, and they are collected and released as
they are. Nothing here claims anything about calls the adapter does not
make.

**A queued release and a running call both block mutations.** A host with a
release queued behind one of those calls, and a host with such a call
running, take no request, no setting and no capture mode, and this process
opens no further host while a release is still owed. A release that the
library refused still leaves the outcome unresolved: it is not retried, and
the bytes that were copied stay usable.

**A close while native work is active fails closed.** The close does not
cancel the call and nothing here shows that it was cancelled, so the host
is not reused and this process opens no further host. The thread that owns
the call holds everything the call needs, and it writes to the host only
when the host's identity says the host is still the live one - a closed
host's address can be given to a new one, so the address alone does not
say. The shared object stays loaded, so a release that was queued can still
be made after the close.

**A close while native work is active leaves the native client alone.**
Destroying the client while a call into the library is running is not
established as safe, so the client is not destroyed: that native client
obligation is left unrecovered, the destroy is not retried later, and the
process's exit is not claimed to recover it. An ordinary close of an idle
host still destroys the client, as before.

The host tests cover this sequencing with the stub: the stub counts the
releases that were made while a call of each kind was on the stack, and
those counts have to be zero. They are not evidence about the camera: the
stub is the stub's own delivery order, not the library's.

**Unsupported program behaviour.** `capture-shot` cannot take a still with this
adapter: the adapter has no verified way to ask the camera to keep the sensor
samples, so the program refuses **before** it opens the library and exits with
its own unsupported status (3). It does not enable the camera's raw switch by a
guess, and it does not take the still and hand the set's first image out as
the sensor samples while pretending the ask succeeded. That mapping - the
first image of the still's set into the session's `sensor_data` - is the
adapter's own experimental mapping, not a proven sensor output.

`--probe --probe-hold-ms N` keeps the initialized-client attempt observable for
1..30000 milliseconds before the existing close path. It requests no still.
The bounded sleep and open/hold/close logs let a target experiment inspect the
process's actual descriptors and threads. Verbose library logs distinguish
entry/return of `dlopen`, `mm_camera_create` and callback registration. The
probe reports readiness as unverified; neither its return nor its close proves
receiver readiness or remote cleanup. Invalid hold arguments and combining
`--probe` with `--load-only` are refused before log or library access. This
diagnostic still requires the applicable target-execution authorization.

`--probe --probe-local-bootstrap` additionally attempts the pinned library's
local adapter initialization and orientation singleton construction before
client creation. It resolves all three native exports first, passes the actual
`Instance()` pointer to `Initialize`, and stops before client creation on a
missing symbol, null pointer or reported initialization error. This opt-in
diagnostic leaves the library mapped and performs no shared-preference clear,
retry or extra realization. It is intended for a guarded stock-absent test;
normal SDK initialization can touch hardware. Returned calls do not establish
readiness or cleanup, and a failed partial startup may need a power cycle.

**Diagnostic modes, and what they may touch.** `--load-only` loads the shared
object and resolves its entry points; no client is created, but loading may
run the constructors of the library and of newly loaded dependencies, and the
object stays loaded afterwards. `--probe` additionally creates the client,
and the client's creation is the library's power-on call - that may touch the
device. Neither mode proves that the camera is untouched, and neither
establishes a device effect: a returned handle does not establish that the
module was initialised, and an event arriving does not establish one either.

**The native gaps, exactly.** *Request:* the native route hands out no request
id to match a delivery against, so the adapter's "arrived after the submission"
rule is a heuristic and not a correlation. *Provenance:* the first image is the
firmware's "Raw Compress" image; a native name is not a provenance, and
whether those bytes are the sensor's own samples is not established. *Bounds:*
the descriptor states the buffer's address and size, and the copy extent is
that size; the stride words' unit is not established, so the reported stride
is zero, and the sample packing is not established either. *Cleanup:* the first
image's plane is returned with the library's own release call, and the shared
object is not unloaded, so whether the capture module is powered off is not
established either.

The reviewed adapter uses a callback label within the native nine-byte limit
and shutter variable facade ID `0x101`. Native image-quality ID4 accepts
positions0..7 or tagged values0x40000..0x40007; it rejects other bit patterns
and translates to the API's tagged values. These writes do not establish
sensor retention. Native buffer release runs outside the callback lock, with
per-open identity guarding error reporting after a host closes. The release's
own state is tracked separately from the ownership of the private copy, and
the whole-result JPEG and metadata retirement stays open.


### Manual access-point request helper

`camera/ap-boot.sh` requests the access point through mobileap-agent's checked
six-key D-Bus interface. It preserves the passphrase file and existing services,
refuses on missing prerequisites or unreadable state, and sends at most one
mutating request. It reports the method result separately from observed
hostapd presence; neither proves connectivity. This is a manually invoked
helper, not an installed startup change. It has passed synthetic host checks
(`python3 camera/test_ap_boot.py`), and has not been run on this camera by
the parent review. Camera utility support and the read-only GetAll probe
remain unvalidated. See the script header for paths, controls and exit codes.
The gdbus CLI exposes the passphrase in its argument vector while it runs;
logs omit the request dictionary.

### Required host services (offline finding)

The pinned library contains an `fd_alg` subscriber for expansion topic
`0x4010008`. Its tag3 arm writes the orientation auxiliary and sends the
process event; the adapter's generic notification callback is not that
responder. Shared preference word `0x1268` can skip the process-local setup
that creates it. Qualifying this subscriber, its device-property/history
inputs and mappings is still required; a reply can carry default orientation
values. Do not add a replacement or delay a reply on the basis of callback
registration alone. See [host contracts](../../../reverse-engineering/facts/contracts/host-services.yaml)
and the review (not published: `../../../output/decompile/rea/pass-host-services-20261026/parent-review.md`).
