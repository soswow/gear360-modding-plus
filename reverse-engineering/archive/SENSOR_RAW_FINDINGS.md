> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Sensor RAW investigation: Linux callback, RTOS buffer, and sensor bus

Date: 2026-10-13 (Australia/Sydney). Device image: Gear 360 SM-R210, build R210GLU0AQC6.

The [2026-10-14 continuation](RAW_BUFFER_AND_DUMP_TRANSPORT.md) adds RAW pointer lifecycle, matching firmware/Linux file-dump contracts, and a compiled offline C++ prototype. Earlier open questions here are historical; the continuation records which remain unresolved.

## Result

We have found a concrete sensor-register control path in the captured RTOS and a still-image manager that stores objects in a list named `MainRaw`. We have **not** recovered a sensor RAW frame or established a usable Linux RAW callback. The previously suggested `0x02030002` callback is DISPLAY1, a liveview path.

This pass used the connected REA MCP with Ghidra 12.1.3. Existing capture-library analysis was reused through its matching snapshot. Two independent offline investigations corroborated RTOS byte references and the ARM callback ABI. No live camera command, service restart, sensor write, target execution, or original-image modification occurred. Native REA sessions were closed and snapshots/evidence were saved.

## Questions and status

| Question | Status | Evidence |
| --- | --- | --- |
| What does `0x02030002` mean? | Answered: DISPLAY1 | RTOS numeric-to-name branches; Linux `setDisplay1FrameData` publishes that exact ID |
| What is the single-image MAIN_RAW topic? | Answered: internal `0x02010001`, public `0x03010001` | RTOS formatter, Linux `ID2String`/image-name table and both ID converters |
| Who writes the `0x1274` descriptor? | Answered for DISPLAY1 | RTOS `CLiveviewImageCallback::Publish` converts an image and writes a 136-byte descriptor |
| Where does software reach sensor control hardware? | Static path recovered | Sensor class/register helpers → registered I2C operations → MMIO controller |
| Where are candidate RAW images retained? | Partially answered | `SetMainRawImage`, its callers, and the manager list at `this+0x6c` |
| Can our C++ function receive actual sensor RAW? | Unresolved | Producer/export, active mode, pixel format values, ownership, and cache/lifetime are still missing |

## Exact event namespaces

| Meaning | RTOS/internal topic | Public Linux callback ID |
| --- | --- | --- |
| Still MAIN_RAW, single image | `0x02010001` | `0x03010001` |
| Still MAIN_YCC, single image | `0x02010002` | `0x03010002` |
| Liveview DISPLAY0 | `0x02030001` | `0x03030001` |
| Liveview DISPLAY1 | `0x02030002` | `0x03030002` |
| Liveview MOVIE | `0x02030003` | `0x03030003` |
| Still RAW image set | `0x03010002` | `0x04010002` |

The same integer can mean different things at different interfaces. In particular, `0x03010002` is a **public single-image MAIN_YCC ID** in one namespace and an **internal RAW image-set topic** in another. Neither is single-image MAIN_RAW.

In the original RTOS bytes, the MAIN_RAW comparison at file `0x6f87c..0x6f888` builds `0x02010001` and branches to the return block at `0x6fe60`, which constructs string address `0x805ee728`. DISPLAY1 comparison at `0x6f938..0x6f944` builds `0x02030002` and branches to `0x6fef0`, returning `0x805ee8e0`. The portable topic script records the full selected branch paths and rejects unsupported instructions/conditions.

These are name bindings, not observations of event production. The link base `0x80000000` is supported separately by the boot jump literal at file `0x25c` (`0x80000e38`) and absolute string/code tables. A current live firmware mapping was not measured.

## The old candidate is now traced end to end

Linux `CAttributeHandler::setDisplay1FrameData` at Ghidra `0xacba8` publishes `0x02030002`. Linux `CCapImageCb::OnCallback` at `0x9403c` reads `0x88` bytes at `pref_rtos+0x1274`, follows descriptor physical-address field `+0x44`, creates virtual plane addresses at `+0x54` and `+0x58`, and forces address type `+0x40=7`. The wrapper logs `DP1 CB`.

RTOS `CLiveviewImageCallback::Publish` at `0x8015e424` converts its incoming image with `0x8027a7b0` into a descriptor at `this+0x28+slot*0x88`. For DISPLAY1 it writes that descriptor through `0x8052643c`, with selector `0xff`, offset `0x1274`, and length `0x88`. Its descriptor staging index wraps after ten slots. The selector helper resolves `0xff` to `0x85400000`, so the destination is `0x85401274`.

This closes the descriptor producer/consumer edge for DISPLAY1. It does not locate a sensor RAW producer, nor does it prove the image pixels live in the 512 KiB descriptor window.

