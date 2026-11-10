#include "encoded_set.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/*
 * Every byte in this file is invented for the host test.  The fixture is not
 * camera output and not firmware bytes; it only carries distinctive values
 * so that a wrong offset or a wrong byte order cannot pass unnoticed.
 */
#define FIXTURE_FILLER UINT8_C(0x7e)
#define FIXTURE_SET_TYPE UINT32_C(0x12345678)

static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & UINT32_C(0xff));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xff));
}

/* Write one image block with its four decoded fields. */
static void write_block(uint8_t *constituent,
                        size_t block_offset,
                        uint8_t address_type,
                        uint32_t physical_word,
                        uint32_t virtual_word,
                        uint32_t buffer_size_word)
{
    memset(constituent + block_offset, FIXTURE_FILLER,
           ENCODED_SET_IMAGE_BLOCK_SIZE);
    constituent[block_offset + ENCODED_SET_ADDRESS_TYPE_OFFSET] = address_type;
    put_u32_le(constituent + block_offset + ENCODED_SET_FIRST_PHYSICAL_OFFSET,
               physical_word);
    put_u32_le(constituent + block_offset + ENCODED_SET_FIRST_VIRTUAL_OFFSET,
               virtual_word);
    put_u32_le(constituent + block_offset + ENCODED_SET_BUFFER_SIZE_OFFSET,
               buffer_size_word);
}

/* Use literal wire offsets independently of the decoder constants.
 * Write one image block's geometry: the stride x and y, the width and the
 * height. */
static void write_geometry(uint8_t *constituent, size_t block_offset,
                           uint32_t stride_x, uint32_t stride_y,
                           uint32_t width, uint32_t height)
{
    put_u32_le(constituent + block_offset + 0x00u,
               stride_x);
    put_u32_le(constituent + block_offset + 0x04u,
               stride_y);
    put_u32_le(constituent + block_offset + 0x08u, width);
    put_u32_le(constituent + block_offset + 0x0cu, height);
}

/* Build one synthetic constituent metadata block. */
static void build_fixture(uint8_t *constituent)
{
    memset(constituent, FIXTURE_FILLER, ENCODED_SET_METADATA_SIZE);
    put_u32_le(constituent + ENCODED_SET_TYPE_OFFSET, FIXTURE_SET_TYPE);

    /* The reserved bytes at +4 stay filler; they are not decoded. */
    write_block(constituent, ENCODED_SET_IMAGE_FIRST_OFFSET,
                UINT8_C(0x11), UINT32_C(0x00010203), UINT32_C(0x04050607),
                UINT32_C(0x08090a0b));
    write_block(constituent, ENCODED_SET_IMAGE_MAIN_OFFSET,
                UINT8_C(0x22), UINT32_C(0x11121314), UINT32_C(0x15161718),
                UINT32_C(0x191a1b1c));
    write_block(constituent, ENCODED_SET_IMAGE_SCREEN_OFFSET,
                UINT8_C(0x33), UINT32_C(0x21222324), UINT32_C(0x25262728),
                UINT32_C(0x292a2b2c));
    write_block(constituent, ENCODED_SET_IMAGE_THUMBNAIL_OFFSET,
                UINT8_C(0x44), UINT32_C(0x31323334), UINT32_C(0x35363738),
                UINT32_C(0x393a3b3c));

    /* And each block's own geometry. */
    write_geometry(constituent, ENCODED_SET_IMAGE_FIRST_OFFSET,
                   UINT32_C(0x00001001), UINT32_C(0x00001002),
                   UINT32_C(0x00001003), UINT32_C(0x00001004));
    write_geometry(constituent, ENCODED_SET_IMAGE_MAIN_OFFSET,
                   UINT32_C(0x00002001), UINT32_C(0x00002002),
                   UINT32_C(0x00002003), UINT32_C(0x00002004));
    write_geometry(constituent, ENCODED_SET_IMAGE_SCREEN_OFFSET,
                   UINT32_C(0x00003001), UINT32_C(0x00003002),
                   UINT32_C(0x00003003), UINT32_C(0x00003004));
    write_geometry(constituent, ENCODED_SET_IMAGE_THUMBNAIL_OFFSET,
                   UINT32_C(0x00004001), UINT32_C(0x00004002),
                   UINT32_C(0x00004003), UINT32_C(0x00004004));
}

