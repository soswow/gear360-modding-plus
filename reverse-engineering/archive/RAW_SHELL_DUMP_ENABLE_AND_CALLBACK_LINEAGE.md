> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Shell dump-enable route and recipe callback lineage

Reviewed 16 October2026,03:45 investigation. This is offline static analysis. No shell command, firmware or camera operation was executed. The sensor RAW acquisition goal remains unfinished.

## A conditional external route to the candidate dump flag

The recovered candidate spelling is `st cap seq dump on ssif`. This is a static route candidate, not a tested acquisition instruction. It differs from the older `st cap back unitest ssifdump on` branch: the latter changes a different flag at `0x8186c898+0x19`.

In `libshell-command.so`, top-level row8 is `cap`, handler `shell_capture`, process option0. Its nested table contains `log`, `mcb`, `end`; unmatched `seq` takes the generic forwarding branch. Fresh analysis confirms that this branch prefixes `st `, appends every supplied argument and a space, then calls imported `mcb_send(0xb6,0,strlen(text),text)`. The concrete candidate payload is `st cap seq dump on ssif ` with a trailing space and no NUL included in the length. Linux need not contain a literal `seq` for this generic forwarding route.

| Stage | Static argument state |
|---|---|
| Linux capture handler | count5, `[cap,seq,dump,on,ssif]` |
| B6 text | six tokens, `[st,cap,seq,dump,on,ssif]` |
| RTOS `0x8006606c` → registry `0x8004a848` | count5, vector advanced one pointer: `[cap,seq,dump,on,ssif]` |
| Registry selects its vector slot1, `seq` | forwards count4, `[seq,dump,on,ssif]` |
| Sequence method `0x8027d9e8` selects slot1, `dump` | copies the remaining two option strings, `[on,ssif]` |
| Handler `0x8027eb24` | `on` branch plus `ssif` contributes bit`0x10` |
| Setter `0x80570b64` | assigns the computed whole word to `0x819521c4` |

The registry accessor is `0x8004a79c` on both the B6 dispatch and retained `seq` registration path. The sequence object's constructor registers the dump callback. This joins the previously separate shell, B6 and sequence-parser observations. The candidate builder `0x802ba7b4` tests bit`0x10` during construction to include the optional DumpSize material; sending a flag after a recipe is already built does not establish that material was installed.

Parent review checked original machine instructions: tokenizer output uses actual `r11-0x150` at `0x800660d4`; registry vector uses the same subtraction at `0x80066110` followed by `add #4` at `0x80066114`, hence `r11-0x14c`. Ghidra stack-variable labels have a four-byte frame convention difference and must not be read as these machine offsets. The old MiniMax unadvanced-vector claim is rejected.

**Limits:** the imported transport implementation, live registration/listener state, command success, exact flag value at active recipe construction, output completion and pixels remain unverified. The sequence argument constructor initializes five slots to a non-null empty-string pointer; the candidate overwrites slots0/1, and the handler scans slots1–4. Parent-reviewed original helper bytes show unused slots2–4 remain empty, so they contribute no other option bits on this exact two-option path. Arbitrary malformed inputs are outside this conclusion. Flag assignment is a full-word write, not a preserving OR. This route establishes neither sensor provenance nor safe capture-buffer retention.

## Recipe and callback lineage

Fresh fulltext analysis resolves four kitchen-helper callsites to these containing callback methods:

| Containing method | Helper call | Recipe ID | Map-selector argument8 | Callback source |
|---|---|---:|---:|---|
| `0x803373cc` | `0x80337524` |4|0|`0x8057f8ec(0x80251e4c(material))`|
| `0x8033757c` | `0x803376ec` |5|0|same owner-getter path|
| `0x80337734` | `0x80337920` |6|0|same owner-getter path|
| `0x80337aac` | `0x80337c04` |9|0|same owner-getter path|

`0x80251e4c` returns the material owner; `0x8057f8ec` supplies that owner's callback field at`+0x60`. This value is forwarded as kitchen-helper argument5. Source owner, destination cook, recipe ID and map selector are distinct roles. It does not identify the earlier producer that installed this callback. The callback method `0x803373cc` occupies vtable slot`+8` at address point`0x806b4278`; constructor `0x805750ec` installs that vptr. Original RTTI bytes name `35CCallback_Still_Send_RawQView_Order`. Direct constructor callers are factory methods `0x802ad89c` and `0x802ade70`; that fact alone does not prove the object is the value later stored in owner`+0x60`.

Earlier partial-carrier body ranges excluded callsite`0x80337524` even though the pseudocode included it. Fresh fulltext body resolution closes that analysis-boundary discrepancy. Nearest sparse LLVM symbol labels are not enclosing-function identities; the Cursor report's earlier giant-function interpretation is rejected.

