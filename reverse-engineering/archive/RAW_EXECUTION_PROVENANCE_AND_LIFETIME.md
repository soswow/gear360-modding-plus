> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# RAW execution, provenance and lifetime investigation

This continues the [activation and geometry findings](RAW_ACTIVATION_GEOMETRY_AND_COMPLETION.md). The long-term objective is a file containing verified sensor RAW pixels, acquired reproducibly with established provenance, geometry/stride, packing, completion and safe buffer lifetime. Checkpoints and credit cutoffs are milestones. **No verified sensor RAW frame has been recovered.**

**Continuation:** [IRQ delivery and recipe execution](RAW_INTERRUPT_AND_RECIPE_EXECUTION.md) supersedes the task-entry and factory-entry gaps and resolves the frontend gate constructor default.

All findings below are offline static analysis. The RTOS source is `output/decompile/rea/rtos-p6.bin`, SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. RTOS addresses use its internal link-base coordinate `0x80000000`; this is distinct from observing a bootloader load or live execution. Full pass8 evidence is retained under `output/decompile/rea/continuation-20261015/pass8/`.

## Capture provenance

The captured full eMMC GPT identifies partition 6 as `rtos`, LBAs **186369–247808 inclusive**. Its byte interval is **[95420928,126878208)**, equivalently **[0x05b00200,0x07900200)**, exactly 31,457,280 bytes. Parent review independently checked the primary GPT header and entry-array CRCs and compared that range byte-for-byte with the analyzed RTOS file. Its independently computed SHA-256 matches the pinned source. The full 7.8 GB image was not rehashed; its existing sidecar digest remains a recorded identity.

Boot0 contains strings describing M4 image/SRAM loading, A7 T-kernel startup and A9 Linux startup, plus a Linux root argument selecting `/dev/mmcblk0p9`. These strings do not establish selected image partitions or a load-code path. The p5/p12 Linux uImage headers give load/entry `0x86008000`; that value must not be assigned to p6. Boot0 analysis carriers use synthetic file-offset coordinates, explicitly distinct from a runtime load base. Investigation of the boot image descriptor table is continuing; loader consumption and active boot selection must be distinguished from matching table values.

Source records: `boot-artifact-binding/capture-provenance.json`, `partition-image-provenance.json`, `report.md`, byte-preserving boot-carrier manifests and closed REA snapshots.

Boot0's row at file offset **0x180** contains little-endian values **LBA 186369, 0x618d sectors, 0x80000000**, followed by **`rom.bin`**. Parent checked these exact bytes. The start matches p6; the declared transfer length is **12786176 bytes**, covering p6's final nonzero byte at offset **0xc3181f**, with zeros afterward. Another row identifies the Linux image's GPT start and its header-adjusted destination, independently supporting the table interpretation. This is a strong static image-map binding to the RTOS link base. The table consumer and actual boot selection remain unobserved.

Under the bootloader row's declared load base **0x02000200**, boot0's reset stub branches to file offset 0x24, switches mode and transfers to **0x80000000**. The p6 reset word in turn branches to **0x80000140**. These mutually consistent instructions do not establish that the device executed that boot path. Evidence: table `ev_b7096affbf6e17b41ee34e1bde5129484c91f176a944d9334d4e7e5be8b4e93e`; boot stub `ev_f002d086a90b5643a7b92815cdc88766651eb30a2103b58b41425e7b36cd6fa2`; p6 vector `ev_fd19fead2c3cbdb2fc7946ab0b755992c66d2620e8fe5798d8030e571170630c`.

### Later runtime copy and zero initialization

Routine **0x80011600** explicitly copies **[0x807ef900,0x80c31840)** to **[0x81006000,0x81447f40)**, then zeroes **[0x81447f40,0x81c16060)**. The copy consumes 0x441f40 bytes, ending within the boot descriptor's declared payload. Both settings-table addresses and export flag **0x819521c4** lie in the zero interval. This establishes their conditional zero state at this routine's clear step, using actual stores rather than the partition's zero padding.

The routine appears in a startup callback registration path and calls later helpers after the loops. Its execution and ordering against specific configuration getters/constructors remain unobserved. In particular, the geometry initializer subsequently has its own explicit nonzero template copies, and no active export-flag value follows from the startup clear alone. Evidence: complete routine `ev_948da8b550d7cd7628eb8a7b69e389ebbcfa9a1e1a56060430a294e0ca148916`; callback pointer table `ev_23623ce572201b43d31cc5928e8abfb5957284cede66c80e277651e5c20d4bc5`. Full report and closed private snapshot: `rtos-static-initialization/`.

## Exact candidate geometry boundary

