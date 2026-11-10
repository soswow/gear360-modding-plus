#include "mcb_frame.h"

/* The ARM check is a freestanding compile with no target libc headers. */
extern void *memmove(void *destination, const void *source, size_t length);

static uint16_t load_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      (uint16_t)((uint16_t)bytes[1] << 8));
}

static void store_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & UINT16_C(0xff));
    bytes[1] = (uint8_t)(value >> 8);
}

mcb_frame_status mcb_frame_copy(const uint8_t *wire,
                                size_t wire_size,
                                uint8_t *caller_storage,
                                size_t storage_capacity,
                                mcb_frame_message *out_message)
{
    uint16_t category;
    uint16_t attribute;
    size_t payload_length;

    if (wire == NULL || caller_storage == NULL || out_message == NULL) {
        return MCB_FRAME_INVALID_ARGUMENT;
    }
    if (wire_size < MCB_FRAME_MIN_SIZE || wire_size > MCB_FRAME_MAX_SIZE) {
        return MCB_FRAME_INVALID_SIZE;
    }
    if (wire[0] != UINT8_C(0x00) || wire[1] != UINT8_C(0x03)) {
        return MCB_FRAME_INVALID_PREFIX;
    }

    payload_length = (size_t)load_u16_le(wire + 6);
    category = load_u16_le(wire + 2);
    attribute = load_u16_le(wire + 4);
    if (payload_length > MCB_FRAME_MAX_PAYLOAD ||
        payload_length + MCB_FRAME_MIN_SIZE != wire_size) {
        return MCB_FRAME_INVALID_LENGTH;
    }
    if (storage_capacity < wire_size) {
        return MCB_FRAME_INSUFFICIENT_CAPACITY;
    }

    /* All validation is complete before either destination is mutated. */
    memmove(caller_storage, wire, wire_size);
    out_message->category = category;
    out_message->attribute = attribute;
    out_message->body = caller_storage + 8;
    out_message->body_length = payload_length;
    return MCB_FRAME_OK;
}

mcb_frame_status mcb_frame_encode_linux(uint16_t category,
                                        uint16_t attribute,
                                        const uint8_t *body,
                                        size_t body_length,
                                        uint8_t *caller_storage,
                                        size_t storage_capacity,
                                        size_t *out_frame_size)
{
    size_t frame_size;

    if (caller_storage == NULL || out_frame_size == NULL ||
        (body == NULL && body_length != 0)) {
        return MCB_FRAME_INVALID_ARGUMENT;
    }
    if (body_length > MCB_FRAME_MAX_PAYLOAD) {
        return MCB_FRAME_INVALID_SIZE;
    }

    frame_size = body_length + MCB_FRAME_TX_MIN_SIZE;
    if (storage_capacity < frame_size) {
        return MCB_FRAME_INSUFFICIENT_CAPACITY;
    }

    /* Move first so arbitrary body/destination overlap cannot corrupt input. */
    if (body_length != 0) {
        memmove(caller_storage + 6, body, body_length);
    }
    store_u16_le(caller_storage + 0, category);
    store_u16_le(caller_storage + 2, attribute);
    store_u16_le(caller_storage + 4, (uint16_t)body_length);
    caller_storage[6 + body_length] = UINT8_C(0x00);
    caller_storage[7 + body_length] = UINT8_C(0x00);
    *out_frame_size = frame_size;
    return MCB_FRAME_OK;
}
