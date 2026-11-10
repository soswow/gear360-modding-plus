> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Gear 360 goal-focused investigation — 16 October 2026

The goal remains **verified sensor RAW pixels in a file**, with reproducible acquisition and proven source, geometry/packing, fresh completion and safe backing lifetime. It is unfinished. All work in this pass was offline; no camera access, firmware execution, restart, preload, deployment or remote push occurred. The latest user instruction allows any agents and reserves5% of the five-hour allowance. Earlier external-only/10% instructions are historical.

## What moved us closer

The best new first-file candidate is the stock FrontendNx debug route, recovered through the compiled registration/parser: `st cap fenx lv rtmem lv ssif`. This candidate has not been executed. It selects one embedded image, a tag0x35 contiguous allocation, explicit SSIF WDMA output programming, and FileDump of P with stride×height. Its initialized provider0→settings-row10 geometry would request3,686,400bytes. Actual settings and source may differ. See [the direct route report](RAW_STOCK_SSIF_DEBUG_CAPTURE.md).

The event chain is now more specific: conditional SSIF interruptID1 registration leads to the VdFalling callback, observer event0 and standalone event mask2. This is distinct from the previously recovered PP WDMA-done path. The capture wait is not proof that this allocation received a complete fresh frame. The apparent arming helper is a no-op; failed waits merely log; the stop helper clears three bits without a software drain.

Ownership is a concrete issue, not an abstract remaining unknown. The selected cleanup branch releases global addresses without clearing them; a later capture can skip allocation and reinstall a released address. Allocation failure is not guarded by the caller, and tag0x35/mode3 provides no bulk-clean exemption. Do not blindly repeat the stock command. A safe attempt needs a valid owned run, verified stop/completion and retention through copy return.

The BF geometry review reused existing evidence. The builder replaces raw table dimension positions with settings W/H before packing the size registers. Thus raw row22's2904 is not proof of the final height; unchanged settings would supply2896. The low14 bits of successive register halfwords follow `(0,H)`, `(0,W)`, `(H,W)`; preserved top bits and physical geometry remain unspecified.

The Still fallback's static factory question is closed: recipe1 under its guards can select key27decimal/0x1b in Still-book map+4, which reaches factory802b3b90, builder802ba7b4 and MID15000/selector28. Active R210 execution remains unobserved. The separate frontend producer lead configures the SSIF singleton rather than proving writers of frontend C+64/+68. See [factory and control-message findings](RAW_STILL_FACTORY_AND_CONTROL_MESSAGE.md).

Cursor recovered the actual RTOS CAttributeHandler and Linux serializer. Parent byte review keeps their coordinate systems separate: Linux body+6/send12 versus RTOS payload+8/size14 requirement for a four-byte payload on the examined pump. The receive driver probes4bytes then calls its lower read helper at the same destination, without an explicit two-byte adjustment. The stream helper and size accounting remain open. This transport gap does not block the direct debug route and must not become another broad investigation.

## The finite next step

Follow the [acquisition decision plan](RAW_ACQUISITION_DECISION_PLAN.md). Resolve only four decisions: current entry/input geometry; fresh completed sensor-to-P output; same-allocation ownership/cache validity; actual file and pixel validation. If stock completion/retention cannot be established, design one bounded ownership/copy solution offline. If the debug entry is unavailable, use the bound Still route. Do not reconstruct all C++ classes or repeat closed map/token searches. Preserve any eventual original buffer unsplit: the old half-row splitter contract is not established for this new route.

A state-changing camera trial needs separate authorization under the standing offline boundary. The plan is concrete enough to review but does not authorize execution. No `.raw` extension, ACK, semaphore post or credit cutoff completes the overarching goal.

## Agents, evidence and review

Luna handled focused native/reuse tasks. Cursor was explicitly launched with `--model cursor-grok-4.6-medium`; each run reported **Grok4.6Medium**. MiniMax was requested at `--effort high`; completed-turn metadata reported **MiniMax-M3.1-Flash-Preview**, thinking variant, managed provider. That verifies reported routing, not hidden internal effort; MiniMax was not falsely treated as hard-pinned. Its retrieval missed existing send records, and most plan-review technical objections were rejected; its allocation-failure suggestion improved the plan. Parent owns the reviewed conclusions and local integration. There is no measured counterfactual proving a numerical credit saving.

Evidence package: versioned archive (not published: `../evidence/pass-goal-20261016.tar.gz`), member/hash index (not published: `../evidence/pass-goal-20261016.index.json`). It preserves complete fresh native dossiers, reused selected records, original source windows, provider snapshots, LLVM reviews, CLI prompts/launchers/stream logs, errors and corrections. Canonical source bytes remain in their existing local locations; manifests bind their digests. The parent REA session closed successfully and its immutable snapshot is retained. All worker tasks completed and private engines exited; a filtered process check found no owned analysis worker left. Shared servers and unrelated agents were left intact.

Parent audited74fresh native dossier bodies covering101ranges against the original RTOS bytes or Linux ELF mappings, with no errors. The evidence validator passed with85EIDs,214source windows,3carrier manifests/11segments,24structured snapshot files and2closed-snapshot files. Counts are schema-specific, not separate claims of24live sessions. Reused prior pseudocode bundles are not relabeled as fresh native dossiers. Neither check proves semantics, target execution or RAW content. The archive builder verifies every member against its index.

Allowance was checked throughout:0,7,15,24,30,42,55,61,64,70,75,83,85,87,88% used before final integration. Wind-down started early enough to preserve evidence and local commits before the95% boundary. See the final chat report for the last reading and resulting commit.