The selector-`0x28` configurator `0x80141c78` reads the image width and derives/stores stride at `+0x60`; it sets format/buffer metadata and resets the descriptor. It does not assign image width/height at `+4/+8`. The candidate pre-callback `0x80138d54` installs backing through `0x8025cc24`; its descriptor calculator reads geometry and can calculate a zero-stride fallback. These inspected methods contain no width/height overwrite. This is a bounded negative, not a whole-program exclusion of indirect setters or aliases.

The WDMA consumer reads the image in **CPP_Core+0x10**. Its preceding receiver **CPP_Preview+0x14** is a separate object from allocated FrontendNx. The conditional descriptor-pointer chain recovered later in this pass is detailed below; active selection and buffer contents remain unobserved.

### Corrected provider and initialized dimensions

The exact configurator requests **provider entry `0x0d` (13)**, not **`0x13` (19)**. Registry `0x80308534` constructs that slot through **`0x80309bfc(...,1,2)`**. Its vptr **0x806a6c08**, virtual **+8 → 0x8057b37c**, copies the embedded image-info record through `0x8025c960`. The selector **0x80211c08** explicitly maps role **0x0d → settings row 0x16 (22)**. The previous association of this exact candidate with role 19 / LDC-selected rows 25/26 is withdrawn; those rows describe a separate provider.

Initializer **0x8020c3dc** clears the two setting tables, then seeds row 22 using **100-byte constant records at 0x80670178 and 0x8066fe58**. Both records contain width/height **0x16c0 / 0x0b50 = 5824 / 2896** at offsets +4/+8. The byte-copy helper takes destination first, source second. The selected table is subsequently copied into manager+4 by **0x8020cb48**; row getters read those fields at manager **0x81882e1c/0x81882e20**. Parent independently checked both original constant records and the arithmetic; a separate Luna review checked the constructor, selector switch and copy direction.

| Conditional initialized layout | Value |
| --- | ---: |
| Width × height | 5824 × 2896 |
| Derived row stride, `2 * align_up(width,16)` | 11648 bytes (`0x2d80`) |
| Stride × height | 33732608 bytes |
| Candidate allocation request | 67108864 bytes |

These are **initialized settings and a derived layout**, not a measured live frame or an observed export length. The exact provider independently sets metadata bytes **1/2/4/1** at image offsets +54/+58/+59/+5a, matching the previous branch's numeric values. Thus the format/packing numbers survive the correction; the geometry source changes. Later row writes, active selection and actual sample content remain unobserved. Flat zero words at the tables' BSS destinations were rejected as default evidence because the initializer copies nonzero constant records there.

Key corrected records: provider constructor `ev_e0156f0a81ecc7c560e204ea2bab5e6663050478a69b3d680db31cf8b4ee28d3`; selector switch `ev_74bd84ea347f9d7e87046ef9e40d087dace759eca6e7d2344879bfb9c6a9b4ae`; initializer `ev_c52615668bffe10d08e5cd6c6bb3463038202be50cb07f03610518ad2ca92598`; selected table copy `ev_f376f54c504b99ae3e2311e3a5a1a14be476bb6cb42fd20df5b9853fdc61e829`; copy helper `ev_03536511788845a424ba6eca64f4bda7c16b1631e68686b3eaaecf40728216f7`. The parent original-record check is `parent-geometry-template-review.json`.

Evidence: configurator `ev_243a14906940d19c8c217d2590cea3c1261b12bb2b14c9a0a028ea92657d7bd1`; callback `ev_f82128b7bee927a05009d810551a65db501794d7b93af200f2946268f884ce7a`; descriptor setter `ev_c19ace5db7ad2c354af2c45740d87cdabacc950555539543b4dd28e5775f8b2a`; WDMA consumer `ev_7051062c41beb0bc6ffc9e58f89ade36310c3674d85316d68557de978ffd9095`. Full report and original windows: `active-geometry/`.

## Backing ownership and possible bulk release

The state-`0x16` candidate path requests a contiguous **64 MiB**, tag `0x1c`, in 16 KiB pages. Ordinary release, manager virtual `+0x5c` → `0x800adb1c` → `0x800b04b4`, clears the accepted pointer's allocation records, allowing subsequent reuse. The inspected release implementation contains no per-buffer reference-count gate. That does not exclude ownership/synchronization elsewhere.

The known generic image-list insertion `0x8013b5f0` in callback `0x80138d54` is guarded by state `0x11`; the candidate's `0x16` branch skips this insertion. Thus that site does not establish membership in the generic list later drained by `0x8013cdd4`. The inspected image destructor frees the image object without returning its arena backing. These facts do **not** establish a leak, indefinite retention or a stable pixel lease.

