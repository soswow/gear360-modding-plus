> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Proposed one-photo stock SSIF dump trial

Status: ready for explicit user approval; **not executed**. This changes a volatile RTOS debug setting through the existing stock command. A user-operated normal power cycle after file completion is part of the proposed restoration procedure. No flashing, injection, service restart, arbitrary RAM/MMIO write or blind SSIF unit-test capture is proposed.

## Evidence making this a bounded next step

The confirmed ordinary shutter window changed actual C/Q/Core still routes. Its post-event5824×2896 input has stride11648, firmware Bayer format1, metadata pixel enum2/packing enum4/pack1, layout2/request22/kind1, size33732608 and four equally spaced planes. This matches the previously traced allocation profile, without identifying its exact factory/producer or proving backing lifetime. The2912×2896 output already has null primary planes. Existing optional DumpSize can export at a recipe callback rather than reading that cleared output afterward.

Stock `/usr/bin/st`, `libshell-command.so`, and `libmulticore-bridge.so` match the locally analyzed hashes. The SD card reports7,785,024KiB available. Current flags819521c4/c8/cc/d0 are all0; override81952130 is0. No current root `.raw` file was listed. Native source binds the candidate command to flagc4=0x10 at recipe construction. Actual installation/callback execution remains a test result, not a promise.

## Proposed sequence

1. Recheck usage and leave enough reserve for restoration/verification. Recheck process/build/code anchors, all four flags0, free storage and root `.raw` inventory. Preserve existing files; abort on mismatch or collision.
2. Send **one** documented stock command through recorded CGI:
   `/usr/bin/st cap seq dump on ssif`
   Read backc4=0x10 and the other flags0 before any shutter press. No repeated enable attempt after an ambiguous response.
3. Announce readiness and request **one ordinary hardware shutter press**. No agent-issued capture command. Observe process health, still metadata and new root dump files. The expected candidate data extent is33,732,608bytes, but acceptance must use actual descriptor/file evidence; a file name or ACK alone is insufficient.
4. Confirm new file writing has finished/closed; retrieve only the new trial files byFTP, preserving camera and Mac size/SHA256. Analyze firmware12/16 metadata against actual bytes, row/plane extent and Bayer evidence. Retain all results as unverified until provenance/freshness/lifetime/cache requirements are met. Do not delete personal media or overwrite existing dump files.
5. User performs a **normal power cycle once writes are complete**; restore connectivity and revalidate build/process/current addresses and all four dump flags0. Remove/verify absence of owned Linux helpers before the reset where possible. A timeout or unexpected state stops the trial; no autonomous service restart or substitute memory write.

The built-in synchronous dump wait can stall capture if its transport does not complete. A stall is a failed experiment, not proof of successful export. Do not promise a complete RAW acquisition or a firmware buffer lease from this trial alone.

## Why restoration uses a power cycle

**Do not use `dump off ssif` as an inverse.** Handler8027eb24 complements the selected mask then assigns a whole word; `off ssif` would writec4=0xffffffef, enabling unrelated bits. **Do not use `dump off all` as an exact restore either.** Its special local20 is not complemented, setter80570be8 writescc, and consumer802c5650 testscc&1 to install another optional callback. It would leavecc=0xffffffff instead of the observed0. Original literal bytes confirm `on/off/all/ssif`; original instructions and new REA records are saved. Proposed power-cycle restoration avoids both command traps and must itself be verified afterward.

References: RAW_SHELL_DUMP_ENABLE_AND_CALLBACK_LINEAGE.md; RAW_RECIPE_SELECTION_AND_COMMAND_GATES.md; RAW_CANDIDATE_ALLOCATION_AND_EXPORT_CONTRACT.md; evidence `pass-camera-resume-20261018/{dump-route-current-review.json,st-stock-main.evidence.json,dumpflags-special.evidence.json,dump-special-consumer.evidence.json}`. The standing continuation prompt authorizes read-only diagnostics; this temporary setting change and reset require additional user authorization.
