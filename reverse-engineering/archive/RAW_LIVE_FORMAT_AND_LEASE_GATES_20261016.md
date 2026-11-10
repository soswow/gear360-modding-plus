> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Live format consistency and allocator gate boundaries

Reviewed 16 October2026. This pass reuses the authorized [software snapshots](RAW_LIVE_RAM_OBSERVATIONS_20261016.md) and [three-register observation](IMAGING_CONFIG_OBSERVATION_20261016.md), then checks original firmware instructions. No new capture, register write, service restart or firmware modification is implied by these conclusions.

## Current format selector differs from the RAW/debug candidate

The selected image at82246000 returned byte+5a=0 (pack type) and byte+59=2 (packing enum). The complete `CPP_Core::ConvertToD5BitWidth` body at8031c658 tests pack type against1. When it differs, it reads the packing enum through80541a58; case2 returns internal control code2. The group-3 WDMA dispatcher80511950 sends that control code to80511e54, which writes code2 into bits0–2 of20800340.

Our separate hardware snapshot returned **20800340=00680102**. Its low-three-bit value is **2**, numerically consistent with that conversion of the observed image fields. In contrast, the configured MID15000 candidate uses pack type1/pixel enum2, producing internal code4. The stock SSIF debug path also supplies0401, whose high byte4 makes80511e54 write low-three-bit value4.

This gives a source-bound distinction between the current selection and the proposed RAW/debug selection. It does not prove that the two snapshots name one hardware submission, identify pixel bytes/Bayer order, or establish frame completion. The earlier extracted bits12–21 value640 is a **different field**; this review does not assign it pixel-format semantics. The live width/height agreement1920×960 remains a separate observation.

Retained full dossiers: `continuation-20261015/pass8/output-packing-programming/packing-programming.evidence.json`, conversion EID `ev_3cd108e9c56e23d566c3e706999a2e72776fa8a14f19ef66fe989e0d8c9d2aaf`, encoder EID `ev_381530dcb3fa0f4044f79b0864cf0e9d4b09d0b99e7aad2d0cafa4b6c9593665`, dispatcher EID `ev_494d5b2b1b862e0aecb0a6e77a266971d3436786f098e0454589b96d9075e7aa`. Copies, original-byte windows and the explicit join are retained under this pass's `parent-format-join/`.

## Allocation serialization is not a pixel-buffer lease

Let M denote the capture-memory-manager object. These are distinct gates in the recovered generic run paths:

| Path | Gate/field established by instructions | Limit |
|---|---|---|
| Extended allocation800af108 | Calls8052078c with handle stored at M+686a0; later805209d0 | Ordinary release and cleanBuffers do not acquire this same outer gate on the examined paths. Holding it alone does not exclude reclamation. |
| Run allocation800afb80 | Enter/leave helper on M+68600 | Distinct from the normal release gate. |
| Normal release800adb1c | Enter/leave helper on M+68620, then800b04b4 | Does not itself establish protection against bulk cleanup. |
| Allocation/free record writes800afef8/800b04b4 | Enter/leave helper on M+68640 | Short metadata critical section, not a DMA fence or recovered pin API. |
| Bulk cleanup800adfec | Scans records in configured bounds and calls800b04b4 directly | Bypasses normal release's outer M+68620 gate and the allocation outer gate. |

Fresh REA dossiers establish an additional condition: enter helper **8025c29c** returns false when object+8 is less than1 or object+4 differs from1; otherwise it calls80517bdc(handle,ffffffff) and returns whether status is zero. The matching leave helper8025c308 applies the same validity checks and calls80517fa8. The allocation/free call sites inspected discard those Boolean results. Therefore a future lease design must verify successful gate acquisition; simply calling the helper does not prove a gate was held. This is compiled control flow, not an observation that a live gate is invalid.

80517bdc uses SVC6 with literal selector802f0200. The decompiler renders it as a bare software interrupt and does not recover its argument contract; this pass does not claim to have traced the kernel primitive. The gate locations and differing caller coverage are established independently of that unresolved primitive's full semantics.

