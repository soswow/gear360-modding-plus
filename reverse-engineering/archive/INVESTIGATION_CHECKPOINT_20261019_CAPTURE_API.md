> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# C/C++ RAW capture investigation — reviewed checkpoint

Saved 2026-10-19T00:49:41.624863+11:00. **User method correction governs next work:** build our own C/C++ acquisition path retaining sensor RAW before stock processing/discard, optionally also producing processed image. The debug RAW file is diagnostic evidence only. Debug-flag solution branch retired; no new toggle, capture trigger, firmware write or pixel copy this pass. See CONTINUE_INVESTIGATION_PROMPT.md top section and pass-api-library-20261018/native-raw-retention-integration.md. User unavailable current/next session. One-shot automation prompt updated and saved rule verified for19October2026 04:45 Australia/Sydney; no additional runs.

## Source milestone and strongest findings

Reviewed source commit on local master **5b31dbf907fb65ac5a812e7e0b5756cda1a70ac9**, clean/ahead88, no push. still-request/ contains C11 encoder/submission adapter with C++ linkage. Exact48-byte ordinary packet01010001, caller-prepared count/storagebytes, caller-provided compatible initialized vendor backend, exactlyonecall, nonzero result indeterminate/no automaticretry. Host C/C++ checks and ARMv7 object compile passed. This is request infrastructure, NOT completed RAW library. ARMobject needs ordinary C-runtime memset when linking; compile is not a standalone freestanding-link test.

Native capture API C6: capture-api-review-final.evidence.json/snapshot, app-capture-fields.evidence.json/snapshot, bridge-control.evidence.json/snapshot and capture-api-evidence-directory.json. CaptureStillImage ELF40b40/Ghidra50b40 accepts command/buffer/length; normal48-byte packet ->adaptercommand201/bridge83. App normal defaults type/count fields1,1; storagevalue from statfs availablebytes minus5MiBreserve whencardpresent/enough. CaptureStillImage return is sendstatus, not capturecompletion. CSender::Send may return-1 and queue; never retry capture on that result. CFrame copiespayload synchronously. mm_camera_create ignoresrealizeresult; OperateCapture01070001 pref1268 branch can skipAdapterInitialize. Separate-client safe initialization/coexistence remains unproved; mcb_terminate is not general bridge cleanup. Proposed original17parameter or64bit mm_camera_capture_still prototype remains an inference, not public ABI.

Newest useful native retention result: selected material End wrapper802e58d0 -> material virtual+40=802f3848 -> interrupt listener removal8030f3f0 plus material virtual+68=805743d0 clearing byte+258. **+68 receiver is material, not frontend**; no pixel release in those selected bodies. See full original windows, vtable/receiver proofs and limitations in native-raw-retention-integration.md / native-retention-boundary.evidence.json and snapshot. This does not prove framecompletion or retention across all graph cleanup. Do not substitute unrelated Ipc_End for this path. Prior Ipc_End release-beforeRAWqueue finding remains valid for its distinct path.

Reuse prior ordinary-shot sampler's full-resolution input descriptor (C+24/tag1) versus processed output distinction. Current highest-value join: actual sensor/RAW input backing -> completing producer -> processing material -> safe retain/copy point before release/reuse. Linux convertRaw queue/pack-type3 versus candidateABI1 is already source-bound but no producer delivery was established. Do not assume the PNG/JPEG encoder or main Linux .so receives sensor RAW just because generic RAW functions exist.

## Current read-only observations (no new pixel acquisition)

All7owned tempdirs removed/verified; owned-cleanup-directory.json. O_RDONLY/PROT_READ fixed-page helpers hash-pinned, host/camera bytes/hash verified,2immutablecodeanchors80000000/80541000 matched original; same oneuntracedapp PID/starttime pre/post. No following unexpected pointers.

