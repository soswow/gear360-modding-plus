> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Completion identity and recipe-book dispatch

This continues the [interrupt and recipe execution findings](RAW_INTERRUPT_AND_RECIPE_EXECUTION.md). The goal is still a file of verified sensor RAW pixels, with a reproducible acquisition method and evidence for provenance, geometry, packing, fresh completion, and safe buffer lifetime. **No verified sensor RAW frame has been captured.**

All work is offline static analysis of `output/decompile/rea/rtos-p6.bin`, SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, link base `0x80000000`. REA 6.4.0 was used through its CLI because this session had no REA MCP tools. Ghidra was the already installed 12.1 provider. The new carrier, function evidence, source windows, and closed-session audit are under `output/decompile/rea/continuation-20261015/pass11/`. An independent byte review rechecked the instruction claims against the original image and agreed with all 15; see `pass11/independent-byte-review/report.md`.

## What the material “completion” method actually does

The state machine still calls material virtual **+0x18** when it moves a material to state 5. For the MID `0x15000` table at `0x806a1f70`, that slot’s cell `0x806a1f88` contains **`0x802e5c74`**. That target is not a wait for the PP WDMA interrupt.

On the path where `0x802e5908(material)` returns zero, `0x802e5c74` fills a 16-byte stack record from the same material and passes it to the process-wide frontend interface. The record pointer is `r11 - 24`. The interface pointer comes from `0x8031e490`, which returns the word at **`0x81952fb8`**. The captured image stores zero at that cell because the cell is a runtime pointer, not because a live object was observed to be null.

| Record offset | Material source | Constructor value |
| --- | --- | --- |
| +0x00 word | +0x128, getter `0x80577198` | 0, stored at `0x802e4ea4` |
| +0x04 word | +0x130, getter `0x805771c0` | 60, stored at `0x802e4eb0` |
| +0x08 word | +0x134, getter `0x805771e8` | 0 |
| +0x0c byte | +0x138 | 0 |
| +0x0d byte | +0x139 | 0 |
| +0x0e byte | +0x12c | 0 |
| +0x0f byte | +0x13a, then `0x802e595c` keeps 0–15 and maps any larger value to 1 | 0 |

The only direct caller of the two word getters is this completion method. Later writers of the words exist, including setter `0x80569248` for +0x128, but this pass did not tie a particular writer to the MID `0x15000` instance. The constant 60 and the log text below are not a measured frame rate or a measured width.

`0x8031e630` then does the following, in instruction order:

1. `0x8031f958` reads record byte +0x0f and calls `0x800a1ca4` with `0x20000` for byte 1 and for the default, or `0x20000 + (byte - 1)` for bytes 2 through 14. A failing check for bytes other than 3 and 11 calls `0x800a1f1c`. The log format is `videoPwrCtrlParam : video size/framerate = %d/%d`, using record words +0 and +4.
2. It logs `StartVideoStream` with the same two words, using `[FE_IF] %s : sz %d, %d fps`.
3. It logs `Video frame started : sz %d, %d fps` **before** the start call. That string is not evidence that a frame completed.
4. It waits through `0x80520160` on interface **+0x10**, then on interface **+0x0c**.
5. It calls `0x8031feac` (`StartVideoStream`) with interface **+4** and the same record. Interface +4 is the `CFrontEndPP` object from `*0x81952fbc`.
6. If that call returns zero, it calls `0x8057b93c(interface, 1)`.
7. It posts `0x80520378` on interface **+0x0c** only, then returns the `StartVideoStream` result.

The nonzero result from `0x802e5908` skips this start path and logs an already-running message. Both paths then call `0x802e587c` and `0x805770c8(material, 1)`.

The constructor `0x8031dfd0` installs vptr `0x806aead0`, stores the `CFrontEndPP` getter result at +4, and creates the two semaphore objects with initial count **1**:

```text
0x8051ff04(..., interface+0x0c, 1)
0x8051ff04(..., interface+0x10, 1)
```

`0x8051ff04` passes that count into the request block and, when `0x80517834` returns non-negative, stores the returned id at the object’s first word. The wait and post wrappers are the same functions previously reviewed for FileDump: wait `0x80520160` (`ev_08e82c952b579ea6ac486d6d352123204ed4759e9ecc93ec7a90f5e859587484`) and post `0x80520378` (`ev_520d4c87a83011c7edad9f6bdc50743bf6724199ee1b6b99627dc1b6890987c6`), both in the pass6 `filedump-contract/evidence.json` function list. This pass does not repeat their kernel-timeout analysis.

An aligned ARM BL scan of the whole image finds three callers of `0x8031e630`: `0x802e5e34` inside this virtual method, plus `0x8021bfd8` and `0x80221358`. The carrier used for decompilation contains only the first of those callers. Thumb and indirect callers were not scanned.

### Inference

If the semaphore created with count 1 still holds that count, the first wait can proceed without another poster. That joins the create argument with the earlier wait-wrapper behavior. It was not executed, and it is not a WDMA status bit. Interface +0x10 is waited here and not posted here, so a later entry can block until some other writer posts that same object. No such writer was identified.

### What this does not establish

The stack record contains no image pointer, pixel address, or frame sequence. The interface object is a singleton cell, not the material and not the candidate arena allocation. Software state 5 therefore reaches a video-start request. It does not prove fresh sensor pixels, DMA completion, or a buffer lease.

## BranchManager virtual +8 is a recipe-book switch

CCook `0x802536bc` loads manager field +4 and calls that object’s virtual **+8**. The only direct caller of the +4 setter `0x80325b48` is `0x8027b034`, inside builder `0x8027afc8`. There the value stored at manager +4 is the builder object itself. That object’s constructor stores vptr **`0x8067ee58`**. The bytes at `0x8067ee80` are the name `18CBranchManager_NX1`.

