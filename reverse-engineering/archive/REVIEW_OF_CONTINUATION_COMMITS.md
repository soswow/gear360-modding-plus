> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Review of continuation commits d4a5880–e71414e

Reviewed the 35 commits following dd2e113 as an evolving set of findings. This is a bounded, offline review, not a re-derivation or certification of every claim. Two Luna workers reviewed provenance and consequential pointer/lifetime claims; the parent reviewed cross-report consistency, method boundaries and original instruction words. No REA session, target execution or live camera action was needed.

**Assessment:** useful instruction-level investigation, with good explicit uncertainty and preserved evidence. The main weaknesses are synthesis across earlier findings, one function-boundary error, and incomplete indexing of later evidence. The changes are worth retaining, but the following points should be corrected before using the new task list as authoritative.

## Findings

### 1. Restore the already established candidate-to-request-22 link

`RAW_COMPLETION_IDENTITY_AND_RECIPE_DISPATCH.md:189,200` and the ledger's next-work paragraph present the identity of a request-22 callback image as an open gap. Earlier source-bound records already connect selector0x28 → configurator0x80141c78 → request22 at image+0xa4 → MID0x15000 material slot0 and pre-callback0x80574ba4 holding that same image.

The earlier account is in `RAW_FACTORY_FORMAT_AND_SYSCALL.md:62–66`, supported by pass4/image28 and pass7/candidate-lifetime records. Key EIDs: configurator `ev_bcbd5659eb24fa8187466f28d30896a30d628447ca1ad4a563481932e87f7f78`; builder `ev_0fa01a39e8cfbd589a38d032836fbb99184f4a1130f77e1589f5704a21aca82f`; original instruction record `ev_f332ac30a581abcc2f6ffcb104c14b2b9ed763d4114354a64ccd6109271343c0`.

The broad census of 144 constructor callers remains unresolved, but this particular static candidate is identified. The next questions are runtime activation, the image's effective type/configuration at allocation, successful backing installation, sensor/DMA provenance and retention through export. The separate r0=3 sentinel/stack-image path does not invalidate the candidate. Reopening the known link risks spending another investigation on work already established.

### 2. Keep Start and Pause in their separate material methods

The report's line131 attributes both Start at0x802e5e34 and Pause at0x802e5f7c to completion method0x802e5c74. Its own complete-body record ends at0x802e5e73. The MID table's +0x18 cell0x806a1f88 is0x802e5c74, while +0x1c cell0x806a1f8c is0x802e5e74. The Pause call belongs to the latter method.

Original word0xeb00e296 at0x802e5f7c decodes to BL0x8031e9dc. This confirms the call target, not the report's enclosing-function attribution. Correct that attribution before reasoning about completion order or semaphore posting. The recorded completion dossier EID is `ev_e1bae5bec10edda52fa6bb38616dad35dcef50bd1e28ffffb02925b96ccf4544`.

### 3. Extend the identity index to later evidence

All27 files currently indexed in `completion-identity-recipe-dispatch-evidence.json` match their sizes/hashes. Its13 structured function records and20 source windows are internally consistent. However, the final report cites additional pass12 records, including cook-message-send, frontend-semaphore-post and still-book-indirect evidence, whose paths/hashes are absent from that committed index. The cited records exist locally under ignored output; this is an audit-coverage gap, not evidence that their conclusions are false.

Add citation-to-record paths and digests for the later findings, and update the older task list in `INVESTIGATION_CHECKPOINT_20261015_PASS11.md`, which still requests semaphore-poster work the final report already supplies. Preserve old reports as historical where appropriate.

## What passed and what remains unverified

The parent mechanically compared62 unambiguous inline address/instruction-word pairs with original p6 bytes; all matched. One regex match spanning unrelated sentences was explicitly excluded. Luna checked all27 indexed file identities,20 source windows, source/carrier hashes and13 structured IDs. This establishes byte and record consistency, not semantic completeness.

Useful advances include the frontend StartVideoStream parameter record, BranchManager's recipe-book switch, the distinction between its two Still-book maps, service-to-RDMA field copies, and careful separation of a stack image from pixel backing. The report does not prove arena backing release, RAW impossibility, live graph activation, fresh pixels or successful acquisition. Its continued no-RAW-captured qualification is appropriate.

The best evaluation method is a claim-to-evidence matrix plus selected checks at high-consequence joins: same object, same field, same function, and static versus runtime. Reconcile new unknowns against existing findings before launching more traces. Commit count and agreement with instruction words do not measure closeness to RAW acquisition.

## Local review records

The detailed worker reports and parent byte audit are retained under `output/decompile/rea/review-other-agent/`. These records are local generated artifacts:

- `claims-review.md` — SHA-256 `d5a0ddaf03ab5b83e5cef6968127aebfebf85209d3b1251c9efac1357c1f6e48`.

- `provenance-review.md` — SHA-256 `a89de204545368c3437545623717be416f0a198be66b356f330ecd37dc1906e8`.

- `parent-byte-audit.json` — SHA-256 `130c08447264451dd84cb664392bd2d07e9eec20964730b4613a4ec19468de1f`.
