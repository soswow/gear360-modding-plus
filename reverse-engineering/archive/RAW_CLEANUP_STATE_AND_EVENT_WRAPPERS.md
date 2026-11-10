> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Cleanup dispatch and separate event-wrapper construction

Reviewed 16 October 2026. Cursor/Grok 4.6 Medium and MiniMax M3.1-Flash-Preview with high reasoning supplied bounded native dossiers; parent checked the returned records and disputed original instructions. All work is offline. No verified sensor RAW file exists.

## A concrete software route into Sleep cleanup

`CResLiveview::onStopLiveview` at **0x800f83d4** obtains a software value through `0x8054e2a0`. Its fanout runs only when that value is 3 or 5. Near the end of that branch it obtains the state manager through `0x8010021c` and calls **0x80100c64**.

That forwarder checks the current index at **manager+0x20** through `0x80100e68`, which accepts 0 through 5. It then selects `*(manager+4+index*4)` and passes that state pointer to **0x80101304**. The helper calls virtual **+0x44** on that state.

Construction stores a 16-byte Sleep object at **manager+8**, the slot selected by index 1. Its constructor is **0x801022f4**: it calls the base constructor `0x80100f2c`, installs vptr **0x8063cc08**, and clears bytes +0xc/+0xd. Function **0x80102350** also installs that vptr but calls base destructor `0x80100f80`; it is destructor behavior. This resolves the constructor-versus-destructor uncertainty in the prior cleanup report.

Sleep vptr+0x44 contains **0x80102758**, the already recovered method that obtains the capture-memory manager and calls `cleanBuffers`. Consequently the stop fanout can reach cleanup **if the selected manager index is 1 and the retained state pointer/vptr are unchanged**. Construction alone does not establish that condition during capture or export.

State-transition function **0x80100d34** calls the previous state's virtual+0xc when the old index is not -1, stores its second argument to manager+0x20, then calls the selected new state's virtual+8. It has no range check of its own. These enter/leave slots differ from cleanup slot+0x44. The caller's validation and actual state values remain separate proof obligations.

This closes a dispatcher edge, not a buffer-lifetime guarantee. The stop method carries no candidate backing address, export descriptor or retention token. Whether cleanup reaches the candidate arena pages, and its ordering against FileDump, remain unknown.

Full native Evidence:

| Function | Evidence ID |
|---|---|
| Manager stop forwarder 0x80100c64 | `ev_dd72e2da2ef292a00c36fcd5c022b55527fa92cc18f6298461947f19854aa79b` |
| State transition 0x80100d34 | `ev_e4b723f37186f16747c43790dae425c260f009f978316f38f71b44fda4bd73d6` |
| Stop-liveview caller 0x800f83d4 | `ev_ac42271c3538a459a2eac3b4e7f66f7bb0d3a8e98e2483cbe875f72ace535850` |

The stop-liveview body has two provider ranges: 0x800f83d4–0x800f850f and 0x800f8524–0x800f852f. The intervening bytes are preserved separately, not represented as part of its body. Names come from exact diagnostic strings; sparse carrier symbols are not enclosing function identities.

## A clear/wait sequence on a separately published wrapper

Function **0x803237a4** loads its receiver from global pointer cell **0x81952fc4**. On a nonnull receiver it calls clear `0x8051e0ec(receiver, 8)`, wait `0x8051e1dc(receiver, 8, &result, argument)`, then clear with mask 8 again. The native body contains no loop: the disputed branch 0x8032388c→0x803238d4 goes forward. Its provider ranges are 0x803237a4–0x803238db and 0x803238e0–0x803238eb; the four-byte gap is not part of the body.

Constructor **0x80322e08** requests a separate 16-byte allocation through `0x8059d454`, initializes it through **0x8051df6c**, and publishes the allocation pointer to **0x81952fc4**. The initializer writes table pointer 0x80783548, clears word+4, validity byte+8 and word+0xc. This differs from the **inline frontend C+0x298** event wrapper created in the retained FrontendNx factory path. Adjacent global cells are not fields on the object pointed to by another cell.

The distinction is established for these construction paths. Later replacement, shared kernel handles, initialization and runtime values require their own evidence. A zero word at a corresponding location in the saved firmware file is not an observation of live RAM.

The wait wrapper at **0x8051e1dc** checks receiver byte+8 before calling 0x8051e134, and returns -1 if it is zero. Construction-time initialization does not create a kernel event group by itself. The new factory dossier **0x80322f70** closes the next static edge: when singleton cell **0x81952fc8** is null, it allocates a four-byte owner, calls 0x80322e08 (which allocates/publishes the 16-byte wrapper), stores the owner at 0x81952fc8, and calls **0x8051df88(*(0x81952fc4), 0x0f)**. The receiver comes from that exact cell at 0x803230c8–0x803230d4; the mask is passed at 0x803230d8 and create is called at 0x803230dc. The creation call's return is discarded. Its successful execution is unobserved.

Thus the initial zero-validity constructor is followed by a compiled creation attempt on the same nonnull path. The worker's temporary suggestion that the waiter necessarily remains disabled is superseded. Mask 8 lies within creation mask 0x0f; the frontend's inline group instead uses mask 0x3f. These are masks, not bit indices: mask 8 means bit index 3.

Clear-before-wait does not establish clear-before-submit, current-frame identity or successful hardware completion. None of these records proves fresh sensor pixels.

| Function | Evidence ID |
|---|---|
| Clear/wait/clear 0x803237a4 | `ev_ab3b9a31f439ecd1944329dcfc87eab618f647066afcdae25e6ffc0b3da26332` |
| Wait wrapper 0x8051e1dc | `ev_38f4b34d8d5cd5956b29d41a8811679def9b9f05b36df9f8ba9c64b2171eefb1` |
| Separate wrapper allocation 0x80322e08 | `ev_e8972dd86260f25fe3b33c712ee7baebc27bc185bc810038329fc8ab4a382399` |
| Wrapper initializer 0x8051df6c | `ev_a369794d35946b5777b10ff980d6e24371ab13ec636640d9d817f31f8ea1bc9b` |
| Owner factory and mask-0x0f creation 0x80322f70 | `ev_52ceeca3a0f634cfb6c9a6910148613e5023f4bd74be25283a45a5aaa677f810` |

## Review corrections and remaining boundaries

MiniMax's initial “genuine freshness hole” conclusion was rejected: it generalized bounded scans, conflated event receivers and had not read the full helper. Subsequent native records corrected its forward-branch-as-loop mistake and separated global-cell addresses from pointee fields. The claim that 0x802129f0 calls the mask-8 helper is also unsupported: that instruction calls the mask-1 helper **0x803232b8**. Parent's syntactic all-condition aligned A32 B/BL scan over the first 0x5e0000 source bytes found no direct branch to 0x803237a4; Thumb, BLX, indirect and computed targets are outside that scan.

The retained FrontendNx factory **0x8020d948** calls 0x80322de4 and 0x80322f70, not 0x80322e08 directly. The new 0x80322f70 dossier now supplies the intermediate factory-to-constructor edge: FrontendNx factory → 0x80322f70 → 0x80322e08. This is conditional on each factory's initialization guard. The parent report uses these narrower claims instead of the worker's broader “sole assigner/no later alias” language.

The [evidence index](../cleanup-event-wrapper-evidence.json) preserves full dossiers, snapshots, original worker reports and parent corrections. Hash/schema checks support source identity and record structure; they do not certify runtime activation or freshness. RTOS SHA256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`; carrier SHA256 `3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`.