A separate `cleanBuffers` method `0x800adfec` sweeps the page-index interval stored at manager `+0x680c4/+0x685c8` and frees records with nonzero tag/length metadata. Mode-0 allocation can search that same interval when manager flag bit 2 is clear; with the bit set its search starts at index zero. A candidate run in the interval could therefore be reclaimed by that sweep **if it executes**. No invocation edge, actual page index or runtime flag value is established. Empty direct xrefs do not exclude an indirect caller.

Manager virtual `+0x68` is `0x800adfa4`, which calls `+0x94` and `+0x84`; these release three High-ISO pointers and deferred lists for tags `0x39/0x3a`. They are not an identified global free-by-tag-`0x1c` route. Export ACK handling still supplies no proof of candidate backing ownership or order relative to reuse.

Evidence: allocation/release `ev_3a7ba22f41750fc725010fdab3e89bbe3fa6247e86876b9c5c46ea0ecd1afb27`; sweep `ev_dd23e0c0aadbae80f497a9d5825ba32aabc99b54c58f10ce9322188c50ec484e`; vtable bytes `ev_65262ee87dd742cf36b34d982a4c31627a654a3cfac5312cde38c20e7c841be2`; candidate list-insertion boundary `ev_92866b71e4ddaef410e853bc66f777c73ee59b5553f41af9ba80d6797f6317d0`. Full report, windows and closed snapshots: `backing-owner/`.

## Review discipline

Parent review checks original bytes, carrier mappings and signed branch arithmetic before accepting inferred edges. A manager event identifier can be translated before the final handler receives it; names and diagnostic labels cannot substitute for that code. A partial carrier ending inside a function cannot support an absence claim. Such interpretations are corrected before integration. Independent Luna review supplements the parent byte checks.

## Interrupt registration and event translation

Factory `0x8020d948` allocates FrontendNx **C**, publishes it through pointer cell `0x81952fc0`, and registers descriptor **C+0x238** with interrupt managers through their virtual `+0x0c`. The constructor stores vtable addresspoint **0x806719ac** in that subobject; its **+8** entry is **0x802128e0**. The preceding ABI table-group address is a different coordinate, not the stored vptr.

The thunk's exact ARM words are `e2400f8e eaffffb1`: subtract `0x238` from the receiver, then branch to **0x802127b0**. It does **not** branch directly to handler `0x802128e8`. Parent independently decoded the signed imm24 with ARM PC+8 and checked the original source bytes; independent Luna review agrees. The intermediate switch translates incoming manager **ID 3** to primary virtual **+0x80, argument 2**, on complete receiver **C**. The primary handler then sets **C+0x298, mask 8**.

The manager labelled `PP_WDMA0` in the factory diagnostics constructs with ID 3 at manager `+0x10`. Thus the recovered route is PP_WDMA0 manager → registered subobject → ID translation → software event bit 8. VdRising has manager ID 1, not the argument 2 used by the final handler. Confusing the two identifier domains would produce the wrong completion conclusion.

The task adapter at **C+0x234** is a separate subobject from the ISR listener at **C+0x238**; they belong to the same constructed complete object. Its slot+8 thunk **0x8021277c** adjusts by 0x234 and branches to **0x8021274c**, which sets **event mask 0x20**. It is neither the manager-ID switch nor the task-loop entry. The exact task bootstrap-to-loop call still needs proof. Frame start sets the arming bytes, while the previously traced loop clears returned event flags after waking. This establishes more software object identity than the earlier report, but no frame sequence token, clear-before-submission guarantee or upstream hardware status/acknowledgement is yet established. A named manager and a “WDMA Done” diagnostic still do not prove fresh, sensor-derived output.

Evidence: registration `ev_519799b20961605f0d147a469a37f0a62cd064132c646eb8f5204c00f3904ba0`; vptr bytes `ev_0ef56bfe885f71be32761afec92e8c640cabf6d5c7ac911b32e90cb9c5ab5275`; thunk `ev_8b10f143aec09b50e798f66a12586eccd8faae0e207fd6f789c8908fbddbb29c`; intermediate switch `ev_f5a7e5adfe38d8320cb1a7ce2ba309fe7c0afdaf67aa1b6afe168ab9dded8c8a`; final handler `ev_a9cb3de6f4ee61d56176130adcc92b8cb1fc0a03d1ca9518841dc89932cb5fa5`; manager constructor `ev_52f91b8455f191a94ede8d5f53d30910f0cd077c8e3fab2195c2630c65666148`. Full report and independent review: `frontend-completion/`; parent signed-branch verification and disassembly: `parent-irq-source-review.json` and `parent-irq-source-disassembly.txt`.

