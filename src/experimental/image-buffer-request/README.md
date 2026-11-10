# Image-buffer request component

This pure C component encodes one image-buffer acquire request, encodes one
release request and decodes one reply body. It performs no device I/O, no
allocation, no transport, no retry, no release on its own and no target-memory I/O.
The caller owns every buffer.

It is built on the [mcb-frame](../mcb-frame/README.md) component: the request
and release frames use `mcb_frame_encode_linux`, and the reply is validated
with `mcb_frame_copy`.

## Acquire request

The request body is 20 bytes, five little-endian u32 words:

| word | offset | value |
| --- | --- | --- |
| 0 | 0x00 | 0 |
| 1 | 0x04 | 0 |
| 2 | 0x08 | requested bytes |
| 3 | 0x0c | bank selector |
| 4 | 0x10 | 0 |

The complete outgoing frame is 28 bytes: category `0x80`, attribute `0x018c`,
the 20-byte body and the two zero trailer bytes of the mcb-frame TX envelope.
The retained vector for 1 MiB in bank 2 is

```text
80 00 8c 01 14 00 00 00 00 00 00 00 00 00 00 00
10 00 02 00 00 00 00 00 00 00 00 00
```

The encoder refuses a zero requested size, an unsupported bank and a size
whose native 16 KiB rounding would wrap a u32. The largest accepted size is
`0xffffc000` bytes, because `requested_bytes + 0x3fff` has to fit in 32 bits.
No image geometry, lease or pool limit is imposed, so the component never
invents a maximum frame size for a still.

The three known bank selectors are `IMAGE_BUFFER_REQUEST_BANK_DEFAULT` (0),
`IMAGE_BUFFER_REQUEST_BANK_FIRST` (1) and `IMAGE_BUFFER_REQUEST_BANK_SECOND`
(2). The native producer maps bank 1 and bank 2 to one manager slot and every
other value to a different slot whose interval semantics are not established,
so other values are refused here.

## Release request

The release body is 4 bytes, one little-endian u32 physical address, sent as
category `0x81` with attribute `0x018d`. The complete frame is 12 bytes; the
retained vector for address `0xab134000` is

```text
81 00 8d 01 04 00 00 40 13 ab 00 00
```

A zero address and the no-allocation sentinel `0xbfffffff` are refused. No
pool bound is invented, so any other nonzero address is encoded.

## Reply decoding

`image_buffer_request_decode_reply` takes one complete received mcb-frame,
that is the bytes the kernel leaves after it consumes its four-byte outer
header: the `00 03` route prefix, the category, attribute and body length,
the body and the two trailer bytes. The frame must be category `0x81`,
attribute `0x018c` and carry exactly 20 body bytes.

The decoded reply exposes the five raw wire words:

| field | offset | meaning |
| --- | --- | --- |
| `wire_word0` | 0x00 | raw word 0 |
| `address_word` | 0x04 | the allocation result written by the RTOS producer |
| `requested_bytes` | 0x08 | echo of the request |
| `bank` | 0x0c | echo of the request |
| `manager_query_word` | 0x10 | the manager free-space query |

The field roles above are source-bound. The pinned Linux client reads the allocation
result from word 1 and converts it into its public word 0, so `wire_word0` is
not the public return value. Under the conditional manager binding, word 4 is
the largest contiguous free run in bytes counted in 16 KiB units; it is not a
pool size, an image stride or a geometry.

A reply whose address word is zero is a syntactically valid refusal, not an
allocation, and `image_buffer_request_reply_is_refusal` reports it. The
component never establishes that a reply answers any request: the echoed size
and bank are reported as raw words, and even a well formed reply with matching
echoes is not treated as an association. The trailer bytes stay uninterpreted,
exactly as the mcb-frame component documents for received frames.

## Limits

- This component is wire construction and syntax checking only. It does not
  establish an allocator, a lease, an owned allocation, backing retention,
  sensor binding, capture completion, RAW ownership or transport behavior.
- The manager behind the request, the release slot and the free-space query is
  a conditional implementation binding. A live instance may differ.
- The zero address refusal means "no allocation result". It does not mean the
  request failed, and it does not license a retry.
- Nothing here retries, releases, or touches memory on its own.

## Build

```sh
sh src/experimental/image-buffer-request/build.sh
```

`SANITIZE=1` adds AddressSanitizer and UndefinedBehaviorSanitizer, and
`CHECK_ARM=1` compiles the component for ARMv7:

```sh
SANITIZE=1 sh src/experimental/image-buffer-request/build.sh
CHECK_ARM=1 sh src/experimental/image-buffer-request/build.sh
```

The build output goes to `output/image-buffer-request-build` by default. A
different directory can be passed as the first argument.
