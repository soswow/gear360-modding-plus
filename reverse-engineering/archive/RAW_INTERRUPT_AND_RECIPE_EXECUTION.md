> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# RAW interrupt delivery and recipe execution

This continues the [previous execution/provenance findings](RAW_EXECUTION_PROVENANCE_AND_LIFETIME.md). The objective remains a reproducible file of verified sensor RAW pixels, with geometry, packing, completion and lifetime established. **No sensor RAW pixels have been verified or captured in this investigation.** All work here uses captured artifacts offline.

**Continuation:** [completion identity and recipe-book dispatch](RAW_COMPLETION_IDENTITY_AND_RECIPE_DISPATCH.md) shows that material virtual +0x18 starts a frontend video stream and that BranchManager virtual +8 selects a recipe book. Those results supersede the open “trace +0x18 / bind +8 to 0x802ba7b4” items below where they conflict.

The RTOS source is `output/decompile/rea/rtos-p6.bin`, SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, using its internal link base `0x80000000`. Complete pass9 records, source windows, immutable analysis carriers and closed snapshots are retained under `output/decompile/rea/continuation-20261015/pass9/`. Analyst function labels and Ghidra boundaries are interpretations; original instructions take precedence.

## Interrupt status reaches the registered completion callback

The missing callback-table consumer is **0x80038fb0**. It reads one pending-mask snapshot through **0x80039188**, which loads **0x20800018**, then scans bits 0–31. For each set bit it loads **0x8144a780 + 4*bit**, invokes a non-null callback, then calls **0x80039194(bit)**, which writes **1 << bit** to **0x20800020**. Parent independently checked the original ARM instruction sequence and register literals; Luna independently validated the source-bound carrier.

The previously recovered key-6 registration stores **callback 0x803118c0** at **0x8144a798**. The resulting static chain is:

```text
pending register 0x20800018, bit 6
 → callback table 0x8144a798 → 0x803118c0
 → PP_WDMA0 manager fanout, manager ID 3
 → FrontendNx listener adjustment / ID normalization
 → handler argument 2 → event mask 8 on FrontendNx+0x298
```

The one-hot register write occurs **after** callback return. That software ordering is established; the silicon's status-bit meaning, exclusivity and write-one-to-clear behavior are not independently verified. The name PP_WDMA0 describes the registered software route. It is not a measurement of transferred pixels or a current-frame identity token. Registry bit/key 6, manager ID3, handler argument2 and event mask8 are separate identifier domains.

Setup **0x80038e04** builds a descriptor naming this dispatcher and requests registration through **0x8051787c** with vector **0x27**. This establishes a compiled registration path, not observed on-device installation. The separate **0x2080602c / 0x8144a814** dispatcher remains a different path.

Evidence: consumer `ev_b2f5ecc6d37a74b33dafb64508ec238d73553657edbc94e499722b4dacc29bd9`; original bytes `ev_a6e361c0fbdefd2b0f081a07557cecb874e8fb6a049602a79145a0164d1d34b5`; pending read `ev_cb107b48b7e34e7d4a4f02a29bad3afc2cd24d2a6fdf31728b07129dda8313c9`; one-hot writer `ev_9d7e171af978d38c2e41bf23e325c59fbb2007860be274d2973e135c917c8fa9`; setup `ev_136232860d251098ac8d4601c939024f40a4ad16c40cabba5256281cd8fea97d`. Reports: `irq-registry-consumer/` and `independent-review/`.

## The event task belongs to the same frontend object

The earlier task-entry gap came from following a temporary base-class vptr. Base constructor **0x80541db0** puts **0x80783638** at **C+0x210**, but derived FrontendNx constructor **0x8020d260** later replaces it with **0x80671958**. That final addresspoint's slot0 is **0x8020e49c**. Its exact instructions subtract **0x210** from the receiver and branch to **0x8020e240**, the frontend event loop.

Factory **0x8020d948** passes that same **C+0x210** to process creator **0x8051e7b8**. Generic entry **0x8051e6b4** receives the subobject and calls slot0. Therefore the loop, event group, frame arming bytes and registered ISR listener statically belong to the **same complete C object**. This supersedes the old unresolved task-identity conclusion. It does not merge the separate task, termination-callback and ISR-listener subobjects.