Another compiled order path `0x8027510c` calls `0x80138a40` with recipe3 when helper`0x8054e27c` returns`0x15`, otherwise recipe1; its map-selector argument is zero. `0x80138a40` forwards its recipe argument and its seventh parameter into the kitchen's map-selector argument8. The incoming caller `0x8027354c` is at CDriveAdjust vptr`0x8067cc78`, slot`+0xcc`. Its internal virtual`+0x98` call is outgoing behavior, not an established inbound activation edge.

Constructor `0x80272c20` installs that vptr and clears byte`+0x19`. Parent original-byte review confirms caller`0x800f96bc` allocates`0x1c` bytes at`0x800f97b4`, calls the constructor at`0x800f97c8`, and stores the same pointer at parent`+0x2c`. A prior `+0x64` description was wrong; there is no allocation-size discrepancy after correcting it. Active R210 selection of these methods remains unknown.

## Evidence and reproducibility

The versioned [evidence index](../shell-dump-enable-callback-lineage-evidence.json) binds full records, snapshots, reports, source windows, parent review and prior registration/table dependencies by hash. Raw working artifacts are under `output/decompile/rea/pass0345-20261016/{recipe-request,callback-supplier,b6-seq-route,linux-shell-route}`.

RTOS original: `rtos-p6.bin`, SHA256`cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, link base`0x80000000`. Fulltext carrier SHA256`3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`. Linux shell ELF SHA256`0fd079a9ed4cc8287be409dd3427ef9b28e0a3295eaedfddba548b0409d4a1a8`; ELF VMAs`0x9dd4/0x8524` correspond to REA addresses`0x19dd4/0x18524` by verified LOAD mapping and bias`+0x10000`. Ghidra12.1.3/profile digest`2efb2839e0e54d98e487132a182b70bf8f5b5989724d969b01494dc44c0182a2`.

Full key Evidence IDs:

- Linux capture `ev_4dfaf2b0eba0d9c4e66b61b53831484d514d51ddd8a159eaa35840473eccf2aa`; shell dispatch `ev_a0d36e1642729580884ae0526beb124762f17e41ca773fd6fcf823ffeb4f930b`.
- B6 function `ev_f1e912a105f690356fbd7e4edc8a7827ba1678219287032991c66b5e8c8b7cb7`; registry `ev_858c4aa5ec7b79756b7ccbdb8ac29d26d009cdd167292d2cdca970d35d369101`; sequence method `ev_5c39719581af50105502f17257b7f602368a6b9ba1dea58e1fe0795fee790c4c`; handler `ev_2047826fe108b05772f8c1aa2f672cb2a1948c4fac8ffb1dc38f660fba8cabd3`; setter `ev_3fc8464d53ea6754e9c385710e690c7f1c463d3fe78026f74b3b88d9de1f9f56`.
- Dump-child registration `ev_5038cdea11f9c316c71e378b5192f33d58ebefa4bbac96e84691e76b0552982c`; actual sequence-root constructor `ev_362ed582882924a3db632c5bb62eb72a2b958d2575925c615f598f24d785ef00`; generic registry-insertion helper `ev_084108a53e040d574fd676e1b39e58b3daa5eff7df3db16e964fb2eedbdecc4e`. The historical report ID`ev_75ccfd7e91ca1ebc2fc5ea3311f99271eb16a002c9feef1a7467d0c6fb225573` could not be found as a full record and is superseded here by the retained constructor record, whose separate carrier is hash-bound in the index.
- Recipe forwarding `ev_3cf3b80a1040742034accb24d704a4596ff15781f10e51fc8e327097c9a5bc07`; recipe1/3 caller `ev_0f2ab7689191719e45c46a22173539ebc7b4c9ed591a80fb408e597c57585277`; CDriveAdjust method `ev_a4ad172bbedf88f0fe24533495e14fa53b4068f14107256d1e41278ce9fe0894`.
- Four callback bodies: `ev_599b7b1106b598614abf7af64df41f2873105b0f0608cf17a101838108bf31ba`, `ev_eb87ed69f2f9fa9700e79d55a5277b0eaa56c511d712e002d8c23818f1249b80`, `ev_ab37cfc4ac981dba8b726b3b363980b262637045f3f85277e72b74a9303d178c`, `ev_2d9b692e886ea73b79f654663bbb0e4a6ff9641300ae0122f06fe96d40b47996`.

Parent independently compared the retained RTOS windows with the original image, all matching, and preserved exact token-shift/constructor instructions. The evidence validator checks schema/source consistency, not runtime behavior or decompiler correctness. Continue from the remaining runtime activation, hardware provenance, freshness and backing-lifetime obligations; no verified sensor RAW file exists.
