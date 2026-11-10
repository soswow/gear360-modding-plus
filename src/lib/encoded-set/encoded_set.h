#ifndef ENCODED_SET_H
#define ENCODED_SET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decoder for one encoded-set metadata constituent.
 *
 * The layout is the one accepted in the parent fact
 * raw.encoded-set-descriptor-writer, as summarized in
 * ../facts/contracts/raw-buffer.yaml.  It is a static layout: the serializer, the block
 * order and the field offsets are established, the runtime provenance, the
 * lifetime and the ownership of the described buffers are not.
 *
 * Input is exactly one constituent metadata block of 0x228 bytes:
 *
 *   +0x000  u32   set_type
 *   +0x004  4     unknown reserved bytes, not decoded
 *   +0x008  image block 0, 0x88 bytes
 *   +0x090  image block 1, 0x88 bytes
 *   +0x118  image block 2, 0x88 bytes
 *   +0x1a0  image block 3, 0x88 bytes
 *
 * The four blocks are the optional first image, the main image, the screen
 * image and the thumbnail, in that order.  Inside a block, relative to the
 * block base:
 *
 *   +0x00  u32   stride x
 *   +0x04  u32   stride y
 *   +0x08  u32   width
 *   +0x0c  u32   height
 *   +0x40  u8    address_type
 *   +0x44  u32   first physical word
 *   +0x54  u32   first virtual word
 *   +0x74  u32   buffer size word
 *
 * The four geometry words are named by the capture library's own diagnostic
 * print, which writes the set's images as
 * "- [ENC_SET] <MAIN> Width(%4dx%4d), Stride(%4dx%4d)":
 * the width is at the block's +0x08, the height at its +0x0c, the stride x at
 * its +0x00 and the stride y at its +0x04.  The library prints that for the
 * main, the screen and the thumbnail image; the first image is written by the
 * same writer into the same structure, so the layout is the same, and the
 * decoder reads all four blocks the same way. The stride words have no
 * established byte unit: preserve them as metadata, never as copy bounds.
 * All callers must rebuild when this output structure changes size.
 *
 * The physical and virtual words and the size word come from the block's
 * buffer fields at +4, +0x14 and +0x28.  They are opaque 32-bit values in
 * the target's own domain.  This component does not map them, dereference
 * them, own them or derive any bound from them.
 *
 * This component is a metadata wire decoder only.  It performs no allocation,
 * no I/O, no target address access, no callback and no ring fetch, and it
 * does not complete a capture or a backend.
 */

/* Size of one constituent metadata block in bytes: 8 + 4 * 0x88. */
#define ENCODED_SET_METADATA_SIZE ((size_t)0x228u)
/* The set type word and the unknown reserved bytes in front of the blocks. */
#define ENCODED_SET_HEADER_SIZE ((size_t)0x008u)
/* Size of one image info block. */
#define ENCODED_SET_IMAGE_BLOCK_SIZE ((size_t)0x88u)
/* Number of image info blocks in a constituent. */
#define ENCODED_SET_IMAGE_COUNT ((size_t)4u)

/* Offsets of the header fields inside the constituent. */
#define ENCODED_SET_TYPE_OFFSET ((size_t)0x000u)
#define ENCODED_SET_RESERVED_OFFSET ((size_t)0x004u)

/* Offsets of the four image blocks inside the constituent. */
#define ENCODED_SET_IMAGE_FIRST_OFFSET ((size_t)0x008u)
#define ENCODED_SET_IMAGE_MAIN_OFFSET ((size_t)0x090u)
#define ENCODED_SET_IMAGE_SCREEN_OFFSET ((size_t)0x118u)
#define ENCODED_SET_IMAGE_THUMBNAIL_OFFSET ((size_t)0x1a0u)

/* Offsets of the decoded fields inside one image block. */
#define ENCODED_SET_STRIDE_X_OFFSET ((size_t)0x00u)
#define ENCODED_SET_STRIDE_Y_OFFSET ((size_t)0x04u)
#define ENCODED_SET_WIDTH_OFFSET ((size_t)0x08u)
#define ENCODED_SET_HEIGHT_OFFSET ((size_t)0x0cu)
#define ENCODED_SET_ADDRESS_TYPE_OFFSET ((size_t)0x40u)
#define ENCODED_SET_FIRST_PHYSICAL_OFFSET ((size_t)0x44u)
#define ENCODED_SET_FIRST_VIRTUAL_OFFSET ((size_t)0x54u)
#define ENCODED_SET_BUFFER_SIZE_OFFSET ((size_t)0x74u)

/*
 * The four image block positions, in constituent order.  The first image
 * carries a native "RawCompress" label; that label is a native name only.  It
 * is not established that this slot holds sensor RAW, and the decoder makes no
 * such claim.
 */
typedef enum encoded_set_image_index {
    ENCODED_SET_IMAGE_FIRST = 0,
    ENCODED_SET_IMAGE_MAIN = 1,
    ENCODED_SET_IMAGE_SCREEN = 2,
    ENCODED_SET_IMAGE_THUMBNAIL = 3
} encoded_set_image_index;

/*
 * One decoded image descriptor.  Every field is the raw value at the offset
 * above.  No field is validated, interpreted or converted.
 */
typedef struct encoded_set_image_descriptor {
    /* Raw address type byte.  Unknown values are preserved as they are. */
    uint8_t address_type;
    /* Raw first physical word of the block's buffer field. */
    uint32_t first_physical_word;
    /* Raw first virtual word of the block's buffer field. */
    uint32_t first_virtual_word;
    /* Raw buffer size word of the block's buffer field. */
    uint32_t buffer_size_word;
    /* The image's stride x, as the capture library's own print names it. */
    uint32_t stride_x;
    /* The image's stride y. */
    uint32_t stride_y;
    /* The image's width word; preserved without validation. */
    uint32_t width;
    /* The image's height word; preserved without validation. */
    uint32_t height;
} encoded_set_image_descriptor;

/*
 * The decoded constituent metadata.  set_type is kept as the raw word; the
 * decoder does not map it onto a known set kind and does not reject unknown
 * values.
 */
typedef struct encoded_set_metadata {
    uint32_t set_type;
    encoded_set_image_descriptor images[ENCODED_SET_IMAGE_COUNT];
} encoded_set_metadata;

typedef enum encoded_set_status {
    ENCODED_SET_OK = 0,
    /* A null input pointer or a null output pointer. */
    ENCODED_SET_INVALID_ARGUMENT = 1,
    /* The input is not exactly ENCODED_SET_METADATA_SIZE bytes. */
    ENCODED_SET_INVALID_LENGTH = 2
} encoded_set_status;

/*
 * Decode one constituent metadata block.
 *
 * input points at exactly ENCODED_SET_METADATA_SIZE bytes.  The input may be
 * unaligned; the fields are read byte by byte as little-endian words, so no
 * alignment or aliasing assumption is made.  The input may overlap
 * out_metadata in any direction: all required fields are read into a local
 * structure first and the output is written once at the end.
 *
 * Any other length is refused, including the full 0xd70 preference record and
 * a 4-byte context pointer, which are different objects and not constituents.
 *
 * On failure out_metadata is left unchanged.
 */
encoded_set_status encoded_set_decode(const uint8_t *input,
                                       size_t input_size,
                                       encoded_set_metadata *out_metadata);

#ifdef __cplusplus
}
#endif

#endif /* ENCODED_SET_H */