The ordinary free body reads tag/run-length before its record-gate call, clears the run metadata inside that call pair, and updates aggregate free count after leaving. Holding a gate across the Linux FileDump wait would be a new synchronization design with unproved progress/deadlock behavior. It is not an existing reference-count operation. DMA writes remain independent of allocator metadata gates.

Fresh EIDs: enter8025c29c `ev_4b3aa7c2f20a6d0ace4b4f7aac2445e8271fe561834c816b89ac30860b9313f9`; leave8025c308 `ev_b376abff4a8cfe8547b300701039d2f04f2206ef1922287f336a17e556182ede`; SVC wrapper80517bdc `ev_03cb2f019812c87b64bbc1b2c64286f850be0188da5bd9b4a3cd6c7f06ac85fd`; cleanup800adfec `ev_98f20ecd3bb1d35202b9e8be6a41ca38949f48b3f4c246f6e3bd0b40b586a5a8`. Full records and a closed immutable REA snapshot are retained in `parent-owner-contract/`. Parent verified original byte windows and independently decoded six direct BL targets.

## Completion leads ruled out in bounded source checks

Luna's focused REA follow-up resolves three misleading names. Function805118b4, formerly labelled `hw_wait_state`, is a52-byte argument selector, with no status loop or wait; its arg0 target805123c0 programs2080000c. The string `pp_top_wdma_ready_signal_auto_adjust` belongs to a range-checked setter80512060 for20800044 bits0–13. `PP_IDLE` is a DVFS scenario diagnostic label inside800a1ff0, not a hardware idle test. These specific routines do not supply a DMA drain. This is not an exhaustive absence claim for the firmware.

The full dossiers, string xrefs, original code/string bytes and closed one-shot snapshots are retained in `luna-wdma-drain/native-1/`; follow-up report (not published: `../../output/decompile/rea/pass-completion-20261016/luna-wdma-drain/followup-report.md`) lists exact EIDs. The native query made at interior logging address80512088 resolves to method80512060; the one at800a2754 resolves to800a1ff0. Filenames do not define method entry points.

## Consequence for acquisition

A passive preview sample may provide useful byte evidence, but must remain unverified because no pixel lease or frame-specific completion is established. A copied bounce buffer preserves only what was read; it does not retroactively make racing input complete. For a validated capture, the submission needs a new completion tied to that allocation, hardware quiescence before export, and protection against both ordinary release and the bulk sweep through copy completion. The existing VDfalling wait and unrelated allocation gate cannot supply those guarantees.

Cursor Grok4.6Medium supplied a bounded read-only design critique; its init transcript confirms that model. Parent accepted the ownership distinctions and rejected its suggestion to search again for an already-absent compiled wait inside the complete debug handler. Bit6/PP_WDMA0 remains a software registry naming/dispatch relation, not a verified group-3 silicon completion source. No per-buffer refcount primitive or safe unmodified repeated-capture procedure is claimed. The overarching sensor-RAW-file goal is unfinished.

## First passive pixel fragment saved

A new fixed read-only helper copied **4096bytes** from the selected buffer at `a77bb000`, with the expected image/global fields matching before and after. It performed no capture command or MMIO access. The helper was removed; appPID254/starttime1042 and21untraced threads remained unchanged. The saved `UNVERIFIED-preview-sample.bin` has SHA256 `59c5a5d172ed0487a0d2f76d935c175ae57e3a6ce75a4b16f366afebcb1ec9cb`.

The fragment is nonzero, with112distinct byte values and entropy4.419bits/byte. Only827of2048 aligned little-endian words fit mask3ffc; alternate endian/byte phase also fails to fit every word. This rejects treating the entire fragment as that exact16-bit-container representation, not sensor RAW generally. In particular, **1920×12/8=2880**: the observed stride is also consistent with packed12-bit samples. Earlier YUV suggestions remain hypotheses; do not promote them to a format label. A 3-byte/two-sample interpretation alone cannot prove12-bit packing because any three bytes can be split that way.

The next high-value task is packing validation from the original configurator plus a fuller passive byte sample, before more broad completion-name searches. Source/geometry, freshness, safe buffer lifetime and actual complete-frame saving remain necessary before the main goal can be called achieved.