### Upstream callback registry

The manager's registration method **0x80311858** passes **callback 0x803118c0** through CPP_Core wrapper **0x8031d5d0 → 0x800cad30(6,callback) → 0x804ff790 → 0x80038f4c**. The final non-null case stores the callback at **0x8144a780 + 4*6 = 0x8144a798**. The callback obtains the PP_WDMA0 manager and invokes its **+0x14** fanout, extending the software bridge above.

This registry is distinct from SSIF status-dispatch table **0x8144a814**. No hardware status/acknowledgement edge joins them. The adjacent API labelled unregister passes null, but the inspected registry's null path logs without clearing the slot; it must not be treated as proven callback removal. Parent raw-instruction review also corrected a Ghidra boundary error: **0x804ff7cc** is a valid tail branch to **0x80517fa8**, not bad data.

Evidence: core registration wrapper `ev_079d7172c1b82c92d7f32e44f0963cbbda375622e5d93b78292615a9ec675034`; registry `ev_a9e006363f8cd5269e26e4aa9a67ca3c17058b12c19857e2b382dfb67ec02f78`; original registry bytes `ev_d108641e3bcbb816e8c3227207057e895fd59ff0e06d30d0250dce9dc802839c`; registered callback `ev_2a083fdab590bff1e37ae0b86618288988c1b8eccf1ae08a36db64b31f793fff`. Full report and closed snapshots: `hardware-completion-source/`.

## Conditional material image identity

MID15000's image slot zero feeds enabled tag 3 in the six-row material table. The manager must reach its secondary copier **0x80322598**, and helper **0x80568364(C)**, reading **C+0x204**, must equal **zero**. Under those gates, the exact **CCapImage descriptor pointer** flows:

```text
material slot0 → tag3 payload → FrontendNx C+0x18
 → base VideoSetInfo → CPP_Preview B+0x14
 → frame consumer → CPP_Core A+0x10 → start_wdma
```

The next tag-4 row is processed **even though its enable byte is zero**. If the helper is nonzero, its null payload replaces or suppresses the tag-3 route. Therefore merely observing an enabled tag 3 or helper!=1 is insufficient; helper==0 is required for this candidate path. Runtime gate values, invocation and outer execution remain unknown.

The pointer above names an image descriptor. WDMA calls its accessor **0x80541b48 → 0x800b4e00**, reading the embedded wrapper's **+4** value; it does not use the descriptor address as pixel memory. This closes a conditional descriptor alias chain without identifying a live sensor receive allocation or proving completed pixels/lifetime.

Evidence: row writer `ev_5095dc4bdd868172ac86dd823ed11bc8348d0736d9c63bce28cdb5b0f621823b`; manager `ev_6631a35b435bbe721d6b323d7e676bed3769e234eeadfc76f9795a351bd2381d`; copier `ev_d89f0f4456ae7365382c163120c9f98780dac38e6778ec9ccee9a088accd7ffb`; base setter `ev_013962ceae2d2ff6385c0dbd2e9f6f97d83535737a6f88f556fb863a08fe1c35`; Preview consumer `ev_b6f21bfb11b29158b1d750db4d72eaacdecf8da6e620a52596dd49cf4470151a`; Core setter `ev_bb82bb0f393113f01e5268b5fbdc49fcce74d9f303710b892948cea2b69d2b1d`; buffer getter `ev_c1451fa6cff3550948c0dbfe3178919f014041b92362af216e71e802bfb7e17a`. Full report: `material-image-alias/`.

## Recipe and event execution layers

The graph parent is **CRecipe**: it holds CProcedure children in vector **+0xa4** and fixed child slots **+4..+0x50**. Each 0x20-byte CProcedure stores its parent at **+0x14** and its own material vector at **+4**. SetMaterial adds a material to the child vector and conditionally links the same pointer into the parent's distinct fixed material slots **+0x54..+0xa0**. The bounded class scan finds linking/clearing of that fixed material array, without its executor.

The actual phase runner is **CCook virtual +0x30 → 0x802538d4**, identified by vtable addresspoint **0x80678f48** and RTTI `CCook`. It reads recipe **this+0x58**, then calls **0x80259518**, which walks CRecipe children; **0x802578f8** walks each CProcedure's materials; **0x80251178** invokes eligible material virtual **+0x1c**, then sets state 8. This is a concrete nested lifecycle operation. It does **not** call material virtual **+0x10 / 0x802e508c**, so it does not close the missing table-submission trigger.

