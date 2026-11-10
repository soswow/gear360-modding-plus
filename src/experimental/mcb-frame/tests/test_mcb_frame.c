#include "mcb_frame.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static size_t make_frame(uint8_t *frame,
                         size_t payload_length,
                         uint16_t category,
                         uint16_t attribute)
{
    size_t i;

    assert(payload_length <= MCB_FRAME_MAX_PAYLOAD);
    frame[0] = UINT8_C(0x00);
    frame[1] = UINT8_C(0x03);
    frame[2] = (uint8_t)(category & UINT16_C(0xff));
    frame[3] = (uint8_t)(category >> 8);
    frame[4] = (uint8_t)(attribute & UINT16_C(0xff));
    frame[5] = (uint8_t)(attribute >> 8);
    frame[6] = (uint8_t)(payload_length & UINT16_C(0xff));
    frame[7] = (uint8_t)(payload_length >> 8);
    for (i = 0; i < payload_length; ++i) {
        frame[8 + i] = (uint8_t)(i * 13u + 7u);
    }
    frame[8 + payload_length] = UINT8_C(0xa5);
    frame[9 + payload_length] = UINT8_C(0x5a);
    return payload_length + MCB_FRAME_MIN_SIZE;
}

static void assert_message(const mcb_frame_message *message,
                           const uint8_t *storage,
                           uint16_t category,
                           uint16_t attribute,
                           size_t payload_length)
{
    assert(message->category == category);
    assert(message->attribute == attribute);
    assert(message->body == storage + 8);
    assert(message->body_length == payload_length);
}

static void assert_same_message(const mcb_frame_message *actual,
                                const mcb_frame_message *expected)
{
    assert(actual->category == expected->category);
    assert(actual->attribute == expected->attribute);
    assert(actual->body == expected->body);
    assert(actual->body_length == expected->body_length);
}

static void test_empty_frame(void)
{
    uint8_t frame[MCB_FRAME_MIN_SIZE];
    uint8_t storage[MCB_FRAME_MIN_SIZE];
    mcb_frame_message message;
    const size_t frame_size = make_frame(frame, 0, UINT16_C(0xcafe),
                                         UINT16_C(0xbeef));

    memset(storage, 0, sizeof(storage));
    assert(frame_size == sizeof(frame));
    assert(mcb_frame_copy(frame, sizeof(frame), storage, sizeof(storage),
                          &message) == MCB_FRAME_OK);
    assert(memcmp(storage, frame, sizeof(frame)) == 0);
    assert_message(&message, storage, UINT16_C(0xcafe), UINT16_C(0xbeef), 0);
}

static void test_maximum_frame_and_nonzero_trailer(void)
{
    uint8_t frame[MCB_FRAME_MAX_SIZE];
    uint8_t storage[MCB_FRAME_MAX_SIZE];
    mcb_frame_message message;
    const size_t frame_size = make_frame(frame, MCB_FRAME_MAX_PAYLOAD,
                                         UINT16_C(0x1357), UINT16_C(0x2468));

    assert(frame_size == MCB_FRAME_MAX_SIZE);
    assert(frame[MCB_FRAME_MAX_SIZE - 2] == UINT8_C(0xa5));
    assert(frame[MCB_FRAME_MAX_SIZE - 1] == UINT8_C(0x5a));
    assert(mcb_frame_copy(frame, frame_size, storage, sizeof(storage),
                          &message) == MCB_FRAME_OK);
    assert(memcmp(storage, frame, frame_size) == 0);
    assert_message(&message, storage, UINT16_C(0x1357), UINT16_C(0x2468),
                   MCB_FRAME_MAX_PAYLOAD);
}

static void test_truncation_lengths(void)
{
    uint8_t frame[MCB_FRAME_MAX_SIZE];
    uint8_t storage[MCB_FRAME_MAX_SIZE];
    uint8_t storage_before[MCB_FRAME_MAX_SIZE];
    mcb_frame_message message;
    mcb_frame_message before_message;
    size_t length;

    (void)make_frame(frame, MCB_FRAME_MAX_PAYLOAD, UINT16_C(1), UINT16_C(2));
    memset(storage, UINT8_C(0x91), sizeof(storage));
    memset(&before_message, 0, sizeof(before_message));
    before_message.category = UINT16_C(0xaaaa);
    before_message.attribute = UINT16_C(0xbbbb);
    before_message.body = frame;
    before_message.body_length = SIZE_MAX;
    message = before_message;
    memcpy(storage_before, storage, sizeof(storage));
    for (length = 0; length < MCB_FRAME_MAX_SIZE; ++length) {
        message = before_message;
        assert(mcb_frame_copy(frame, length, storage, sizeof(storage),
                              &message) == (length < MCB_FRAME_MIN_SIZE
                                             ? MCB_FRAME_INVALID_SIZE
                                             : MCB_FRAME_INVALID_LENGTH));
        assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
        assert_same_message(&message, &before_message);
    }
}

