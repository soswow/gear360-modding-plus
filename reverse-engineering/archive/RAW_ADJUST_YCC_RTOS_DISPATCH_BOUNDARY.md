> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# RTOS Adjust YCC dispatch boundary

Reviewed 16 October 2026, offline. This extends the [Linux call-argument and RTOS logical contract](RAW_ADJUST_YCC_CONTROL_ROUTE.md). No camera/firmware was executed and no verified sensor RAW file exists.

## What the native records establish

RTOS wrapper **0x8012e0e8** checks word **this+4 == 4**. Success passes logical operation **0x01090006**, the pointer stored at **this+0x10**, and length4 into **0x80051340**; otherwise it returns -2. Its complete body is 0x8012e0e8–0x8012e14f. Evidence: `ev_13f64da376c1a2c1c960b94d6d7b0b5e3b6a39f6aee162aa5ed9ce5cdaf8be9a`.

Dispatcher **0x80061b44** saves inbound pointer and length at **this+8/this+4**, clearing both when the pointer is null. It obtains a category from **0x80064ef8**, then selects a two-word member-function record at **0x8100f7c8 + category*8**, applies its encoded receiver adjustment and calls the selected target. The dispatcher body is 0x80061b44–0x80061c23. Evidence: `ev_5244fe24acce0c76202fa66439ae55fe17296ac33a330a26f6133c72c97a3855`.

A small original-assembly window for 0x80064ef8 shows that it reads a halfword at **inbound_pointer+4** and derives the category from its high byte; null input selects0. Category1's table target is the already recovered 0x80061c24, while category2 selects 0x80061d28. These table entries alone neither join nor exclude the setting wrapper further downstream. In particular, the first-level table lacking the wrapper's address is not proof that an indirect descendant cannot reach it.

Probe **0x80131740** was not a packet parser. It is a selector-to-ordinal switch: input0x81 maps to0x0c, input0x0288 takes its default -1 branch, and the function reads no packet buffer. Its relationship to this MCB transport remains unproved. The matching number0x81 is not itself a transport edge. Its non-contiguous provider ranges are preserved. Evidence: `ev_35f2d4f54d3aa68ed0d5985dddf7be25f5703ccfb9ba9c7299f99970d79b607d`.

## The method-table cell has a startup RAM alias

Original source cell **0x807fb544** contains wrapper pointer **0x8012e0e8**. This source location is omitted from the analysis carrier's data segment, so absent native references on that carrier cannot establish absent callers.

The existing startup-copy proof copies source **0x807ef900–0x80c31840** to destination **0x81006000–0x81447f40**, using delta **0x816700**. The ROM/source cell therefore has runtime alias **0x81011c44**. Comparing a source address directly to the destination interval had incorrectly suggested it was outside the copy. That coordinate mistake is corrected here. This is a compiled initialization mapping, not an observation of current RAM.

A parent arithmetic hypothesis proposed possible table bases0x81010800/0x81010804 under different eight-byte row shapes. That arithmetic is explicitly **not** a discovered base or mapping of sub-ID0x0288. The original hypothesis and the bounded worker followup are preserved so later work can accept or reject them using an actual indexer.

## A real bank now reaches the wrapper

Final native dossier **0x8012a084**, Evidence `ev_772bbbeccebc7ff9f294afd45c849aa9588cc6ac34d758cd88f1cc2f6326bc37`, calls **0x801300a8(this)** and indexes member-function bank **0x81011ba4 + index*8**, applying its encoded adjustment before the indirect call. Original helper instructions load **p=*(this+0x0c)**, read the halfword at **p+4**, truncate it to its low byte, and map **0x88 to index20**. Runtime cell **0x81011c44** then contains **0x8012e0e8**, with zero adjustment. Its ROM/source cell is0x807fb544. Parent independently checked the helper's load/truncation/comparison and return-20 instructions.

Thus the compiled **low-byte0x88 → index20 → YCC wrapper → logical0x01090006** lookup is now recovered. It uses an actual bank, not the parent’s guessed0x81010800/0x81010804 base. Any halfword with low byte0x88 can satisfy this helper; this alone does not identify0x0288 uniquely. The remaining join is the source/identity of p, the outer handler selecting this bank, and the payload-pointer/length setup. No serialized packet or live delivery was observed.

## Remaining acquisition boundary

The Linux native records prove arguments **(0x81, 0x0288, length4, payload)** at the captured bridge Send call. These arguments alone do not prove the serialized header layout or that the RTOS halfword at inbound+4 contains0x0288. The exact send/receive/indexer join remains a separate obligation. Neither the known dump ACK0x0105 nor a category1 dispatch proves that join.

The [evidence index](../adjust-ycc-rtos-dispatch-evidence.json) binds complete dossiers, snapshots, original-only table/assembly windows, manifest mappings, reviewed corrections and the final bounded indexer result. RTOS source SHA256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`; carrier SHA256 `7f5ab09a495106bcabebd87f979fda5c9f585881a5bd1bb29021d553a741c2ca`. Ghidra/source identity checks do not certify runtime execution, delivery, active recipe selection or sensor pixels.
