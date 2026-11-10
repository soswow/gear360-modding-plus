# Camera JPEG Exif and C DNG, 5 November 2026

T1/T2 implementation and validation record. The existing whole-frame colour
model remains unchanged: P2 as D65 is assumed; T3 owns further colour work.

## Native Exif source (parent review)

C0: question: where does stock obtain the actual exposure and ISO for each
still? Permitted inputs: pinned `libmm-photo.so`, existing source/format
reference; no native target execution. SHA256
`22d7d37f3d8e28234fbc450d0be959a2655500242a5986e43dc5ee593bfa9c46`,
`linux-mm-photo-elf` in `facts/coordinates.yaml`. Addresses below are **Ghidra
addresses**, ELF virtual address + `0x10000`. Fresh REA 6.7.0, Ghidra 12.1.3,
JDK21 through the repository's existing stdio bridge; no client configuration
change. Initial nonexistent `.so.0.0.0` target refused; corrected to `.so`.

C1/C2: `stCAPTURE_EXIFDATA` is an inline record independent of the basic
image descriptors and IQ MakerNote buffer. Selected fields, all little endian:

| Record offset | Width | Meaning | Stock use |
|---|---|---|---|
| `0x00`, `0x04` | 32 bits each | ExposureTime numerator, denominator, seconds | copied to `sImgExifInfo+0/+4`; stock diagnostic explicitly labels them exposure time |
| `0x6c` | 32 bits | ExposureProgram | copied to `sImgExifInfo+0x78`; labelled exposure program |
| `0x70` | 32 bits | ISO speed | loaded and narrowed to Exif SHORT at `sImgExifInfo+0x9e`; labelled capture ISO and ISO speed rating |
| `0x84` | 32 bits | WhiteBalance | narrowed to `sImgExifInfo+0x7e`; labelled white balance |

C3: proven static edges:

1. `ImageGenerator::captureDataReceiver` (`0x9a388`) copies `0x2d8`
   bytes from incoming image-set **+8** into its queued `DataFromCapture`
   record at +`0x10`. Decisive instructions:
   `0x9f414 add r2,r3,#0x8`; `0x9f418 mov r3,#0x2d8`;
   `0x9f424 cpy r2,r3`; `0x9f428 bl 0x3dddc` (memcpy thunk).
   REA `ev_76366aeb9840ef51839e6e876e4559d702e92617a615c5fa59aa1eb6eb878a94`
   (instructions), `ev_1dc177727829885fb6fb674beec04770bead6dcb0e879f7b5fa9cbb99793dc14`
   (pseudocode).
2. The queue consumer obtains the queued front, adds `0x10` and calls
   `CaptureDataParser::convertExif` via its PLT thunk at `0x3a740`:
   `0xb0b28 add r2,r3,#0x10`; `0xb0b3c cpy r1,r2`;
   `0xb0b44 bl 0x3a740`.
   REA `ev_89e1970b1a1ff7ff1c69da15630c7005c6af91e75b3de82bfcdbcc54d13614ee`.
   Incoming refs distinguish the thunk from implementation `0xdcbbc`;
   bounded queries `ev_0ce972d45ae20010fada6bac09f3390e598802699caf03003e0e241e92e740ad`
   and `ev_72d90393cfc5addf788cc8ef60f45615acbd9d5c3ae1a8943d9a5ed2d3f452f5`.
3. `convertExif` uses the record offsets above. In particular,
   `0xdcf94 ldr r3,[r3,#0x70]` -> narrowing ->
   `0xdcfa4 strh r2,[r3,#0x9e]`. Exposure numerator/denominator are
   passed from record +0/+4 into the first pair at `0xdcbd4..0xdcc04`.
   Its diagnostics explicitly identify the semantic fields; no AE-record
   correlation or manual TV approximation is needed.
   REA `ev_dd0554dd2cb3cc61b4031102b9763485089a0aa15aadd0f56461d528b6893dcc`
   (instructions), `ev_61bcd3b3adcb0222a6b28ae488ad1e9f8e0428aeacc234bf314ea544bb0d9d1a`
   (pseudocode).

