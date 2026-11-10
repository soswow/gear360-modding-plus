# MCB frame helpers

This component provides syntax-and-copy support for one received RTOS-to-Linux
control frame, a bounded encoder for one outgoing Linux MCB frame, and a pure
admission planner for the circular byte transport that carries the encoded
frame. All three use caller-owned storage. They perform no device I/O,
allocation, IPC, retry, or readiness checks.

`mcb_frame_copy` handles an incoming frame after the kernel has consumed its
four-byte outer header. That inner representation starts with route bytes
`00 03`, followed by little-endian category, attribute, and payload-length
fields, the payload, and two uninterpreted trailer bytes. Its conservative
input bounds are 10 through 390 bytes with a payload from 0 through 380 bytes.
The trailer is retained verbatim. Category and attribute values are returned
without assigning meanings to unknown values.

`mcb_frame_encode_linux` produces the outgoing buffer passed to
`CIPCCDriverIf::Send`: little-endian category, attribute, and body-length
fields; body bytes; and two zero trailer bytes. The outgoing buffer does not
include the `00 03` receive route prefix. The Linux bridge constructor allocates
`body_length + 12` bytes, its allocator zeroes that allocation and reports a
size of `body_length + 8`, and `Build` copies the body at byte 6. Thus the
outgoing buffer is `body_length + 8` bytes, with the two trailing zero bytes
left by the allocator. Unknown category and attribute values are accepted.

Both helpers cap the body at 380 bytes. The transmit helper therefore emits at
most 388 bytes. For the reviewed default 395-byte ring, adding the six-byte
outer overhead gives 394 bytes, leaving one byte after the packet. This is a
deliberately conservative single-packet bound; it does not fix aggregate
exact-free behavior, ring coherence, ownership, or admission while the ring is
partly occupied. The receive bound likewise does not establish a safe receive
transport. Evidence and the scope of the native review are recorded in the
encoder review (not published: `../../../output/decompile/rea/pass-peer-alive-20261021/parent-review.md`).
The incoming layout and its exact-free limit are in the earlier
receive review (not published: `../../../output/decompile/rea/pass-owned-receive-20261021/parent-review.md`).

The encoder accepts NULL body only for a zero-length body. It validates the
body limit and destination capacity before writing. It uses `memmove` for any
body/destination overlap, including in-place use, and writes metadata only
after the body move. The caller must ensure the output-size object does not
overlap the body or destination. Failures leave both destination bytes and the
output size unchanged. The copy helper has corresponding failure atomicity;
its returned body points into caller storage at byte 8, and its metadata must
not overlap the input or destination byte ranges.

## Strict ring admission planner

`mcb_ring_plan_admission` plans the admission of one already encoded outgoing
frame into the circular byte transport. It is pure arithmetic: it copies no
bytes, reserves nothing, takes no lock and touches no device.

The caller supplies the ring capacity `N` in bytes, the producer index `W` and
the consumer index `R`, both in `[0, N)`, and the encoded frame size. The
planner evaluates

```text
used = (W + N - R) mod N
free = N - used
```

without the overflowing `W + N` term, and returns a plan with the total bytes
required, the starting index, the length of the first and second ring range,
and the new producer index.

The outer overhead is exactly 6 bytes: a 4-byte length header plus 2 bytes of
transport overhead. The encoded frame is 8 through 388 bytes and no even
length is required.

Full and empty share equal cursors, so `W == R` cannot express a full ring.
The planner therefore admits a packet only when the packet is strictly smaller
than the free area, `total_bytes < free`, and refuses equality. The result is
that one byte of slack always remains and `W == R` keeps meaning "empty" for
any caller that admits only through this planner.

The planner rejects a null output object, a zero capacity, a producer or
consumer index outside `[0, N)`, and a frame size outside 8..388. On failure it
returns a distinct status and leaves the plan object unchanged.

Caller obligations:

- The caller must serialize the plan, the byte copy and the publication of the
  new producer index. The planner performs no synchronization and no atomic
  reservation, so a concurrent producer or consumer can invalidate the plan
  between the call and the copy.
- The caller must treat `W == R` as "empty" and must not change either cursor
  between the call and the copy.
- The caller owns the packet bytes and the transport framing. The planner
  defines neither header, trailer nor overhead contents.

The planner is not a transport. It does not establish ring coherence, byte
ownership, atomic reservation, completion, RAW acceptance, or that the
reviewed transport uses this cursor convention.

These helpers do not establish a working capture path, freshness, completion,
RAW ownership, thread synchronization, camera initialization, or firmware
acceptance. A successful encode only produces bytes in the reviewed format.

The host checks compose the real 48-byte still-request encoder with the
category `0x83`, attribute `0x0201` envelope and compare all 56 outgoing bytes
to a fixed vector using synthetic storage values. This runs no vendor submit
function and sends no packet.

Build and run host tests with:

```sh
./build.sh
```

The optional ARM compile check follows the still-request component's command:

```sh
CHECK_ARM=1 ./build.sh
```

`SANITIZE=1 ./build.sh` adds AddressSanitizer and UndefinedBehaviorSanitizer
to host compilation and tests when supported by the selected compiler.
