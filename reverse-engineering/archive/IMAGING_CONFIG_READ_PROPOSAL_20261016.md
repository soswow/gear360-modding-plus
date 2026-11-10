> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Proposed read-only imaging configuration check

**Subsequent approved execution:** the user approved this exact check; [one live snapshot](IMAGING_CONFIG_OBSERVATION_20261016.md) completed successfully, and the helper was removed. The preparation text below records the original proposal. No capture or device write occurred.

This is a prepared next experiment, not a camera operation already performed. The completed RAM checks are recorded in [live observations](RAW_LIVE_RAM_OBSERVATIONS_20261016.md), local commit `5a44513`. The user's approval there covered the temporary ordinary-RAM reader. This separate device-register reader has not been uploaded or executed.

The next useful question is: **what WDMA output geometry, format field and source-selection bits are currently programmed?** The selected C++ image says 1920×960/stride 2880, whereas the independent debug provider defaults imply stride 3840. Reading configuration can compare software state with hardware programming before any capture attempt. It cannot establish sensor sample packing, a new DMA completion, a buffer lease or RAW pixels.

The final proposed reader accepts only `ssif-config` and reads these three aligned 32-bit words, once each, through one fixed read-only `/dev/mem` mapping:

| Physical register | Existing firmware read evidence | Intended observation |
|---|---|---|
| `0x20800024` | Original `80512284` and `805122d4` load/modify/store this same word. | Preserve full word; inspect low-three-bit output selector separately from bits 12–13 OTF diagnostics. |
| `0x20800340` | Original `80511e3c` loads this word and replaces bits 12–21. | Current output configuration/format field; no RAW label inferred from a numeric value. |
| `0x2080034c` | Original `80511eb4` loads this word and replaces its high/low 16-bit fields from two arguments. | Current programmed output dimension word, interpreted only with the retained caller contract. |

Parent independently compared these instruction windows with the original p6 image and byte-identical carrier, then decoded them using ARMv7/Cortex-A9 LLVM. Routine firmware reads provide a stronger basis for this limited snapshot than configuration-write evidence alone. This is source evidence, not a hardware specification or a guarantee that the device's current clock/power state permits access.

The earlier eight-word draft is retained as a rejected proposal. The final reader omits the BF size registers and output-pointer/stride registers because this bounded review established writes but no ordinary readback for them. It omits all interrupt/status/acknowledgement words, including `20800018` and `20806028/2c`. It performs no register writes, cache operations, pointer following, camera command, mode change or capture. It does not call the firmware helpers shown above.

For an authorized trial, parent would upload only the independently rebuilt hash-verified binary into a new unique `/tmp` directory, retrieve/compare it, verify camera-side hash, test invalid/no-argument startup without opening `/dev/mem`, then invoke the one fixed profile once. Preserve raw output and exit status; stop on an error rather than widen the address set. Delete the helper/directory and verify unchanged camera-app identity and no tracer/staging leftovers. No retries, capture or writes are implied.

Mocked host tests must verify exactly these three volatile loads, the single fixed page, read-only flags, argument rejection before open, output correctness and failure cleanup. Actual device-read behavior remains untested. Sources, build/test evidence and the parent byte review are preserved with this proposal. The overarching RAW-file goal remains unfinished.

The reviewed source is [mmio_config_reader.c](../../tools/probes/mmio-config-reader/mmio_config_reader.c), built by [build.sh](../../tools/probes/mmio-config-reader/build.sh). Parent rebuild is byte-identical to the worker executable: **4216 bytes**, SHA256 `95817293277899509a87270d02a246870b7986604e8f6e425e471ef50640cf22`. Static ELF checks and the host contract tests pass. Frozen proposal evidence (not published: `../evidence/pass-runtime-20261016-mmio-proposal.tar.gz`) and hash index (not published: `../evidence/pass-runtime-20261016-mmio-proposal.index.json`) retain the reviewed and rejected drafts, tests, build metadata and independent source-byte windows.