The loop clears returned event bits after waking, then processes them. The recovered Route-A setup/frame-start tree does not establish a clear of old software bit8 or hardware pending bit6 before submission. A conditional pre-frame helper clears bit0 at **0x20800500, 0x20800600, 0x20801400 and 0x20800a00**; these are not the recovered pending/one-hot-write addresses. No frame-freshness guarantee follows from a shared object or a WDMA Done message.

Evidence: derived constructor `ev_76b21ab83e2a4d8f5cef52c4d04d11e303f748d61dc23cd8de6af1c1b1fbe638`; final table `ev_ca2a45cbacf77ac04537bd6b8a66162ca393e021a753285772c1f5a54531c051`; exact thunk bytes `ev_dc95b44af9ee76ce84c231d74c9beabac1ac5fc0b9377d6377a52b78673f7ac5`; process creator `ev_883170a5fea166521b7a4cb05e8d2b7b4ea7f35797b3d06be2cd2366904a1b44`; generic entry `ev_7f794caeb477b9945e8567fd7321988517470fa5e4f2552f5db53d0e0b8d1a8c`. Reports: `frontend-task-identity/` and `frame-submission-freshness/`.

## Candidate routing gate has a real constructor default

Setter **0x80568f3c(C,value)** writes byte **C+0x204**. Complete base constructor **0x80320e90** calls it with **zero**. Thus the tag-3 descriptor route's gate==0 condition is the constructed default, rather than an assumption from BSS or a partially inspected nested constructor. Debug handler **0x8022c928** can change the same byte for its on/off option, logged as SensorTeam Mode. Runtime changes and active candidate invocation remain unobserved.

Core's geometry-selection byte is a separate field, **CPP_Core+8**. Its constructor initially clears that byte. **Mode4 → path code10** selects image +4/+8; other returned paths select +0x0c/+0x10. The conditional candidate route is now resolved: the secondary copier writes tag3 enable/payload to C+0x14/+0x18, disabled tag4 to C+0x38/+0x3c, and disabled tag5 to C+0x44/+0x48. With helper zero, those rows leave C+0x14=1 and C+0x38=0. Selector **0x803212c4** consequently returns mode4. Frontend VideoSetInfo **0x80321df4** passes it through Preview **0x8031a250**, bridge **0x8031a37c**, and setter **0x8031ac28** to Core+8. Thus this conditional route selects primary dimensions, whose initialized row22 values are **5824×2896, stride11648**. The image-information initializer **0x8025c668** zeros both dimension pairs; the provider subsequently assigns only primary +4/+8. Alternate +0x0c/+0x10 remain zero on the traced constructor/copy path. Active invocation, later row/alias writers and actual image content remain unobserved.

Evidence for this extension: Core writer `ev_59ed67a9f1ffb2b3b7720f1f24144f5d1490678e418ad2353cc3b2109df57b09`; bridge `ev_8e783094975303f2405bb3380c55ad1f34fcb47809fcf45446f16417617bbb54`; selector `ev_24b30618809d9067513c59009690e1e9156163b5a48af0836f9a9b161ba2fbbc`; tag5 copier `ev_72156217ad5054a67efd7f7d1344ae91dfce8ed51530526c10197f44658b5664`; image-info initializer `ev_62f0be84a1d7340faa2bd7e1195cefb0443accc750877414c622d23b52529673`. Detailed report: `active-frontend-gates/core-mode-writer/report.md`.

Evidence: setter `ev_2ffadce05950b0b94a50d7bfbd3f9867096715d5c7417acea558e833f9a94892`; complete constructor `ev_70f6c4048e2b66ebef70e164e07eada641ad5dc9be001bc15e0a381fdc83d1df`; debug handler `ev_00309bf876111f40d5babf042d61b54f919a70c8c8861b06fd5ebc963fe8afef`; mode switch `ev_ac9b1a8da3f406316881f2e5b95d75d1d32e63a97b49b4aaa36d1b3bc3c3ebdd`. Report: `active-frontend-gates/`.

## Corrected factory entry and compiled dispatch

