> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Read the whole selected buffer, passively

Objective: extend the successful fixed read-only reader to save the entire currently selected buffer to the Mac. This is an unverified memory snapshot, not a triggered camera capture or verified RAW acquisition.

## Read these first

- `preview-sample-reader/README.md` and its `preview_sample.c`, `start.S`, `build.sh`: working ARM helper, fixed guards, build command and limits.
- `INVESTIGATION_CHECKPOINT_20261016_PIXEL_SAMPLE.md`: latest evidence locations and unfinished questions.
- `RAW_LIVE_FORMAT_AND_LEASE_GATES_20261016.md`: metadata, packing and ownership distinctions.
- `../output/decompile/rea/buffer-format-independent-review/report.md`: enum2 selects12bits/pixel; proposed little-endian packed12 decoding. Sensor origin and frame coherence remain unverified.
- `CAMERA_NETWORK_APPROACH_20261016.md`: existing camera access.

The camera was accessible at `http://192.168.43.1:8888/cgi-bin/execute?rawcmd=...`; FTP at192.168.43.1:21 accepts root with an empty password. Existing request/FTP code and complete traces are in `output/decompile/rea/pass-completion-20261016/preview-sample-runtime/`. Check current connectivity rather than assuming the camera is still connected.

## Exact expected instance

Selected-image globals81952e50/fa4 must both equal82246000. Image fields: vptr8067a158, width1920, height960, stride2880, PDataa77bb000. Addresses are hexadecimal. Existing helper checks these before and after copying. Abort if any differ; do not follow unexpected pointers or silently widen scope.

Expected full extent: **2880×960=2,764,800 bytes**, exactly6754096-byte pages. Map that fixed ordinary-RAM extent read-only and copy into Linux-owned memory before exporting. Do not put a multi-megabyte buffer on an assumed-small stack. No MMIO, imaging command, mode change, device-memory write, service restart or firmware patch is needed.

## Transfer and validation

The existing helper prints hex and its HTTP runner caps responses at1MiB. Do not send a full hex dump through it. Adapt the helper to emit exact binary bytes only after successful postchecks; send diagnostics separately. Redirect output into a unique temporary file on the camera, then retrieve with FTP. This writes a temporary Linux file, not camera memory or configuration. Check exit status, exact byte count and camera/Mac SHA256. Upload only a hash-verified independently rebuilt helper. Remove the helper, temporary sample and owned directory afterward; verify unchanged camera-app identity and no leftover tracer/staging files.

Retain sources, host tests, ELF/build hashes, raw request responses, metadata, bytes and cleanup evidence in a new directory. Save as `UNVERIFIED-full-buffer.bin`; preserve existing artifacts. Test mapping lengths, guards, failures and output without executing ARM firmware on the host. Do not deploy or execute the firmware image.

Stable guards do not exclude concurrent DMA, reclamation or ABA. A full snapshot may be torn. Do not claim verified RAW, sensor origin, full native resolution or fresh completion. Current surface is1920×960; larger firmware defaults are separate. First inspect the complete image structure using the packed12 hypothesis and alternatives. Close any owned analysis sessions; no remote push.
