> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Recipe selection and command gates

Reviewed16 October2026. The following are static software paths in the captured firmware, not observed capture execution. They narrow how the candidate could be selected; they do not establish sensor pixels, completion, format packing or a backing lease.

## Recipe word reaches the factory selector

The caller`0x8027510c` supplies recipe word3 if`0x8054e27c()==0x15`, otherwise1, and map selector0. The order path installs the recipe word in CCook`+0x70`. CCook's preparation method loads that word into r1; BranchManager`0x8027b888` uses a copy's top byte to select a book, then reloads the unchanged original word into r1 for the selected book's virtual`+8`. Still Book`0x80280570` forwards it to selector`0x80281460` as argument2. For words1/3 the book index is0.

The selector's argument3 is a capture-parameter object P, not a material. Still Book uses local`local_14`: when its incoming argument4 is null it allocates0x260 bytes, calls`0x80132258`, and attaches that pointer to the recipe through`0x8057226c`; otherwise it casts the supplied argument4. Recipe object, material, parameter object, recipe word, map selector and factory key must remain distinct.

Normal selector behavior:

| Recipe word | Factory-key selection |
|---:|---|
|1|Nested predicates can yield key27 (`0x1b`), which is a compiled caller of the candidate MID0x15000 builder.|
|3|Selects key18 (`0x12`) directly; it does not take the normal key27 branch.|
|4/5/6/9|Known compiled request values remain separate from map keys; selector logic maps them.|

A nonzero global`0x81952130` overrides the computed key with its low16 bits and is then cleared, except for input word`0xff`. Thus normal decision predicates alone do not determine the final key in the presence of a pending override.

For key27 on normal recipe1, P's byte`+0x30` must lie in`{1,0x0d,0x13,0x14,0x1a,0x1b,0x22}`; compound helper`0x8013372c(P)` must be zero; signed byte`+0x49` must avoid1/2/3/4; byte`+0x68` must be0 or greater than0x12; word`+0x64` must differ from1; byte`+0x254` must be0; and byte`+0x170` must be nonzero. These conditions assume stable fields across repeated getter calls. The compound helper starts true when byte`+0x52` is nonzero and signed word`+0x6c >4,900,000`, then suppresses that result for its documented byte`+0x30` exclusion set. Exact predicates, source ranges and full IDs are in the archived worker report.

Parent original-byte review corrected three report errors before integration: getter offsets are hexadecimal`+0x49/+0x170`, not`+0x31/+0xaa`; selector argument3 is P, not a material; the recipe word is forwarded unchanged into selector argument2. The compound comparison is strictly greater than4,900,000, equivalent to at least4,900,001.

## Source of parameter byte+0x170

`0x80137574` writes`P+0x170 = ((G &3)!=0)`, where getter`0x80546c54` reads global`G=0x81482ea4`. Setup routine`0x80132bfc` calls this writer at`0x80132d70`; constructor path`0x80132258` calls that setup routine. Copy routine`0x80132414` also copies this byte from another parameter object. This is a source for one selection guard, not proof that all key27 guards pass.

Global setter`0x800ff824` computes a boolean from its input's low two bits, passes that boolean with tag`0xb9` to a manager helper, then stores the complete original input into G. Tag`0xb9` is not established as an MCB command ID.

A specific caller`0x801153cc` interprets a payload word:

| Payload value | Stored G value | Low-two-bit selection guard |
|---:|---:|---|
|1|0|false|
|2|1|true|
|3|2|true|
|4|4|false|
|5|0|false|

Null payload returns−4. The code's diagnostic at`0x80640434` says `Set Adjust Ycc Capture = %d`. Original bytes for the diagnostic and jump table are separately preserved; the partial analysis carrier does not map that string address. The other known setter call at`0x80060cfc` clears the low two bits first. These observations do not justify renaming G “RAW quality,” or interpreting the option values as a physical sensor format. The higher-level dispatch that supplies these payloads remains to be established.

G is separate from dump-enable flag`0x819521c4`, and from one-shot factory override`0x81952130`. Selecting the candidate and enabling its optional DumpSize child are different operations.

## C API handoff for the recovered dump route

The [shell route](RAW_SHELL_DUMP_ENABLE_AND_CALLBACK_LINEAGE.md) constructs24 payload bytes: `st cap seq dump on ssif `, including its trailing space and excluding NUL. Fresh REA plus original ELF relocation/disassembly checks establish:

`mcb_send(ELF0x5008) →Mcb_send(ELF0x6704) →CMulticoreBridge::Send(0x6700) →send(0x66c8) →CSender::Send`.

The uppercase C wrapper applies unsigned16-bit narrowing to ID, sub-ID and length. Values`0xb6/0/24` survive unchanged, and the buffer pointer is forwarded. This closes a concrete userspace API edge; runtime transport, RTOS receipt and successful flag-before-recipe ordering remain unobserved. The older command0x81 C++ transport trace is a separate entry path, not by itself proof of this C wrapper.

## Evidence

[Source-bound evidence index](../recipe-command-gates-evidence.json) preserves native records, snapshots, source windows, retained flow dependencies and corrections. RTOS original SHA256`cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`; fulltext carrier SHA256`3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`. Linux bridge ELF SHA256`9129761c6ee9f6b62968d3a8307a8b5fbf02e1493323658c42d9ebe587673a20`, REA address bias+0x10000. Ghidra12.1.3/profile`2efb2839e0e54d98e487132a182b70bf8f5b5989724d969b01494dc44c0182a2`.

- Selector `ev_4cbdab0b0684d43e209ea13be9bcb61ffe96d6032ac027889eb770d5a7ccf7a6`; compound guard `ev_1444c712a858c651adb502f173234fbfbb8c4bb8dc11357b4fb6f4a550079de1`.
- Parameter writer `ev_68653d4f3f3ddf4537daf34edfaf2fbfdd03623c36bff810ad299f86092a1259`; G setter `ev_5c4a73f877b0c3ae6946f4408d0ca51cb63167698e9c2d4059a867f042f117fe`; option caller `ev_b89a7e750c1c77112dcf6989a54913a0164832385d6468ba6342c69a8e568203`.
- Copy routine `ev_62df15576195a7c95c932f8b9320e574cb11314037d42b871de90ce1b6e6eac4`; byte getter `ev_c618298d6f0065f1ae85c309718c4916ac6fe6a1d652b65f2dad431cab1b41c3`.
- Linux C wrapper `ev_be92ef86a472e1ed68bfb5beb7a34eb5bff1b5ec1f9fe476e3e3fb0d5aa306f0`.

Working artifacts are under`output/decompile/rea/pass0345-20261016/{recipe-key-predicates,recipe-raw-flag-source,mcb-c-api-bridge}`. Key20/21 factory bodies were also compared: they construct different conditional graphs and are not aliases. No live activation or verified RAW output is claimed.
