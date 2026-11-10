> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Pause gates, image slots and recipe-map selection

This continues the [candidate allocation/export contract](RAW_CANDIDATE_ALLOCATION_AND_EXPORT_CONTRACT.md). These findings narrow software control and identity questions; **no sensor RAW frame has been captured**. All analysis is offline, using the original `rtos-p6.bin`, SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, base `0x80000000`. The REA/Ghidra carrier is `fulltext-row-state.elf`, SHA-256 `3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`.

## The first appended image really occupies material +0xc4

The MID material constructor chain is `0x802f33a0 → 0x802ece3c → 0x802e5aac(this,0) → 0x802e4e3c → 0x8024fd5c`. The final base constructor explicitly writes the four-byte count at **material+0x110 to zero**. Separate byte stores at +0x115 do not initialize that count.

Image append `0x80251b40` takes a material and an image pointer, with no explicit slot index. For nonnull input and count below five it stores at `material + (count+0x30)*4 + 4`, then increments the count. Thus the first successful append after that initialization writes **+0xc4**, and the count becomes one. The known MID builder `0x802ba7b4` passes its selector-0x28 image to this append. This closes the initial-count assumption; it does not observe a live material or rule out intervening appends.

| Role | Evidence |
|---|---|
| MID constructor | `ev_65bdb515d8c31cc5d18b31dcf347248448233e18f6d2afcb941880358953c1c0` |
| Next constructor | `ev_c3923a4f22fdb33dc89be3945cc3ddcea267e5f1abe2250f2fa167e921f67277` |
| Constructor with zero argument | `ev_f9760fdaab5dc57345a365ee4d98425d9142ece3b8d695dbf2935e08c3d785d1` |
| Intermediate base constructor | `ev_a98ae0b9ddfb702f903ee5df7a9ca23826e56106fbcbd3ed389985152e395436` |
| Count-zero base constructor | `ev_49237abacd99bc317bf2f3d230efad4ca06951e9f30384764a49c055bc0e9f70` |
| Append | `ev_dcdae6ee9f6e4bcf94c2c98c9561b87cff8b2048c3341b2a3de0aa15e6bf2756` |
| Builder | `ev_578300aeddd79af3d115439076943573f26270b8538fcaeda85588cf17f2b2ad` |

## Pause is gated software work, with a discarded result

The material event-3 handler `0x802f3928` reads byte **material+0x124** through getter `0x805770f8`. It calls Pause only when that byte is nonzero. The adjacent material virtual **+0x1c**, entry `0x802e5e74`, has a different conditional Pause route and later clears that byte. Material virtual **+0x18** remains the previously established video-start path; these methods must not be merged.

Pause `0x8031e9dc` reads byte **interface+0x1c**, through `0x80568bb8`. If it equals two, the sequence is skipped. Otherwise it waits on interface+0xc, writes state 0xc, calls `0x8031fffc` with the **pointer loaded from interface+4**, writes state two if the call returned zero, and posts interface+0xc and interface+0x10. The second post is outside the return-value check but inside the initial state gate. These are software semaphore operations, not frame sequence numbers.

The full 88-byte body of `0x8031fffc` dereferences its wrapper argument to obtain a receiver, calls that receiver's virtual +0x30, **ignores that virtual result**, calls `0x8057b9bc(wrapper,0)`, and returns zero. Consequently, state two and the +0x10 post do not independently prove that physical capture paused successfully.

| Role | Evidence |
|---|---|
| Material +0x124 getter | `ev_0bbafcc22fd0d6349701cc230e6c87695a4e154686461377f22e3e91baad45f2` |
| Interface +0x1c getter | `ev_a8ec310791f1fa161a1d1339b527570e9b83165916309373e6c22372a6b66abc` |
| Material +0x1c method | `ev_538656f4be0aa91d331e17f670a14b6491cd49b686ee75332adfa11ad7a805b6` |
| Material event-3 handler | `ev_a8c65d6664994f0a97443daecfd3d96e18ec3c70459dbc40143ca496ada54f56` |
| Pause | `ev_fadbca9a75442fc73e7d1c018309829062d13b65ba59521ce5f655a08a6c5fc8` |
| Underlying wrapper | `ev_0ae4a75f4dbfc3642fcb77eac3d6a82fa393af899662b47350f8726343ba2463` |