Virtual slot +8 is the word at `0x8067ee60`, which is **`0x8027b888`**. It is not `0x802ba7b4`. The method takes the recipe-id argument in r1, uses `r1 >> 24`, and rejects values above 2. It then calls virtual +8 on entry `index` of the pointer table at BranchManager **+0x0c**. CCook passes CCook **+0x70** as that recipe id and CCook **+0x58** as the preallocated recipe pointer.

The builder allocates 12 bytes for the table and fills it with three constructed objects:

| Index | Constructor | Vptr | Virtual +8 | Type name |
| --- | --- | --- | --- | --- |
| 0 | `0x802804d0` | `0x806808d8` | `0x80280570` | `21CRecipeBook_NX1_Still` |
| 1 | `0x802f927c` | `0x806a4688` | `0x802f9348` | `24CRecipeBook_Liveview_NX1` |
| 2 | `0x802d1888` | `0x80696340` | `0x802d1904` | `21CRecipeBook_NX1_QView` |

A whole-image ARM BL scan finds nine callers of the MID builder `0x802ba7b4` (`0x802a5930`, `0x802a59a4`, `0x802a59fc`, `0x802a9ef0`, `0x802aa7c8`, `0x802ab078`, `0x802ab91c`, `0x802b3c40`, `0x802b54c8`). None is `0x8027b888` or `0x80280570`. The Still book method does contain a later indirect call, at `0x80280780`, through a pointer loaded from another returned object. That target is data-dependent and was not resolved to `0x802ba7b4`.

Material id `0x15000` and the recipe id at CCook +0x70 are different fields. Inside the CCook method cluster `0x80250000–0x80258000`, the direct stores to +0x70 are:

| Store | Function | Value |
| --- | --- | --- |
| `0x80252310` | constructor `0x8025227c` | constant 0 |
| `0x80252464` | cleanup `0x80252410` | constant 0 |
| `0x80253074` | message loop `0x80252fb0`, switch case 0 | stack slot `[r11, #-0x1c]` |
| `0x8025323c` | same loop, switch case 1 | stack slot `[r11, #-0x1c]` |

`CCook::create` only reads the field. None of those four instructions writes the constant `0x15000`. The message loop calls `0x80254558(this, &stack)` at `0x80252fe0`, and that helper receives into the stack buffer through `0x8051e3a4(this+0x1c, buffer, 0)` (`ev_e504e814b9e92001febefe90121b202b4b067144150c78dc4d4ccd4a0639816f`). The buffer starts at `r11-0x38`. The switch discriminant is the word at message **+0x18** (`[r11, #-0x20]`). Cases 0 and 1 copy message **+0x1c** (`[r11, #-0x1c]`) into CCook +0x70.

`CCook_Still` vptr `0x8067f0a0` overrides slot **+0x10** with `0x8027bfac` (base CCook slot +0x10 is `0x80252900`). That override stores command **1** at message +0x18 and its own r1 argument at message +0x1c, then calls send wrapper `0x802544cc` (`ev_4a0f63e3bedac7324d00f2030743d36cac67862db57373cf76a7731988a49dfe`), which passes the message to `0x8051e560` (`ev_04288ad9ad41c191bdeda4a015014c1505fd47d4b7adfe08feef5b06b1294b62`) on `this+0x1c`. There is no direct branch to `0x8027bfac`; the only pointer is the vtable word at `0x8067f0b0`. The caller that supplies r1, and therefore the numeric recipe id, is still unknown.

The only instruction that loads vptr `0x8067f0a0` outside the destructor is constructor `0x8027bcac` at `0x8027bccc`. It first calls base constructor `0x8025227c`. Its only direct caller is `0x8027b168`, inside BranchManager builder `0x8027afc8`. That loop allocates 124 bytes, constructs `CCook_Still`, and passes the object to `0x80256680` with a third argument of 0. The first argument is the object at CManager **+0x0c**, obtained by `0x80325c88` after loading the manager from the builder’s `+4` field. The loop compares its index with 7 at `0x8027b1dc`. Two later loops in the same builder construct other cook classes and pass indices 1 and 2. There is still no direct branch to `0x8027bfac`. A scan for “load vptr, add 0x10, indirect call” found 24 sites in the image; the capture-range sites that were decoded belong to other objects, including a `CCapHandler` helper at `0x8025a80c` whose own slot +0x10 is `0x8025a920`. Which registered Still cook receives slot +0x10, and what value is passed as r1, remains unknown. A wider scan found 76 other `STR` instructions with immediate offset `0x70` between `0x80200000` and `0x80500000`. They were not shown to target a CCook object. Indirect and bulk writes were not searched. Vtable slot 0 at `0x80678f48` is the message loop `0x80252fb0`. Evidence: constructor `ev_f236c68455cbad92afe92f1c6691dcc19e9ce24004aa1430e17e253eb491eb30`, cleanup `ev_c3a207a379b987bdd5daf6f8828f30dc49dfac3d1934577cfb250f8266ce2051`, loop `ev_f10e1652c588ba7c31595210b59215718a1df1add771f31849c9a6e539b08a94`, parser `ev_ad353411ce44744a4e34166794683e21214e6657e8aa0127e669cc159c5db796`. The worker report’s positive `[r11, #0x1c]` wording is wrong; the original instruction is `ldr r2, [r11, #-0x1c]` (`0xe51b201c`). The active R210 graph therefore remains unknown. Manager +8 is still the separate Fridge object used by `SetMaterial`; this result does not merge those two fields.

## Evidence