static void test_invalid_prefix_and_lengths(void)
{
    uint8_t frame[MCB_FRAME_MAX_SIZE];
    uint8_t storage[MCB_FRAME_MAX_SIZE];
    uint8_t storage_before[MCB_FRAME_MAX_SIZE];
    mcb_frame_message message;
    mcb_frame_message before_message;
    size_t frame_size;

    frame_size = make_frame(frame, 3, UINT16_C(7), UINT16_C(8));
    frame[0] = UINT8_C(0x01);
    memset(storage, UINT8_C(0x42), sizeof(storage));
    memcpy(storage_before, storage, sizeof(storage));
    memset(&before_message, 0, sizeof(before_message));
    before_message.category = UINT16_C(0x1111);
    before_message.attribute = UINT16_C(0x2222);
    before_message.body = storage + 4;
    before_message.body_length = SIZE_MAX - 1;
    message = before_message;
    assert(mcb_frame_copy(frame, frame_size, storage, sizeof(storage),
                          &message) == MCB_FRAME_INVALID_PREFIX);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert_same_message(&message, &before_message);

    frame[0] = UINT8_C(0x00);
    frame[1] = UINT8_C(0x04);
    assert(mcb_frame_copy(frame, frame_size, storage, sizeof(storage),
                          &message) == MCB_FRAME_INVALID_PREFIX);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert_same_message(&message, &before_message);
    frame[1] = UINT8_C(0x03);
    frame[6] = UINT8_C(0x04);
    frame[7] = UINT8_C(0x00);
    message = before_message;
    assert(mcb_frame_copy(frame, frame_size, storage, sizeof(storage),
                          &message) == MCB_FRAME_INVALID_LENGTH);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert_same_message(&message, &before_message);

    /* The wire length remains within the 390-byte bound, but the payload does not. */
    frame_size = MCB_FRAME_MAX_SIZE;
    frame[6] = UINT8_C(0x7d);
    frame[7] = UINT8_C(0x01);
    message = before_message;
    assert(mcb_frame_copy(frame, frame_size, storage, sizeof(storage),
                          &message) == MCB_FRAME_INVALID_LENGTH);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert_same_message(&message, &before_message);

    /* A full ring is rejected by the component size bound before dereference. */
    assert(mcb_frame_copy(frame, MCB_FRAME_MAX_SIZE + 1, storage,
                          sizeof(storage), &message) == MCB_FRAME_INVALID_SIZE);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert_same_message(&message, &before_message);
}

static void test_capacity_failure_and_invalid_arguments(void)
{
    uint8_t frame[MCB_FRAME_MAX_SIZE];
    uint8_t storage[MCB_FRAME_MAX_SIZE];
    uint8_t storage_before[MCB_FRAME_MAX_SIZE];
    mcb_frame_message message;
    mcb_frame_message before_message;
    const size_t frame_size = make_frame(frame, 20, UINT16_C(1), UINT16_C(2));

    memset(storage, UINT8_C(0x6d), sizeof(storage));
    memcpy(storage_before, storage, sizeof(storage));
    memset(&before_message, 0, sizeof(before_message));
    before_message.category = UINT16_C(0xaaaa);
    before_message.attribute = UINT16_C(0xbbbb);
    before_message.body = storage + 3;
    before_message.body_length = UINT16_MAX;
    message = before_message;
    assert(mcb_frame_copy(frame, frame_size, storage, frame_size - 1,
                          &message) == MCB_FRAME_INSUFFICIENT_CAPACITY);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert_same_message(&message, &before_message);

    assert(mcb_frame_copy(NULL, frame_size, storage, sizeof(storage),
                          &message) == MCB_FRAME_INVALID_ARGUMENT);
    assert(mcb_frame_copy(frame, frame_size, NULL, sizeof(storage),
                          &message) == MCB_FRAME_INVALID_ARGUMENT);
    assert(mcb_frame_copy(frame, frame_size, storage, sizeof(storage), NULL) ==
           MCB_FRAME_INVALID_ARGUMENT);
}