The separate **CCook_Assistant** queue dispatcher **0x80254b5c** receives a material pointer in an event and invokes post lifecycle **0x80250880** for event codes 3/4/6. An event-provided pointer is not proof that it names the exact MID15000 instance, belongs to CCook's selected recipe, or follows fresh DMA. Its event producer, active recipe assignment and candidate export/reuse ordering remain open.

Evidence: graph construction/link `ev_b04b2e9ea68ec0004fa227a9887988a5fd4021843f282243e0fb2c65aee89a1a`; phase walkers `ev_1ff2af027c8c8263a6da778f9d2a7fde6a3c7ec50f46fcca86d0d091f422fc0e`; CCook vtable/RTTI `ev_d638e8d217b9870d8d0e11fb026709d2fad4f1291f0f717b77d1e9c9e2591f13`; complete phase method `ev_94d7b8aa9986e04f22bfdc197ceb4bf6b8aec1fe982b14a2e854b1ae61197743`; assistant dispatcher body coverage `ev_f68beb0514f334a1bb7cd06ff958b946d89ca1bc28a4cbd179658f7e1d0f8703`. Reports: `outer-graph-executor/` and `backing-owner/outer-independent-review.md`.

## Export configuration boundary

The registered `dump/ssif` handler belongs to the lazy **CBranchManager_NX1** tree rooted at **0x819520e4**, with dump child **0x81952108**. This is the builder that separately creates the CFridge_NX1 object; the builder and its fridge are not interchangeable receivers. The call through **0x805836e0** is destructor registration, not command execution or proof of activation. A liveview-process method reads from the same tree, but no invocation setting `ssif` before recipe creation is established.

A bounded original-code scan found nine adjacent MOVW/MOVT references to **0x819521c4**: eight readers and the already-recovered full-word setter **0x80570b64**. This does not exclude indirect or bulk writes. The flag's active value remains unknown; its recipe-time test and complement/full-assignment semantics still apply. Evidence: root `ev_c9f79e9e5d49103a8c424b22a20bd3526b40dec5ca782dbed769225a8f1695b0`; builder `ev_d33c2d0919f68f0fda8f2e18ececf3ba13597e68331d3d3ef524dcbc2d072cf2`; liveview consumer `ev_318fa7b183daa44e6b71aa6d541f3afcc5400ff266f1d44e15ee9f7b5f887320`. Full report and scan limits: `export-config-invocation/`.

## Output encoding and receive-path limits

For this candidate's pack type 1, **0x8031c658** selects pixel enum 2 and returns internal D5 control code **4**. With a nonzero buffer accessor result, start-WDMA builds **0x0401** for the third output group; fixed 0x0400 controls in the other groups have zero low bytes and skip their guarded branches. Parent checked the conversion switch and dispatcher guards against ARM instructions. The resulting register bitfields are software encodings; they establish no sample byte order, Bayer layout or 12-bit unpacking rule.

Start-WDMA's dimension choice also depends on its mode-to-path conversion. It uses image +4/+8 only for path code 10; otherwise it reads alternate +0x0c/+0x10 fields. The initialized 5824×2896 dimensions therefore must not be advertised as every invocation's submitted dimensions. Evidence: conversion `ev_3cd108e9c56e23d566c3e706999a2e72776fa8a14f19ef66fe989e0d8c9d2aaf`; start `ev_e1e4b520b33af1ddd1f9eeb282d692e455a1148b39b9809c8213092cc968ee81`; dispatcher `ev_494d5b2b1b862e0aecb0a6e77a266971d3436786f098e0454589b96d9075e7aa`. Full field/guard trace: `output-packing-programming/`.

The separate SSIF/BF path **0x80314f64 → 0x8032a770 → 0x804e37f0** packs table fields into a control block and writes 0x208068xx registers. No candidate arena pointer or receive destination is established on that input path. A distinct 0x2080602c status dispatcher uses callback table **0x8144a814**; it must not be merged with a different callback registry merely because both relate to image hardware. Evidence: complete BF setter `ev_3c32416749bb5ba4e8d8804ea808002491dc0beec142eeb8fd5c62ded8bfb5b0`; register writer `ev_275095532dba9d90dbddfdb89bf9736beb9532031b892eb4e897aca5836c3640`; status dispatcher `ev_dde4abc7e7bfc10258c1aa328ef3f5f4dde6ae8efddaa66faf017eb9da689fd4`. Full bounded receive investigation: `ssif-receive/`.

Investigation remains in progress. Active graph/recipe execution and gate values, actual sensor-input DMA, live geometry, fresh frame completion and safe backing lifetime must still be joined before an acquisition implementation can be validated. The recovered default layout and conditional descriptor chain are useful constraints, not a verified RAW file.
