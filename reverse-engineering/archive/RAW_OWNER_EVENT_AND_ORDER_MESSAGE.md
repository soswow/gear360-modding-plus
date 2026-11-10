> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Material event payload and cook order messages

This continues [Pause gates and recipe-map selection](RAW_PAUSE_GATES_AND_RECIPE_SELECTOR.md). The purpose is to follow the same material pointer through control messages rather than infer identity from matching offsets. All results are offline static observations; **no verified sensor RAW pixels have been captured**.

Source: `output/decompile/rea/rtos-p6.bin`, SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, base `0x80000000`. New REA records use the source-bound `fulltext-row-state.elf`, SHA-256 `3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`. Earlier complete batch records are identified separately in the companion index.

## Material to owner, with the same material in the message

The getter `0x80251e4c` returns **material+0xf0**, the owner pointer. It does not return a material pointer from an owner. In the already recovered event-3 handler `0x802f3928`, the nonzero material+0x124 gate leads to Pause, then:

```c
owner = get_owner(material);               // 0x80251e4c
if (owner != 0)
    send_capture_notice(owner, material, 0x3005, 0, 0); // 0x80252d40
```

The equality of this supplied material argument and the handler's material is explicit at the call site. Construction, actual event delivery and live identity with the selected MID candidate remain separate conditions.

The actual owner setter is **`0x80250504`**, not the earlier guessed `0x80251e20` (which is a return instruction). The setter stores its second argument at material+0xf0 only when nonnull. The existing `CProcedure::SetMaterial` body at `0x80257bac` calls it with the returned material and **procedure+0xc**. This supplies a concrete static association writer; it does not by itself identify that procedure field's live owner or the selected R210 graph.

| Role | Evidence |
|---|---|
| Event-3 handler | `ev_a8c65d6664994f0a97443daecfd3d96e18ec3c70459dbc40143ca496ada54f56` |
| Owner getter | `ev_0b510a6e959006678f806377f4230fc4ed570879560fee2622751eff297a0cd1` |
| Owner setter and material lifecycle wrappers, complete batch | `ev_c1541ce94b3a346496a69eb4769db6a8da7aad92b817e56db38321a2aa22526c` |
| CProcedure SetMaterial, complete batch | `ev_439a952396376920726af9dda29f22bf87bf4bc422c9c13f01b9b8da01c8419b` |

## Selector six preserves that supplied material word

Sender `0x80252d40` builds a stack record at `r11-0x34`. For the handler call above its explicitly written fields are:

| Message offset | Value |
|---|---|
| +0x04 | material pointer |
| +0x18 | selector 6 |
| +0x1c | 0x3005 |
| +0x20 | 0 |
| +0x24 | 0 |
| +0x28 | 0 |

This table makes no claim about the other stack words. `0x802544cc` passes the record to `0x8051e560(owner+0x1c, message)` and returns its status. The handler's call does not demonstrate that enqueue or delivery succeeded.

The CCook loop `0x80252fb0` receives from its queue and, in selector six, calls **`0x80252dc4(owner, message+4, message+0x1c, message+0x20, message+0x28)`**. Here `message+offset` denotes the loaded word at that offset. Consumer `0x80252dc4` checks owner+0x60. If it contains callback object C, it calls C's virtual +8 with:

```text
(C, 0x3005, 0, 0, 0, 0x3005, material)
```

The duplicated 0x3005 is present in the reconstructed call; it is not a second observed event. The consumer returns zero whether C is null or the indirect call runs, discarding the callback return. Neither that zero nor receipt of selector six proves physical DMA completion, a retained pixel allocation, or a successful export. The callback implementation and its lifetime are unresolved here.

| Role | Evidence |
|---|---|
| Selector-six sender | `ev_b0641daee5401b0c1c0881f78e434127af10ddcb6db72b54cea02a88477454ae` |
| Queue-send wrapper | `ev_46f2312ea46b87c221dbf8709bf0ddc0043f33a93807a81b3e299604e3a84df9` |
| CCook receive/dispatch loop | `ev_7a6f5302301cb1af949282ff3d42ee809b402f516ee1c7150124b86d8cb112f7` |
| Callback consumer | `ev_97fd9b0d39e6a5f141aa659e26ff6fed0a826cf0229b95f008a03ce90324a6e6` |

## The handler has additional lifecycle messages

After the notice, the same handler conditionally calls `0x80250270(material,0)`, then calls `0x80250150(material, material+0x118)`. The latter argument is the loaded field value. The complete retained lifecycle batch shows:

- `0x80250270` writes material state +0x10c=6 and, for nonnull material+0xf0, calls `0x80252c80(owner,material,0,argument)`.
- `0x80250150` chooses its route using **material+0xf4**, a different field. When zero it can call `0x80252bb8(owner,material,0,argument)`; when nonzero it calls `0x80254988(assistant,material,0,argument)`.