The committed identity index is [completion-identity-recipe-dispatch-evidence.json](../completion-identity-recipe-dispatch-evidence.json). Local generated dossiers stay under `output/`, which this repository does not track.

Carrier `output/decompile/rea/continuation-20261015/pass11/completion-and-branch/completion-and-branch.elf`, SHA-256 `99ff1af5cc51dd029b063f589e54c159d47c35c88d4548243e45146a25adb80f`. Its manifest records 17 source-identical segments. Function dossiers are in the `analyze-*.json` files and in `session.snapshot.json`. Ranges below are inclusive.

| Role | Entry | Inclusive ranges | Evidence |
| --- | --- | --- | --- |
| Completion virtual +0x18 | `0x802e5c74` | `0x802e5c74–0x802e5e63`, `0x802e5e68–0x802e5e73` | `ev_e1bae5bec10edda52fa6bb38616dad35dcef50bd1e28ffffb02925b96ccf4544` |
| Frontend start | `0x8031e630` | `0x8031e630–0x8031e857` | `ev_09f720191fde9052b800efc8eae7ab61253a51f3c042093a956675b90e020e8e` |
| Power-control selector | `0x8031f958` | `0x8031f958–0x8031f98b`, `0x8031f9c4–0x8031fb87` | `ev_ee963d8d87e4f5c507e0e4f63e9a7540175b3667187e3264063898c9dd658727` |
| Interface getter | `0x8031e490` | `0x8031e490–0x8031e4b3` | `ev_4df8ad87deaee6467c507258cea7755879cfe7e6845113d878125211d23f478f` |
| Interface constructor | `0x8031dfd0` | `0x8031dfd0–0x8031e3c3` | `ev_2fac4ea25a5f4f8b957cbca22d021e37133b3ed6c551089146ef2a209834faf4` |
| Semaphore create | `0x8051ff04` | `0x8051ff04–0x8052001b` | `ev_9d299c97f9d463676196f8460cd98954695b73ff7d4063a759465c9306f7b78f` |
| Material constructor | `0x802e4e3c` | `0x802e4e3c–0x802e4f6f`, `0x802e4f90–0x802e4f9b` | `ev_5379a6540275f66b974b9be2d1ec6defcc57bb79b3a8f483263501dbc13c07f3` |
| Word +0x128 setter | `0x80569248` | `0x80569248–0x80569273` | `ev_d03942d6635229940f10de3025c5c48185376f354905ff07845ff935a5e4782c` |
| CCook create | `0x802536bc` | `0x802536bc–0x8025380b` | `ev_1782f8cb648969f4de831c3f4e0b1616245539599c3eea17ba36953e93ebed89` |
| Branch builder | `0x8027afc8` | `0x8027afc8–0x8027b40f`, `0x8027b480–0x8027b48b` | `ev_d30be48323027531cab8a0f238674e7442336ab256fd0e287694745ab3ee08db` |
| Branch virtual +8 | `0x8027b888` | `0x8027b888–0x8027b943` | `ev_eb1b238fa4f8a76a666f4e69842cffcd67cd442bc917644641fd187fa49c0c1a` |
| Manager +4 setter | `0x80325b48` | `0x80325b48–0x80325bbf` | `ev_bc43f686427e00d00edf075f0dc799a550835614c19cea4de3fa805db078b436` |
| Still book virtual +8 | `0x80280570` | `0x80280570–0x802808fb`, `0x80280914–0x8028091f` | `ev_77fcf3c420d4b541c4d5b7f4bf0465693c0727e49dda17b2f9bbc33ba7482ef5` |

The completion body’s gap at `0x802e5e64–0x802e5e67` is an unreached call. The power selector’s gap is its jump table. Original windows and hashes are in `source-byte-windows.json`. Decompiler names are analyst symbols from the carrier.

## Still-book lookup uses two different maps

`0x80280570` does not call `0x802ba7b4` directly. When the stack argument at `[r11, #0xc]` is nonzero, `0x802806f4` selects the map at **this+0x1c**. `0x80572700` returns `*iterator + 0x10` (`ev_f81cb84f0028be47b57777f104e8a34c30621a1f1b56a1b685b00d079238163e`, inclusive `0x80572700–0x8057272b`). `0x8028076c` loads `[that + 4]`, and `0x80280780` branches to it. That word is a factory pointer stored in the map value, not a fixed address.

Populate `0x80280920` (`ev_9d38393f3b7c53115f561881b2a07981dfc7fc384e6447b5e217f404c4e5d960`) inserts into the other map, **this+4** (`add r4, r3, #4` at `0x80280934`). It does not insert into this+0x1c. All 31 direct callers of insert helper `0x80572820` have that `add r4, r3, #4` earlier in the same populate function, and none has `add r4, r3, #0x1c`. The Still constructor `0x802804d0` builds the this+0x1c map by adding `#0x1c` at `0x8028050c` (word `0xe283301c`) and calling `0x80572630` at `0x80280514`. That is the only direct caller of `0x80572630`. The following call, at `0x8028051c`, is populate. The only direct caller of the constructor is `0x8027b3a4`. A failure path at `0x80280534` calls `0x8057233c` on this+0x1c, which tears the map down. No static insert into this+0x1c was found, so the `0x80280780` branch has no static factory table. If that map is still empty at the lookup, the following compare fails and the code logs at `0x80280788`.

