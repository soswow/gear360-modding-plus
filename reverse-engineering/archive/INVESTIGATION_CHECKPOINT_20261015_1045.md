> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Investigation checkpoint — 15 October 2026, 10:45 Sydney

The scheduled pass resumed with a refreshed five-hour allowance at 0% used and weekly allowance at 82% used. Seven Luna workers performed focused traces and independent reviews; the parent checked original bytes, corrected interpretations and integrated reviewed milestones into local master. The stopping target is approximately 96% five-hour usage, leaving 4%. Closing measurements are recorded below.

The overarching objective remains **capture verified sensor RAW pixels into a file**, reproducibly, with provenance, geometry/stride, packing, completion and safe lifetime established. **No verified sensor RAW frame has been recovered.** This checkpoint and the credit cutoff do not complete that goal. The goal API still carried the previous reserve-related blocked status; the heartbeat explicitly authorized this investigation. No resource-bound replacement goal was created and no goal was marked complete.

## Main advances

- Corrected an important provider mix-up: selector 0x28 requests provider **0x0d (13)**, mapping to settings **row 0x16 (22)**. Separate provider 0x13 (19) uses rows 25/26. Both row-22 constant templates initialize **5824×2896**, giving derived stride **11648 bytes** and stride×height **33732608 bytes**. These are initialized defaults, not measured live output. The exact provider retains metadata bytes 1/2/4/1.
- Recovered the conditional descriptor chain from material image slot zero through the frontend and preview to Core output0. It requires the secondary copier and helper byte **FrontendNx+0x204 == 0**. A disabled tag-4 row still executes and overwrites the candidate route for nonzero values. The descriptor address is distinct from the pixel address read from its embedded wrapper.
- Extended completion tracing: key-6 registration stores callback 0x803118c0 at **0x8144a798**. It fans out PP_WDMA0 manager ID3; the adjusted frontend listener translates that to handler argument2 and event bit8. The separate 0x2080602c dispatcher uses table **0x8144a814**. No hardware-source/ack link or current-frame freshness is established. The null API path does not prove callback removal.
- Identified CCook's recipe phase runner and CCook_Assistant's event/post dispatcher, while preserving the distinction between material virtual +0x1c and the still-unresolved +0x10 submission trigger. Active recipe identity and export/reuse ordering remain unknown.
- Verified captured GPT p6 byte-for-byte against the analyzed RTOS. Boot0's rom.bin descriptor matches p6's start and declares destination 0x80000000. A later initialization routine copies the known initialized-data range and explicitly clears the high-global range. Actual boot selection and callback execution/order remain unobserved.
- Recovered candidate output control code4 and its low-byte programming gates. No sample byte order/unpacking was inferred. SSIF/BF register stores remain configuration evidence, without a proven receive DMA destination.

The [reviewed findings](RAW_EXECUTION_PROVENANCE_AND_LIFETIME.md), [component map](BINARY_COMPONENT_MAP.md), and [evidence index](../execution-provenance-lifetime-evidence.json) contain exact addresses, complete evidence IDs and qualifications. Earlier factory/activation reports now point to the corrected provider/geometry relationship.

## Evidence, validation and integration

Full traces, source windows, carrier manifests, REA records, closed snapshots and independent reviews are retained under `output/decompile/rea/continuation-20261015/pass8/`. Parent validation checks byte equality and hashes against immutable inputs, GPT CRCs/p6 identity, and saved REA byte reads where a complete carrier mapping is available. Unmapped or empty byte-read records are excluded from that last validation, not silently accepted. Evidence-record counts are not counts of recovered functions or completed goals.

Milestones: **94f46aa** (provenance/lifetime), **607f83c** (provider/geometry correction and listener bridge), **a4c3fb4** (boot/recipe/configuration evidence). Final integration accompanies this checkpoint. No remote push occurred. No C++ prototype implementation changed in this pass; validation concerns source/evidence bindings and documentation consistency.

All tracing was offline: no camera commands, firmware execution, service restarts, preload or deployment. Unrelated ledger edits and untracked work are preserved. Connected REA independently reported **open:false** and **analysis_provider_binding:null** at closure. Worker cleanup warnings and exact-PID audits are retained; temporary directories remain preserved where deletion was not confirmed. Final worker/validation counts and usage are appended after closure.

## Next investigation boundaries

1. Bind the active R210 recipe and material virtual +0x10 caller. Establish the actual secondary-copier/helper==0 gate and mode selecting normal versus alternate dimensions.
2. Resolve hardware delivery into callback registry 0x8144a780/key6: status source, acknowledgement, frame identity and freshness. Do not substitute the different 0x8144a814 table or a WDMA Done log.
3. Establish sensor/SSIF receive destination and producer identity for the allocation ultimately read/exported. Configured BF fields do not supply this proof.
4. Bind actual dump/ssif enablement before recipe creation, and establish exact backing ownership, completion-to-copy order and protection against reuse. Startup clearing alone does not establish the active flag.
5. Only with separately authorized live work and a proven acquisition contract can saved pixels validate dimensions, stride, sample packing, Bayer content and sensor provenance. Continue offline until then.

Closing validation: **307 source windows**, **39 RTOS carriers / 344 segments**, **3 boot carriers**, **92 closed snapshot files**, and **859 retained evidence IDs**. Saved REA byte reads: 104 matched original mapped bytes, 9 excluded as unmapped/empty, zero mismatches. Seven workers finished; connected REA is closed. Exact-PID parent checks found all six checked cleanup PIDs absent.

Final integration usage check: **95% five-hour used / 5% remaining; 97% weekly used**. Integration and closure consume the final margin toward the requested approximately 96% boundary. No new substantive tracing follows this check. The RAW-capture objective remains unfinished.