C4/C6: accepted **static** layout/source evidence, parent checked the decisive
instructions and offset arithmetic. Image-set +8 +`0x2d8` ends at `0x2e0`,
inside g360cap's existing `0x2e4` readability guard. This is independent of
and does not expand the native RAW-memory mapping contract. Requested manual
parameters are not proof of actual exposure. Runtime comparison is recorded
below separately. The source of all remaining Exif fields and the full
MakerNote tail are not established by this bounded trace.

Complete REA responses, provider logs and the closed session snapshot remain
local in `output/t1-t2-20261105/`; nine retained Evidence records were saved
in `photo-snapshot.json`. No RTOS analysis or firmware modification occurred.

## Output design and consecutive capture

`g360_dng_header` computes only 588 metadata bytes. `g360_save_dng` writes
those bytes followed by the already owned 33,732,608 sample bytes. It allocates
and copies no second frame, and does not scan the pixels or call the SDK.
The JPEG builder similarly inserts APP1 without copying the entire JPEG.
All formatting and IO happen after capture has returned its owned result.

The CLI saves synchronously and fsyncs each file before success. A later batch
API can hand retained results to a bounded output worker, then free them only
after completion, with explicit queue capacity, backpressure, errors and final
drain. That work belongs with the batch/result-ownership API. Simply moving IO
into the capture callback would prolong the RAW hold. No unbounded async
queue or extra per-shot pixel allocation is introduced here.

Initial JPEG scope: IFD0 and Exif IFD, actual exposure/ISO/program/WB where
available, F2.2, 1.2 mm / 6 mm equivalent, sRGB and the four IQ record tags
(AWB twice, AE, colour). No MPF second image, IFD1 thumbnail, orientation
angles, Samsung UTC trailer or unknown MakerNote tags. The date can be supplied
to the pure builder; CLI dates remain omitted because the per-shot date source
has not been established. `--exif off` retains bare encoder JPEG output.

## Host and camera results

Host `g360cap/tests/run.sh` passed: existing capture/RAW/error tests, C++
linkage, copied Exif lifetime after SDK overwrite (including requested settings
that deliberately differ from native values), pure format and IO tests.
DNG tests compare a full synthetic file and 32 per-shot matrix/gain variants
with Python, plus 20,046 exact-double rational cases and malformed inputs.
ASan/UBSan cover bounded IQ reads, output guards, mutated inputs and partial,
zero, failed and oversized writes, failed fsync/close, and refusal before file
truncation. Exif tests use a separate Python layout builder and existing stock
MakerNote fixture, validate optional fields and real encoded JPEGs with
ExifTool, and compare every IQ record. The freestanding ARM build passes with
only the existing camera libc/libgcc dependencies.

Four camera sessions, all **reenter on boot 1793781685**, stock app inactive:

| Session | Build prefix | Count | Output | Result |
|---|---|---:|---|---|
| T2rawA | `449c0bbb` | 3 | frame.bin, bare JPEG | all passed |
| T2dngA | `449c0bbb` | 3 | camera DNG, bare JPEG | all passed; DNG Python parity / ExifTool OK |
| T1auto | `b7eff2ef` | 2 | camera DNG, Exif JPEG | all passed; 1/100s ISO125 (Program AE) |
| T1manual | `b7eff2ef` | 1 | camera DNG, Exif JPEG | all passed; requested TV32/ISO400, recorded 1/4s ISO400 |

Each DNG is **33,733,196 bytes** (588 + 33,732,608). All six equal
`make_dng.py` byte for byte from their own sample strip and same-shot
MakerNote. All six validate; the first DNG of each DNG session renders through
macOS `sips`. Visual inspection shows both fisheye halves and plausible chart
colours; this does not replace T3's raw-developer/illuminant validation.
All nine final-build JPEGs (main, screen, thumbnail for three stills) validate
without warnings and their four IQ values equal their own sidecar records and
decode through `stock_iq.py`. JPEG entropy/payload bytes remain unchanged.
All nine captures returned status0, full frame flags0x103, and capture-ready;
the logs show 36 successful native buffer returns. Every fetched camera file
was checked against its on-camera SHA256 by the existing trial runner.