When `[r11, #0xc]` is zero, `0x802806ec` branches to `0x802807c0`, which uses **this+4** and the same pointer shape through `0x805727d0` at `0x80280834`. That map holds the static factories, including key **27** (`0x1b`) → `0x802b3b90`. The key halfword is stored at `0x802811b0` and the pointer at `0x802811c0`. Factory `0x802b3b90` (`ev_e31a3977b6c855346905c535b97a197d79a8b0caa5dcef548a4892a0715fc8f3`) calls `0x802ba7b4` at `0x802b3c40`. Selector `0x80281460` (`ev_413625e8cdd0fc2f244a95765d8df0fbf24972d5491ad71b3d3cc84b5c2438ba`) emits key `0x1b` only on one nested path. That path is a compiled option for the this+4 map, not evidence that the R210 takes it, and it is not the this+0x1c branch.

## Who posts the frontend semaphore at +0x10

An aligned ARM scan found 118 calls of post helper `0x80520378`. Eighteen of them have `add r3, r3, #12` or `#16` within the previous 16 instructions. The three `+16` sites are inside `CFrontEndInterface` methods, identified by the file string `product/FrontEnd/Service/FrontEndInterface.cpp`:

| Method | Entry | Post | Evidence |
| --- | --- | --- | --- |
| `StopVideoStream` | `0x8031e858` | `0x8031e998`, object +0x10 | `ev_4dbd59afc0997df268e07692107d0ba264bd3805411af6032ca0bc945720463f` |
| `PauseVideoStream` | `0x8031e9dc` | `0x8031eb80`, object +0x10 | `ev_6fa70d1078958c3e4686b898b7e91648c7fe4f635a04ef0847d6ec38f6a04f74` |
| `PostStillCapture` | `0x8031f3f8` | `0x8031f53c`, object +0x10 | `ev_371fc8cb4ca21b62bc7f76d213fd7f907e5ec760001a75a1a88e3a1c1b11ff20` |

`StillCapture` at `0x8031f274` (`ev_518ce43419e7cada848babeec803bb66855ad52c0f0e650fc35cc58302ec0e42`) waits on +0x10 and then on +0x0c, and posts only +0x0c. A 12-instruction window finds the same three +0x10 posts and no others. The registered WDMA callback `0x803118c0` (`ev_87961a13b925d476895fe41ad1b7e22cb22a28c72942916206d3b661a186707f`, body `0x803118c0–0x803119e3`) contains no call to the wait or post helpers. Frontend event-mask handler `0x802128e8` branches to `0x8051e0a4` at `0x80212934`, not to the semaphore post. A poster that computes the semaphore address earlier than the scan window would not appear.

Every direct ARM caller of `StartVideoStream` (3), `PauseVideoStream` (19), and `PostStillCapture` (10) passes the return value of `0x8031e490` as `r0`. That getter returns the word at `0x81952fb8` (`movw`/`movt` at `0x8031e498`/`0x8031e49c`, then `ldr` at `0x8031e4a0`). The captured image stores zero in that cell because it is runtime storage. The three Start sites copy the getter result through `r2` and pass a stack record in `r1`; the 29 Pause and Post sites copy it through `r3`. `StopVideoStream` has no ARM `BL`, no Thumb-2 `BL` among 3440 decoded candidates, no `movw`/`movt` pair, and no literal `0x8031e858` or `0x8031e859`. The interface vtable at `0x806aead0` points slot +0 at `0x8031e3c8` and slot +4 at `0x8031e460`. Stop’s body would post `+0x10` from incoming `r0`, but this image does not show a caller. The completion method `0x802e5c74` calls Start at `0x802e5e34`. The separate material virtual +0x1c method begins at `0x802e5e74` and calls Pause at `0x802e5f7c`; its table cell is `0x806a1f8c`. Each caller obtains the interface through the same getter. The Pause call is outside the completion body, whose last range ends at `0x802e5e73`. No direct call to `PostStillCapture` is in the completion method.

## Sensor-driver field +0x64 is a small info object

`CS5K2M8SXDriver` vptr `0x80676a70` has slot **+0x48** = `0x8022f0fc`. That method allocates three 40-byte objects, constructs them with `0x8056a4b8`, and stores them at driver **+0x5c**, **+0x60**, and **+0x64**. The constructor writes vptr `0x80789da0`, whose type name is `13CSensorTgInfo`, then stores its three register arguments at `+0x10`, `+0x14`, and `+0x1c` and a stack argument at `+0x20`. A 40-byte object cannot be the 64 MiB candidate allocation. These fields are also not the frontend RDMA inputs at frontend `C+0x64`/`C+0x68`; no `STR` with immediate offset `0x64` or `0x68` appears in the frontend method range `0x8020d000–0x80222000`.

## RDMA inputs are words copied from the service object

`rdmaConfigure` (`0x803954e4`) has one direct caller, the BL at `0x803219b4` inside `0x803217d8`. That method is the word at `0x806af0c0`, slot +0x50 of vtable `0x806af070` (`20CFrontEndServiceBase`). The call passes `this+0x50`.

The service constructor reached from the frontend constructor is `0x80320e90`, called at `0x8020d278` with the frontend object as `r0`. At `0x80320eb0` it loads the pool word at `0x80321038`, which is `0x806af070`, and stores that vptr. At `0x80320ebc` it adds `#0x50` and calls `0x80393ec0`. `0x80393ec0` stores vptr `0x806c0970` (pool word at `0x80393f58`). The name at `0x806c0980` is `11CPP_Capture`. After that call returns, `0x80320e90` stores zero at service `+0x24` (`0x80320f40`, word `0xe5832024`) and at service `+0x30` (`0x80320f64`, word `0xe5832030`).

`rdmaConfigure` reads the capture object, not the service root: `0x803955dc` is `ldr r3, [r3, #0x14]` (word `0xe5933014`) and `0x80395600` is `ldr r3, [r3, #0x18]` (word `0xe5933018`). Those are service `+0x64` and `+0x68`. It passes each word to `0x8031b8f8` with index 0 or 1. The error string inside that function, at `0x806ae434`, begins `void CPP_Core::set_rdma_info(CCapImage*,`. The stored values are 32-bit words. The string names the argument; it does not show that the word is the 64 MiB allocation or a sensor address.

