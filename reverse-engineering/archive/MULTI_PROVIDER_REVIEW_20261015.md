> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Multi-provider investigation review

This pass used internal Luna workers, Cursor Agent CLI and MiniMax Code CLI for bounded offline tasks. The parent retained ownership of interpretation, original-byte checks, evidence indexing and local integration. The earlier 35-commit review remains in [REVIEW_OF_CONTINUATION_COMMITS.md](REVIEW_OF_CONTINUATION_COMMITS.md); this report concerns the continuation and the delegated results, not a second complete re-analysis of those commits.

## Actual models and boundaries

| Worker | Model observed/configured | Work |
|---|---|---|
| Internal workers | `gpt-6-luna`, high reasoning | Candidate configuration, layout/export contract, independent allocation-CFG review, pointer-chain/recipe-selector review, native Pause/event tracing |
| Cursor Agent CLI | `claude-haiku-5-5-medium` | Retained-record retrieval, Pause callers, recipe selector and constructor default |
| MiniMax Code CLI | `MiniMax-M3.1-Flash-Preview`, managed MiniMax provider, thinking variant | Allocation CFG, image append, constructor chain, event-message retrieval/layout |

The initial Cursor `gemini-3.8-flash-medium` attempt stalled without output and was interrupted after checking its owned process. It produced no findings. Cursor subsequently used read-only ask mode; MiniMax used bounded exec tasks with smart permissions, low or medium effort. No worker was authorized to commit, merge, push, execute firmware or contact the camera. Raw prompts, streams, stderr and final responses are retained in `output/decompile/rea/multi-provider-run/`.

## Accepted results and corrections

| Area | Accepted after review | Rejected or corrected |
|---|---|---|
| Allocation CFG | Two helper calls are alternative arms; successful request22 takes the unequal-to33 arm, subject to that field remaining22. | MiniMax initially misdecoded MOVW/MOVT, then inverted the normalized branch and computed a MOVT constant incorrectly. Parent source-word checks and Luna's independent review corrected the predicates. |
| Candidate layout/export | Luna recovered the configurator's layout byte2, request22 and type2 plane/length calculation. The known image identity was reused. | Constructor layout0 was not promoted as the effective candidate type. Kind+b0 setter attribution and a percentage were corrected before integration. |
| Material image slot | MiniMax identified append's count-based addressing and the beginning of the constructor chain. Luna followed the remaining base constructors to the explicit count-word zero. | An immediate `STR +0xc4` census is insufficient for a computed append. Missing retained intermediate bodies did not establish that the count was never initialized. |
| Pause | Cursor usefully retrieved the state gate and semaphore sequence. Native Luna recovered the full wrapper and concrete receiver helpers. | Cursor's first claim that no material ISR-to-Pause edge existed was contradicted by a retained handler. Diagnostic strings/line numbers were not accepted as command IDs or semaphore values. |
| Wrapper identity | Parent source instructions and Luna independently joined outer wrapper cell → wrapper+0 → inner frontend cell → virtual+0x30. | MiniMax's claim that two different singleton cells disproved receiver identity was false: they hold different levels of the pointer chain. The intermediate and final C vptrs were checked separately; both have the same +0x30 target. |
| Recipe-map choice | Cursor found that the map selector is CCook+0x54 rather than recipe ID+0x70, and found the base zero default. Luna found the retained message-loop overwrite and successful case-five clear. | A base constructor default does not prove active map choice. No absence claim was accepted solely because a worker had not opened the requested derived or message-loop body. |
| Event initialization | Native Luna resolved negative SVC selectors through their table and joined request word2 to the bit-pattern field consumed by OR/clear operations. | Numeric resemblance between a selector and a frontend code address is not a call edge. Field+c was kept distinct from the bit pattern at+0x14. Create-time zero is not per-frame freshness. |
| Later helper/message searches | Narrow call-site excerpts and exact source layouts remain useful leads. | Both external workers made overly broad absence claims after skipping gitignored `output/`. Explicit paths and `rg --no-ignore` were required. MiniMax's later owner-message conclusion again confused object fields, queue members and stack-buffer offsets; that conclusion is not integrated. |

These were mixed results. The external agents were useful for retrieval and bounded comparisons, but their negative searches and pointer/stack interpretations required substantial review. Luna's focused native tracing plus independent parent checks produced the integrated conclusions. This pass does not establish a general model ranking.

## Verification and reuse

The efficient review method was to compare a claimed edge against an existing complete dossier, check exact original instruction words for disputed branches or stores, and obtain new native evidence only for a genuinely missing body. Shared offsets alone were never treated as shared objects. Complete Evidence IDs and carrier/source hashes were reconciled into committed indices; large raw snapshots remain local under `output/`.

For later delegation, give retrieval workers exact artifact paths and explicitly mention ignored directories. Ask them to return one field flow or branch condition, with full Evidence IDs and the search boundary. Escalate ambiguous pointer identities, signed dispatch arithmetic and branch normalization to independent review before integrating a conclusion. A worker's uncertainty is useful; an unchecked global negative is not.

All conclusions remain static. No verified sensor RAW frame, live allocation, completed DMA transfer or successful export was observed.
