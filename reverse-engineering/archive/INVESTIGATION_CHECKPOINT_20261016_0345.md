> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Reviewed handoff:16 October2026,03:45 pass

The overarching goal remains unfinished: verified sensor RAW pixels saved to a file with reproducible acquisition, provenance, geometry/packing, fresh completion and safe lifetime. No camera commands, target execution, restarts, preload, deployment or remote push occurred.

## Results

- [Shell and callback lineage](RAW_SHELL_DUMP_ENABLE_AND_CALLBACK_LINEAGE.md): generic Linux capture forwarding joins B6 token parsing and seq/dump; exact candidate payload is24 bytes including trailing space. Four compiled callbacks forward the material owner's callback. Recipe1/3 and CDriveAdjust table membership are recovered; active invocation is unproved. Committed earlier as54d31e7.
- [Recipe and command gates](RAW_RECIPE_SELECTION_AND_COMMAND_GATES.md): recipe word reaches selector unchanged; recipe1 can select candidate key27 under explicit parameter predicates, recipe3 normally selects key18. Parameter+0x170 derives from global0x81482ea4 low bits. An option handler labeled “Set Adjust Ycc Capture” writes that global. This is separate from dump-enable0x819521c4 and one-shot override0x81952130. C API narrowing preserves B6/0/24 to the userspace sender.
- [Core inputs and cleanup](RAW_CORE_INPUT_AND_BUFFER_CLEANUP.md): exact input0/input1 setters and a conditional descriptor-forwarding caller are recovered, alongside a positive clearing caller. A direct bulk-cleanup caller is identified; preceding frontend receiver and Sleep table address point are resolved statically. Active transition and candidate overlap/timing remain unknown.
- [Historical RAM](RAW_HIBERNATION_MEMORY_BOUNDARY.md): reconstructed19,755 saved Linux pages, independently rechecked by parent. None lies in candidate arena0xa6000000–0xc0000000. No RAW buffer or active command flow follows from saved strings. Earlier commits fbb2ba2/4e15e23 preserve this work.
- Additional drive callback review confirms constructor-time+8 zero and forwarding, but no later writer. Its bounded three-body review is retained; indirect calls remain open.

## Evidence and review

Two versioned compressed native archives retain full Evidence records, snapshots and original-byte windows; their indexes bind every member by SHA256. The shell archive has86 members/70 unique Evidence IDs; the final native archive has114 members. Parent verified archive contents, disputed ARM instructions, selector argument flow and source windows. See [provider review](MULTI_PROVIDER_REVIEW_20261016_0345.md) for accepted/rejected Cursor/Haiku, MiniMax/Flash and Luna findings.

Validator checks passed for shell/callback phases, Core phases, cleanup, recipe selector and parameter-gate source ranges. The drive provenance initially triggered a schema collision: its descriptive mapping string looked like a boot-carrier mapping object. Parent preserved the original, renamed the descriptive key, independently checked all seven windows and normalized source-byte-windows.json; whole-folder validation now passes with zero errors. A canceled Sleep Ghidra request has no claimed dossier/Evidence ID; its exact vptr assignment is supported by original source instructions instead.

## Resume priorities

1. Follow the actual dispatch/table record at0x8005152c feeding option handler0x801153cc; establish the setting's delivery semantics without equating manager tag0xb9 with an MCB command.
2. Bind active R210 parameters to the other recipe1 guards and one-shot override; follow callback installation into CDriveAdjust+8/owner+0x60 only through real pointer assignments.
3. Trace the producer of descriptor fields forwarded by0x803954e4, and distinguish memory input from a possible direct SSIF hardware source. Input setters are no longer unknown.
4. Identify hardware producer/ack semantics and per-frame identity for the software completion event. Constructor zero and named WDMA callbacks are insufficient.
5. Trace the consumer of Sleep vptr0x8063cc08 slot+0x44 and candidate backing retention through export/reuse. Neither ordinary release nor bulk cleanup has a demonstrated candidate-specific lease/order.

## Shutdown and reserve

Pre-finalization five-hour checks reached95% used; reserve was rechecked after the final local commit. No new research was launched after wind-down. Every worker reported its private CLI closed; the shared REA session reported open=false with no observed analysis activity. Process inspection found no analyzeHeadless, one-shot REA function, headless agent or mcode exec workers. Unrelated persistent servers and local files were preserved. Work is integrated directly on local master; no merge is needed for this checkout. Stop this pass near the requested97% used boundary; do not mark the acquisition goal complete.