Address **0x8054a794** is an internal BL to factory constructor **0x800a0d04**, not an initializer function entry. Its containing lazy getter begins at **0x8054a744** and returns object **0x81c0d7bc**. Searching for callers of the interior instruction missed the real call path.

Thunk **0x800a0ebc** calls the real getter. Client **0x80115940** obtains its result and calls **0x800b53ac(factory,0xffffffff)**. That helper's mask-bit-0x4 branch invokes factory virtual **+0x10**, whose source-bound table cell **0x80626510** contains **0x800a10d4**, the NX1 createFrontEnd implementation. Guarded initializer **0x80111664** and dispatcher **0x800502a8**, command **0x01070001**, lead into this client. A longer compiled chain is now recovered: **0x80028e00 → 0x80013968 → 0x80011b80 → 0x800121e4 → 0x800a985c → dispatcher(0x01070001,0,0)**. Original word **0x805e55d4** contains 0x80028e00 in a record near ASCII `SLogDMSyDEvt`; this is a raw candidate task record, whose format, consumer and actual scheduling remain unknown.

This replaces the earlier inference that no compiled caller path was recovered. It still does not identify the physical sensor or observe runtime execution. Captured product metadata saying SM-R210 and a compiled CS5K driver class remain different forms of evidence.

Evidence: getter `ev_c47cf94d25a5c08d58677cb299ab701ba7286ef6d06a96f6d38e3402149f0cea`; client `ev_448418ccc34de941b3860f7c79d6efc03a18370b0bfaa6ae81c09f7701a8510d`; mask dispatcher `ev_b532a97d2fcd468cd3bfaade0b5f7a81dfefe13c9407c8aa8f20c161e396779c`; guarded initializer `ev_45e64c4aeab2638aaa0548221f4d7a5ab5bcdd40d8e661efcd1b4d25bb7a7f8b`. Reports: `r210-profile-selection/factory-activation/` and `independent-review/`.

## Material state runners reach the candidate updater

The previous material virtual **+0x10** caller gap is closed in the compiled state machines. Assistant **0x80254b5c** cases1/2 forward a CProcedure through **0x80255398 → 0x80257a48**, which selects runner **0x80258478** or **0x80258828**. Both iterate the procedure's +4 material vector. A state3 material reaches **0x80250ca0**, which checks material+0x10c, stores state4, and invokes virtual byte slot **+0x10** with that same **CMaterial pointer**. The MID-0x15000 table at **0x806a1f70** resolves this slot to **0x802e508c**, the wrapper calling +0x44 (table updater) and +0x48 (start route). Independent review checked the original branch instructions, table cell, complete body windows and source/carrier mapping.

The surrounding lifecycle is preparation **0x80250604 → state3 / virtual+8**, submission **0x80250ca0 → state4 / virtual+0x10**, completion **0x80250ec8 → state5 / virtual+0x18**, and post **0x80250880 → state9 / virtual+0x0c / post list**. These are static transitions with state and nested-child conditions. They do not prove successful hardware transfer. The procedure pointer, material pointer, image descriptor and pixel backing are distinct objects.

The actual CCook constructor is **0x8025227c**, which creates CRecipe at CCook+0x58; **0x80252410** is its nondeleting destructor, correcting stale analyst labels. A separate manager-identity trace shows constructor **0x803258e8** publishes the same CManager pointer into cells **0x81884628** and **0x81884634**. CCook follows that manager's +4 BranchManager; SetMaterial follows +8 Fridge. The concrete BranchManager +8 virtual target to builder **0x802ba7b4**, the corresponding recipe/procedure identity and active R210 queue payload remain unbound.

Evidence: submit wrapper `ev_b4a162ff8e1a4b9137ad36af3327a32e1da2c9dc2a43c7d4def41550ade98527`; candidate table `ev_0e1d651cfc7875a08723f7f180fe25ace004ee9761517dd1c67ec6d4105d30fb`; common method `ev_1eb866ea423e6dc30becab2183aa92e95689d27ff4f46c81a1b0daba99223a8a`; mode0 runner `ev_0996e2de2b9cd6fe7bf9d4d543bb639c1a3d93ed19e9f96bd1f04c6cbb1d52ef`; assistant `ev_823bfac974a9487c3c9a91a5ad98be9c244338fffa64a8846dbcd5fa7bcb891b`; manager constructor `ev_265fe324e48d9d7fadd872eddbfd60d244a6a16c60e45f4d39c80de3236fe715`. Reports: `active-recipe-executor/material-dispatch/`, `recipe-factory-binding/` and `independent-review/material-dispatch-review.md`.