The non-zero stores of capture `+0x14` and `+0x18` are `0x803940e4` (word `0xe5832014`) and `0x80394100` (word `0xe5832018`), in `0x803940b8`. Index 0 takes the first store and index 1 takes the second. The only direct callers are `0x803214b0` and `0x803214d0`, both in helper `0x80321484`. That helper is called only from `CapSetInfo` (`0x803214e4`, vtable slot +0x44) at `0x80321538`. The helper loads `[service+0x24]` (`0x803214a0`, word `0xe5933024`) and `[service+0x30]` (`0x803214c0`, word `0xe5933030`) and passes those words through. `CapSetInfo` does not itself write service `+0x24` or `+0x30`.

Function `0x80322598` stores a word at incoming `r0+0x24` (`0x8032272c`, word `0xe5832024`) and at `r0+0x30` (`0x80322780`, word `0xe5832030`). Its only direct caller is `0x8031e614`. That caller loads `[its this+8]` at `0x8031e604` (word `0xe5933008`) and passes it as `r0`. The same object’s constructor-side store at `0x8031e1d0` writes `this+8` from the return of `0x80321114`: `mov r3, r0` at `0x8031e1c4`, then `mov r2, r3` at `0x8031e1c8`. The getter returns the word at `0x81952fc0` (`movw`/`movt` at `0x8032111c`/`0x80321120`). The captured image stores zero there.

The publisher is `0x8020d9cc` (word `0xe5832000` with base `0x81952fc0`). The stored pointer is the object allocated just above that store, constructed by `0x8020d260` at `0x8020d9b8`, which is the constructor that calls the service constructor `0x80320e90`. So the non-zero stores land on that service object. In `0x80320000–0x80324000` the only other immediate stores of offsets `0x24` and `0x30` are the constructor’s zeros.

The stored value is `[record+8]`. The record address is `base + index*12`: `lsl #1`, add the index, `lsl #2` at `0x803226e8`–`0x803226f0`, then the load at `0x80322724` (word `0xe5932008`). The switch word is the record’s first word. Tag 1 (`0x803226e0`) writes service `+0x24`. Tag 2 (`0x80322734`) writes service `+0x30` at `0x80322780`. That word is not shown to be the 64 MiB allocation.

`0x8031e578` has seven direct callers, and every one passes count 6. Four of them build the six records on the stack and store immediate 0 in the tag-1 and tag-2 third words: `0x80219d34` / `0x80219d4c`, `0x8021be58` / `0x8021be70`, and the same pair of zeros in `0x802211d8` / `0x802211f0` and `0x802217cc` / `0x802217e4`. Those calls therefore copy 0 into the two service fields. The other three pass an embedded array: `this+0x124` at `0x80292638` (word `0xe2833f49`), `this+0x1f8` at `0x802955ac` (word `0xe2833f7e`), and `this+0x140` at `0x802e535c` (word `0xe2833d05`).

Those three functions belong to `23CMaterial_PP_Core_Still` (vtable `0x80684c5c`, name at `0x80684cbc`), `39CMaterial_PP_LRC_Still_MemToMem_OnlyLRC` (name at `0x80685ae4`), and `26CMaterial_PP_Core_Still_3D` (name at `0x80687a30`). The first class stores 0 at the tag-1 and tag-2 value slots `+0x138` and `+0x144` (`0x802926a4` is `mov r2, #0`, then `0x802926a8`). The LRC class stores tag 1 at `+0x204` and then stores the return of `0x80251c58` at `+0x20c` (`0x80295640`, word `0xe583220c`). Its tag-2 value at `+0x218` is 0 (`0x80295664`). With r1 equal to 0, `0x80251c58` returns `[this+0xc4]`: it adds `0x30`, shifts left by 2, adds 4, and loads that word (`0x80251c74`, `0x80251c7c`, `0x80251c88`). The 3D class stores that same return at `+0x154` when `0x80251498` returns 2 (`0x802e5450` is `beq`, then `0x802e5474`). If `0x802e0454` does not return 0, the value stored instead is `this+0x190` (`0x802e5418`). Neither value is shown to be the 64 MiB allocation.

The material constructor `0x802e4e3c` calls `0x8024fd5c` at `0x802e4e54`, and `0x80294bc8` does the same at `0x80294be0` before storing vptr `0x80685948` (name `22CMaterial_PP_LRC_Still` at `0x806859a8`). `0x8024fd5c` stores 0 at `this+0xc4` (`0x8024fe34` is `mov r2, #0`, word `0xe3a02000`; `0x8024fe38` is word `0xe58320c4`). In `0x80200000–0x80350000` the other immediate stores of that offset are 21 (`0x8029df90`, word `0xe3a02015`) and 40960 (`0x8031a0ec`, word `0xe3a02a0a`), written through an argument pointer rather than the constructor’s `this`. The only such store in `0x80280000–0x80300000` is the constant 21. One more store, at `0x8025d69c`, is inside `0x8025d4ac`, which copies `+0xc4` from its second argument onto `this`. None of these stores records a newly allocated pointer.

Zero stores of the capture fields also exist at `0x80390230` / `0x8039023c` and `0x803965dc` / `0x803965e8`. They write immediate zero, not a buffer pointer.

## A kitchen virtual +0x10 forwards its caller's r1