static void check_fixture_decoded(const encoded_set_metadata *decoded)
{
    size_t image;

    /* Each slot has distinct values, so field or block swaps are visible. */
    for (image = 0; image < 4u; ++image) {
        uint32_t tag = (uint32_t)(image + 1u) * UINT32_C(0x1000);
        assert(decoded->images[image].stride_x == tag + 1u);
        assert(decoded->images[image].stride_y == tag + 2u);
        assert(decoded->images[image].width == tag + 3u);
        assert(decoded->images[image].height == tag + 4u);
    }

    assert(decoded->set_type == FIXTURE_SET_TYPE);

    assert(decoded->images[ENCODED_SET_IMAGE_FIRST].address_type == 0x11);
    assert(decoded->images[ENCODED_SET_IMAGE_FIRST].first_physical_word ==
           UINT32_C(0x00010203));
    assert(decoded->images[ENCODED_SET_IMAGE_FIRST].first_virtual_word ==
           UINT32_C(0x04050607));
    assert(decoded->images[ENCODED_SET_IMAGE_FIRST].buffer_size_word ==
           UINT32_C(0x08090a0b));
    assert(decoded->images[ENCODED_SET_IMAGE_FIRST].stride_x ==
           UINT32_C(0x00001001));
    assert(decoded->images[ENCODED_SET_IMAGE_FIRST].stride_y ==
           UINT32_C(0x00001002));
    assert(decoded->images[ENCODED_SET_IMAGE_FIRST].width ==
           UINT32_C(0x00001003));
    assert(decoded->images[ENCODED_SET_IMAGE_FIRST].height ==
           UINT32_C(0x00001004));

    assert(decoded->images[ENCODED_SET_IMAGE_MAIN].address_type == 0x22);
    assert(decoded->images[ENCODED_SET_IMAGE_MAIN].stride_x ==
           UINT32_C(0x00002001));
    assert(decoded->images[ENCODED_SET_IMAGE_MAIN].width ==
           UINT32_C(0x00002003));
    assert(decoded->images[ENCODED_SET_IMAGE_MAIN].first_physical_word ==
           UINT32_C(0x11121314));
    assert(decoded->images[ENCODED_SET_IMAGE_MAIN].first_virtual_word ==
           UINT32_C(0x15161718));
    assert(decoded->images[ENCODED_SET_IMAGE_MAIN].buffer_size_word ==
           UINT32_C(0x191a1b1c));

    assert(decoded->images[ENCODED_SET_IMAGE_SCREEN].address_type == 0x33);
    assert(decoded->images[ENCODED_SET_IMAGE_SCREEN].first_physical_word ==
           UINT32_C(0x21222324));
    assert(decoded->images[ENCODED_SET_IMAGE_SCREEN].first_virtual_word ==
           UINT32_C(0x25262728));
    assert(decoded->images[ENCODED_SET_IMAGE_SCREEN].buffer_size_word ==
           UINT32_C(0x292a2b2c));

    assert(decoded->images[ENCODED_SET_IMAGE_THUMBNAIL].address_type == 0x44);
    assert(decoded->images[ENCODED_SET_IMAGE_THUMBNAIL].first_physical_word ==
           UINT32_C(0x31323334));
    assert(decoded->images[ENCODED_SET_IMAGE_THUMBNAIL].first_virtual_word ==
           UINT32_C(0x35363738));
    assert(decoded->images[ENCODED_SET_IMAGE_THUMBNAIL].buffer_size_word ==
           UINT32_C(0x393a3b3c));
}

/* The four blocks must not be confused with each other. */
static void test_decode_synthetic_vector(void)
{
    uint8_t constituent[ENCODED_SET_METADATA_SIZE];
    encoded_set_metadata decoded;

    build_fixture(constituent);
    memset(&decoded, 0, sizeof(decoded));
    assert(encoded_set_decode(constituent, sizeof(constituent),
                               &decoded) == ENCODED_SET_OK);
    check_fixture_decoded(&decoded);
}

/* The same fixture read from an unaligned address. */
static void test_decode_unaligned_input(void)
{
    uint8_t storage[ENCODED_SET_METADATA_SIZE + 8];
    uint8_t constituent[ENCODED_SET_METADATA_SIZE];
    encoded_set_metadata decoded;

    build_fixture(constituent);
    memset(storage, 0, sizeof(storage));
    memcpy(storage + 1, constituent, sizeof(constituent));

    memset(&decoded, 0, sizeof(decoded));
    assert(encoded_set_decode(storage + 1, sizeof(constituent),
                               &decoded) == ENCODED_SET_OK);
    check_fixture_decoded(&decoded);
}

