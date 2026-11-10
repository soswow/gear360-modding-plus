> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# A finite route to the first verified RAW file

**Live progress, 16 October:** [approved fixed RAM observations](RAW_LIVE_RAM_OBSERVATIONS_20261016.md) validate two source anchors, selected software geometry and callback activity. The debug image remains unconfigured, and allocator free count passes only the aggregate 64 MiB threshold. Capture, MMIO and writes remain outside this read-only helper authorization. The historical offline plan below remains a decision framework, not a capture approval.

The objective is an actual file containing sensor RAW pixels, with reproducible acquisition and verified provenance, geometry/stride, packing, fresh completion and safe buffer lifetime. No file has met those criteria. This plan is an offline deliverable: **no camera access, capture, restart, hook or deployment has been performed or authorized by this plan.**

## Preferred first experiment

Investigate the stock **FrontendNx liveview SSIF debug capture** before spending another pass mapping broad recipe infrastructure. It has a compiled direct command candidate, one identifiable image/backing pointer, explicit WDMA programming, a dump call and a filename algorithm. It bypasses the Still recipe and YCC activation gates. The recovered candidate spelling is `st cap fenx lv rtmem lv ssif`; details and evidence are in [the stock debug report](RAW_STOCK_SSIF_DEBUG_CAPTURE.md). This is a hypothesis for a future authorized trial, not an instruction to execute it now.

The Still recipe remains the fallback for full-resolution acquisition. Its conditional factory binding is now `recipe1 → key27 decimal/0x1b → factory0x802b3b90 → builder0x802ba7b4 → MID0x15000, selector0x28`. Actual R210 invocation and the parameter predicates remain to be observed. Neither route requires us to reconstruct all firmware C++ classes before trying to obtain a file.

## Four decisions, then stop expanding static scope

| Decision | Existing evidence | Decisive observation or remaining work |
|---|---|---|
| Can the stock debug entry run on this build? | `fenx` registration, `lv/rtmem/ssif` parser and exact handler recovered. | Verify running build and registered entry, current sensor/liveview configuration and storage health. The requested provider geometry must agree with the current input. A JPEG/control observation should establish the ordinary camera path is healthy. |
| Does P contain a completed, fresh sensor frame? | `ssif` selects low-three-bit mux value1; address/stride/dimensions reach WDMA. Wait is SSIF VdFalling, not established WDMA completion. | Bind one submission's image/P and programmed geometry to a newly completed WDMA output and its actual SSIF source. Establish the stop/drain behavior. Correlate frame/interrupt timing and reject timeout, old completion, test pattern or competing writer. |
| Will the file copy use the same allocation safely? | FileDump uses P and S*H, then the selected cleanup branch attempts release without clearing the globals. The allocation caller has no failure/sentinel guard. | Establish that a valid owned allocation will reach submission, with an observable stop/abort condition for failure; if this cannot be established, do not use the stock command unmodified. Retain that allocation through copy return and observe or prevent bulk cleanup/reuse. Do not blindly repeat the command. A nonzero stale global is not an owned run. Validate address mapping and cache conditions for that exact allocation. |
| Did an actual file arrive, and what is in it? | Suffix naming and Linux write path recovered. | Check the newly created file's exact name, byte count, hash and sample content; record the request/P/geometry with it. ACK is insufficient. Derive packing from bytes and source configuration, verify Bayer/sample behavior with controlled scene/exposure changes, then reproduce the result. |

## Smallest useful evidence package

For one eventual attempt, preserve a record with build hashes, command and timestamps; current SSIF/provider geometry; the debug image pointer; allocation tag/run identity and P; submission registers; fresh completion and stop results; FileDump P/length/name and copy entry/return; allocator release/reuse/bulk-clean ordering; final file size/hash; and the pixel-layout analysis. Use available diagnostics first. If obtaining a missing item requires instrumentation, define a bounded hook with explicit ownership and a rollback before seeking separate deployment authorization. Do not pretend that existing logs expose fields they do not expose.

At the initialized defaults for **provider key0 → settings row10**, the candidate is 1920×960 pixels with derived stride3840 and requested extent3,686,400 bytes. The name contains **stride×height**: base `fenx_lv_rtmem_3840x960.raw`, normally suffixed `_001.raw` for the first available name. Runtime geometry can differ; those numbers must never substitute for observations. No Bayer phase, endian/alignment rule or RAW bit depth is accepted from the extension or metadata alone.

Sample analysis should compare candidate packing hypotheses against the actual row byte count, value ranges, bit occupancy, padding and spatial continuity. Compare dark, illuminated and colored scenes and exposure changes for fresh scene response and a plausible color-filter mosaic. These checks help reject stale data, processed YUV, test patterns and wrong decoding; they do not replace the source-to-buffer provenance evidence. The separate OTF diagnostic is not the acceptance method.

The repository already has the offline [C++ inspector/splitter](../raw-buffer-prototype/README.md). Preserve the original buffer unsplit. The common FileDump descriptor can be inspected, but the splitter's half-row front/rear contract came from a different SSIF writer; it is not established for this debug route. Use splitting only after confirming that layout. Successful parsing cannot independently establish sensor provenance.

## Exit rules

- If the stock path yields correctly sized but unverified bytes, preserve them as a **candidate**, analyze them offline and close only the specific remaining provenance/packing questions.
- If stock completion or retention cannot be demonstrated, stop trying repeated stock commands. Prepare one bounded copy/pin solution whose allocation cannot be released or reused until copy completion; both ordinary release and bulk cleanup must honor ownership. Build/review it offline before requesting live authorization.
- If the debug entry is unavailable, use the now-bound Still factory path and observe its guards; do not reopen numeric factory-map searches.
- Mark the overarching goal complete only after sensor RAW pixels have actually been saved and the acquisition is reproducible. A checkpoint, a command response, a `.raw` extension, a semaphore post, or a credit cutoff does not satisfy it.

The next camera-state-changing step requires separate user authorization because this investigation explicitly retains offline-only boundaries. That authorization should identify the exact proposed trial and any diagnostics/instrumentation; it is not implied by the static command recovery.