The twelve direct uses of container getter `0x80325c88` do not perform a virtual +0x10 call. One kitchen function does. `0x802562e4` saves incoming r1 at `[r11, #-0x24]` (`0x802562f4`). At `0x8025648c` (word `0xeb000144`) it calls `0x802569a4`, stores the result, then loads that object's vptr, adds `#0x10`, and branches to the slot at `0x802564c4` (word `0xe12fff33`). The r1 of that branch is the saved argument: `0x802564b4` is `ldr r2, [r11, #-0x24]` (word `0xe51b2024`) and `0x802564bc` is `mov r1, r2`. A second branch in the same function, at `0x802563d4`, uses the same saved r1.

The only direct caller of `0x802562e4` is `0x80138c50`, inside `0x80138bd0`. That function saves its own incoming r1 at `0x80138be0` and passes it unchanged at `0x80138c48`. The log string used by `0x802562e4` is `Kitchen` at `0x80679224`. `0x80138bd0` has 31 direct callers, so the value in r1 is not one immediate.

`0x802569a4` returns the word at `[r11, #-0x8]` (`0x80256ac4`, word `0xe51b3008`, then `mov r0, r3` at `0x80256ad0`). That slot is set to zero at `0x802569c0`. The only later store is `0x80256a68`. Before returning, the function loads `[r0+0x1c]` (`0x802569c8`, word `0xe593201c`), shifts its r1 left by 3 (`0x802569d0`, word `0xe1a03183`), and adds those to address the table. On this path that r1 is `[r11, #-0xc]` of `0x802562e4`, and the only store to that slot is `0x8025630c` (word `0xe50b300c`) after loading the function's incoming r1 from `[r11, #-0x24]`. The table index and the virtual call's r1 are the same argument. If the helper returns zero, `0x802564a0` (word `0x0a00001a`, `beq`) skips the virtual call. `0x8025357c` returns `[argument+0x68]` (`0x80253590`, word `0xe5933068`) and is not a constructor of `CCook_Still`. Neither function contains the vptr `0x8067f0a0`. The call remains unidentified as slot `0x8027bfac`.

The word at `0x802569d0` is `mov r3, r3, lsl #3`, so the table step is 8 bytes. `0x802569dc` calls `0x8056dbd4`, which returns the word at `[argument+0]` (`0x8056dbe8`, word `0xe5933000`). Registration helper `0x80256680` uses the same `[object+0x1c] + (index<<3)` address. The Still factory calls it at `0x8027b19c` with the constructed cook in r1 and index 0 (`0x8027b198`, word `0xe3a02000`). That cook's constructor stores vptr `0x8067f0a0` from the pool word at `0x8027be78`. The kitchen lookup does not pass index 0, so its returned word is not shown to be that object.

## The pixel-pointer accessor reads a word copied from a fixed table

`0x80541b48` adds `0x80` to its argument (`0x80541b5c`, word `0xe2833080`) and calls `0x800b4e00`. Every arm of that switch returns the word at `[subobject+4]` (`0x800b4e44`, word `0xe5933004`), which is argument `+0x84`.

One store of a loaded word at `+0x84` is `0x80319878` (word `0xe5832084`), inside `0x80319798`. It copies offset `0xc` of its second argument. The only caller is `0x8031a328`. There the destination is the `this` of `0x8031a250`, and the source is a stack record filled by `0x8020cb90`. That wrapper calls `0x805856ec`, which copies 100 bytes (`0x8020cbe0`, word `0xe3a02064`). The source address is `table + index*100 + 4`: `0x8020cbc8` is `mul r3, r2, r3` (word `0xe0030392`) after `r2` was set to 100, then `0x8020cbd4` adds 4. The index is `[this+0x18]` and the copy is skipped when that index is above 55 (`0x8020cbac`, word `0xe3530037`). The table getter `0x8020ca7c` returns the address `0x8188257c`. The object getter `0x80319188` returns the address `0x81952e3c`, and the only caller of `0x8031a250` is `0x80321fc8`, which passes that object as `r0`.

The word that lands at `+0x84` is therefore offset `0x10` of the selected 100-byte record (`+4` to the record, then `+0xc`). It is not the tag-1 integer from material `+0xc4`. Other stores of offset `0x84` in the same address band write the immediate 1 into a different object.

The table constructor is `0x8020c9bc`, called only from the getter. It stores vptr `0x80670310` (pool word at `0x8020ca00`) and then calls `0x80585914` with the address `this+4`, value 0, and length 100 (`0x8020c9e4`–`0x8020c9ec`). That leaves the first record’s pointer word at 0. The indexed helpers immediately after the copy wrapper write only bytes: `0x8020cc90` stores a byte at record `+4`, `0x8020cde0` at `+5`, and `0x8020cd38` at `+6`. None of the 70 direct callers of the getter stores through the returned pointer in the following instructions. A later word store into that record was not found in this helper cluster.

`0x8029ea60` is `mov r0, #3` (word `0xe3a00003`) before the call at `0x8029ea64`. In `0x8025d9c0` the switch index is that argument minus 14 (`0x8025d9f4`, word `0xe243300e`). Argument 3 does not land in the table: the following compare is unsigned, and `0x8025da00` branches to the return. The return value is the word stored at `[r11, #-0x8]` by `0x8025d9d0` (word `0xe3e03101`), which is `0xbfffffff`, not an allocation pointer. The `mov r1, #0x1c` at `0x8025da8c` belongs to a later case, argument 22. The value passed to `0x8025cc24` on this path is that sentinel.