## Why registering the RAW ID is insufficient

The Linux ID converters support public `0x03010001` ↔ internal `0x02010001`. However, `CCapImageCb::OnCallback` populates the outgoing descriptor only for the three liveview IDs. A still MAIN_RAW event would leave its descriptor initially zero, pass through zero-dimension/zero-address handling, and still invoke the client callback. The wrapper therefore does not supply a valid RAW descriptor merely because registration succeeds.

ARM instructions also correct a misleading decompiler signature. This virtual member takes `r0=this, r1=topic, r2=integer payload, r3=CCapImage*`. The external C callback receives the original **integer payload** in its `void*` argument; it does not receive the incoming image object. The wrapper ignores that original image-object argument. The descriptor itself is borrowed stack storage, valid only during the synchronous callback.

A direct `IImageCallback` subscription may bypass this wrapper, but a working RAW publisher and a valid image-object/buffer contract have not been established. Registering in a separate process was already an uncalibrated historical experiment; no success is claimed here.

## Actual sensor-facing code

The RTOS contains a sensor-class RTTI name `CS5K2M8SXDriver`, typeinfo at `0x80676b90`, and a vtable address point at `0x80676a70`. A slot points to `0x80242018`, whose log identifies `SetStillFineGain`. That method clamps a fine-gain value and calls a register-write routine for register `0x0204`.

There are **two distinct static transfer paths**:

1. `0x80243a04` builds five bytes `[2, register_hi, register_lo, value_hi, value_lo]` and writes them through an object interface using `0x8030e1ac`. This queued/object path has not been traced to its final receiver.
2. `0x80243928` locks the driver context and calls `0x8030e61c`, which builds four bytes `[register_hi, register_lo, value_hi, value_lo]`. It sets request byte `0x10`, pointer, and length four, then calls `0x80035224` for channels 3 and 4.

The register-`0x0204` method at `0x80241c58` and register-`0x0600` method at `0x80244960` select between those paths using a word at `0x85180000`. That word's live value and meaning remain unknown.

The direct path's controller binding is stronger than a bus-related string: `register_i2c` at `0x80034cdc` passes operations table `0x805e5a88` into `0x80035048`. Table slot `+0x0c` contains `0x80035d48`. `0x80035224` dispatches through the registered per-channel table. `0x80035d48` loops over bytes; `0x80035b34` writes them to controller data register `0x1003400c+channel*0x100` for nonzero channels, with status polling. Channel zero uses a separate controller block.

Thus the image contains a static route from sensor register settings to I2C controller MMIO. Runtime registration, selected bus path, sensor address convention, and current hardware execution are not observations from this pass.

## Candidate RAW storage and metadata

RTOS `SetMainRawImage` at `0x8013b098` appends an accepted object to a list at manager `this+0x6c` and logs its count/address. Direct calls were recovered at `0x8013c538`, `0x8013cbb8`, and `0x8032bef8`.

The last caller, function `0x8032be1c`, logs **Ipc_End**. It retrieves an image through `0x80251c58`, calls `SetMainRawImage`, then calls `0x80251c9c`. The precise semantics of the last call still need tracing. Separately, control functions at `0x8013c0d4` and `0x8013c754` request images, emit `Raw Image is NULL` on failure, and call the same RAW-list setter. This is a concrete allocation/storage lead, not a verified sensor frame.

An important guard is **object kind**, not pixel format: `0x80560d24` reads a word at RTOS object `+0xb0`; both RAW-list and YCC-list setters require it to equal one. RTOS image-format getter `0x80541a08` reads a **byte at +0x54**. The similarly named Linux object uses a different layout.

| Field | Linux `CCapImage` | RTOS image object | Shared `stCAPTURE_IMAGE` |
| --- | --- | --- | --- |
| Pixel format | word `+0x30` | byte `+0x54` | word `+0x24` |
| RAW pixel-bit enum | word `+0x34` | byte `+0x58` | word `+0x2c` |
| RAW packing-bit enum | word `+0x38` | byte `+0x59` | word `+0x30` |
| RAW packing type | word `+0x3c` | byte `+0x5a` | word `+0x28` |
| RAW stride | word `+0x48` | word `+0x60` | word `+0x20` |
| Buffer object | `+0x68` | `+0x80` | address structure `+0x40` |
| Buffer size | buffer `+0x28` | buffer `+0x28` | word `+0x74` |

Both conversion routines represent RAW and YCC, and translate format enums into the shared descriptor. Generic RAW bit masks/packing enums are recovered; **no actual R210 frame's values have been observed**. The format byte, bit enums, stride, size, and physical buffer pointer in a real candidate object are the next decisive evidence.