## Two singleton cells are linked by a wrapper constructor

The outer cell `0x81952fbc` contains wrapper W. Its constructor `0x8031fbac` calls getter `0x80321114` and stores the returned C pointer at **W+0**. That getter reads the separate cell `0x81952fc0`. The interface constructor stores W at interface+4 and C separately at interface+8. Thus different singleton-cell addresses do not disprove the inner receiver's identity.

`0x80320e90` temporarily installs C vptr `0x806af070`. Its enclosing constructor `0x8020d260` subsequently installs final vptr `0x806718a0` at `0x8020d2d4`. Both tables' +0x30 slots contain **`0x80322408`**. Parent source windows cover both stores, literal words, getter, wrapper construction/publication and indirect call; they are indexed with original-byte hashes. This remains conditional on construction and runtime cells containing those instances, without later replacement.

The resolved method `0x80322408` calls `0x80322974(this,0)`, then obtains an object through `0x8031aa88` and calls `0x8031cc40(object,0)`. The first arm reaches `0x8031a84c`, which writes its receiver's +0x148 to zero and calls `0x8031d920(receiver,0,0)`. The second reaches `0x8031cc18` followed by `0x800cb154`. No direct hardware stop appears in these bounded bodies; deeper helper behavior and hardware acknowledgement remain separate questions.

| Role | Evidence |
|---|---|
| Enclosing C constructor | `ev_ce2b46478fc371b9d7e7ec9aaaefb9042c4becd6e5afbd149781ea7121da635a` |
| Intermediate C constructor | `ev_0c4a929d12cda3ed5b6a86e36c40f0ba1a6cb135f7aa103912a804bef9e5440e` |
| Virtual +0x30 target | `ev_ad80e5a4d26adbf35cf8670b61672b027ce5162d38042f166f60ed0b19c89690` |
| First helper | `ev_e4c0d9c093707ee0df74bfa005fb97d709c0fe9c4a0cb3965abbda512936e580` |
| +0x148 writer and next call | `ev_c56f4dd044c22f6c434429bd779aef6d220d91f274bf60933e5901fbef05bf1a` |
| Second helper | `ev_a6853d19874dc07cdfb4370070a23088c0141fc58f350aa0f7127f46481e4206` |

The separate Start proxy `0x8031feac` returns -0x6a for a null second argument. For nonnull input it selects `0x80320ce8` if either of two status queries is nonzero, otherwise `0x80320b30`; it returns the selected callee's result unchanged. This is a different return contract from the Pause wrapper (`ev_01a027a4b465a60d80ee21a3cb17b40439f0d4b9bef74631d635604b301fe47c`).

## CCook +0x54 selects the Still-book map

The recipe ID is **CCook+0x70**, passed in r1 and used by BranchManager to choose a recipe book by its top byte. The distinct field **CCook+0x54** travels in outgoing stack slot +8 through `0x8027b888` into Still-book `0x80280570` as argument seven. Zero selects the book's statically populated **+4 map**; nonzero selects its **+0x1c map**. The latter's runtime population is still unproved.

The base CCook constructor writes zero to +0x54 at `0x802522f8`; cleanup clears it at `0x80252488`. Crucially, the retained message loop also changes it: switch case zero loads `[r11,-0x28]` and stores it at **+0x54 at `0x802530c0`**. The receive buffer starts at `r11-0x38`, so this is message word **+0x10**. Case five clears the field at `0x80253478` if its virtual +0x3c call returns zero. The constructor default therefore cannot establish which map an active recipe uses. The sender/value of that message word and derived-constructor behavior remain to be connected.

