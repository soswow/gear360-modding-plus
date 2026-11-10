> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Frontend input descriptor: identity and local initialization

Reviewed 16 October 2026. This extends the [earlier enclosing-object trace](RAW_INPUT_DESCRIPTOR_PRODUCER_AND_RECIPE_BYTE.md) using complete native records and original bytes. No firmware or camera was executed. No verified sensor RAW file exists.

## The enclosing object is the constructed FrontendNx

Constructor **0x8020d260** installs primary vptr **0x806718a0**. Original table cell **0x806718f0**, slot **+0x50**, contains **0x80210a88** (`CapSetPPCore`). The same table's slots+0x80 and+0x94 match the previously recovered handler 0x802128e8 and task loop 0x8020e240. This identifies the method on that construction path; a runtime instance or later vptr replacement is not observed.

The method passes its receiver **C** unchanged to **0x803217d8**, which passes **C+0x50** to **0x803954e4**. That descriptor's fields **+0x14/+0x18** consequently correspond to **C+0x64/+0x68**. Under the already recovered configuration guard, their values are forwarded into Core input0/input1 through 0x8031b8f8. Input slots remain distinct from the candidate's output descriptor and backing.

## A range clear covers the two input words

Base constructor **0x80320e90** calls **0x80393ec0(C+0x50)**. Its pseudocode parameter is a `u32*`: `param_1+0x14` means byte offset **0x50**, not byte offset 0x14.

Descriptor initializer 0x80393ec0 installs vptr **0x806c0970**, initializes an event wrapper at descriptor+0x194 and calls creation with mask3, sets another field through 0x8057b780, then calls:

`0x80585914(descriptor+4, 0, 0x18c)`

The complete 248-byte fill helper **0x80585914** handles an unaligned byte prefix, aligned word/16-byte fills and a byte tail. Its literal-zero call covers **[C+0x54, C+0x1e0)**. Both input words C+0x64/C+0x68 lie wholly within that range.

The following and final initializer call **0x80580aa4(descriptor, 0)** writes only byte **descriptor+0x191 = C+0x1e1**. It does not overwrite either input word. Thus the input words are zero when this descriptor initializer returns along the traced path. This is a local initialization result; later base/derived setup calls, runtime assignments and values at actual submission remain separate questions. Zero initialization does not prove that a direct hardware source is absent.

## A nearby selector configures a window

CapSetPPCore's diagnostic names its third argument `fInputOTF`. One branch later calls **0x80212b9c(C, 0x10)**, while other branches pass 0x11 or 0x12. A native dossier establishes that this helper is diagnosed as **updateWaveDetectionWindow**. It obtains seven outputs from **0x80213240** and forwards them through **0x800b91ec**. These constants are selector arguments for that window update; this evidence does not establish that this helper selects the sensor/PP hardware input mode. Do not follow its name or nearby branch as if it had already proved OTF routing.

## Full native evidence

| Function | Evidence ID |
|---|---|
| FrontendNx constructor 0x8020d260 | `ev_af039c51feba28e33761e3101a27f9cb07f1a31e4472967e068c51ebfb55ff78` |
| Base constructor 0x80320e90 | `ev_11d713cc037f0f095a02b5454ff503eb3b5a8e81c51c4e1246b7f0d836137cdb` |
| Descriptor initializer 0x80393ec0 | `ev_4578bd60f8cd3818cceb3392add1cfa79573a44cda4aa7f8da9bd65ebefdfd61` |
| Fill helper 0x80585914 | `ev_d1f917b80f7d25ebf0f171400e8efe3aeeae93b794311012273865bc76633910` |
| Final byte setter 0x80580aa4 | `ev_b6af7230a07995ec3ff37ae29f6a161df6a73381b9218afa7e99b89ac25137f5` |
| Window helper 0x80212b9c | `ev_d3a3ab40a304ea85578f9e6375acba0eccd367240a111975b71f992af9ebade1` |

Provider body ranges and gaps are preserved in the full records; source/carrier/procedure-range checks do not independently rediscover function boundaries or certify runtime effects. The [evidence index](../frontend-input-initialization-evidence.json) retains the records, snapshots, original worker outputs, parent audit and corrections. RTOS SHA256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`; carrier SHA256 `3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`.

## Rejected shortcuts

The worker's original “no writer”/“never touches” claims came from a text search and a broken instruction-offset census. Its three alleged +0x64 instructions actually encode +0x410/+0x44c/+0x4bc. Those counts are withdrawn entirely. Several register/mnemonic readings were also wrong and corrected against LLVM/Ghidra. The correct bulk-clear behavior is established by complete native helper bodies, not those handwritten decodings.

A bounded runtime-writer followup produced native record `ev_8610620154e4365a78760705843ce6ae4f6bc33250079c8af378789992cdaf6d` for **0x80393f5c**, vtable slot0. The worker called it a second constructor and suggested slot4 (**0x80393fdc**) as the next writer query. Parent rejects that classification: original slot4 instructions call 0x80393f5c, then operator delete 0x8059d0fc. This is the paired destructor/deleting-destructor pattern. These methods do not identify a runtime input producer; do not reopen slot4 as a promising setter merely from this historical recommendation. The selected byte window also includes a separately marked next-function prefix beyond the return.

The next useful edge is a runtime setter or copy that supplies these descriptor fields before the guarded Core forwarding call. A null initialized memory input is not an SSIF receive destination, and it cannot certify or exclude sensor-derived output.
