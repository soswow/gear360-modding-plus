> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# RAW dump protocol and diagnostic activation

2026-10-14 continuation of [RAW buffer lifecycle and dump transport](RAW_BUFFER_AND_DUMP_TRANSPORT.md). REA MCP and private REA sessions analyzed captured binaries with Ghidra 12.1.3. All work was offline; no camera command, restart, preload, deployment, or original firmware execution occurred.

The later [image and cache continuation](RAW_IMAGE_AND_CACHE_FINDINGS.md) resolves the static memory-operation receiver and cacheFlush call path, and identifies the manager's factory-pointer setter. Its remaining runtime and image-provenance limits still apply.

**Both dump routes now have reconstructed request and completion contracts. No sensor RAW frame has been obtained.** The unresolved priority is the active R210 graph and the provenance, format, and lifetime of the selected buffer.

## Initialized RTOS tables recovered

The flat RTOS image contains zero bytes at many runtime data destinations. Reading those bytes as initialized dispatch tables loses real code connections.

Startup instructions `0x80011620..0x8001163c` load three original literals and copy 64 bytes per iteration:

| Value | Address |
| --- | --- |
| ROM source | `0x807ef900` |
| RAM destination | `0x81006000` |
| Destination end, exclusive | `0x81447f40` |
| Copied length | `0x441f40` |
| Destination minus source | `0x816700` |

Thus a table's initialized source offset is `runtime_address - 0x80000000 - 0x816700`, within this copy range. This establishes its startup contents, not its current live RAM contents. [Selected original instructions, literals, tables, and hashes](../dump-protocol-evidence.json) preserve the binding.

The carrier builder accepts `virtual_address` for read-only data ranges. This places exact ROM initializer bytes at their established destination address for analysis. It validates virtual overlap, alignment, 32-bit bounds, and source-byte equality; executable remapping is rejected. Original binaries remain unchanged. Ordinary link-base carriers retain their previous digest.

## Whole-buffer dump: request through completion

RTOS `FileDump` (`0x8010ccd8`) constructs the previously recovered `0x4c` filename/address/count/flag descriptor, performs memory-manager operations, publishes event `0x01070005`, and waits on its object's semaphore at `+4`.

The normal notification subscription registers this event in `0x8005ea2c`, using the control object's notify interface at `this+8`. Constructor `0x8005dd90` installs that interface's vtable. Its `+8` entry is adjustor `0x8005e3e4`, which subtracts 8 from the receiver and branches to `0x8005e3b4`; that wrapper calls transmitter `0x8005fadc`. Callback-register dispatch `0x800b3af0` invokes the registered interface at virtual slot `+8`.

For event `0x01070005`, the transmitter selects category 1 and subcommand `0x82`, packs them through `0x80060854` into **`0x0182`**, and submits family `0x82`, payload length 4, and the descriptor address. This closes the previously missing event-to-selector mapping. The special gate at `0x800a6848` belongs to unit-test infrastructure; its default flag is cleared. It should not be mistaken for the ordinary transmitter.

Linux `CNotifyHandler::Run` (`0xa168c`) selects system category 1. `subCategorySystem` maps wire subcommand `0x82` to table index 9, whose relocated entry is `onFileDumpRequest` (`0xa24c8`). That handler requires four payload bytes and calls `DumpFileData` (`0x627d4`). It maps the descriptor and pixels through `/dev/mem`, writes the named file, performs cleanup, and sends **`0x0105` with zero payload bytes**.

RTOS command dispatch `0x80061b44` selects category 1 from the command word. Category method `0x80061c24` selects low byte 5. The initialized table at `0x8100f810`, entry 5, points to `0x80062694`, which accepts only zero payload bytes and dispatches internal command **`0x0107000b`**. Operation dispatcher `0x800502a8` calls `FileDumpDone` (`0x8010ce50`), which posts the same semaphore at object `+4`.

```mermaid
sequenceDiagram
    participant R as RTOS FileDump
    participant L as Linux DumpFileData
    participant A as RTOS ACK handler
    R->>L: 0x0182, four-byte descriptor address
    Note over L: Map descriptor and buffer; write file
    L->>A: 0x0105, zero-byte payload
    A->>R: FileDumpDone posts semaphore +4
```

