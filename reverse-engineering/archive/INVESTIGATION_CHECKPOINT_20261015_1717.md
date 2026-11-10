> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Investigation checkpoint — 15 October 2026, 17:17 Sydney continuation

User-authorized continuation started 06:17:37 UTC (17:17 Sydney), from local master `571fc0e`, with both five-hour and weekly usage refreshed to 0%. Nine Luna workers supplied focused traces, review or evidence tooling; the parent checked original bytes, reviewed interpretations and integrated findings. Stop target is approximately 96% five-hour used, leaving 4%. Closing measurements and audit counts follow below after validation.

The overarching goal remains **capture verified SM-R210 sensor RAW pixels into a file**, with reproducible acquisition and verified provenance, geometry/stride, packing, completion and buffer lifetime. **No verified sensor RAW pixels have been written to a file.** This is a checkpoint, not goal completion. The goal API still reports the earlier reserve-related blocked state; explicit user continuation authorized this work, and no replacement resource goal or completion claim was made.

## Advances and corrections

- Closed the pending-register callback consumer: 0x80038fb0 reads 0x20800018, scans table 0x8144a780, invokes key6 callback 0x803118c0, and writes the one-hot acknowledgement to 0x20800020 after return. The software route reaches PP_WDMA0 manager ID3 and frontend event mask8. Silicon bit semantics, exclusivity and frame freshness remain unverified.
- Resolved frontend task identity: the final derived vptr replaces a temporary base vptr at C+0x210; its thunk adjusts back to the same complete frontend object and runs t_Main. This binds the task/event/listener instance statically. Initial kernel event-pattern semantics and per-frame old-bit clearing remain open.
- Found actual material +0x10 invocation: assistant cases1/2 → CProcedure mode runners → state3 material → 0x80250ca0, state4 and virtual +0x10. The candidate table resolves to 0x802e508c, invoking table updater and start wrapper. Independent review validated original instructions and object levels. CCook constructor/destructor labels were corrected.
- Closed the conditional geometry-mode chain: C+0x204 constructor default is zero; candidate rows through the secondary copier select Core mode4 and primary +4/+8 dimensions, initialized 5824×2896 with stride11648. Alternate dimensions initialize to zero on this constructor/copy path. Live geometry and later aliases remain unobserved.
- Extended the compiled frontend factory startup chain through dispatcher command0x01070001 to createFrontEnd. A raw candidate task record points at the outer routine, but record format/consumer and actual scheduling remain unknown. Corrected an interior BL previously treated as initializer entry.
- Resolved two manager pointer cells to the same constructed CManager receiver. Its +4 BranchManager and +8 Fridge remain distinct; BranchManager virtual +8 to the specific MID builder and active recipe identity remain unbound.
- Recovered RTOS request-record parsing through global seq / child dump to the flag setter. The reconstructed argument slots can write 0x10, allowing optional recipe-time export registration. External transport, unidentified prefix, loop activation and actual runtime flag remain unknown; no shell command was invented or run.
- Corrected RDMA nested fields to frontend C+0x64/+0x68, separate from material tag3 C+0x14/+0x18. No sensor receive/DMA destination or pixel provenance was established. Lifetime synthesis confirms a synchronous FileDump ACK is not a pixel-buffer lease; completion virtual +0x18 and reclamation ordering require more work.

The [reviewed findings](RAW_INTERRUPT_AND_RECIPE_EXECUTION.md), [component map](BINARY_COMPONENT_MAP.md), and [evidence identity index](../interrupt-recipe-execution-evidence.json) bind these conclusions to exact reports, immutable inputs and retained REA records. Full local traces/carriers/source windows/snapshots are under `output/decompile/rea/continuation-20261015/pass9/`. Historical worker conclusions superseded by newer traces are explicitly disposed of, including the lifetime synthesis's former +0x10 caller gap.

## Validation, boundaries and integration

The reusable offline evidence validator was committed as `cb2db9b`. It checks carrier/source hashes and bytes, source-window bounds/hashes, retained record IDs and mapped REA byte reads; empty/unmapped reads are reported separately. Negative fixtures check malformed JSON, invalid coordinates and corruption. Snapshot file/schema counts are not function counts or independent native-exit proof.

All work was offline: no camera commands, firmware execution, service restarts, preload or deployment. All workers finished and report their owned native sessions closed. Connected REA independently reports open:false, no provider binding and no analysis run. Exact-PID follow-up found cleanup-warning PIDs 53295, 54103 and 57840 absent; the sysctl ownership-token warnings remain recorded, with no process killed and no unsupported claim of independently certified historical exit.

Validated artifacts are committed directly on the existing local master checkout. Unrelated ledger draft edits and untracked captures/projects remain preserved; no remote push occurred. No acquisition prototype was changed or executed in this pass.

## Next evidence needed