static void test_overlap_both_directions(void)
{
    uint8_t arena[MCB_FRAME_MAX_SIZE + 64];
    uint8_t expected[MCB_FRAME_MAX_SIZE];
    mcb_frame_message message;
    size_t frame_size;

    memset(arena, UINT8_C(0xcc), sizeof(arena));
    frame_size = make_frame(arena + 24, 120, UINT16_C(0x1020),
                            UINT16_C(0x3040));
    memcpy(expected, arena + 24, frame_size);
    assert(mcb_frame_copy(arena + 24, frame_size, arena + 8, frame_size,
                          &message) == MCB_FRAME_OK);
    assert(memcmp(arena + 8, expected, frame_size) == 0);
    assert_message(&message, arena + 8, UINT16_C(0x1020), UINT16_C(0x3040),
                   120);

    memset(arena, UINT8_C(0xdd), sizeof(arena));
    frame_size = make_frame(arena + 8, 120, UINT16_C(0x5060), UINT16_C(0x7080));
    memcpy(expected, arena + 8, frame_size);
    assert(mcb_frame_copy(arena + 8, frame_size, arena + 24, frame_size,
                          &message) == MCB_FRAME_OK);
    assert(memcmp(arena + 24, expected, frame_size) == 0);
    assert_message(&message, arena + 24, UINT16_C(0x5060), UINT16_C(0x7080),
                   120);
}

static void test_misalignment_and_owned_independence(void)
{
    uint8_t raw[MCB_FRAME_MIN_SIZE + 21];
    uint8_t storage_a[MCB_FRAME_MIN_SIZE + 20];
    uint8_t storage_b[MCB_FRAME_MIN_SIZE + 20];
    uint8_t expected[MCB_FRAME_MIN_SIZE + 20];
    mcb_frame_message message_a;
    mcb_frame_message message_b;
    const size_t length = make_frame(raw + 1, 20, UINT16_C(0x0102),
                                      UINT16_C(0x0304));

    memcpy(expected, raw + 1, length);
    assert(mcb_frame_copy(raw + 1, length, storage_a,
                          sizeof(storage_a), &message_a) == MCB_FRAME_OK);
    assert(mcb_frame_copy(raw + 1, length, storage_b,
                          sizeof(storage_b), &message_b) == MCB_FRAME_OK);
    memset(raw, 0xff, sizeof(raw));
    assert(memcmp(storage_a, expected, length) == 0);
    assert(memcmp(storage_b, expected, length) == 0);
    memset(storage_a + 8, 0x55, 20);
    assert(memcmp(message_b.body, expected + 8, 20) == 0);
    assert(message_a.body == storage_a + 8);
    assert(message_b.body == storage_b + 8);

    /* Exact in-place use retains the body and metadata. */
    assert(mcb_frame_copy(storage_b, length, storage_b,
                          sizeof(storage_b), &message_b) == MCB_FRAME_OK);
    assert(memcmp(storage_b, expected, length) == 0);
    assert_message(&message_b, storage_b, UINT16_C(0x0102), UINT16_C(0x0304), 20);
}

static void assert_encoded(const uint8_t *frame,
                           size_t frame_size,
                           uint16_t category,
                           uint16_t attribute,
                           const uint8_t *body,
                           size_t body_length)
{
    size_t i;

    assert(frame_size == body_length + MCB_FRAME_TX_MIN_SIZE);
    assert(frame[0] == (uint8_t)(category & UINT16_C(0xff)));
    assert(frame[1] == (uint8_t)(category >> 8));
    assert(frame[2] == (uint8_t)(attribute & UINT16_C(0xff)));
    assert(frame[3] == (uint8_t)(attribute >> 8));
    assert(frame[4] == (uint8_t)(body_length & UINT16_C(0xff)));
    assert(frame[5] == (uint8_t)(body_length >> 8));
    for (i = 0; i < body_length; ++i) {
        assert(frame[6 + i] == body[i]);
    }
    assert(frame[6 + body_length] == 0);
    assert(frame[7 + body_length] == 0);
}

static void test_tx_empty_max_and_still_sized_bodies(void)
{
    uint8_t frame[MCB_FRAME_TX_MAX_SIZE];
    uint8_t body[MCB_FRAME_MAX_PAYLOAD];
    size_t frame_size = SIZE_MAX;
    size_t i;

    memset(frame, UINT8_C(0xa5), sizeof(frame));
    assert(mcb_frame_encode_linux(UINT16_C(0xcafe), UINT16_C(0xbeef), NULL,
                                  0, frame, sizeof(frame), &frame_size) ==
           MCB_FRAME_OK);
    assert_encoded(frame, frame_size, UINT16_C(0xcafe), UINT16_C(0xbeef),
                   NULL, 0);

    for (i = 0; i < sizeof(body); ++i) {
        body[i] = (uint8_t)(i * 13u + 7u);
    }
    frame_size = 0;
    assert(mcb_frame_encode_linux(UINT16_C(0x1357), UINT16_C(0x2468), body,
                                  sizeof(body), frame, sizeof(frame),
                                  &frame_size) == MCB_FRAME_OK);
    assert(frame_size == MCB_FRAME_TX_MAX_SIZE);
    assert_encoded(frame, frame_size, UINT16_C(0x1357), UINT16_C(0x2468),
                   body, sizeof(body));

    assert(mcb_frame_encode_linux(UINT16_C(0x0102), UINT16_C(0x0304), body,
                                  48, frame, sizeof(frame), &frame_size) ==
           MCB_FRAME_OK);
    assert_encoded(frame, frame_size, UINT16_C(0x0102), UINT16_C(0x0304),
                   body, 48);
}