**ACK arrival is not evidence of a valid saved frame.** Linux sends the completion command after its cleanup path, including the file-open failure branch; its return value is the send result. Validation must inspect file existence, size, and contents independently.

## Streamed SSIF file operations: result and wakeup

The SSIF row writer uses operation selectors `0x0c/0x0e/0x11` for open/write/close in a `0x20` request. Its broker submits family `0x82`, **`0xaa06`**, with a four-byte request address. The immediate path `0x8006bae8` attempts `CFrame::Send` through `0x80130a94` and `0x80069d24`. Failed sends can enter a queue. The traced generic queue callback performs cleanup for this request type; retry/delivery behavior under failure remains unresolved.

Linux category decoding maps `0xaa` to production table index 10. `execProductionNotify` (`0xa217c`) accepts subcommand 6, whose relocated table entry points to `onFileIORequest` (`0xa4b24`). Its four-byte address feeds `RunAdjustFunction` (`0x65b10`), which maps the 32-byte request, calls the corresponding file API, and sends **`0xaa0b`, four-byte result**.

RTOS command category `0xaa` maps to index 8, selecting `0x80062340`. Its production subtable at `0x8100fa38` accepts indices below 13; entry 11 is `0x80064df4`. This handler validates length 4, reads the result at message `+8`, and calls `0x8005215c`. That wrapper gets singleton **`0x81475270`** through `0x8004e100` and calls `AdjustProcessDone` (`0x8004e910`): store result at **`+0x24`**, then post semaphore **`+0x20`**. The file-operation wrappers wait through `0x8004e958` on that semaphore and return the stored result.

The opaque Linux file-handle token and operation result are integers in RTOS. They are not RTOS pointers to dereference. The streamed route remains distinct from the whole-buffer descriptor and its `0x105` ACK.

## SSIF activation and graph identity

The backend parser `0x801e7f88` recognizes **`unitest`**, obtains singleton `0x801ebe3c`, and calls nested parser `0x801ebf20`. It recognizes `ssifdump on|off` and sets byte `+0x19`. The help text suggests the outer route `st cap back`, but misspells the listed token as `uniteset`; the actual comparison is `unitest`.

The static command candidate is `st cap back unitest ssifdump on` (and `off` to clear the flag). Both parser and dump routine `0x80282760` use singleton **`0x8186c898`**. Getter `0x80573cec` reads its byte `+0x19`, proving the setter-to-guard link. Setting this flag alone does not prove the selected capture path runs.

The dump is called by method `0x80282b84`, whose constructors and vtable identify **`CMaterial_NX1_Still_IPC_Main`**. The firmware's inclusion of this shared class does not establish its use for an R210 shot. Additional guards require another property to equal zero and the current output image's mode getter to equal one.

Token `0x15000` is a **material ID**, compared against each graph element's word `+0x100`. Setter `0x802519d0` writes that field; `SetMaterial` (`0x80257bac`) obtains a material from a factory and stores its MID. A full ARM immediate scan found an assignment in `0x802ba7b4`: original instruction `0x802ba874` supplies MID `0x15000`, which reaches `SetMaterial` at `0x802ba960`. Nine raw branch-target candidates are saved; the provider confirms seven caller functions. The builder creates a `0xc8`-byte image with selector `0x28` and inserts it into material output slot zero. Selector `0x28` is distinct from main-RAW selector `0x33`; its actual configurator is `0x80141c78`, while `0x80141444` is an interior jump case. Normal and HDR recipe error paths identify the builder's result as an SSIF material. The concrete factory entry, active R210 graph, and link from that image pointer to sensor DMA remain open. The hardcoded `10bit(16bit).raw` filename does not identify its actual pixel or memory-packing bits.