/* Unknown set types and address types are kept as raw values. */
static void test_unknown_values_are_preserved(void)
{
    uint8_t constituent[ENCODED_SET_METADATA_SIZE];
    encoded_set_metadata decoded;

    build_fixture(constituent);
    put_u32_le(constituent + ENCODED_SET_TYPE_OFFSET, UINT32_MAX);
    constituent[ENCODED_SET_IMAGE_FIRST_OFFSET +
                ENCODED_SET_ADDRESS_TYPE_OFFSET] = UINT8_C(0xff);
    constituent[ENCODED_SET_IMAGE_MAIN_OFFSET +
                ENCODED_SET_ADDRESS_TYPE_OFFSET] = UINT8_C(0x00);

    memset(&decoded, 0, sizeof(decoded));
    assert(encoded_set_decode(constituent, sizeof(constituent),
                               &decoded) == ENCODED_SET_OK);
    assert(decoded.set_type == UINT32_MAX);
    assert(decoded.images[ENCODED_SET_IMAGE_FIRST].address_type == 0xff);
    assert(decoded.images[ENCODED_SET_IMAGE_MAIN].address_type == 0x00);
}

/*
 * Only an exact constituent is accepted.  The full preference record and a
 * 4-byte context pointer are different objects and are refused, as are
 * truncated and oversized inputs.
 */
static void test_rejected_lengths_leave_output_unchanged(void)
{
    static const size_t rejected_sizes[] = {
        0u, 1u, 4u, 8u,
        (size_t)0x227u,
        (size_t)0x229u,
        (size_t)0xd70u,
        (size_t)0x1000u
    };
    uint8_t constituent[ENCODED_SET_METADATA_SIZE];
    encoded_set_metadata decoded;
    encoded_set_metadata sentinel;
    size_t index;

    build_fixture(constituent);
    memset(&sentinel, 0xa5, sizeof(sentinel));

    for (index = 0; index < sizeof(rejected_sizes) / sizeof(rejected_sizes[0]);
         ++index) {
        memcpy(&decoded, &sentinel, sizeof(decoded));
        assert(encoded_set_decode(constituent, rejected_sizes[index],
                                   &decoded) == ENCODED_SET_INVALID_LENGTH);
        assert(memcmp(&decoded, &sentinel, sizeof(sentinel)) == 0);
    }
}

static void test_null_arguments_are_refused(void)
{
    uint8_t constituent[ENCODED_SET_METADATA_SIZE];
    encoded_set_metadata decoded;
    encoded_set_metadata sentinel;

    build_fixture(constituent);
    memset(&sentinel, 0xa5, sizeof(sentinel));

    memcpy(&decoded, &sentinel, sizeof(decoded));
    assert(encoded_set_decode(NULL, sizeof(constituent), &decoded) ==
           ENCODED_SET_INVALID_ARGUMENT);
    assert(memcmp(&decoded, &sentinel, sizeof(sentinel)) == 0);

    memcpy(&decoded, &sentinel, sizeof(decoded));
    assert(encoded_set_decode(constituent, sizeof(constituent), NULL) ==
           ENCODED_SET_INVALID_ARGUMENT);
}

/*
 * The output may live inside the input buffer.  Every input byte is read
 * before the output is written, so an overlapping output is decoded
 * correctly.  The output is placed over the screen block, whose fields are
 * read before the write, so an implementation that wrote the output while it
 * parsed would produce the wrong result here.
 */
static void test_overlapping_output(void)
{
    union {
        uint8_t bytes[ENCODED_SET_METADATA_SIZE + 64];
        uint32_t alignment;
    } storage;
    uint8_t constituent[ENCODED_SET_METADATA_SIZE];
    encoded_set_metadata *out_metadata;

    build_fixture(constituent);
    memset(storage.bytes, 0, sizeof(storage.bytes));
    memcpy(storage.bytes, constituent, sizeof(constituent));

    /* An aligned address inside the input buffer, over the screen block. */
    out_metadata = (encoded_set_metadata *)(void *)(storage.bytes + 0x140);
    assert(encoded_set_decode(storage.bytes, sizeof(constituent),
                               out_metadata) == ENCODED_SET_OK);
    check_fixture_decoded(out_metadata);
}

int main(void)
{
    test_decode_synthetic_vector();
    test_decode_unaligned_input();
    test_unknown_values_are_preserved();
    test_rejected_lengths_leave_output_unchanged();
    test_null_arguments_are_refused();
    test_overlapping_output();
    return 0;
}
