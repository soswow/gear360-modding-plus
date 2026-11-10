#include "image_buffer_request.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/* The retained acquire vector: 1 MiB, bank 2. */
static const uint8_t expected_acquire_frame[28] = {
    0x80, 0x00, 0x8c, 0x01, 0x14, 0x00,             /* category, attribute, body size */
    0x00, 0x00, 0x00, 0x00,                         /* word 0 */
    0x00, 0x00, 0x00, 0x00,                         /* word 1 */
    0x00, 0x00, 0x10, 0x00,                         /* word 2: 0x00100000 */
    0x02, 0x00, 0x00, 0x00,                         /* word 3: bank 2 */
    0x00, 0x00, 0x00, 0x00,                         /* word 4 */
    0x00, 0x00                                      /* trailer */
};

/* The retained release vector: physical address 0xab134000. */
static const uint8_t expected_release_frame[12] = {
    0x81, 0x00, 0x8d, 0x01, 0x04, 0x00,             /* category, attribute, body size */
    0x00, 0x40, 0x13, 0xab,                         /* address 0xab134000 */
    0x00, 0x00                                      /* trailer */
};

/*
 * Build one received mcb-frame: the route prefix, the header, the body and the
 * two trailer bytes.  This is the byte layout the kernel leaves after it
 * consumes its four-byte outer header.
 */
static size_t make_reply_frame(uint8_t *out,
                               size_t out_capacity,
                               uint16_t category,
                               uint16_t attribute,
                               uint16_t body_length,
                               const uint8_t *body,
                               uint8_t trailer_first,
                               uint8_t trailer_second)
{
    const size_t frame_size = (size_t)body_length + 10u;

    assert(out_capacity >= frame_size);
    out[0] = 0x00;
    out[1] = 0x03;
    out[2] = (uint8_t)(category & UINT16_C(0xff));
    out[3] = (uint8_t)(category >> 8);
    out[4] = (uint8_t)(attribute & UINT16_C(0xff));
    out[5] = (uint8_t)(attribute >> 8);
    out[6] = (uint8_t)(body_length & UINT16_C(0xff));
    out[7] = (uint8_t)(body_length >> 8);
    if (body_length != 0) {
        memcpy(out + 8, body, body_length);
    }
    out[8 + body_length] = trailer_first;
    out[9 + body_length] = trailer_second;
    return frame_size;
}

static void fill_reply_body(uint8_t *body, uint32_t word0, uint32_t address,
                            uint32_t requested_bytes, uint32_t bank,
                            uint32_t manager_query)
{
    body[0x00] = (uint8_t)(word0 & UINT32_C(0xff));
    body[0x01] = (uint8_t)((word0 >> 8) & UINT32_C(0xff));
    body[0x02] = (uint8_t)((word0 >> 16) & UINT32_C(0xff));
    body[0x03] = (uint8_t)((word0 >> 24) & UINT32_C(0xff));
    body[0x04] = (uint8_t)(address & UINT32_C(0xff));
    body[0x05] = (uint8_t)((address >> 8) & UINT32_C(0xff));
    body[0x06] = (uint8_t)((address >> 16) & UINT32_C(0xff));
    body[0x07] = (uint8_t)((address >> 24) & UINT32_C(0xff));
    body[0x08] = (uint8_t)(requested_bytes & UINT32_C(0xff));
    body[0x09] = (uint8_t)((requested_bytes >> 8) & UINT32_C(0xff));
    body[0x0a] = (uint8_t)((requested_bytes >> 16) & UINT32_C(0xff));
    body[0x0b] = (uint8_t)((requested_bytes >> 24) & UINT32_C(0xff));
    body[0x0c] = (uint8_t)(bank & UINT32_C(0xff));
    body[0x0d] = (uint8_t)((bank >> 8) & UINT32_C(0xff));
    body[0x0e] = (uint8_t)((bank >> 16) & UINT32_C(0xff));
    body[0x0f] = (uint8_t)((bank >> 24) & UINT32_C(0xff));
    body[0x10] = (uint8_t)(manager_query & UINT32_C(0xff));
    body[0x11] = (uint8_t)((manager_query >> 8) & UINT32_C(0xff));
    body[0x12] = (uint8_t)((manager_query >> 16) & UINT32_C(0xff));
    body[0x13] = (uint8_t)((manager_query >> 24) & UINT32_C(0xff));
}

