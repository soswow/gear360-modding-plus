> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Candidate allocation and export contract

This continues the [recipe/identity findings](RAW_COMPLETION_IDENTITY_AND_RECIPE_DISPATCH.md) and reconciles older evidence with the multi-provider continuation. **No verified sensor RAW pixels have been captured.** All results below are conditional static paths from the captured RTOS; they do not establish active R210 execution, physical sensor origin, fresh DMA or a buffer lease.

Original input: `output/decompile/rea/rtos-p6.bin`, SHA-256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, link base0x80000000. Retained source records are indexed by the [continuation evidence index](../completion-identity-recipe-dispatch-evidence.json); new parent byte checks and worker reports are under `output/decompile/rea/multi-provider-run/`.

## Keep the fields distinct

| Field/domain | Static candidate value | Meaning established by code |
| --- | --- | --- |
| Factory selector | 0x28 | Selects the image factory/configurator path |
| Material ID | 0x15000 | Selects a material family through Fridge; not a recipe ID |
| Image+0x80 byte | 2 after configuration | Embedded buffer layout selector; constructor default0 is overwritten |
| Image+0xa4 word | 22 (0x16) | Allocation request ID; helper maps it to allocator tag28 |
| Allocator tag | 28 (0x1c) | Branch requests 64 MiB; not an image pointer |
| Provider/settings | role13 → row22 | Supplies initialized geometry; distinct from allocation request22 |
| Image+0x5a byte | 1 | Separate packing metadata used by the dump length branch |
| Image+0xb0 word | 1 | Kind/queue branch set by the configurator |

Configurator0x80141c78 calls0x805615d0(wrapper,2), then0x80560ccc(wrapper,22). Parent checked the instructions at0x80141dcc/0x80141dd0 and0x80141de4/0x80141de8 and the byte store at0x805615f0 against original bytes. The same selector image is passed by MID builder0x802ba7b4 to material image registration and callback constructor0x80574ba4. Earlier static identity is preserved; a census of144 callback-constructor callers does not reopen this particular link.

Evidence: configurator `ev_bcbd5659eb24fa8187466f28d30896a30d628447ca1ad4a563481932e87f7f78`; type setter `ev_c3af02a8fd88e2152509cc3926d1024eccbe6c042fc5f974ca76fa1e5889489c`; request setter `ev_c4cb78375b7e3487178805c7c490ea604d1fff21e64eeaf87c369f627a16c2a9`; kind setter instructions `ev_5fd64d8ec28e2441284a3a953eef32d2a779c01d9675a5b949e0a53cc4ddce70`; producer `ev_0fa01a39e8cfbd589a38d032836fbb99184f4a1130f77e1589f5704a21aca82f` and original instructions `ev_f332ac30a581abcc2f6ffcb104c14b2b9ed763d4114354a64ccd6109271343c0`.

## One selected allocation arm, not successive allocations

In callback0x80138d54, call instructions0x80139248 and0x8013925c both target0x8025d9c0, but are mutually exclusive in one traversal. The normalization of the image request comparison at0x80139144–0x80139158 takes the latter arm when the value differs from33. A request still equal to22 therefore selects0x8013925c on this path. The earlier arm is for request33 after additional helper-result checks. Other paths can reuse a pointer or exit; these statements do not prove the callback was invoked or allocation succeeded.

The helper maps request22 to tag28/flags0 on CCaptureMemoryManager virtual+0x20, whose resolved target is0x800ac834. That branch requests0x04000000 bytes. The callback rejects sentinel0xbfffffff before installing a selected result. The unrelated r0=3 stack-image invocation returns that sentinel and is not this candidate's request22 invocation.

Parent LLVM audit matched160 instruction words to original p6 bytes. MiniMax supplied the useful alternative-arm observation; its initial predicate/opcode interpretations and the inverted condition in its correction were rejected. The accepted conditions were independently reviewed against the actual CMP/MOVcc/BEQ instructions. Those decisions are saved alongside the raw worker responses.

## Type2 descriptor: four plane addresses

For nonnull successful backing pointer P, calculator0x8025cd4c uses the nonzero stored stride S and height H:

```text
q = (S * H) >> 2
planes = [P, P+q, P+2q, P+3q]
size = q << 2
```

With initialized S=11648 and H=2896, q=0x80ae00 and size=0x202b800 (33,732,608 bytes). The four words are `[P, P+0x80ae00, P+0x1015c00, P+0x1820a00]`. Setter0x8025cc24 copies them into image+0x84..+0x93 and writes size at image+0xa8. These writes preserve layout byte+0x80 and request word+0xa4. A separate conditional setter can populate the secondary descriptor at image+0x94. This arithmetic is not a Bayer/sample-packing identification.

Evidence: calculator `ev_de221dc24730480ec96e0faa45dc44a501a84d342510daa021647491ad0021b8`; descriptor setter `ev_3b509edd00a2f17600226d9eb3abcddb53cec3018b71dde7f1e881ebc9f3fd93`; descriptor copy `ev_2723f54bd96f746667a1f21344a7a508135d327324699a90a03670fca57ec61b`; size setter `ev_03ee3e9b25eda928fe03ee19756a39485b9ee8ebf66f3c4e6ecfe13886cabf4f`; callback `ev_5ccbeb4a0d1a5a1568e395e6009a60c354e2fdb78225a70dbd477b4ab8834c11`; allocator `ev_3a7ba22f41750fc725010fdab3e89bbe3fa6247e86876b9c5c46ea0ecd1afb27`.

## Optional export reads P and requests stride × height

The optional DumpSize callback holds the same candidate image. Layout2 PData accessor0x800b4e00 returns the descriptor's first word at wrapper+4, joining the installed pointer P to FileDump's data argument. DumpSize0x803333fc calls0x80051618(path,PData,length,0).

For the configured kind1 and pack-type byte+0x5a=1, both branches of image+0x5b yield `length = GetStride(image) * GetHeight(image)`. The alternative use of the embedded descriptor size requires a different pack-type value2; that pack-type field is not wrapper layout2. Thus, at the initialized geometry, the request length is33,732,608 bytes, equal numerically to the calculated descriptor extent but derived independently. It is50.27% of the64MiB allocator capacity, leaving33,376,256 bytes outside the requested dump extent.

Evidence: same-image optional callback construction `ev_3a1bfff0657365c278ce63dbe46d43f9e15fb774587b70a89210a8f3d2c52f85`; DumpSize body `ev_041e97f97459b2653208c43f68082ee82537100b5dd73ee2618587bb2909fa1c`; PData accessor `ev_c1451fa6cff3550948c0dbfe3178919f014041b92362af216e71e802bfb7e17a`. The optional callback still requires the recipe-time global flag0x819521c4&0x10 and an eligible lifecycle transition.

## Remaining acquisition contract

We have a conditional static image → allocation → descriptor → dump-argument chain. We do not have observed graph activation, a live pointer/address-domain validation, sensor/SSIF producer identity, current-frame association, protection against independent reuse, actual file-write success or verified sample bytes. Constructor/configurator defaults and software state transitions do not close those gaps. Later image copies/alias writes must be source-bound to this instance before changing the field assumptions above.
