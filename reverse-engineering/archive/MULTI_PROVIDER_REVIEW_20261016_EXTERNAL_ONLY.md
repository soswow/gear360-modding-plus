> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# External-only provider review, 16 October 2026

The user's current preference is **external subagents exclusively**: Cursor **Grok 4.6 Medium** and MiniMax **M3.1-Flash-Preview with high reasoning**. Parent reviews evidence and owns commits. The initial part of this run also used internal Luna workers because the parent misread “only two external” as allowing Luna alongside them. All internal workers were stopped when the user clarified exclusivity; no new internal task was launched afterward. Older automation text mentioning Luna is superseded by this preference.

## What the two providers contributed

Grok's first focused source review found the actual Sleep constructor and separated it from a destructor that wrote the same vptr. Its next three native dossiers recovered the stop-liveview → state-manager → selected-state virtual+0x44 chain and the state-index transition. Parent checked exact original constructor, vtable, bounds and trampoline instructions instead of repeating all decompilation. The reviewed result remains conditional on current state and says nothing about a backing lease.

MiniMax found a genuine clear/wait/clear helper, then native allocation/initializer/factory records showed that it operates through a separately published event wrapper. A later dossier found that wrapper's mask-0x0f creation call. These are useful static edges. They do not establish freshness for the frontend's distinct inline event wrapper.

MiniMax required substantial correction. Initial claims generalized a bounded branch scan to every compiled path, conflated a global cell with fields on its pointee, called a forward branch a loop, assigned an adjacent function's instruction to the wrong body, and invented a mask-8 call edge from an instruction that actually calls the mask-1 helper. Native records and targeted parent checks corrected these. “Separate allocation on this construction path” replaced an unsupported global no-alias claim. The temporary uninitialized-wrapper concern was superseded by the subsequently found creation path. Original outputs and retractions are retained, with authoritative claims in the reviewed reports.

A later MiniMax input-field search exposed another failure mode: a numeric ARM-offset census misdecoded three +0x410/+0x44c/+0x4bc stores as +0x64. Text searches also missed a bulk clear invoked through a base constructor. Parent rejected the entire census and required complete native helpers plus official LLVM/Ghidra decoding. The final bounded runtime-writer search also misclassified the descriptor destructor as a constructor and proposed its paired deleting destructor as a next writer query. A short parent LLVM window showed the call to operator delete; that recommendation is excluded. The recovered constructor chain now proves a local range clear of the frontend input words; a following setter writes elsewhere. A nearby branch proved to be a wave-detection window update, not established sensor input-mode programming.

Parent added a reusable offline provenance audit of each provider function body's exact ranges against both the carrier manifest and original firmware. This avoids repeating the decompilation. It verifies source binding and range arithmetic while leaving function-boundary correctness, interpretation and runtime behavior separate.

This is one investigation pass, not a controlled model benchmark. Grok's two completed initial tasks took about 9.5 and 10.4 minutes of recorded stream time. MiniMax's completed native/followup tasks took about 5.3, 3.6 and 2.6 minutes; the shorter answers also required more corrective review. These timings include tool/provider latency, use different questions and do not establish relative cost or general ability.

## Reproducible selection and boundaries

Cursor stream metadata identifies **Grok 4.6 Medium**, selected with model id `cursor-grok-4.6-medium`. MiniMax's explicit OAuth-prefixed model override was rejected by its managed-session model list. The retry used the configured managed model with CLI `--effort high`; completed-run metadata identifies **MiniMax-M3.1-Flash-Preview**. This verifies the selected model and requested effort flag; it does not independently measure the provider's internal reasoning allocation or billing.

The same two external sessions are reused for followups. Tasks have separate output folders, a small native-function budget, complete Evidence/snapshot requirements, exact sources and an explicit stop condition. Workers cannot edit tracked/source files, commit, merge, push, run the firmware or camera, restart services, preload, deploy, or create further agents. Analysis executes Ghidra on the Mac only. Parent retains integration authority and leaves unrelated shared MCP servers alone.

## Working pattern for subsequent passes

Require a compact claim table with function/body range, exact receiver expression, original argument flow, source mapping and the next unresolved edge. Demand full native Evidence records for important new functions, rather than trusting a decompiler excerpt or a confident summary. Interpret each negative scan only within its instruction-set and search boundaries. Review disputed instructions and pointer assignments; avoid repeating every successful analysis.

Keep “constructed”, “creation attempted”, “active”, “completed” and “verified pixels” separate. A successful analysis tool does not mean successful target execution. Hashing bytes at the wrong file offset does not establish address mapping; the parent corrected two Linux-app mappings in the initial part of this run. These review lessons apply to every provider.

The [lifecycle evidence index](../cleanup-event-wrapper-evidence.json) preserves completed worker native records and traces. The [final provider evidence index](../external-only-provider-evidence.json) also binds subsequent wire-table/input-writer work, provider metrics and correction logs. Grok’s final bounded task recovered an actual low-byte0x88→index20→YCC-wrapper lookup after parent review corrected a ROM-versus-RAM coordinate mistake. The outer transport/handler join remains unproved. No verified sensor RAW file exists.
