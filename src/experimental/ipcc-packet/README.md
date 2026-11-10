# IPCC packet ABI helpers

Pure C11 helpers, callable from C++, for three recovered Linux IPCC packet
ioctls. They encode request bytes and validate returned ABI observations;
they perform no device I/O, allocation, capture or thread management.

| Command | Little-endian 32-bit words |
|---|---|
| `0xc0087404` | selector, cursor-inequality output |
| `0xc0087405` | selector, peeked u16-length output |
| `0xc0107406` | selector, requested length, reported count, destination address |

The 23 October review separately confirmed the vendor **write** ABI:
`0xc0107407` takes `[destination CPU, length, reported, source address]`.
`CIPCCDriverIf::Send` uses destination zero. The earlier MiniMax buffer-first
layout was incorrect; its channel-probe source still uses that layout in
snapshot `cacf50c` and has not been integrated. This helper currently implements
the three read-side operations above, not write admission or transmission.
See the review (not published: `../../../output/decompile/rea/pass-minimax-review-20261023/parent-review.md`).

Logical selectors are sparse: `0,3,0x10,0x11,0x12` map to internal
indices `0,2,4,5,6`; other values are refused. This mapping does not establish
that any channel is currently initialized or owned by the caller.

The request encoders leave output untouched on refusal. Frame reads require
capacity for `max(4, requested_length)`: the kernel stages a four-byte header
in the destination before copying the inner frame over it. The full write
span must fit the 32-bit address representation and must not overlap the
request block. These are local checks, not proof that an address is accessible.
The requested length is the inner image size, including route and trailers;
it is not the MCB body-length field.

Payload limits are explicit caller policy. `ipcc_pkt_limits_mcb_incoming`
selects inner lengths10..390 and pins logical selector0; requests must explicitly choose that selector for
that ordinary MCB route. The reviewed default ring has395 bytes, but this
per-packet limit does not prevent aggregate exact-free publication or prove
available bytes, coherence, or sole ownership.

Result decoding refuses unmapped expected selectors with `IPCC_PKT_ERR_ARG`,
clears all observation fields, and preserves the supplied raw return. This
argument check precedes syscall/echo/output checks.
Result decoding supports misaligned input and input/result overlap. Failures
clear observation fields in a valid result object; only status and
`syscall_result` then have meaning. Success means checked return/echo/range
observations and an output word different from the initial sentinel. An
unchanged sentinel is rejected. Neither a changed sentinel nor a reported
count proves a complete copy: the kernel can return the requested length
without copying when its cursors are equal. No result asserts transport
readiness, fresh completion or ownership of pixels.

Build and run host tests and C++ linkage:

```sh
./build.sh
```

`SANITIZE=1` additionally runs ASan/UBSan on the host tests; `CHECK_ARM=1`
adds an ARMv7 Cortex-A9 freestanding compile check. Set `CC`, `CXX`, `AR`
and `ARM_CLANG` to use existing tools. Builds default to
`output/ipcc-packet-build`; an optional first argument selects another output
directory. Target compilation is not camera execution or runtime validation.

See the parent review (not published: `../../../output/decompile/rea/pass-minimax-transport-20261021/parent-review.md`)
for native evidence, rejected drafts and validation. The [session contract](../../../reverse-engineering/facts/contracts/session.yaml)
keeps complete-copy visibility, strict admission and external sole ownership open.