The 64 MiB request is inside allocator `0x800ac834`. When the tag byte at `[r11, #-0x21]` equals 28, `0x800acb04` (word `0xe3a01301`) moves `0x04000000` into r1 and calls `0x8054aef4`. The not-equal branch at `0x800acafc` skips that request. Helper argument 22 is the case that passes tag 28. None of the six direct callers of `0x8025d9c0` passes the immediate 22. `0x80139244` and `0x80139258` both pass `[r11, #-0x2c]`. That slot is written at `0x80138f54` from the return of `0x80560ca4`. The getter’s argument is `image+0x80`, produced by `0x80541b20` (`0x80541b34`, word `0xe2833080`), and `0x80560ca4` returns `[argument+0x24]` (`0x80560cb8`, word `0xe5933024`). The image is `[this+4]` of `0x80138d54` (`0x80138d60` saves r0; `0x80138f18` and `0x80138f3c` load `[that+4]`). So the request id is the word at `image+0xa4`. When that word is 22, the helper selects tag 28 and the allocator requests `0x04000000` bytes. The function is referenced from `0x80651028`. The two call instructions are at `0x80139248` and `0x8013925c`; `0x80139244` and `0x80139258` load their arguments. Their stores to `[r11, #-0x14]` are mutually exclusive alternatives, not successive writes on one traversal. Compare/normalization at `0x80139144–0x80139158` sends a value unequal to33 to `0x80139258`; thus request22 selects the call at `0x8013925c` when the tested field still holds22. The request33 arm can reach the other call after additional helper-result conditions. No runtime allocation is observed.

The setter for that same word is `0x80560ccc`. It stores r1 at `[argument+0x24]` (`0x80560ce8`, word `0xe5832024`). Every caller shown above passes the return of `0x80541b20`, so the store lands at `image+0xa4`. Six call sites pass the immediate 22 (`mov r1, #22`, word `0xe3a01016`): `0x80141404`, `0x801416e8`, `0x80141c60`, `0x80141de8`, `0x80143d8c`, and `0x80147878`. Their prologues are `0x80140e94`, `0x80141598`, `0x80141af0`, `0x80141c78`, `0x801438ac`, and `0x80147690`. None of those six calls the allocation helper in the following `0x400` bytes. The same setter is also called with other immediates, including 0 at `0x80139a5c`, so the field is not always 22. The only static pointer to the reader `0x80138d54` is the word at `0x80651028`. The type string `30CCallback_Still_Set_2ndAddress` begins at `0x8065104c`; that nearby name is not proof of the function’s class.

The selected helper result, saved at `[r11, #-0x14]`, is what gets installed; the two helper-call arms do not execute sequentially. `0x8013934c` (word `0xe3730105`) is `cmn r3, #0x40000001`, which compares equal when the result is the sentinel `0xbfffffff`. The following `beq` at `0x80139350` (word `0x0a000046`) skips the install in that case. Otherwise `0x80139368` calls `0x8025cc24(image, result)` with the image from `[this+4]`. That helper reads the byte at `image+0x80` through `0x80541940` (`0x80541954`, word `0xe5d33000`). When that byte is 0, the switch arm at `0x8025cde8` stores the result at the first word of a 16-byte record (`0x8025cdf0`, word `0xe5832000`). `0x800b4f9c` then copies those 16 bytes to `image+0x84` (`0x800b4fc0` adds 4; `0x800b4fcc` moves 16). `0x80541b48` reads that same word: it adds `0x80` and `0x800b4e00` returns `[subobject+4]` in every arm, including `0x800b4e44` (word `0xe5933004`). If the request id is 22, 23, or 24, `0x80139438` also calls `0x8025ccb8`, and `0x800b4ff8` copies the same kind of record to `image+0x94` (`0x800b501c` adds `0x14`). The byte at `image+0x80` is 0 after construction. Both image constructors, `0x8025c620` and `0x8025c808`, add `#0x80` and call `0x800b4b34` (`0x8025c648` and `0x8025c834`). That helper’s `0x800b4b60` (word `0xe3a02000`) moves 0 into r2, and `0x800b4b64` (word `0xe5c32000`) stores that byte at `[image+0x80]`. The same helper stores 0 at `image+0x84` (`0x800b4b7c`, word `0xe5832004`) and `0xffffffff` at `image+0xa4` (`0x800b4b6c`, word `0xe3e02000`, then `0x800b4b70`). With the type byte still 0, `0x8025cd4c` takes the arm that writes the allocation pointer at `image+0x84`. The copy helper `0x800b4cec`, called from `0x8025c860`, can replace the type byte from another image (`0x800b4d24` loads it and `0x800b4d2c` stores it). No other store of this byte was tied to an image passed through `0x80541b20`.

The image `0x80138d54` reads is the pointer stored at `this+4`. Constructor `0x80574ba4` writes vtable `0x80651020` at `this+0` (pool word at `0x80574bec`) and writes its second argument at `this+4` (`0x80574bb4` saves r1; `0x80574bd8`, word `0xe5832004`, stores it). Virtual slot +8 of that vtable is `0x80138d54` (the word at `0x80651028`). The word before the vtable, at `0x8065101c`, is `0x806512b4`, and that record contains the type name `31CCallback_Still_Get_ImageBuffer`. There are 144 direct callers of the constructor. The broad census does not classify all 144 runtime image arguments. However, the previously traced MID `0x15000` builder `0x802ba7b4` passes the same selector-0x28 image to material slot0 and this callback constructor; configurator `0x80141c78` sets its request id to 22. This specific static identity is established even though active R210 execution remains unproved. The constructor first sets type byte0, then this candidate configurator sets it to2; a later copy can replace it. No sensor RAW file has been written. Separately, the image argument to `0x8025cc24` in `0x8029e918` is the stack address `r11-0x114` (`0x8029ea74`, word `0xe24b2f45`). `0x8025cc24` adds `0x80` and calls `0x800b4f9c`. When its source pointer is non-null, `0x800b4f9c` copies 16 bytes to `[image+0x84]` (`0x800b4fc0` adds 4, `0x800b4fcc` is `mov r2, #16`). When the source is null it clears those 16 bytes. One arm of `0x8025cd4c`, at `0x8025cdf0`, stores that returned word into an output word. This stack image is not the static object at `0x81952e3c`. The function that performs the call is `0x8029e918`. It saves its `this` at `[r11, #-0x160]`. The PData read at `0x8029eac0` uses the previous value of `this+4`, before the later store. At `0x8029eadc` (word `0xe5832004`) it stores the stack image address into `this+4`. The function has no direct call to `start_wdma`. The pointer to `0x8029e918` is the word at `0x80688ae8`. Before the epilogue at `0x8029f118`, `0x8029f0f4` calls `0x8025c8d8` with the same stack address (`0x8029f0ec` is `sub r3, r11, #0x114`). That destructor calls `0x800b4cb8` and `0x8059d0fc`. It does not call arena release `0x800adb1c`. The stack image therefore does not survive the function, and this function does not hand it to `start_wdma`. After `0x8029eadc`, no later instruction stores a new value at `this+4` before the epilogue, so that field still holds the stack address when the destructor runs.

