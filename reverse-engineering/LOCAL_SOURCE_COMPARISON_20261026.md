# Local comparison of public capture research — 26 October 2026

The five leads were retrieved and compared locally. They supply useful platform
source, failure reports, control helpers and a potential decoder. The inspected
corpus does not demonstrate an app-absent SM-R210 capture host, safe receiver
handover, fresh request-associated sensor RAW or retained backing. Phase2 remains
unfinished and capture/retention capability remains false.

The local source inventory (not published: `../output/decompile/rea/pass-local-comparison-20261026/source-inventory.md`)
pins 98 source/research/license files, their Git blob identities and SHA256s,
seven retained SM-R210 plaintext files, and manufacturer documentation. Original
snapshots are under `output/decompile/rea/pass-local-comparison-20261026/sources/`.
These are selected text/source snapshots, not full binary-containing clones.
No downloaded code, SDK, camera command or service operation was executed.

| Lead | What survives comparison | Useful for our project | What it does not supply |
|---|---|---|---|
| [Metol](https://github.com/WApaperplane/metol/tree/cb82f4886c1174261cbba8127000c7b696944ff4) | Four published systemd units and `hib_tmp.sh` exactly match our retained files; main hibernation script is closely related | Stronger evidence for shared Linux startup scaffolding; named lifecycle dependencies to investigate | A demonstrated restart mechanism or clean handover; NX500 app/native addresses are not SM-R210 evidence |
| [Gear360 C200](https://github.com/vcxzvfe/gear360-c200/tree/7373bf39ac55793b46c0c6cd9ee80805cd5e5876) | Later bench reports qualify earlier injection recipes; shipped `rvf-start.sh` now establishes USB shell transport | Published failed experiments, root/USB transport and video-container tooling as references | Onboard-app-free acquisition, a reliable RVF trigger from the published injection attempts, or sensor RAW ownership |
| [nx500ex](https://github.com/NR2BJ/nx500ex/tree/fb7fe9bd3207cad16c1fd18f65d512ce8857e8f9) | Actual shutter/interval/control code operates the stock camera; firmware notes describe a debug-file interface | Names and source relationships for comparison; later state-dependent failure reports | A capture-library host, caller-owned RAW or permission to transplant NX500 commands/addresses |
| [LibRaw](https://github.com/LibRaw/LibRaw/tree/2caab57e269cc7fce17c3189e4d4d7e3e5861875) | Real Samsung3 stream decoder, TIFF dispatch, bit reader and output-allocation code | A possible existing decoder once our acquired representation is identified | Evidence that our LRC buffer is Samsung3, or acquisition/completion/cache/lifetime contracts |
| [NX1 manufacturer documentation](https://downloadcenter.samsung.com/content/UM/201511/20151102105725627/NX1_English.pdf) | Windows-PC/USB remote-control boundary; later manual separates Remote Studio download from SDK request | Clarifies which SDK was advertised and why it is a different interface | Original SDK headers/sample archive/API manual, or documentation for our on-camera libcapture bootstrap |

## Stock-app ownership and the lifecycle comparison

Parent extracted each embedded file from Metol's pinned
`raw8/gates/dicam/resume_chain_2026-10-16/scripts_and_units.txt`, verified its
displayed SHA256 against the embedded bytes, then compared those bytes with the
retained SM-R210 extraction. This checks content rather than accepting a displayed
hash on trust.

| Published NX500 file vs retained SM-R210 file | Result |
|---|---|
| `usr/lib/systemd/system/di-camera-app.service` | Byte-identical |
| `usr/lib/systemd/system/hibernation.service` | Byte-identical |
| `usr/lib/systemd/system/hib_post.service` | Byte-identical |
| `usr/lib/systemd/system/hib_post.path` | Byte-identical |
| `usr/bin/hib_tmp.sh` | Byte-identical |
| `usr/bin/hibernation.sh` | Different: NX500 snapshot device `mmcblk0p9`, SM-R210 `mmcblk0p8`; LED path `card` vs `led.2`; three whitespace differences |

The shared app unit starts the onboard executable, reads `/run/tizen-mobile-env`,
and has `Restart=always` commented out. The hibernation unit runs its preparatory
script. `hib_post.path` watches `/tmp/.hib_post`; its service orders itself after
the app and runs `swapoff -a`. In both main hibernation scripts the HIB export,
`hib_exjob -r`, and direct app launch lines are comments. These files do not show
which native code produces the markers or how remote capture state is retired.
An `After=` reference also does not prove the named unit exists or ran.

The retained `regpmon.sh:3–11` skips its listed process registrations while
either hibernation-capture marker exists. Its explicit process lists do not
name `di-camera-app`; this is not a census of other registrations or watchdog
users, and supplies no app-death recovery mechanism.

Metol's [online report](https://github.com/WApaperplane/metol/blob/cb82f4886c1174261cbba8127000c7b696944ff4/docs/current/CAMERA_ONLINE_PROBE_2026-10-13.md)
reports an NX500 restart freeze and app startup at boot+12.2s versus a mod hook
at boot+37.8s. Its hibernation explanation is explicitly tentative (lines215–227).
Later native call-site listings in that repository are third-party leads, not
independently validated SM-R210 instructions. Matching scripts do not transfer
the reported failure or establish its cause.

Our already accepted evidence gives the more direct ownership problem:

- `session.stock-poweron-create-before-script`: the followed stock power-on path
  attempts camera creation before its later card-script workflow.
- `session.init-failure-dropped` and `session.rtos-preference-shared-byte-offset`:
  the shared word at physical `0x85401268` can suppress process-local adapter
  initialization; it is set before that initialization and is not an ownership ACK.
- `session.ipcc-queues-are-shared` and `session.ipcc-open-release-ownership`:
  receive state is shared; opening/closing another descriptor does not claim,
  reset or drain the transport.
- `session.bridge-receiver-lifecycle` and `session.shutdown-incomplete`: checked
  teardown paths do not provide a demonstrated quiescence and recovery contract.

Thus a successful process kill or a closed fd would still leave an unproved
remote-state and initialization boundary. The comparison does not reduce this
to a conventional filesystem lock. No retrieved project closes that boundary.

## C200: keep the later bench results with the source

The C200 project's goal excludes the **Android phone app**, while its RVF
operation still depends on the onboard `di-camera-app`. Report10 describes an
in-process trigger and presents permission as the remaining uncertainty. Report11
then reports successful attach/detach probes but crashes during the listed main-
and worker-thread injected calls, including `getpid`; later app kills reportedly
rebooted the unit and cleared its temporary preload configuration.

At the pinned revision `tools/card/rvf-start.sh:2–15` instead describes a v13 USB
shell procedure and performs no injection. `rvf-inject.c:128–190` still implements
register modification and restoration; `rvftrig.c:61–74,97–115` implements the
app-guarded preload attempt. These are source implementations and historical
attempts, not a validated takeover recipe. USB transport and TTTS video-container
parsing are separate from a still sensor-RAW acquisition backend.

Two causal claims need particular restraint. The crash observations do not prove
all ptrace techniques impossible or independently prove syscall-restart
corruption. The report's watchdog headline also exceeds its decisive evidence:
lines145–155 describe a watchdog message and unsuccessful countermeasures, and
say the reboot happened faster or otherwise than a plain timeout. Neither is a
validated SM-R210 diagnosis.

The preload helper's claimed success check is weak: `rvftrig.c:77–92` searches
for the port string in `/proc/net/tcp` without verifying the socket state or
owning process. Its log saying LISTENING is not itself readiness proof. No
preload, injection, watchdog or persistent-rootfs procedure was adopted.

## nx500ex: control code and debug-file leads

`common.sh:43–52` sends stock `st` commands and sequences shutter keys;
`tools/nxkey/nxkey.c:28–55` writes input events. Other supplied helpers operate
GPIO/control paths, settings or stock media files. They automate existing
camera behavior rather than construct our own capture-library context.

The [23 October firmware note](https://github.com/NR2BJ/nx500ex/blob/fb7fe9bd3207cad16c1fd18f65d512ce8857e8f9/docs/research/2026-10-23-lut-firmware-analysis.md)
names `CCapAdjIf`, message `0xaa06`, `IPC_%04d.raw` and debug save/load modes.
Those are reported NX500 native relationships; this pass did not validate their
instructions or bind the numeric values to SM-R210. The supplied sources do not
provide a saved diagnostic RAW artifact demonstrating the claimed route, much
less caller ownership. The debug-file backend remains rejected for our goal.

The appended [25/26 October experiments](https://github.com/NR2BJ/nx500ex/blob/fb7fe9bd3207cad16c1fd18f65d512ce8857e8f9/docs/research/2026-10-25-lut-dump-analysis.md#L149)
report MCB refusal and a freeze after a sequence involving mismatched app mode
and a capture-stop command. The author's attribution is a hypothesis, not a
controlled isolation of either command. Later LUT reload successes are also
reported NX500 results, not a general safe-command set for our camera.

## LibRaw: a concrete conditional reuse boundary

Parent reviewed `samsung3_load_raw` at `src/decoders/decoders_libraw.cpp:937–1063`,
TIFF tag `0xa010` dispatch at `src/metadata/tiff.cpp:968–981`, the exact bit reader
at `src/decoders/load_mfbacks.cpp:600–634`, `get4` at
`src/utils/read_utils.cpp:60–78`, decoder flags at
`src/utils/decoder_info.cpp:24–31,339–342`, and allocation/call handling at
`src/decoders/unpack.cpp:315–449`.

The normal container path selects this decoder for compression `0x8005`.
The decoder itself consumes caller/parser-established dimensions, stream origin
and an allocated output plane; a TIFF parser is one provider of that context,
not an inherent requirement to wrap every possible input stream in TIFF.

| Logical header field | Bit offset | Width | Checked interpretation |
|---|---:|---:|---|
| Ignored initial fields | 0,16 | 16,4 | No magic comparison in this body |
| Depth code | 20 | 4 | `bit_depth = code + 1` |
| Ignored fields | 24,32,48,64,80 | 8,16,16,16,4 | Values discarded |
| Option flags | 84 | 4 | Scale, motion and difference-width coding gates |
| Ignored fields | 88,104,112 | 16,8,2 | Values discarded |
| Initial sample | 114 | 14 | `init_val` |

Parent summed the requests to 128 bits/16 bytes. The reader assembles little-
endian 32-bit words and consumes their high available bits; reset discards its
buffered bits. Each row aligns relative to `data_offset` modulo16. This is not
evidence that an opaque LRC descriptor has a 16-byte header or this packing.

The decoder writes reconstructed/clipped ushort samples in 16-pixel blocks and
has several dimension, mode and index checks; it is not a complete input validator.
`get4` initializes four bytes to `0xff` and does not test `fread`'s return. The
underlying stream can have its own error behavior. Samsung3's decoder flags
remain zero. Ordinary Bayer/single-channel unpack allocation reserves
`rwidth*(rheight+8)` ushorts, with malloc or calloc depending on the build flag;
the other metadata branch uses a zeroed four-channel temporary plane. None of
these allocations retains a camera DMA buffer.

Before reuse, establish our selected backing and actual payload extent, stream
origin/header, dimensions, depth, packing/predictors and row organization. Fresh
sensor provenance, request association, visibility and lifetime are independent
requirements. Existing LRC factory IDs/sizes, opaque hardware result words and
serialized descriptors do not establish those properties. No decoder code was
copied into our backend or run on camera pixels.

License notices were retained: Metol and nx500ex carry AGPLv3 text; C200 licenses
its original content under MIT with an explicit third-party-material caveat;
LibRaw identifies LGPL2.1 or CDDL1.0 and incorporated components. These are source
inventory facts, not a legal compatibility determination.

## SDK availability and what to do next

Samsung's [announcement](https://api.samsungmobilepress.com/press-releases/samsung-camera-nx1-firmware-upgrade)
describes a Windows-PC SDK for USB remote control. The locally retained later
manual, physical page218/printed217, separates downloadable Remote Studio from
requesting SDK sample source and its API manual. Neither source documents our
on-camera capture ABI. The actual SDK package was not found/acquired; this is
an availability gap, not evidence that it cannot exist or has no further value.

The highest-value next acquisition work remains `session.owned-initializer`:
recover a concrete clean-start or orderly handover that creates exactly one
receiver, supplies required local services, verifies readiness/failure and
retires remote state. Use the native SM-R210 contracts already recovered; merely
enumerating the known create-before-script path again would not close it.
In parallel, follow the selected nonzero LRC backing into the encoded first-image
descriptor, with actual extent and producer identity. The public decoder becomes
actionable only after that format/buffer boundary is established.

All device experiments cited above are external reports. No new native
instruction/decompilation claim or camera runtime claim was accepted in this pass.
Parent acceptance and worker qualifications are in the
review (not published: `../output/decompile/rea/pass-local-comparison-20261026/parent-review.md`).
