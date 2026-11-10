# Capture milestones and research priorities

29 October 2026. This is the execution plan for PROJECT_GOAL.md (not published: `../PROJECT_GOAL.md`),
not a change to its acceptance criteria. No end-to-end owned capture works yet.

| Milestone | Demonstration that counts | Current gap |
|---|---|---|
| Our process starts the engine | Stock app absent, our receiver observed running, and a defined remote response received during one bounded startup attempt | Own workers and alive-latch rise observed; corrected still attempt lost access and camera powered off; clean lifecycle remains open |
| Our process requests one still | One software request leads to its identified completion, with failure and indeterminate submission reported separately | Two own-client facade attempts returned0; corrected subscription tested, final result lost; no identified completion |
| Caller receives owned sensor RAW | Verified sensor samples, actual layout/extent, copy or retained backing valid until caller release; a second capture works | Selected backing writer, provenance and lifetime remain open |
| Complete the callable library/demo | Integrate those demonstrated contracts into the existing C/C++ API; save RAW, configure optional same-shot stock JPEG, handle errors/release/shutdown | Host components exist; actual backend is incomplete |

## Work that matters now

30October review identifies a concrete startup difference: stock conditionally
starts liveview via ioctl0x1b before still capture and waits on callback8. Close
that callback/status/failure contract from the retained native receipts, then
prepare a bounded liveview-only diagnostic. The exported mm_camera_start uses
a different body and is not a substitute. Shared0x342f8 boolean and category10
latch are not fresh request-associated readiness. Index21/selected flags are
consistent with a stock branch; arbitrary selector changes are not justified.

Stop-alone and suppressed-still controls survived their bounded intervals;
they do not solve reusable handover or identify the power-off cause. No owned
capture or RAW works. User is away and cannot recover the camera: offline or
documented bounded readonly work only, with no SDK/init/capture, service or
settings/hardware changes. A later test needs recovery availability. Prefer
this minimum startup contract over additional shutdown/thermal/lock catalogues.

## Defer until a demonstrated dependency requires them

- Full live stock-thread cancellation/unwinding and library unload: needed for
  reusable clean handover, but not a prerequisite for every recoverable
  initialization experiment with a whole-device power-cycle recovery.
- More lock/MMU/peripheral-client archaeology: resume only if a specific
  observed startup failure requires that contract. Known register addresses
  are not an invitation to read or reset hardware locks.
- Complete RAW graph, format, JPEG or processing inventories: recover the
  selected buffer's producer, ownership and request completion first.
- More mock backends/API polishing: implement a target-demonstrated native
  operation before broadening the facade.

## Evaluate progress

Report which milestone became possible or which experimental hypothesis was
eliminated. Accepted fact counts, agent output volume and decompiled function
counts are not product milestones. Reuse prior facts; stop a branch when it
does not resolve the named implementation or experiment dependency. Record a
bounded failure honestly and choose the next discriminating observation.