A different caller keeps allocation results on its object. `0x80176664` saves `this` at `[r11, #-0x298]` (`0x80176670`, word `0xe50b0298`). It calls `0x8025d9c0` with r0 equal to 14, 15, and 16, and stores the three returned pointers at `this+0x298`, `this+0x29c`, and `this+0x2a0` (`0x801766c0` word `0xe5832298`, `0x801766d8` word `0xe583229c`, `0x801766f0` word `0xe58322a0`). Inside the helper, argument minus 14 selects the case. Arguments 14, 15, and 16 pass tags `0x22`, `0x23`, and `0x24` to the manager virtual call (`0x8025dbc0`, `0x8025dbec`, `0x8025dc18`). Those are not the r0=3 call at `0x8029ea60`.

A fixed object at `0x81866980` also has words at `+0x298`, `+0x29c`, and `+0x2a0`. The sequence at `0x801caf70` loads each word, executes `0xe7df2011` (`bfi r2, r1, #0, #32`, which replaces r2 with r1), and stores it back. The three constants are `0x3e1a040b`, `0x021c3d50`, and `0x251e0c01`. None of those is in the arena range that begins at `0xa6000000`. The caller of `0x80176664` passes `[r11, #-0x28]` as `this`, and that value is not shown to be `0x81866980`. Offset equality is not enough to treat the two objects as one.

## Candidate identity and configured type: reconciliation

The static candidate chain already retained in pass4/image28 and pass7/candidate-lifetime is **MID producer0x802ba7b4 → selector0x28/configurator0x80141c78 → the same image in material slot0 and callback0x80574ba4**. Configurator instructions0x80141dcc/0x80141dd0 pass2 to **0x805615d0**, whose instruction0x805615f0 stores that byte at the embedded wrapper. Thus **image+0x80=2 after this configuration**, distinct from its constructor default0. Instructions0x80141de4/0x80141de8 independently pass22 to **0x80560ccc**, storing request22 at **image+0xa4**. Provider settings row22 is a third, separate identifier domain.

This restores known evidence rather than establishing runtime activation. A generic type0 installation branch is not the configured candidate's branch merely because the constructor initially wrote0. The existing descriptor calculator has a type2 layout path; successful pointer installation, calculated planes and backing retention must be interpreted using that path and any subsequently established copy/alias writes.

Retained evidence: configurator `ev_bcbd5659eb24fa8187466f28d30896a30d628447ca1ad4a563481932e87f7f78`; original configurator instructions `ev_23b637bff0f70fdb90f9c3d5e2a0254eabae316b2927e7b1f1009bef7145a63d`; wrapper accessor `ev_caee950de9434961fd192d23e8eb6ff74b4d63eede7782451bd5f84ba97f4d28`; type setter `ev_c3af02a8fd88e2152509cc3926d1024eccbe6c042fc5f974ca76fa1e5889489c`; request setter `ev_c4cb78375b7e3487178805c7c490ea604d1fff21e64eeaf87c369f627a16c2a9`; MID producer `ev_0fa01a39e8cfbd589a38d032836fbb99184f4a1130f77e1589f5704a21aca82f`; producer instruction evidence `ev_f332ac30a581abcc2f6ffcb104c14b2b9ed763d4114354a64ccd6109271343c0`. The multi-provider continuation independently checked the type/configuration sequence against original bytes. Later runtime copies and graph activation are still unknown.

## Still open

- A runtime writer for the Still-book map at this+0x1c. The constructor default-constructs that map and no static insert targets it. The indirect call at `0x80280780` uses it, while the static factory table, including key `0x1b`, belongs to the other map at this+4.
- Whether the kitchen lookup's runtime index is 0 and its table is the one filled by `0x80256680`. Index 0 of that registration receives the `CCook_Still` object. The lookup returns the first word of an 8-byte slot and does not pass the constant 0.
- How `StopVideoStream` is entered. This image has no direct branch and no stored address for `0x8031e858`. Pause and PostStillCapture use the interface at `0x81952fb8`. No live post was observed.
- The exact static MID0x15000 candidate is linked to request22 and callback `0x80574ba4`; its configurator changes the embedded type byte from0 to2. Active invocation, subsequent aliases/copies, successful allocation and safe backing retention remain unproved. The unrelated r0=3 call returns `0xbfffffff`; that does not invalidate the request22 candidate. No sensor RAW file exists.
- Ownership that keeps that allocation stable through an export copy.
- External delivery of the dump-enable command record before recipe creation.
- Actual sample bytes, packing, and live geometry. Those still require a captured frame, which this offline pass does not have.