static void test_acquire_vector(void)
{
    uint8_t frame[IMAGE_BUFFER_REQUEST_ACQUIRE_FRAME_SIZE];
    size_t frame_size = 0;

    memset(frame, 0xa5, sizeof(frame));
    assert(image_buffer_request_encode_acquire(UINT32_C(0x00100000),
                                               IMAGE_BUFFER_REQUEST_BANK_SECOND,
                                               frame, sizeof(frame),
                                               &frame_size) == IMAGE_BUFFER_REQUEST_OK);
    assert(frame_size == sizeof(expected_acquire_frame));
    assert(memcmp(frame, expected_acquire_frame, sizeof(expected_acquire_frame)) == 0);
}

static void test_release_vector(void)
{
    uint8_t frame[IMAGE_BUFFER_REQUEST_RELEASE_FRAME_SIZE];
    size_t frame_size = 0;

    memset(frame, 0xa5, sizeof(frame));
    assert(image_buffer_request_encode_release(UINT32_C(0xab134000),
                                               frame, sizeof(frame),
                                               &frame_size) == IMAGE_BUFFER_REQUEST_OK);
    assert(frame_size == sizeof(expected_release_frame));
    assert(memcmp(frame, expected_release_frame, sizeof(expected_release_frame)) == 0);
}

/*
 * The words are little endian at fixed offsets.  Check the offsets and the
 * byte order of every field instead of only comparing whole frames.
 */
static void test_field_endianness(void)
{
    uint8_t body[IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE];
    uint8_t frame[IMAGE_BUFFER_REQUEST_ACQUIRE_FRAME_SIZE];
    uint8_t release_body[IMAGE_BUFFER_REQUEST_RELEASE_BODY_SIZE];
    size_t frame_size = 0;

    assert(image_buffer_request_encode_acquire_body(UINT32_C(0x00123456),
                                                    IMAGE_BUFFER_REQUEST_BANK_FIRST,
                                                    body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_OK);
    assert(body[0x00] == 0x00 && body[0x04] == 0x00 && body[0x10] == 0x00);
    assert(body[0x08] == 0x56 && body[0x09] == 0x34 &&
           body[0x0a] == 0x12 && body[0x0b] == 0x00);
    assert(body[0x0c] == 0x01 && body[0x0d] == 0x00 &&
           body[0x0e] == 0x00 && body[0x0f] == 0x00);

    /* The frame carries the little-endian header, the body and a zero trailer. */
    assert(image_buffer_request_encode_acquire(UINT32_C(0x00123456),
                                               IMAGE_BUFFER_REQUEST_BANK_FIRST,
                                               frame, sizeof(frame),
                                               &frame_size) == IMAGE_BUFFER_REQUEST_OK);
    assert(frame[0] == 0x80 && frame[1] == 0x00);
    assert(frame[2] == 0x8c && frame[3] == 0x01);
    assert(frame[4] == 0x14 && frame[5] == 0x00);
    assert(memcmp(frame + 6, body, sizeof(body)) == 0);
    assert(frame[26] == 0x00 && frame[27] == 0x00);

    assert(image_buffer_request_encode_release_body(UINT32_C(0xab134000),
                                                    release_body,
                                                    sizeof(release_body)) ==
           IMAGE_BUFFER_REQUEST_OK);
    assert(release_body[0] == 0x00 && release_body[1] == 0x40 &&
           release_body[2] == 0x13 && release_body[3] == 0xab);
}

static void test_reply_decode_refusal(void)
{
    uint8_t body[IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE];
    uint8_t frame[MCB_FRAME_MAX_SIZE];
    uint8_t scratch[MCB_FRAME_MAX_SIZE];
    image_buffer_request_reply reply;
    size_t frame_size;

    /* A zero address is a syntactically valid refusal, not an allocation. */
    fill_reply_body(body, UINT32_C(0), UINT32_C(0), UINT32_C(0x00100000),
                    IMAGE_BUFFER_REQUEST_BANK_SECOND, UINT32_C(0x00400000));
    frame_size = make_reply_frame(frame, sizeof(frame),
                                  IMAGE_BUFFER_REQUEST_CATEGORY_REPLY,
                                  IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY,
                                  UINT16_C(20), body, 0x00, 0x00);
    assert(frame_size == 30u);
    memset(&reply, 0xa5, sizeof(reply));
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_OK);
    assert(reply.wire_word0 == UINT32_C(0));
    assert(reply.address_word == UINT32_C(0));
    assert(reply.requested_bytes == UINT32_C(0x00100000));
    assert(reply.bank == IMAGE_BUFFER_REQUEST_BANK_SECOND);
    assert(reply.manager_query_word == UINT32_C(0x00400000));
    assert(image_buffer_request_reply_is_refusal(&reply) == 1);

    /* A nonzero address is not established to be an owned allocation. */
    fill_reply_body(body, UINT32_C(0), UINT32_C(0xab134000),
                    UINT32_C(0x00100000), IMAGE_BUFFER_REQUEST_BANK_SECOND,
                    UINT32_C(0x00400000));
    frame_size = make_reply_frame(frame, sizeof(frame),
                                  IMAGE_BUFFER_REQUEST_CATEGORY_REPLY,
                                  IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY,
                                  UINT16_C(20), body, 0x00, 0x00);
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_OK);
    assert(reply.address_word == UINT32_C(0xab134000));
    assert(reply.manager_query_word == UINT32_C(0x00400000));
    assert(image_buffer_request_reply_is_refusal(&reply) == 0);
    assert(image_buffer_request_reply_is_refusal(NULL) == 0);
}