1. Concrete BranchManager factory target, recipe/procedure identity and active R210 configuration.
2. Completion virtual +0x18, actual event producer and frame/instance identity; distinguish software state completion from fresh DMA.
3. Actual sensor/SSIF receive destination, input mux/configuration and transfer provenance for the exported allocation.
4. Backing ownership/pinning and export/copy-before-reuse ordering, plus remaining cache/syscall semantics.
5. External dump-enable request delivery before recipe creation. Actual samples and packing validation require separately authorized live work; continue offline until then.

Final evidence validation: **32 carrier manifests / 252 source-matched segments**, **172 source windows**, **684 retained Evidence IDs**, **45 structured snapshot files**, of which **31** have names recognized as closed snapshots. REA reads: **110 source-matched**, **2 unmapped exclusions**, **4 empty**, **zero mismatches/errors**. These counts describe retained records, not functions or acquired frames. Prior pass8 regression after schema extension also passed: 39 carriers/344 segments, 3 boot manifests, 332 source windows, 104 mapped reads and zero errors.

Freeze audit resolves all **32** main-report citations: 31 in the current-pass validation (one is a byte-identical retained pass7 record) and one in the immutable pass8 validation. Two stale citation IDs were replaced with their actual retained records before freeze. Independent review checked the conditional mode4 path, material interface and export token ABI; its correction history is preserved. Deliberately corrupt fixtures are explicitly excluded from normal validation and remain available for negative checks.

## Closure addendum

Main findings were integrated on local master as **d1bb0b4**, following validator commit **cb2db9b**. The index's **171 file identities** independently matched current artifact hashes; all eight deliberate negative cases were rejected. The frozen report set comprises 28 reports, 4 parent audits, 32 carrier manifests, 31 closed-named snapshots and 10 window manifests, with no stale artifact hashes.

A final bounded parent byte/disassembly check located completion virtual +0x18 precisely: table cell **0x806a1f88 → 0x802e5c74**. Its zero-gate branch builds a stack record and calls **0x8031e490 → 0x8031e630**; both branches converge through **0x802e587c** and **0x805770c8**. These are next trace targets, not proof of IRQ waiting or fresh completion. The addendum is preserved in `output/decompile/rea/continuation-20261015/pass9/parent-completion-followup/`; its **five additional source windows** independently validate with zero errors. This is separate from the main frozen counts, with no new REA record/session.

The expanded manager instruction view places 0x80520160 calls at 0x8031e774/0x8031e794 with manager+0x10/+0x0c respectively, and 0x80520378 on manager+0x0c. The condition represented by those synchronization subobjects remains unknown. Trace 0x8031f958 / 0x8031feac and the signal producer before calling this an IRQ wait.

The synchronization helper prefix loads the subobject token and calls 0x80518014 with arguments token/1/0xffffffff. Its kernel operation and signaller semantics remain unknown; this is a bounded prefix, not a complete-body or fresh-frame claim.

- Closure addendum `completion-gate-and-post.llvm.txt`: SHA-256 `8f53c1b2242c6190ffc99ba421ac1b9fdd5ee5b09d4bbfedd406a937e012b30e`.

- Closure addendum `completion-manager.llvm.txt`: SHA-256 `812eddd7d87866e9d9f1ddec6f503f9b0fde932cf9839a7cb40604927b58d023`.

- Closure addendum `completion-slot18.llvm.txt`: SHA-256 `f6c7c7120628a1996a48b568f0a10ce0dc76b9db25047ebec2452d4b58727c4c`.

- Closure addendum `report.md`: SHA-256 `ad7aee8d16188e179fea4cd8384191048593e3c865f718c9ae81d3dbefe3a5f9`.

- Closure addendum `source-byte-windows.json`: SHA-256 `fb1adfc2bfe2a8372fb5324395e9d70f676c6916c14ddf18b26ee517384f32d1`.

- Closure addendum `synchronization-helper-prefix.llvm.txt`: SHA-256 `560a8211101f887f8ff66b8f3c383e15acc8b3349937feed7d66fca2d83b31f8`.

- Closure addendum `validation.json`: SHA-256 `87b86ebac993781d117a3f6e14669d7280224996fd2bf6886c7d624bda1e5af0`.

Closing integration measurement: **95% five-hour used / 5% remaining, 15% weekly used**. Final checkpoint commit and closure checks consume the remaining margin toward the requested approximately96% cutoff. No further tracing is scheduled in this pass; the overarching RAW acquisition goal remains unfinished.

Final allowance check reached **96% used / 4% remaining**. Investigation stopped at the requested boundary. The closure addendum's **352 decoded ARM instruction words** match original p6 bytes; audit SHA-256 `48d08503fc1fbc1e10b6f52998b0bf1ebceff15a632855b7709f4eb12d19639e`. All task commits are contained in local master; unrelated ledger draft remains unstaged.
