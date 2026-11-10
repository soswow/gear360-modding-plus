#include "encoded_set.h"

/*
 * Byte-wise little-endian reads.  The input may start at any address, so the
 * fields are never read through a wider or aligned type.
 */
static uint8_t load_u8(const uint8_t *source)
{
    return source[0];
}

static uint32_t load_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static void decode_image_block(const uint8_t *block,
                                encoded_set_image_descriptor *out_descriptor)
{
    out_descriptor->stride_x =
        load_u32_le(block + ENCODED_SET_STRIDE_X_OFFSET);
    out_descriptor->stride_y =
        load_u32_le(block + ENCODED_SET_STRIDE_Y_OFFSET);
    out_descriptor->width =
        load_u32_le(block + ENCODED_SET_WIDTH_OFFSET);
    out_descriptor->height =
        load_u32_le(block + ENCODED_SET_HEIGHT_OFFSET);
    out_descriptor->address_type =
        load_u8(block + ENCODED_SET_ADDRESS_TYPE_OFFSET);
    out_descriptor->first_physical_word =
        load_u32_le(block + ENCODED_SET_FIRST_PHYSICAL_OFFSET);
    out_descriptor->first_virtual_word =
        load_u32_le(block + ENCODED_SET_FIRST_VIRTUAL_OFFSET);
    out_descriptor->buffer_size_word =
        load_u32_le(block + ENCODED_SET_BUFFER_SIZE_OFFSET);
}

/*
 * The block order of the constituent: optional first image, main image,
 * screen image, thumbnail.  These are the serializer's block offsets.
 */
static const size_t image_block_offsets[ENCODED_SET_IMAGE_COUNT] = {
    ENCODED_SET_IMAGE_FIRST_OFFSET,
    ENCODED_SET_IMAGE_MAIN_OFFSET,
    ENCODED_SET_IMAGE_SCREEN_OFFSET,
    ENCODED_SET_IMAGE_THUMBNAIL_OFFSET
};

encoded_set_status encoded_set_decode(const uint8_t *input,
                                       size_t input_size,
                                       encoded_set_metadata *out_metadata)
{
    encoded_set_metadata parsed;
    size_t index;

    if (input == NULL || out_metadata == NULL) {
        return ENCODED_SET_INVALID_ARGUMENT;
    }
    if (input_size != ENCODED_SET_METADATA_SIZE) {
        return ENCODED_SET_INVALID_LENGTH;
    }

    /*
     * Everything is read first.  The output object is written only after the
     * whole input has been consumed, so an input that overlaps the output
     * object stays readable.
     */
    parsed.set_type = load_u32_le(input + ENCODED_SET_TYPE_OFFSET);
    for (index = 0; index < ENCODED_SET_IMAGE_COUNT; ++index) {
        decode_image_block(input + image_block_offsets[index],
                           &parsed.images[index]);
    }

    /*
     * The reserved bytes at +4 and every other field of the image blocks are
     * not decoded.  Their meaning is not established, so no value is claimed
     * for them and none is type-cast.
     */
    out_metadata->set_type = parsed.set_type;
    for (index = 0; index < ENCODED_SET_IMAGE_COUNT; ++index) {
        out_metadata->images[index].stride_x = parsed.images[index].stride_x;
        out_metadata->images[index].stride_y = parsed.images[index].stride_y;
        out_metadata->images[index].width = parsed.images[index].width;
        out_metadata->images[index].height = parsed.images[index].height;
        out_metadata->images[index].address_type =
            parsed.images[index].address_type;
        out_metadata->images[index].first_physical_word =
            parsed.images[index].first_physical_word;
        out_metadata->images[index].first_virtual_word =
            parsed.images[index].first_virtual_word;
        out_metadata->images[index].buffer_size_word =
            parsed.images[index].buffer_size_word;
    }
    return ENCODED_SET_OK;
}
