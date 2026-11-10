> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Still factory binding and YCC message coordinates

Reviewed16 October2026. Offline static analysis; no command was sent and no sensor RAW file was captured. The source is `rtos-p6.bin`, SHA256`cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. These results close specific compiled connections, not active R210 execution.

## The ordinary Still map reaches the known candidate

The missing factory edge is now recovered:

`recipe word1 → BranchManager book0 → Still book, static map+4 → selector key27 decimal/0x1b → factory0x802b3b90 → builder0x802ba7b4(param,0) → MID0x15000 / image selector0x28`.

The key requires the previously documented capture-parameter guards and no overriding one-shot key. Recipe word, map selector, factory key and material ID are separate values; **27 decimal is0x1b, not0x27**. Recipe3/key18 decimal remains a separate branch.

Still-book constructor `0x802804d0` initializes map objects at `this+4` and `this+0x1c`, installs vptr`0x806808d8`, and invokes population`0x80280920`. Lookup`0x80280570` uses map+4 for map selector0. The population body explicitly constructs halfword key`0x001b` paired with function pointer`0x802b3b90` and inserts that pair into map+4. Parent original-byte review confirms the key/pointer instructions at`0x802811b0..0x802811dc`; retained factory body and original BL at`0x802b3c40` reach the MID builder. This replaces the older open static factory-binding question.

Fresh EIDs: book`ev_803e2b77377269c5225d8114a478d6708518bc172dc31358274fbf9595e91ca2`; constructor`ev_d828b750e98605a39a8ab8f3d622ea4d324485fdb7466f67f63e3baf121d7109`; population`ev_ba47cfea09c78c1f47511bfe6346ab65c43ccb0e16ed9a8f23afb8919292cd6a`. Complete records, body gaps, snapshot and source windows: `output/decompile/rea/pass-goal-20261016/recipe-mid-binding-luna/`. Its report cites the retained factory/builder records. Actual selection/execution remains unobserved.

## RTOS outer handler is now identified

The receiving object is **CAttributeHandler**, not the distinct CCommandHandler used by ACK commands. Owner factory`0x80061650` constructs it through`0x801293f4` and stores it at owner+8. Its installed vptr`0x8064fba0`, slot+8, reaches Run`0x801294e4`.

Run stores inbound message pointer at this+`0x0c`, payload pointer **msg+8** at this+`0x10`, and supplied length at this+4. The same receiver traverses:

| Read/lookup | Exact compiled selection |
|---|---|
| Halfword msg+2 |0x81 →index2 →0x80129e98 |
| Halfword msg+4, high byte |2 →index2 →0x8012a084 |
| Same halfword, low byte |0x88 →index20 →0x8012e0e8 |
| Leaf |requires supplied length4; forwards msg+8 to logical0x01090006 |

The table cells are read through their established startup ROM→RAM mapping. These are internal RTOS coordinates, not a captured packet. EIDs: Run`ev_303ca86596d3ba345c08683d1e9e72284d3a0ab39c5b19aaad157566bb1d742a`; category selector`ev_cf37b397d150d8f378e9a8ff8c71c0636a64793a2530cbb7eee3dae2348afefa`; high-byte bank`ev_5122b04b9afe7b8ace30809e595af0c8941e6e11e5b4c9db2ca1ed71cc5557c8`. Details: `pass-goal-20261016/grok-ycc-outer/`.

## Linux serialization must keep its own coordinates

Linux `libmulticore-bridge.so`, SHA256`9129761c6ee9f6b62968d3a8307a8b5fbf02e1493323658c42d9ebe587673a20`, uses verified REA bias`+0x10000` over ELF VMAs. `CSender::createFrame` builds a stack header with initial bytes2,0, category at+2, subcommand+4 and length+6. **CFrame::copyHeader copies only header bytes2..7** to its allocated data pointer; copyBody uses data+6. For `(0x81,0x0288,4,payload)` that buffer has category+0, subcommand+2, length+4, payload+6; CFrame::Send supplies size12. Parent confirmed the critical loads/stores with LLVM on the original ELF.

Fresh EIDs: createFrame`ev_4ae9a0d587a944a5ea4c452feba505fe87c3987684c87283bd5b1f9ecea06670`; copyHeader`ev_7e30fb54f38bf827900bea652e193e87268d3d289db3ae24e10dd27e5bfd7c0c`. Earlier complete CSender/send/CFrame::Send records were reused rather than re-decompiled. They were missed by the first MiniMax retrieval inventory; the parent located them directly under`userspace-capture-path/`.

## Receive-side boundary, without inventing a prefix

Fresh RTOS Receive`0x80130ae8` passes its CFrame data pointer and size to wrapper`0x80069cb4`. Handle`0x80130c74` passes that **same data pointer**, category read at data+2, and **CFrame size−10** to Broadcast`0x8013159c`, which forwards unchanged into the family callback/outer handler. Parent additionally checked the category helper, constructor, allocator and pump callsite against original instructions. No two-byte rewind or header rebuild exists in these examined higher layers.

Therefore the Linux send-buffer coordinates cannot yet be renamed the RTOS message coordinates. Both the two-byte prefix and the effective packet-size accounting have to be explained at the lower IPCC boundary. On the normal examined pump path, CFrame size equals the IPCC-reported size; a four-byte leaf payload would require reported size14, while the Linux CFrame send supplies12. This is a conditional arithmetic observation, not proof of a broken protocol or a measured packet.

EIDs: Receive`ev_2e0cefd34386eb1ab40ac8edeeb21597a8e8dcdc0d9718caf6f52b42702ecdcb`; Handle`ev_1c942cc47f096f8f7e19bd505d6410d07c64b10f4c68ee72ce3652e91a6a876b`; Broadcast`ev_b2239d21bac547eab61ed8a00ace38da8dd77bcad82edcbf313d333146f29908`. Records and snapshots: `pass-goal-20261016/grok-receive-prefix/`.

The worker sentence “Kernel IPCC still fills those bytes” is **not accepted as a finding**: it did not establish which lower layer supplies or transforms them. Parent original decoding narrows the next edge to wrapper`0x80069cb4 → driver0x805084f0(dest,cpu,capacity)` and the IPCC size producer. No live serialization or delivery was observed. The [direct SSIF debug route](RAW_STOCK_SSIF_DEBUG_CAPTURE.md) does not require the YCC operation, so this transport boundary must not indefinitely delay the first-file decision.

## Lower receive driver review

Two bounded fresh dossiers narrow the remaining edge further. `805084f0` maps the CPU to a channel, enters/leaves its guard and forwards the same destination/capacity to `80038164`. That function first reads4bytes to the destination via `80037360`, tests byte0==0xff OR byte1==0x7f, then reads the requested count through the same routine into the **same destination**, returning the second count. It does not insert or restore a two-byte application prefix. This adds an outer framing read but still does not reconcile Linux send12 with the RTOS application offsets/size14 requirement; the stream producer/consumer and size accounting below it remain open. No raw packet is proposed for use. EIDs: `ev_86cfad536b79915d86cec6314c9b11427047539c9c67b46d7fd29a9890dcfc26`, `ev_1b315d9397c63b9e73f813d97643beadb149cd541399fb70d267dc60cbd8ff40`. Complete records/snapshots: `pass-goal-20261016/ipcc-receive-driver-luna/`.

The immediate read helper `80037360` is not analyzed by this bounded task: whether the4byte probe consumes or repositions the stream remains unknown. Calling it an outer framing read describes the compiled calls, not a verified wire format.

Versioned complete records and review traces are indexed in [the goal-focused checkpoint](INVESTIGATION_CHECKPOINT_20261016_GOAL_FOCUSED.md).

## 19 October offline reconciliation of the prefix boundary

The historical two-byte prefix/size gap above is now statically reconciled in `output/decompile/rea/pass-owned-init-20261019/framing/parent-review.md`. The Linux kernel writer adds a six-byte header; the RTOS ring reader consumes four before its application read, leaving the two transport bytes before the Linux frame. For a four-byte payload, Linux size12 becomes RTOS size14 and logical14−10=4. This is conditional source evidence on an adequate-data successful path, not live delivery or a new packet authorization. Empty reads can still report a length without copying. See the new report for original instructions, hashes, independent review and limits; do not retain this old gap as the next static task.
