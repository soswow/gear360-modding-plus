> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Core input descriptors and bulk cleanup callers

Reviewed16 October2026. All evidence is offline static code from the captured R210 firmware. No device/firmware execution occurred; no sensor RAW file exists.

## Core input writers are identified

Core singleton `A=0x81952f94` keeps output0 at`+0x10`, input0 at`+0x14`, input1 at`+0x18`. Original instructions and fresh Ghidra analysis identify the adjacent setter `0x8031b8f8(receiver,value,index)`:

| Index | Store instruction | Destination |
|---:|---|---|
|0|`0x8031b924: str r2,[r3,#0x14]`|input0|
|1|`0x8031b940: str r2,[r3,#0x18]`|input1|
|other|error path|no additional mapping established|

The forwarding caller `0x803954e4` obtains each receiver from `0x8031aa88`, the lazy Core getter. It takes values from its own descriptor's`+0x14/+0x18` and supplies indices0/1. The two calls are at`0x803955ec/0x80395610`. Its configuration block is skipped only when all three conditions hold: the byte returned through`0x80580a7c(0x80319188())` is zero, caller argument3 equals1, and`0x8057bb30(descriptor)` returns zero. This guard must not be silently replaced by an unconditional input assignment.

Setup caller `0x8031a37c` positively clears both input slots using null with indices0/1. It separately passes its descriptor`+0x14` to output setter`0x8031b894`, which stores Core`+0x10`. The same numeric offset in two different object types is not the same role. The candidate's recovered descriptor route reaches output0, not an identified sensor receive destination.

Core constructor `0x8031a9a4` zeroes bytes`+8..+0x1f`. `start_rwdma 0x8031b9b4` later reads input0; its call chain is`0x80321a28 →0x803958b0 →0x8031b9b4`. Constructor zeroing and the setter's null caller do not prove that no sensor path exists. An inline hardware source remains possible, but is not established by a null memory-input descriptor.

**What changed:** the previous “input writer unknown” is narrowed to the producer of the descriptor forwarded by`0x803954e4` and the active configuration/guard. The initial direct xref query for absolute`0x81952fa8` returned empty; that bounded result did not exclude pointer-relative stores. Parent located the adjacent setter in the original code and the followup resolved its callers. Do not repeat the absolute-cell census as an absence argument.

## A concrete bulk cleanup caller

`cleanBuffers 0x800adfec` has a direct compiled caller at`0x80102804`, inside`0x80102758`. Original instruction word`0xebfeadf8` decodes as ARM BL to the cleanup routine. An exact literal-pointer scan had missed this because an ARM branch encodes a displacement, not the target address word.

The routine first obtains an object from`0x8020d23c` and makes a virtual`+0x8c` call on that object. It then obtains the capture-memory manager through`0x80086b20`, calls`cleanBuffers` directly, and later sends event code`0x23`. Getter`0x8020d23c` loads published Frontend pointer cell`0x81952fc0`. Under the already recovered final constructor/vptr path, its virtual`+0x8c` target is`0x8020dfe8`. This preceding receiver is distinct from the subsequently obtained capture-memory manager. Independently, the manager's own table has`+0x5c →0x800adb1c` for ordinary release and`+0x68 →0x800adfa4`; these are separate methods.

Parent review corrected the table boundary: address point`0x8063cc08` follows words`{0,0x8063cc54}` and places`0x80102758` at slot`+0x44`. Original code at`0x80102364/0x80102368` loads literal`0x801023a0=0x8063cc08` and stores it through its first argument. The associated RTTI name is`CCaptureStateSleep`; the exact vptr assignment is stronger than proximity alone. The assignment does not by itself distinguish constructor from destructor behavior or identify an active transition. The dispatcher, transition, runtime invocation and ordering against candidate FileDump remain unresolved. The cleanup sweep can free tagged runs in its configured interval, but the candidate allocation's page index and overlap remain unknown. This is evidence of a possible reclamation route, not a proof that export is safe or that cleanup races it.

## Evidence

[Evidence index](../core-input-cleanup-evidence.json) preserves full native records, source windows and closed snapshots. RTOS source SHA256`cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, link base`0x80000000`; fulltext carrier SHA256`3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`. Ghidra12.1.3; provider-profile digest`2efb2839e0e54d98e487132a182b70bf8f5b5989724d969b01494dc44c0182a2`.

- Input setter `ev_228b7a443d2529d2f2ee44c9e67dd33f41ab904da40ecb772506a0d7f5afec36`; bytes `ev_64beb44267740ecdb5cfb9714dbdac03be8a5e1c2d79db3f54a4df5b19159e1a`.
- Forwarding caller `ev_b582d4c92b456cc3905dc72c7d32aea806de3e7e250d90474d502a9242876727`; bytes `ev_58f099ef7b849e8ebe88b5d47196be221c06b8ec29d4b4cca9bd6aca5e865e23`.
- Clearing caller `ev_8e783094975303f2405bb3380c55ad1f34fcb47809fcf45446f16417617bbb54`; bytes `ev_806cd57f5c74f160eedcd25d5183db439ef95d4f10b87306a1b86c3398c66dfa`.
- Getter `ev_9fdd0deb40e85ebf8eab71cba51506a0b8da357eb082852c89aab3932860ef14`; constructor `ev_e40f8e1711c6993c026d42b377e664ebec80191516697134b49810945ae7886e`.
- Direct cleanup caller `ev_f505b5540c6545c5a03c7ba717cce9cdfe97cc7a79df7001d7198a4d0f4bc7c6`; preceding Frontend getter `ev_c3da197b129bcd980ddb319f7b847f8bb290763bb49154c0e6401aa255d4da65`.

Source-bound working records are under`output/decompile/rea/pass0345-20261016/core-input-writers/` and`bulk-release-invocation/`. Earlier pool-release body evidence remains valid in[execution/lifetime findings](RAW_EXECUTION_PROVENANCE_AND_LIFETIME.md). Fresh conclusions concern specific software paths, not a buffer lease, hardware sample format, sensor provenance or current-frame identity.