/*
 * The envelope has to be complete: the exact frame is accepted, and a frame
 * with a missing trailer, a truncated body, an extra byte, a wrong category, a
 * wrong attribute or a wrong declared length is refused.  A refused decode
 * leaves the reply object untouched.
 */
static void test_reply_frame_rejections(void)
{
    uint8_t body[IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE];
    uint8_t short_body[19];
    uint8_t long_body[21];
    uint8_t frame[MCB_FRAME_MAX_SIZE];
    uint8_t scratch[MCB_FRAME_MAX_SIZE];
    image_buffer_request_reply reply;
    image_buffer_request_reply untouched;
    size_t frame_size;

    memset(body, 0, sizeof(body));
    memset(short_body, 0, sizeof(short_body));
    memset(long_body, 0, sizeof(long_body));
    memset(&untouched, 0x5a, sizeof(untouched));

    /* The exact frame is the accepted boundary. */
    frame_size = make_reply_frame(frame, sizeof(frame),
                                  IMAGE_BUFFER_REQUEST_CATEGORY_REPLY,
                                  IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY,
                                  UINT16_C(20), body, 0x00, 0x00);
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_OK);

    /* One byte short of the trailer. */
    assert(image_buffer_request_decode_reply(frame, frame_size - 2u, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_MALFORMED_FRAME);
    /* One byte short of the body. */
    assert(image_buffer_request_decode_reply(frame, frame_size - 3u, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_MALFORMED_FRAME);
    /* One byte more than the frame. */
    assert(image_buffer_request_decode_reply(frame, frame_size + 1u, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_MALFORMED_FRAME);
    /* A wrong route prefix. */
    frame[0] = 0x00;
    frame[1] = 0x04;
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_MALFORMED_FRAME);
    frame[1] = 0x03;

    /* A wrong category. */
    frame_size = make_reply_frame(frame, sizeof(frame), UINT16_C(0x0080),
                                  IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY,
                                  UINT16_C(20), body, 0x00, 0x00);
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_UNEXPECTED_CATEGORY);
    /* A wrong attribute. */
    frame_size = make_reply_frame(frame, sizeof(frame),
                                  IMAGE_BUFFER_REQUEST_CATEGORY_REPLY,
                                  UINT16_C(0x018d), UINT16_C(20), body,
                                  0x00, 0x00);
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_UNEXPECTED_ATTRIBUTE);
    /* A body that is not exactly twenty bytes. */
    frame_size = make_reply_frame(frame, sizeof(frame),
                                  IMAGE_BUFFER_REQUEST_CATEGORY_REPLY,
                                  IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY,
                                  UINT16_C(19), short_body, 0x00, 0x00);
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_UNEXPECTED_LENGTH);
    frame_size = make_reply_frame(frame, sizeof(frame),
                                  IMAGE_BUFFER_REQUEST_CATEGORY_REPLY,
                                  IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY,
                                  UINT16_C(21), long_body, 0x00, 0x00);
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_UNEXPECTED_LENGTH);

    /* A refused decode leaves the reply object untouched. */
    memset(&reply, 0x5a, sizeof(reply));
    frame_size = make_reply_frame(frame, sizeof(frame), UINT16_C(0x0080),
                                  IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY,
                                  UINT16_C(20), body, 0x00, 0x00);
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_UNEXPECTED_CATEGORY);
    assert(memcmp(&reply, &untouched, sizeof(untouched)) == 0);
}

