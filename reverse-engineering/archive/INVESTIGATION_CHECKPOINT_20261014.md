> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Investigation checkpoint — 14 October 2026

The corrected resource-bound pass stopped at **90% used / 10% remaining** in the five-hour account window. It resumed at 25% used. These are account allowance percentages, not an exact per-task token count. No reset credit was consumed.

Five Luna workers performed focused tracing and independent review. All workers finished; their private sessions were closed. A final connected REA `binary_session` check returned `open:false`. The [reviewed findings](RAW_FACTORY_FORMAT_AND_SYSCALL.md) and evidence index (not published: `../factory-format-syscall-evidence.json`) preserve 20 selected source-byte windows, 13 cited REA evidence records, 76 native snapshots, and hashes for 152 retained analysis artifacts. Full files remain locally under `output/decompile/rea/continuation-20261014/pass5/`.

The static results establish the provider's RAW metadata, default geometry behavior, recipe/global-manager/concrete-factory path, material/pre-callback pointer identity and pool-backed descriptor update, and SVC registry allocation/registration contract. Actual sensor RAW pixels, active R210 graph, live dimensions, SSIF receive-buffer identity, and operation-specific cache effects remain unresolved.

Continue in this order, adapting when evidence warrants it:

1. Resolve pool `0x1c`: `0x8025d9c0(0x16)` → getter `0x80086b20` → manager virtual `+0x20`. Bind the concrete target, pool setup, returned pointer, and lifetime to the actual receive controller.
2. Resolve selector `0x010c0311`: record 17 at `*(u32*)0x814482d4 + 0x280`, callback field `+0x0c`. Registration entry `0x800211d8` receives `(17,descriptor)` and installs descriptor word `+8`; pointer-table cell `0x80023f40` references the entry, but its consumer and descriptor are unknown.
3. Find actual SSIF address/stride/length programming. `0x801ddc20` is IPC SRA output; the named VSYNC event and PP-EOF diagnostic do not prove a frame transfer.
4. Establish R210 recipe activation and later geometry writers. Role `0x13` is provider-array index 19; settings rows 25/26 have zero defaults. Do not substitute an assumed sensor resolution.

Two one-time heartbeat restarts in this same chat are scheduled for **15 October 2026, 00:40 and 05:40 Australia/Sydney**. IDs: `gear360-investigation-00-40-restart` and `gear360-investigation-05-40-restart`. Their prompts resume the latest checkpoint, use Luna tracing with parent review, poll usage, and stop each authorized pass at approximately 90% used. If already at that boundary, they save and stop. They may create a fresh resource-bound goal after this pass's goal is complete.

Preserve original captured binaries and unrelated workspace changes. Keep work offline: no camera commands, firmware execution, app/service restart, preload, or deployment. Save source-bound traces and closed snapshots, distinguish facts from inference and unknowns, and commit validated task artifacts to local master. Sensor capture remains a separate unfinished objective.