Native sender `0x80254988` puts that same M at message+0, selector3 at+0xc, zero at+4/+8, and the two supplied arguments at+0x10/+0x14. Helper `0x802553ec` sends it through `Assistant+0x1c`. The Assistant loop `0x80254b5c` case3 takes M from message+0 and, when Assistant+0x50, Assistant+0x4c and M are nonnull, calls **`0x80250880(M,0,0)`**, the previously recovered post-lifecycle wrapper. This closes a conditional static event-handler → same-material → Assistant → post-lifecycle identity edge. The earlier global uncertainty about an initial event producer is narrowed for this route.

The runtime +0xf4 association, Assistant gates, delivery and execution are not observed. These messages must not be conflated with selector six. No source-bound scheduling or ownership proof establishes that a file copy precedes allocation reuse.

| Role | Evidence |
|---|---|
| Assistant selector-three sender | `ev_ebd2d43a56cfd3130af5ae2e5afc86cf70141651f8b1f7f93d3d579676f2a809` |
| Assistant queue-send wrapper | `ev_00f733d58156857438f4bc86d0cd347f0f4bbfeea3b0d46bdfd7aaf9be76aab4` |
| Assistant loop | `ev_5fe9729af43fa572838a8398b35eca7f8f92eb28d3a8fe701b890698a46856a5` |
| Sender's main source-byte window | `ev_0098db17ebd7956e974ee58b540612a751cbfaa58ff975908bc42a0f081c993c` |

## Four callers explicitly choose the zero map selector

Parent original-source instruction windows establish the following calls to kitchen helper **0x80255f94**:

| Call instruction | Recipe argument r1 | Outgoing argument eight, [sp+0xc] |
|---|---:|---:|
| 0x80337524 | 4 | 0 |
| 0x803376ec | 5 | 0 |
| 0x80337920 | 6 | 0 |
| 0x80337c04 | 9 | 0 |

The helper shifts r1 right by24, calls `0x80256868(this,index)`, and proceeds only for a nonnull result. It forwards its incoming argument eight (`[r11+0x10]`) into outgoing `[sp+0xc]` before calling `0x802526c0` at `0x80256058`. Thus these four compiled sites use book/cook index zero and carry a zero map selector on the successful send route. This is stronger than relying on a constructor default. Runtime object/table construction and actual execution of those sites remain unobserved.

The three native order-sender dossiers independently establish the next hop: their message base is `r11-0x38`; message+0x10 is zero / argument8 / zero for entries **0x802525b8 / 0x802526c0 / 0x802527e0**. Message+0x14 is argument3 / argument5 / argument5, respectively; the CCook loop installs it at owner+0x60. Thus the associated callback is supplied by an order message, rather than an unidentified implicit object field.

| Sender | Dossier | Main source-byte window |
|---|---|---|
| 0x802525b8 | `ev_735f4b100f891c7e4a577e8a64f7576238a909a7afdfb8498a6b3952398996b1` | `ev_518a9b2272b7feead9ea779beba611f472ad17497d7714b10fbd234973ff367e` |
| 0x802526c0 | `ev_d7779ebbb8c5d0e446bbc3a33ce8a047a4e66619b8b29e4dac672c244489f229` | `ev_a3d85de53727c038152234c39c5df10156341b2bf3176d83ce3c0a33e40a3db4` |
| 0x802527e0 | `ev_daa399554720ad5e9a42b4d48b8d0154275f9f5634a669e8e8ba57bd3f75a739` | `ev_ceed0d8bfa7c1e8d2c17d414f9d156a7f521f41c34c98dafaa0d4e1583927aea` |

A final bounded original-source check of getter **0x8057f8ec** shows it returns the word at its own receiver+0x60 (`ldr r3,[r3,#0x60]` at0x8057f900). The four callers therefore supply a callback word read from their supplied object's +0x60. This does not prove that supplied object is the destination CCook, nor resolve the callback's final vptr. Its40-byte source window and ten instruction words are retained in `multi-provider-run/callback-getter-source/`, separately from the earlier freeze.

A fifth aligned ARM BL caller, at **0x80138ae4**, forwards its incoming `[r11+0xc]` instead. That caller's three-register prologue makes this its **seventh** AAPCS argument, forwarded as the target's eighth; the value is unresolved. The scan covers aligned unconditional ARM BL instructions, not Thumb or indirect calls, and is not an absence proof for other callers.

The six source windows and155 decoded instruction words were checked against the original image. Their exact bytes/hashes and the scoped call scan are retained in `parent-order-source/source-byte-windows.json`, `parent-order-instruction-audit.json`, `order-sender-direct-call-scan.json` and `kitchen-selector-next-callers.json` under the multi-provider run. These are parent source checks, not newly invented REA Evidence IDs or complete function-range claims.

## Limits and next work

The known candidate's image/callback/slot association is already established. These new control-message edges concern the same supplied material at the recovered call sites, conditional on runtime objects and routing. Remaining questions are active recipe selection, the owner callback's implementation, exact post-lifecycle delivery, sensor/SSIF provenance, fresh completion, and a backing lease through the file copy.

Raw native reports, full dossiers and snapshots are local under `output/decompile/rea/multi-provider-run/owner-event-path/` and `order-message-path/`. The [companion evidence index](../owner-event-order-message-evidence.json) records retained containers, source identities and validation scope. No live camera operation was used.