static void test_decode_argument_checks(void)
{
    uint8_t body[IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE];
    uint8_t frame[MCB_FRAME_MAX_SIZE];
    uint8_t scratch[MCB_FRAME_MAX_SIZE];
    image_buffer_request_reply reply;
    size_t frame_size;

    memset(body, 0, sizeof(body));
    frame_size = make_reply_frame(frame, sizeof(frame),
                                  IMAGE_BUFFER_REQUEST_CATEGORY_REPLY,
                                  IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY,
                                  UINT16_C(20), body, 0x00, 0x00);

    assert(image_buffer_request_decode_reply(NULL, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT);
    assert(image_buffer_request_decode_reply(frame, frame_size, NULL,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT);
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             sizeof(scratch),
                                             NULL) == IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT);
    /* Scratch smaller than the frame is refused before anything is copied. */
    assert(image_buffer_request_decode_reply(frame, frame_size, scratch,
                                             frame_size - 1u,
                                             &reply) == IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY);
}

/*
 * The request fields are refused on their own terms.  The rounding boundary is
 * the native 16 KiB one: the largest accepted size is 0xffffc000 bytes.
 */
static void test_acquire_argument_checks(void)
{
    uint8_t body[IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE];
    uint8_t frame[IMAGE_BUFFER_REQUEST_ACQUIRE_FRAME_SIZE];
    uint8_t original[IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE];
    size_t frame_size = 12345u;
    size_t original_size = 12345u;

    memset(body, 0x5a, sizeof(body));
    memset(original, 0x5a, sizeof(original));

    /* A zero requested size. */
    assert(image_buffer_request_encode_acquire_body(UINT32_C(0),
                                                    IMAGE_BUFFER_REQUEST_BANK_DEFAULT,
                                                    body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_INVALID_SIZE);
    assert(memcmp(body, original, sizeof(original)) == 0);

    /* An unsupported bank. */
    assert(image_buffer_request_encode_acquire_body(UINT32_C(0x1000),
                                                    UINT32_C(3),
                                                    body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_UNSUPPORTED_BANK);
    assert(memcmp(body, original, sizeof(original)) == 0);

    /* The three known banks are accepted. */
    assert(image_buffer_request_encode_acquire_body(UINT32_C(0x1000),
                                                    IMAGE_BUFFER_REQUEST_BANK_DEFAULT,
                                                    body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_OK);
    assert(image_buffer_request_encode_acquire_body(UINT32_C(0x1000),
                                                    IMAGE_BUFFER_REQUEST_BANK_FIRST,
                                                    body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_OK);
    assert(image_buffer_request_encode_acquire_body(UINT32_C(0x1000),
                                                    IMAGE_BUFFER_REQUEST_BANK_SECOND,
                                                    body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_OK);

    /* The rounding boundary. */
    assert(image_buffer_request_encode_acquire_body(
               IMAGE_BUFFER_REQUEST_MAX_REQUESTED_BYTES,
               IMAGE_BUFFER_REQUEST_BANK_DEFAULT, body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_OK);
    assert(image_buffer_request_encode_acquire_body(
               IMAGE_BUFFER_REQUEST_MAX_REQUESTED_BYTES + UINT32_C(1),
               IMAGE_BUFFER_REQUEST_BANK_DEFAULT, body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_ROUNDING_OVERFLOW);
    /* The largest u32 wraps the native rounding addition as well. */
    assert(image_buffer_request_encode_acquire_body(UINT32_MAX,
                                                    IMAGE_BUFFER_REQUEST_BANK_DEFAULT,
                                                    body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_ROUNDING_OVERFLOW);

    /* A null body and a body that is too small. */
    assert(image_buffer_request_encode_acquire_body(UINT32_C(0x1000),
                                                    IMAGE_BUFFER_REQUEST_BANK_DEFAULT,
                                                    NULL, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT);
    assert(image_buffer_request_encode_acquire_body(UINT32_C(0x1000),
                                                    IMAGE_BUFFER_REQUEST_BANK_DEFAULT,
                                                    body, sizeof(body) - 1u) ==
           IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY);

    /* A null frame or a missing size object. */
    assert(image_buffer_request_encode_acquire(UINT32_C(0x1000),
                                               IMAGE_BUFFER_REQUEST_BANK_DEFAULT,
                                               NULL, sizeof(frame),
                                               &frame_size) ==
           IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT);
    assert(image_buffer_request_encode_acquire(UINT32_C(0x1000),
                                               IMAGE_BUFFER_REQUEST_BANK_DEFAULT,
                                               frame, sizeof(frame),
                                               NULL) == IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT);

    /* A frame that does not fit. */
    assert(image_buffer_request_encode_acquire(UINT32_C(0x1000),
                                               IMAGE_BUFFER_REQUEST_BANK_DEFAULT,
                                               frame, sizeof(frame) - 1u,
                                               &frame_size) ==
           IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY);
    assert(frame_size == original_size);

    /* A refused request leaves the frame and the size object untouched. */
    memset(frame, 0x5a, sizeof(frame));
    assert(image_buffer_request_encode_acquire(UINT32_C(0),
                                               IMAGE_BUFFER_REQUEST_BANK_DEFAULT,
                                               frame, sizeof(frame),
                                               &frame_size) ==
           IMAGE_BUFFER_REQUEST_INVALID_SIZE);
    assert(frame_size == original_size);
    assert(frame[0] == 0x5a && frame[sizeof(frame) - 1u] == 0x5a);
}

static void test_release_argument_checks(void)
{
    uint8_t body[IMAGE_BUFFER_REQUEST_RELEASE_BODY_SIZE];
    uint8_t frame[IMAGE_BUFFER_REQUEST_RELEASE_FRAME_SIZE];
    size_t frame_size = 4321u;

    memset(body, 0x5a, sizeof(body));

    /* The zero address and the no-allocation sentinel are refused. */
    assert(image_buffer_request_encode_release_body(UINT32_C(0), body,
                                                    sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_INVALID_ADDRESS);
    assert(image_buffer_request_encode_release_body(
               IMAGE_BUFFER_REQUEST_ADDRESS_SENTINEL, body, sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_INVALID_ADDRESS);
    assert(body[0] == 0x5a);

    /* A null body and a body that is too small. */
    assert(image_buffer_request_encode_release_body(UINT32_C(0x1000), NULL,
                                                    sizeof(body)) ==
           IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT);
    assert(image_buffer_request_encode_release_body(UINT32_C(0x1000), body,
                                                    sizeof(body) - 1u) ==
           IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY);

    /* A null frame or a missing size object. */
    assert(image_buffer_request_encode_release(UINT32_C(0x1000), NULL,
                                               sizeof(frame),
                                               &frame_size) ==
           IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT);
    assert(image_buffer_request_encode_release(UINT32_C(0x1000), frame,
                                               sizeof(frame),
                                               NULL) == IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT);

    /* A frame that does not fit. */
    assert(image_buffer_request_encode_release(UINT32_C(0x1000), frame,
                                               sizeof(frame) - 1u,
                                               &frame_size) ==
           IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY);
    assert(frame_size == 4321u);
}

/*
 * An encoded request and a synthetic reply that echoes the request words.
 * The echoed size and bank come back as the raw wire words; the echo is not
 * evidence that the reply answers that request.
 */
static void test_encode_and_decode_round_trip(void)
{
    uint8_t request_body[IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE];
    uint8_t reply_body[IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE];
    uint8_t reply_frame[MCB_FRAME_MAX_SIZE];
    uint8_t scratch[MCB_FRAME_MAX_SIZE];
    image_buffer_request_reply reply;
    size_t frame_size;

    assert(image_buffer_request_encode_acquire_body(UINT32_C(0x00200000),
                                                    IMAGE_BUFFER_REQUEST_BANK_SECOND,
                                                    request_body,
                                                    sizeof(request_body)) ==
           IMAGE_BUFFER_REQUEST_OK);
    memcpy(reply_body, request_body, sizeof(reply_body));
    reply_body[0x04] = 0x78; /* address word */
    reply_body[0x05] = 0x56;
    reply_body[0x06] = 0x34;
    reply_body[0x07] = 0x12;
    reply_body[0x10] = 0x00; /* manager query word */
    reply_body[0x11] = 0x00;
    reply_body[0x12] = 0x40;
    reply_body[0x13] = 0x00;

    frame_size = make_reply_frame(reply_frame, sizeof(reply_frame),
                                  IMAGE_BUFFER_REQUEST_CATEGORY_REPLY,
                                  IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY,
                                  UINT16_C(20), reply_body, 0x00, 0x00);
    assert(image_buffer_request_decode_reply(reply_frame, frame_size, scratch,
                                             sizeof(scratch),
                                             &reply) == IMAGE_BUFFER_REQUEST_OK);
    assert(reply.requested_bytes == UINT32_C(0x00200000));
    assert(reply.bank == IMAGE_BUFFER_REQUEST_BANK_SECOND);
    assert(reply.address_word == UINT32_C(0x12345678));
    assert(reply.manager_query_word == UINT32_C(0x00400000));
    assert(image_buffer_request_reply_is_refusal(&reply) == 0);
}

int main(void)
{
    test_acquire_vector();
    test_release_vector();
    test_field_endianness();
    test_reply_decode_refusal();
    test_reply_frame_rejections();
    test_decode_argument_checks();
    test_acquire_argument_checks();
    test_release_argument_checks();
    test_encode_and_decode_round_trip();
    return 0;
}