The functions named `Raw_End` at `0x8032ba2c` and `Raw_End_ExecptDelImg` at `0x8032c140` actually call the **YCC-list setter** at `0x8013b4b0`. Log names alone must not classify the final buffer.

## Evidence, reproduction, and limitations

Input identities:

- Linux capture library SHA-256: `fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7`.
- Captured RTOS SHA-256: `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, 31,457,280 bytes.

REA cannot directly analyze this headerless RTOS through its native ELF route. [make_rtos_analysis_elf.py](../make_rtos_analysis_elf.py) creates a **partial derived analysis carrier**, copying selected original ranges byte-for-byte at the established link addresses. ELF headers, symbols, section permissions, and entrypoint are analyst-created. Omitted callees and runtime RAM remain unresolved; function parameter types can be wrong. This carrier is never executed.

The initial focused carrier has SHA-256 `b89d5e6d6f1d05e3c2e0e41917165d9fc3a41896dd007d0a7b6a91ae7e817b25`; the expanded carrier has `d0369039347b01e21b0984866695bdd2c9d8801cb0dbbee7c6597cacddc1870f`. Two provisional expanded labels were corrected after following strings/getters. The corrected carrier has `bc34bd657f742ee85766f76615bc43c9180f6cbdf82e99dcab0a6e74a04b31f6`. Use `--legacy-labels` to reproduce the original expanded carrier; its labels are not authoritative source names.

Selected REA Evidence IDs (full records are in local `output/decompile/rea/raw-investigation/`):

| Finding | Evidence |
| --- | --- |
| Linux ID conversion and name-table selection | `ev_5a56b9001717a0fb398395d855f295decda9b22df1de95bbb23151b3afc66657` |
| Linux descriptor wrapper and registration | `ev_bf43608a0edee1d724043a20951d7b031f885b7bb437bd6db8e789631aa263fc` |
| Linux DISPLAY1 publisher | `ev_4fd33f0e54feeecff68bcd28bbada7c576d7f6735325111e39c54fcad80272a8` |
| Linux descriptor conversion/public ABI | `ev_9c84a2036d400cd9d7443127da7e0f8e62e376c69107f189ebf70d8a873a8f8f` |
| RTOS liveview descriptor writer/converter | `ev_38771207b90e205d2a9699e83eecb8aaaa0705b887a6cf84862ffec5ab7196b1` |
| RTOS sensor helpers/RAW-list setter | `ev_857086dfb77b2830fead70fec3accb95d5cd4f0a94023f08286732f61faae861` |
| RTOS kind/format getters | `ev_d9c256c72cda4bbeba924112a8ce64f3ff9936c261780788cd7f5b410095465c` |
| RTOS selected register callers/control allocation | `ev_90c88434fa11e3a2f463da4213565455b10a777d4353f5417838b36824e4dcbe` |
| RTOS registered I2C controller/MMIO | `ev_4b78b2075c3e3dfa0bb16ddc4aa68d24e1dd3595b814a5aa0e33e9ba285d9cb8` |
| Corrected RTOS labels/RAW metadata offsets | `ev_7fd5964cac91712db21911394e67383d10ed50e3c96bb008fec50a2ed7ed3682` |

Original byte excerpts and hash-bound topic paths are retained in rtos-byte-evidence.json (not published: `../rtos-byte-evidence.json`) and topic-bindings.json (not published: `../topic-bindings.json`). Detailed local API/RTOS reports and unedited pseudocode are also saved in `output/decompile/rea/raw-investigation/api/` and `rtos/`.

Reproduce without contacting the camera:

```sh
python3 reverse-engineering/recover_rtos_topics.py output/decompile/rea/rtos-p6.bin
python3 reverse-engineering/make_rtos_analysis_elf.py \
  output/decompile/rea/rtos-p6.bin \
  output/decompile/rea/raw-investigation/rtos-corrected.elf --expanded
```

Validation covered source SHA-256, every selected segment's byte equality, carrier determinism, exact replay of the initial expanded digest, the six numeric/name paths, independent original-byte ranges, and malformed-source rejection. Decompilation was corroborated against ARM instructions where signatures, names, or enum meanings could mislead.

## Next focused work

Trace image construction at `0x80140e94` and consumers of the manager's `+0x6c` list. Recover the candidate object's actual format, geometry, packing, buffer address, and ownership through SSIF/IPC completion. Then establish whether a firmware path exports that object or a descriptor through MAIN_RAW `0x02010001` to Linux. A userspace C++ receiver should follow only after that edge is proved; registering a generic enum or calling RTOS addresses from Linux does not create the missing transport.
