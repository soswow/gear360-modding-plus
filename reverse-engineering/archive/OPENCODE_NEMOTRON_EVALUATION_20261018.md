> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# OpenCode / Nemotron bounded evidence-review trial — 18 October 2026

Verdict: useful for a preliminary skeptical review, with mandatory parent verification of field meanings and causal claims. This single trial does not establish that it saves net Codex credit. Setup was expensive, and the final report contained two significant reasoning errors. Speed was not assessed.

## Task and model proof

One task: audit six proposed interpretations of an existing Gear360 allocator/buffer snapshot, identify one additional code-level edge case, and propose the next acquisition-proof step. Inputs were an actual 28,672-byte allocator prefix, raw observations/field layouts, and five saved REA code dossiers. The valid session was not given prior prose conclusions. A rubric and input hashes were saved before launch. No fresh camera acquisition, firmware execution, REA tracing, code modification, or visual analysis was part of the trial.

OpenCode v2.0.32 was invoked with `--model opencode/nemotron-3.5-super-vl-free#xhigh`. Its model catalog identifies **Nemotron 3.5 Super VL Free** and maps `xhigh` to `reasoningEffort: xhigh`. The session record and every assistant message in the exported session identify provider `opencode`, model `nemotron-3.5-super-vl-free`, variant `xhigh`. This verifies client routing and selected configuration; it cannot independently verify remote weights or the backend's hidden reasoning allocation. No substitute model was used.

## Review of the final answer

| Item | Assessment against the supplied evidence |
| --- | --- |
| A: allocator tag `0x40` | Correct rejection: observed tag is decimal 40, hexadecimal `0x28`. All 1,781 complete prefix records have that tag. |
| B: completed 27-frame ring and temporal frame 9 | Correctly refuses completion/ring/VD-tick safety claims, but gives an invalid reason to reject frame 9: `image_fields[1] == 0x202` describes configuration bytes, not a temporal index. The actual useful arithmetic is `(PData-base)/N == 9`; that proves a byte offset, not temporal identity. |
| C: request 22 / 64 MiB Still candidate | Reaches the appropriate rejection, and distinguishes dispatcher selector `0x1c`, decimal 22 and hexadecimal `0x22`. However, omits the strongest descriptor evidence: request field `0xffffffff` and kind 0. Dispatcher selector and image request are different domains and should not be conflated. No historical request-22 exclusion follows. |
| D: stable guards prove coherent retained RAW | Correctly says unproved. Stable metadata does not prove selected DMA completion or backing retention. A checksum by itself would not prove frame coherence either. |
| E: automatic resize with geometry | Correct at the inspected method level: nonzero cache bypasses allocation and geometry checks. It does not exclude resizing/reset elsewhere. |
| F: recorded pixel extent is contained | Correct: `[0xa77bb000, 0xa7a5e000)` lies inside requested movie-work range `[0xa6000000, 0xaa731000)`. Object address and pixel pointer are correctly distinguished. Omits rounded allocator extent, which ends at `0xaa734000`. |
| Additional edge | Fails the causal-evidence requirement: says larger geometry causes subsequent writes to overflow and corrupt memory, without any supplied writer or caller proof. At most, reuse creates a conditional capacity-mismatch risk. It also largely repeats E. |
| Next evidence | Reasonable direction toward actual WDMA completion, but too vague to execute: no verified channel/register/instance binding or retention protocol. This is not authorization to guess registers or infer safe reuse from a done bit alone. |

The direct additional edge available in the supplied `FUN_800ae348` dossier was missed: allocator return `0xbfffffff` is stored in the cache; the failure branch logs without clearing it. A later call sees a nonzero cache and returns the sentinel without retrying, unless another path resets the field. This claim is confined to the inspected method. Source evidence `ev_783a482426ed234ceb806681adac249f59506abd72ebe77576e3fcc5a0ef531c` includes pseudocode and assembly in the fixture.

Five of six headline classifications are acceptable; B is mixed. This is not a sufficient accuracy score by itself: the incorrect field interpretation and definite overflow claim matter more than matching labels. It did avoid claiming verified sensor RAW, obeyed the final word limit (673 whitespace-delimited words), and gave inspectable calculations/source references.

## Harness experience and scope

Two setup problems preceded the valid run. First, standalone mode did not give tools the intended temporary workspace. On missing inputs, the agent searched the repository despite an explicit prohibition and encountered earlier prose conclusions. That attempt was stopped and excluded; a fresh explicit session restored the blind test. The directory mismatch is a harness problem, while searching forbidden sources instead of stopping is also an instruction-following weakness.

Second, an explicit permission configuration denied intended reads. The agent honestly reported that it could not perform the audit rather than inventing findings. Permissions were repaired and the same task resumed, without adding conclusions. The final session used OpenCode's plan agent and explicit read/shell access. This was **not an OS sandbox**: shell read-only/scope boundaries were instructions, audited afterward.

The valid run made nine tool calls. Two execute calls failed through Python/JavaScript runtime assumptions; it recovered using shell Python. All valid-run accesses and computations stayed within supplied fixture inputs. No camera/network/repository access or file edits occurred in that run. Original and runtime fixture hashes match. Tracked repository files were unchanged until this evaluation was recorded.

## Token-saving assessment

Full JSON streams were redirected to disk. Parent inspection used compact status extracts, selected tool audits, the final report, and the relevant original code; the full reasoning stream was not loaded into parent context. The discarded scope-violating run was retained for audit, not used as an independent performance sample.

The repaired run's provider-reported usage, including its final answer, is stored in `tool-audit.json`; the exported session also includes the earlier blocked phase. The provider reported cost 0 for this model. These are OpenCode/provider counters, not Codex-account savings, and token categories should not be combined without knowing the provider's accounting semantics. Account usage moved from approximately 1% to 17% during the overall trial, including parent setup/debugging and review; that account-wide delta is not a precise per-task measurement. There is no credible measured net saving from this first trial.

A reusable, correctly configured harness could reduce future setup cost. Best use: narrow read-only claim triage or arithmetic checks over a small frozen evidence packet, requiring exact field/source citations and explicit unknowns. Parent must review causal claims before they enter the ledger. This trial does not support delegating unreviewed native analysis, camera operations, or integration. Do not make the overflow claim or erroneous frame-field interpretation Gear360 findings.

## Reproducibility and preserved evidence

`reverse-engineering/evidence/opencode-nemotron-evaluation-20261018.tar.gz` preserves the frozen fixture, prompt, predeclared rubric, input hashes, model/session routing records, launch scripts, all three attempt streams, final answer, sanitized session export, and tool audit. Its companion index lists every member hash and the archive hash. Raw working output remains under `output/decompile/rea/opencode-evaluation-20261018/` while available. The temporary fixture path is recorded in that evidence, but the archive is the durable copy.

No Gear360 acquisition conclusion changed. The overarching verified-RAW acquisition goal remains unfinished. No remote push or shared OpenCode server shutdown was performed.