static void test_tx_failures_preserve_outputs(void)
{
    uint8_t frame[MCB_FRAME_TX_MAX_SIZE];
    uint8_t frame_before[MCB_FRAME_TX_MAX_SIZE];
    uint8_t body[MCB_FRAME_MAX_PAYLOAD + 1];
    size_t frame_size = (size_t)0x12345678u;
    size_t i;

    memset(frame, UINT8_C(0x6d), sizeof(frame));
    memcpy(frame_before, frame, sizeof(frame));
    for (i = 0; i < sizeof(body); ++i) {
        body[i] = (uint8_t)i;
    }

    assert(mcb_frame_encode_linux(1, 2, body, 1, frame, 8, &frame_size) ==
           MCB_FRAME_INSUFFICIENT_CAPACITY);
    assert(mcb_frame_encode_linux(1, 2, body, sizeof(body), frame,
                                  sizeof(frame), &frame_size) ==
           MCB_FRAME_INVALID_SIZE);
    assert(mcb_frame_encode_linux(1, 2, NULL, 1, frame, sizeof(frame),
                                  &frame_size) == MCB_FRAME_INVALID_ARGUMENT);
    assert(mcb_frame_encode_linux(1, 2, NULL, 0, NULL, sizeof(frame),
                                  &frame_size) == MCB_FRAME_INVALID_ARGUMENT);
    assert(mcb_frame_encode_linux(1, 2, NULL, 0, frame, sizeof(frame), NULL) ==
           MCB_FRAME_INVALID_ARGUMENT);
    assert(memcmp(frame, frame_before, sizeof(frame)) == 0);
    assert(frame_size == (size_t)0x12345678u);
}

static void test_tx_overlap_directions_and_misalignment(void)
{
    uint8_t arena[MCB_FRAME_TX_MAX_SIZE + 32];
    uint8_t original[64];
    size_t frame_size;
    size_t i;

    /* Destination begins before body; the target body starts six bytes in. */
    memset(arena, UINT8_C(0xcc), sizeof(arena));
    for (i = 0; i < sizeof(original); ++i) {
        arena[17 + i] = (uint8_t)(i * 3u + 1u);
        original[i] = arena[17 + i];
    }
    assert(mcb_frame_encode_linux(UINT16_C(0x1122), UINT16_C(0x3344),
                                  arena + 17, sizeof(original), arena + 13,
                                  6 + sizeof(original) + 2, &frame_size) ==
           MCB_FRAME_OK);
    assert_encoded(arena + 13, frame_size, UINT16_C(0x1122),
                   UINT16_C(0x3344), original, sizeof(original));

    /* Body begins inside the destination, after its eventual body start. */
    memset(arena, UINT8_C(0xdd), sizeof(arena));
    for (i = 0; i < sizeof(original); ++i) {
        arena[33 + i] = (uint8_t)(i * 5u + 9u);
        original[i] = arena[33 + i];
    }
    assert(mcb_frame_encode_linux(UINT16_C(0x5566), UINT16_C(0x7788),
                                  arena + 33, sizeof(original), arena + 21,
                                  6 + sizeof(original) + 2, &frame_size) ==
           MCB_FRAME_OK);
    assert_encoded(arena + 21, frame_size, UINT16_C(0x5566),
                   UINT16_C(0x7788), original, sizeof(original));

    /* Exact in-place body and deliberately misaligned frame storage. */
    memset(arena, UINT8_C(0xee), sizeof(arena));
    for (i = 0; i < 20; ++i) {
        arena[9 + i] = (uint8_t)(i + 0x40u);
        original[i] = arena[9 + i];
    }
    assert(mcb_frame_encode_linux(UINT16_C(0xabcd), UINT16_C(0xef01),
                                  arena + 9, 20, arena + 3, 28, &frame_size) ==
           MCB_FRAME_OK);
    assert_encoded(arena + 3, frame_size, UINT16_C(0xabcd),
                   UINT16_C(0xef01), original, 20);
}

int main(void)
{
    test_empty_frame();
    test_maximum_frame_and_nonzero_trailer();
    test_truncation_lengths();
    test_invalid_prefix_and_lengths();
    test_capacity_failure_and_invalid_arguments();
    test_overlap_both_directions();
    test_misalignment_and_owned_independence();
    test_tx_empty_max_and_still_sized_bodies();
    test_tx_failures_preserve_outputs();
    test_tx_overlap_directions_and_misalignment();
    return 0;
}