The captured Linux command route is also registered: `st` imports `top_cmd_list` and `process_option` from `libshell-command.so`. Its relocated table has `cap` at index 8 with option zero and handler `shell_capture` (`0x19dd4`), reached through `shell_exec`. The nested list contains `log`, `mcb`, and the sentinel; `back` enters its fallback. That fallback assembles the complete command text, including a trailing space, and submits `mcb_send(0xb6, 0, strlen(text), text)`.

RTOS registers family `0xb6` in `0x8005e5e0`; the callback and handler selector reach `CMiscHandler::Run` (`0x800651b4`). Its B0-family subcommand 6 entry is `0x8006606c`, which tokenizes the payload and calls shell manager `0x8004a848` with `argc-1, argv+1`. That method selects its `argv[1]`, therefore original token `back`, rather than `cap`. Registration `0x801e7aac` binds key `back` to `CBackendDebugProcess`; its virtual method is `0x801e7f88`, whose following token is `unitest`. This closes the static command-registration chain. No command execution or runtime availability was observed.

The dump's indirect operation at vtable slot `+0x78` remains unresolved. Getter `0x800a0f74` obtains the receiver from holder `0x81c0d7bc` field `+0x24`; its initializer clears that field. The receiver assignment and concrete vptr still need tracing before calling the operation cache clean/invalidate or assigning physical/virtual address semantics. `0x805836e0` registers a destructor; it does not construct this receiver.

## Next validation boundary

Before a live capture, establish the R210 material factory entry for MID `0x15000` and the unresolved buffer/address contract. Then record a known JPEG positive control from the healthy existing app, verify dump storage and expected byte counts, and observe actual image metadata and lifetime. Keep service/preload recovery constraints in place. A successful command or ACK alone cannot satisfy sensor provenance.

For saved bytes, the [offline C++ inspector/splitter](../raw-buffer-prototype/README.md) implements the verified descriptor and row-copy contracts. A future real buffer must be checked for geometry, padding, Bayer content, pixel bits versus packing bits, and correspondence to both sensors before claiming RAW extraction.

## Evidence and checkpoints

Source RTOS SHA-256: `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. Linux library SHA-256: `fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7`. Full traces, byte excerpts, manifests, private projects, and closed-session snapshots are saved under `output/decompile/rea/continuation-20261014/pass3/`.

| Finding | REA evidence |
| --- | --- |
| Initialized-data copy and mapped dispatch | `ev_e850a09f1fcee61fb7e0a82c8252261562429d1ca773a24b75420649141798f1` |
| Whole-buffer mapping and ACK functions | `ev_5cf8567ccba0cc4ca4316b92f05cc6dcc52f2bb723af637ab91b9ab9f2a905f7` |
| Linux notification selectors | `ev_f109f315eca4f232f3c9d86fd18ddb5dd24c817378f7ac7e8fa8b3795f597dfe` |
| AA0B leaf handler | `ev_74258ae21860206b0f58e30762c90a5c782e62eabd3cbd283b27be9d78d4e6b4` |
| File-operation result and semaphore post | `ev_b1b7f1b4cec73a96d3b717b96b79f3795bb6e867a995a5067f9357d8b689ac4f` |
| Shared SSIF flag singleton | `ev_1604df28e712930ecc9cab1c5bd55655dfcec0303059c7d8ca00f057f7533fd6`, `ev_3adfde7ba9a97e4b853ca0156b47f5f0d619134bb6c6f907606d5a1b66aac53c` |
| Material-ID setter and class constructors | `ev_f62dd7d6509a8c965f084bdd584737337ffe109b9d048e78f5157c930daf4d2e`, `ev_21bffc40804046e59c8ff6082eb583d6ca462c17f472d0c2c417662b9893b22c` |
| Registered Linux shell and capture handoff | `ev_a0d36e1642729580884ae0526beb124762f17e41ca773fd6fcf823ffeb4f930b`, `ev_4dfaf2b0eba0d9c4e66b61b53831484d514d51ddd8a159eaa35840473eccf2aa` |

Two Luna workers performed independent RTOS tracing; a third traced the captured `st` executable. Usage checks during this continuation recorded 44%, 54%, 64%, and 71% of the five-hour window used. Checkpoints were saved between phases.