## Optional export has a static RTOS command-record route

The command object at **0x81952108** registers global key `seq` and child `dump` callback **0x8027eb24**. Wait loop **0x8004e1ac** calls **0x8004e268**, which takes the request pointer at +0x2c into parser **0x8004b5dc**. For an `st` record, that parser reads count+0x20 and fixed-width 0x100-byte token slots+0x24. Registry **0x8004a848** consumes slot1 as `seq`; object method **0x8027d9e8** then consumes its slot1 as `dump` and passes five copied 4-byte string-object handles to the callback.

With original slots **[unidentified prefix, seq, dump, on, ssif]**, the callback receives `on` at slot0 and `ssif` at slot1. This condition writes full word **0x10** to **0x819521c4** through **0x80570b64**, allowing the previously traced optional recipe-time DumpSize registration. This is a reconstructed argument record, not a usable shell command. The prefix meaning, external producer/transport, task activation and actual flag value before recipe creation remain unknown.

Evidence: registration `ev_d5d6358cb07b085c8362dc2ee0a56c87d874f0aaf56d7de704cc65064259861b`; callback `ev_83c717cd560e5b8208822c1cc58399cc24e55d3dfbfbafa7eb72a3f380902fa0`; object registration `ev_084108a53e040d574fd676e1b39e58b3daa5eff7df3db16e964fb2eedbdecc4e`. Complete body ranges, command-parser/dispatcher evidence, argument ABI and versioned closed snapshots are in `export-enable-routing/rtos-dispatch/`.

## Remaining receive, event and lifetime boundaries

The apparent function **0x803219b4** is an internal call/epilogue of CapSetPPCore **0x803217d8**, not a separate entry. It passes frontend subobject C+0x50 into RDMA configuration; the nested input fields are **C+0x64/C+0x68** and mode byte **C+0x59**. They must not be confused with tag3 C+0x14/C+0x18. Neither input is bound to sensor-origin pixels or a verified SSIF receive path.

Event creator **0x8051ecdc** constructs request words `{0,8,0,0}` and calls **0x805177c8**, which issues SWI6 and returns an opaque token. A zero field in this request does not establish the kernel's initial event pattern. Its r12 literal **0x80320100** is frontend route-selection code in this image; no demonstrated SWI-dispatch semantics connect that literal to a kernel handler. Initial event state and per-frame freshness remain unknown.

The optional DumpSize/FileDump post callback waits synchronously for its protocol ACK, preserving its stack request descriptor. The ACK does not establish a pixel-buffer lease. Candidate state0x16 allocation and conditional cleanBuffers reuse still lack a shared owner/lifetime proof. The lifetime worker retained a bounded synthesis of earlier closed evidence in `candidate-lifetime-schedule/`; its old +0x10-caller gap is superseded by the new material-dispatch result above. No new source-bound completion/queue-to-lease edge was recovered. Following virtual +0x18 to establish any actual wait is still required; no acquired frame, completed sensor payload or safe reclamation schedule has been observed.

## Evidence validation

The [evidence identity index](../interrupt-recipe-execution-evidence.json) pins this report, reviewed worker reports, audits, source manifests, carriers and closed snapshot files. The reviewed report copies are frozen under pass9/reviewed-interrupt-recipe-execution/.

The [reusable offline validator](RAW_EVIDENCE_VALIDATOR.md) checks manifested source/carrier bytes and hashes, source windows, retained record identities and mapped REA byte reads. Negative fixtures verify rejection of malformed JSON, fractional coordinates and corrupted segment/window hashes. Unmapped and empty reads are explicitly excluded from checked reads. Snapshot schema/file names do not prove native-process exit; closure is audited separately.

Static delivery, default routing and reconstructed call paths are progress toward acquisition. Active execution, sensor-pixel provenance, frame freshness, actual sample packing and safe buffer lifetime still require evidence before any RAW-file claim.