Final ARM executable SHA256:
`b7eff2ef63d738795ab67d290d7c23a4fa61aef9c142dd34f975646180840adf`.
The first timing build `449c0bbb3a815821cbab7fcf08da2a56cba61e2bf1f4c5fd61edf864a6aea2a1`
used Exif off (its provisional Exif builder was never invoked). Only the
reviewed replacement Exif implementation is accepted; final build has both
features validated together.

### Consecutive-shot timing

Three consecutive auto shots per format, zero requested gap, same timing
build/settings, JPEG save, Exif off, library logging synced. Per-shot metric
`save_us` includes **all JPEGs, MakerNote, frame output and ordinary logging**;
this is not an isolated DNG encoder microbenchmark.

| Mean milliseconds | frame.bin | DNG |
|---|---:|---:|
| Capture | 1340.9 | 1340.4 |
| Save/log phase | 2592.1 | 2529.5 |
| Total | 3933.1 | 3870.0 |
| RAW hold | 601.2 | 608.5 |

This small serial comparison shows no DNG penalty; it does **not** establish a
DNG speedup or isolate header computation from storage variability. Output
currently dominates the per-shot cycle. Deferred saving can overlap work with
later captures for finite bursts; sustained output is still constrained by
storage throughput. Each retained frame costs about 33.7 MB, so any future
batch writer needs bounded memory/backpressure, explicit errors and drain.

### Local retention and cleanup

Camera artifacts/logs were copied and byte-verified into
`output/t1-t2-20261105/camera/{T2rawA,T2dngA,T1auto,T1manual}/` (87 files).
`camera-manifest.json` lists every local size/hash. Validation JSON, timing
rows, host-test log, REA responses/snapshot, build and cleanup journals stay in
that local output directory. Large pixels, JPEGs, full native responses and
agent streams are not versioned. Representative checksums below bind this
compact record to the retained originals.

| Artifact | SHA256 |
|---|---|
| `T2rawA/shot-001-frame.bin` | `39ed51342f758db7d6bf8ca503a66f48cce01ffe3bc322deacdbe03b591ed1cc` |
| `T2dngA/shot-001.dng` | `7dac85a3c52c9150cc27322a881ae815adcf335b0edbe4bd632a2bd94614248c` |
| `T2dngA/shot-002.dng` | `bd644366c1782c7af250e3c3232d989296be479ca4327a51c8abc3e4a16e7565` |
| `T2dngA/shot-003.dng` | `233a6252346ce6117a5bfc044092874cf8b47f740b4dd9cb0e8d23b07649613d` |
| `T1auto/shot-001.dng` | `ae7e2ddf75077f42d82ef48843d3adf8d7a9c2a3952ac44d2e77d57ff3a1040a` |
| `T1auto/shot-002.dng` | `95e51f9b6a453c76591ad9bf1d048c8a6edd3731ef34ab4d60105c4a43394106` |
| `T1manual/shot-001.dng` | `d603b292d6a978c2b16d27e366e6aa2162a16486e4b74e626b80d72384436532` |
| `T1auto/shot-001-main.jpg` | `48a7db8ed7ac76c03b18cdef0c9d58e5fa5c3449fab920d1a5eaf47ceb0ad92b` |
| `T1manual/shot-001-main.jpg` | `8bf1cbdd6bd8211f4ebddfb93687e55a5c6c5d53537ea74552eeeedd5c4b68fc` |
| `T1auto/shot-001-makernote.bin` | `2e36bd7f25cae95cc318b4981c82bdff7d89b43afe4b9dcb58bdaa9a1ad4673e` |
| `T1manual/shot-001-makernote.bin` | `775f7288cee38f5f92a44d6727c3de124b069b8b3b06e32a07759f6b6dc1f944` |

Temporary owned `/tmp/dng-*` runners were removed only after all successful
fetches, no active g360-shot and unchanged boot were checked; the SD
`exp-dng-*` trial folders remain for the owner's cleanup decision. The first
cleanup request had newlines stripped by camsh and was rejected as shell
syntax before executing; the corrected single-line request and explicit
absence check are retained in `camera-cleanup-retry.txt`. No firmware change,
stock-app start/restart, power cycle or arbitrary device-memory write occurred.
REA session exported/closed; owned bridge and workers ended. Parent review
accepted the native layout, reviewed both format modules and independently
ran all validation before local integration.