| Role | Evidence |
|---|---|
| Base CCook constructor | `ev_f236c68455cbad92afe92f1c6691dcc19e9ce24004aa1430e17e253eb491eb30` |
| Cleanup | `ev_c3a207a379b987bdd5daf6f8828f30dc49dfac3d1934577cfb250f8266ce2051` |
| Complete message-loop dossier | `ev_f10e1652c588ba7c31595210b59215718a1df1add771f31849c9a6e539b08a94` |
| CCook create | `ev_1782f8cb648969f4de831c3f4e0b1616245539599c3eea17ba36953e93ebed89` |
| BranchManager forwarding | `ev_eb1b238fa4f8a76a666f4e69842cffcd67cd442bc917644641fd187fa49c0c1a` |
| Still-book branch | `ev_77fcf3c420d4b541c4d5b7f4bf0465693c0727e49dda17b2f9bbc33ba7482ef5` |

## Event creation initializes a bit pattern, not a frame identity

The event-create wrapper `0x805177c8` executes SVC six with selector **0x80320100**. That number is a signed dispatch selector, not a call to the numerically matching frontend address. In the SVC callback code beginning at `0x8002401c`, the signed high half is -32718; table base `0x80043dac` therefore selects word `0x80023e74`, which contains **0x80019824**. The preceding exception-handler function's declared body ends at `0x8002401b`; the callback code is separately evidenced as a byte window.

The target at `0x80019824` loads request word two and writes it at the event control block's **+0x14**. Separately, event-operation selector **0x80340200** selects table word `0x80023e7c`, target **0x80019a44**. The latter reads +0x14, ORs the supplied bits and writes it back; other paths clear selected bits or write zero. These consumers establish +0x14 as the event-bit pattern. The frontend create request's word two is zero, so **this create call initializes the pattern to zero**.

Ghidra has no defined procedure at either kernel target. These claims use bounded raw instruction windows and exact stores, without inventing complete function ranges. The create request also stores word zero at +0xc and word one at +0x10; those are distinct fields, and zero at +0xc alone would not prove an initially clear pattern.

| Role | Evidence |
|---|---|
| Create dispatch word | `ev_907bc5de5a017a5ae757c202f406239bb4114ec85f90767fecd38e01900968d8` |
| Create target's 268-byte window | `ev_5a7a270d947892ffa3e30b53911b463181233bd404609951e0efaf458d33ff2d` |
| Create request-word-two load | `ev_12c22f141af9864c795c2b42d4e50a30c7bc5c80fb4c099d94ba8a3f69e88955` |
| Store into control block +0x14 | `ev_3bd53c54f0242c6240ffeeb5ab211d5568c5c103ea417e3d507884034d1738ab` |
| Event-operation dispatch word | `ev_9d247a0c1f2b4d7ace80f91d3e5eb12c078de5e643ea782effb080025e24b328` |
| Event-operation 248-byte window | `ev_ff849ff901da3a49e30c685809bc342b6792c43b67c48ceb67d3f5e549fb31d3` |
| OR supplied bits | `ev_bffcc297ef400417dd51d59ddd543b624bc77f27ebfcc51726889c6a78fbdae1` |

The initialization says nothing about whether a capture uses a newly created group, reuses an old one, clears the relevant bit between frames, or pairs the event with this candidate buffer. In particular, constructor-time zero does not close per-frame freshness or instance identity.

## Practical consequence and remaining work

The candidate now has a defensible static image/slot/allocation identity. Software Pause and map selection are also more precise. They still do not identify sensor-produced samples, establish the currently active recipe, associate an interrupt with a fresh buffer instance, or keep the allocation alive through a successful file copy. The next useful identity edge is the event-3 handler's owner getter `0x80251e4c` and `0x80252d40(owner,material,0x3005,0,0)` message path. Initial event-group state and per-frame freshness must also be kept separate.

Full retained dossiers, source windows and reviewed worker outputs are under `output/decompile/rea/multi-provider-run/`. The [companion evidence index](../pause-gates-recipe-selector-evidence.json) records exact paths and hashes; validating bytes and records does not validate runtime activation or every semantic claim.