Fixed factory chain now read through manager81884634->82013000(+8)->Fridge82014000(+4)->Still82014800(+14)->PP8201c000 head80684bc8, factory+8=80290e54. Supports availability, not executedrecipe/frame. Seqflags c4/c8/cc/d0 all0. Additional backdebug36-byte baseline8186c894: guard1, objecthead8066bfc0, rest0; flag+19zero. No toggles executed. Preserve static evidence but do not pursue as backend. Source parser on/off byte19 proved; epilogue mayupdateotherdebugaggregatebyte; offssif/offall on seqroute remain unsafeinverse, neveruse.

Stockapp64-byte parameterblock33a1e8 read/hashverified via /proc/254/mem withoutptrace: entirelyzero, unpreparedidle state, notnormal defaults. Source apphash matched. Tempfile/dirremoved. Fixed PIDmapping probe reflects verifiedcurrent254 only; do not reuse as dynamic identity proof.

## RAW/DNG evidence reused

Prior fullRAW preserved local/onSD:33732608B=5824x2896x2, stride11648, SHA5af26b9845007a3f410926c006d269d60b6ae760889c3a8eb356a258f346d780. Original prior reviewed archive SHA6f6032f1c2639d0e41f30d2c7250ab19930c6717bcfcbf317605117b62ccc1bb. Grok DNG34tags/stripoffset960/payload exactlysamebytes independently verified. RightGRBG conditionalchartlabels; leftphase, calibration, precision/provenance open. 10varyinghighalignedbits != filename12-bitprecision. grok-original-artifact-review.json and Luna mechanicsreview retained. No personalpixels external.

## Delegation and review disposition

Cursor requestedgrok-4.7-high, all initclientmetadata confirmsGrok4.7 256KHigh. ABI/fields same-session520231b2-b025-4994-b74c-55b902cdd921 completedexit0; native parent narrowed guessed originalABI and initialization overclaim. ReversibleRAWworker ebdd033f-de8f-4288-959a-2fc24ea2d9a8 completedexit0; exactparser/tail/guard observations useful, partialcarrierabsence not universalcallabsence. Branch retired by user correction. Logs/config/final/completion/scope records retained. No arbitrary deadline or camera exposure. No exact net-credit savings or hiddenbackend identity claimed.

Luna mechanics/requestcomponent and independentcodereview completed. IPCCreview correctedsame-ELFPLTfunctions; kernel packet/event ownership stillunknown. Read-only roundtripfailure review retained, no trialperformed. Preparationreview confirms false->UItype24, true->type0; manageStillCaptureStart(2) possible truecaller. Actualasync3Acallback and RAWcallback not established; report is C0–C5 provisional, not accepted end-to-endcontract. Parent owns C6 acceptedfindingsabove.

All3owned external PIDs absent with exit0. Built-in workers finished. Parent nativeRTOS44records exported and snapshot saved, sessionclosed. Othernativeapp/capture/bridge savedclosed. No helper remains; no service/injection/preload/firmware/MMIO change. Notes/evidence ignored/local only. Original RAW and priorarchives preserved.

## Next pass and resource state

Follow corrected native-raw-retention-integration.md question. Reuse current data before another function census or shot. Need actual completedRAW input association and lifetime/release contract, then concrete Linux/RTOS integration choice for own C/C++ retention/export. Requestcodec and debugdump do not finishgoal. User unavailable: prepare/review permitted source/static work; no manualactiondependency or newunauthorizedinstrumentation.

Latestusage84% of300minused/16%remaining, weekly44%, reset19October04:40:28Sydney; no resetcredit consumed. Default95%cutoff applies, prior2–3%exception previouspassonly. Existingexactgoal unfinished; goaltoolstillblockedfromoldcamera-offlineepoch/noactive setter; no duplicate or falsecompletion. Further runtimebinding needs a specifically supported observation/change; do not keepcataloguing unrelatedfunctions tospendallowance. Finalcredit/checkpoint supplements may follow.
