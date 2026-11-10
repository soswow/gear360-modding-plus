# Encoded-set metadata decoder

This pure C component decodes one encoded-set metadata constituent into a
caller-owned structure. It performs no allocation, no I/O, no target address
access, no callback and no ring fetch. It does not complete a capture or a
backend.

The layout is the one accepted by the parent fact
`raw.encoded-set-descriptor-writer`, summarized in `../facts/contracts/raw-buffer.yaml`.
That fact is a **static** layout: the serializer, the block order and the
field offsets are established. Runtime provenance, format certainty and
lifetime remain open questions.

## Input

Exactly one constituent metadata block of `0x228` bytes:

```text
+0x000  u32   set_type
+0x004  4     unknown reserved bytes, not decoded
+0x008  image block 0, 0x88 bytes
+0x090  image block 1, 0x88 bytes
+0x118  image block 2, 0x88 bytes
+0x1a0  image block 3, 0x88 bytes
```

The four blocks are the optional first image, the main image, the screen image
and the thumbnail, in that order. Inside a block, relative to the block base:

```text
+0x00  u32   stride_x (native diagnostic name; unit unproved)
+0x04  u32   stride_y (native diagnostic name; unit unproved)
+0x08  u32   width
+0x0c  u32   height
+0x40  u8    address_type
+0x44  u32   first physical word
+0x54  u32   first virtual word
+0x74  u32   buffer size word
```

The physical word, virtual word and size word come from the block's buffer
fields at `+4`, `+0x14` and `+0x28`. The four geometry words follow the image-block layout; the library prints
these positions as width/height and two strides for main, screen and thumbnail
(`raw.encoded-set-geometry-diagnostic`). Applying the same layout to the
conditional first image does not establish sensor geometry. No stride word is
converted to a byte count, and no native `CCapImage` offset is typed.

## Output

```c
typedef struct encoded_set_image_descriptor {
    uint8_t  address_type;
    uint32_t first_physical_word;
    uint32_t first_virtual_word;
    uint32_t buffer_size_word;
    uint32_t stride_x;
    uint32_t stride_y;
    uint32_t width;
    uint32_t height;
} encoded_set_image_descriptor;

typedef struct encoded_set_metadata {
    uint32_t set_type;
    encoded_set_image_descriptor images[4];
} encoded_set_metadata;
```

Geometry fields are appended to the output descriptor. Its size has changed;
rebuild all callers and do not exchange this structure across old/new objects.

`encoded_set_decode` returns `ENCODED_SET_OK`, `ENCODED_SET_INVALID_ARGUMENT`
for a null pointer, or `ENCODED_SET_INVALID_LENGTH` for any length other than
`0x228`. On failure the output object is left unchanged.

Any other length is refused, including the full `0xd70` preference record and
a 4-byte context pointer: those are different objects, not constituents, and
the full record carries other metadata that this component does not decode.

## Contract details

- **Unaligned input is allowed.** The fields are read byte by byte as
  little-endian words. Nothing is read through a wider or aligned type, so no
  alignment or aliasing assumption is made.
- **Input and output may overlap in any direction.** Every input byte is read
  into a local structure first and the output is written only at the end, so
  an output object placed inside the input buffer still decodes correctly.
- **Unknown values are preserved.** `set_type` and `address_type` are kept as
  the raw values found on the wire. No range validation, mapping, geometry validation,
  reinterpretation or "is this RAW" flag is applied. For context only: the
  native runtime derives a set type from a sheet byte (4, 5 or 6 to 3; 7 or 8
  to 2; anything else to 1), and the Linux side attempts the first address
  conversion only when the set type is not 1. This component implements
  neither; it only reports the raw word.
- **The first image is conditional.** That slot carries the native
  "RawCompress" label. The label is a native name; it is not established that
  the slot holds sensor RAW, and the decoder makes no such claim.
- **The words prove nothing about memory.** The physical and virtual words are
  opaque 32-bit values in the target's own domain. The decoder does not map,
  dereference or own them, and the size word is not a bound, a geometry, a
  stride or a retention proof. A decoded descriptor is metadata, not an
  allocation.

## Limits

This component is an evidence-backed metadata wire component only. It does not
establish the allocator, a buffer lease, backing ownership, buffer bounds,
sensor binding, capture completion, RAW provenance or retention. The reserved
bytes and every other field of the image blocks stay undecoded because their
meaning is not established.

## Build

```sh
sh src/lib/encoded-set/build.sh
```

`SANITIZE=1` adds AddressSanitizer and UndefinedBehaviorSanitizer, and
`CHECK_ARM=1` compiles the component for ARMv7:

```sh
SANITIZE=1 sh src/lib/encoded-set/build.sh
CHECK_ARM=1 sh src/lib/encoded-set/build.sh
```

The build output goes to `output/encoded-set-build` by default. A different
directory can be passed as the first argument.
