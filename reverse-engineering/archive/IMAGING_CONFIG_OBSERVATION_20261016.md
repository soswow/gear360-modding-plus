> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# One approved imaging-register snapshot — 16 October 2026

The user approved the [prepared three-register check](IMAGING_CONFIG_READ_PROPOSAL_20261016.md). Parent uploaded the exact reviewed 4216-byte executable, verified an FTP round-trip comparison and camera-side SHA256, tested no-argument startup without opening `/dev/mem`, then invoked `ssif-config` **once**. It exited 0 and returned exactly the three allowed words. No capture, device-register write, status/acknowledgement read, pixel read, mode change or service restart occurred.

| Physical register | Exact returned word | Source-bound field extraction |
|---|---|---|
| `0x20800024` | `0x00000010` | Low three selection bits = 0; separate OTF bits 12–13 =0. |
| `0x20800340` | `0x00680102` | Bits 12–21 =`0x280` (640). No high-level pixel-format label is established here. |
| `0x2080034c` | `0x078003c0` | High 16 bits = 1920, low 16 bits = 960. |

The last word's two dimension fields agree numerically with the earlier [selected software image](RAW_LIVE_RAM_OBSERVATIONS_20261016.md), which reported 1920×960. These were separate observations, not an atomic software/hardware snapshot. The register snapshot did not read hardware output pointer/stride, engine-enable state, sensor input geometry or completed pixels; it cannot establish that the register group is currently writing that particular image.

The stock SSIF debug start helper statically writes low-three-bit selection 1. The observed value 0 therefore differs from that debug selection at this moment. This does not prove that no RAW-capable path is running elsewhere, nor identify the physical sensor. The separate OTF diagnostic bits must not be substituted for this WDMA selection. The format-field number 640 is retained without assigning RAW/YUV/Bayer semantics.

The helper and its unique temporary directory were deleted immediately afterward. Post-check app PID 254 and starttime 1042 matched the pre-check; all 21 threads had TracerPid 0. Neither RAM-reader nor register-reader staging directories remained. The battery reported 100% and 39°C. The root SD-card `.raw` glob returned no entries; it is not a full-card search.

## Evidence and next boundary

The response and exit marker are preserved verbatim with UTC client request timing. The response SHA256 is `451aa94fb95c89914d76d488b61ed1b771e93683ee7f3653d17ef9e815374848`. Executable SHA256 remains `95817293277899509a87270d02a246870b7986604e8f6e425e471ef50640cf22`. A parent audit verifies exact register set/byte decoding, all request-file hashes, upload identity, cleanup and process/tracer checks. Independent original-byte RMW windows are copied into this milestone; the earlier proposal and RAM bundles remain unchanged.

Versioned runtime evidence (not published: `../evidence/pass-mmio-runtime-20261016.tar.gz`) and member/hash index (not published: `../evidence/pass-mmio-runtime-20261016.index.json`) preserve the complete observation and audit. Working copies remain under `output/decompile/rea/pass-mmio-runtime-20261016/`.

The read-only approach now reaches selected software metadata and these hardware configuration words on this camera. The remaining acquisition question is a controlled sensor-output submission with verified input packing, fresh DMA completion and retained backing through export. Neither this snapshot nor aggregate free memory resolves the stock debug command's wait/drain/allocation hazards. No sensor RAW file was produced. The single approved register invocation is complete; it does not authorize capture, writes, additional addresses or further invocations.
