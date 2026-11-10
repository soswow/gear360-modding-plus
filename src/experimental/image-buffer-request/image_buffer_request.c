#include "image_buffer_request.h"

static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & UINT32_C(0xff));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xff));
}

static uint32_t load_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

image_buffer_request_status image_buffer_request_encode_acquire_body(
    uint32_t requested_bytes,
    uint32_t bank,
    uint8_t *out_body,
    size_t body_capacity)
{
    if (out_body == NULL) {
        return IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT;
    }
    if (body_capacity < IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE) {
        return IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY;
    }
    if (requested_bytes == UINT32_C(0)) {
        return IMAGE_BUFFER_REQUEST_INVALID_SIZE;
    }
    if (requested_bytes > IMAGE_BUFFER_REQUEST_MAX_REQUESTED_BYTES) {
        return IMAGE_BUFFER_REQUEST_ROUNDING_OVERFLOW;
    }
    if (bank > IMAGE_BUFFER_REQUEST_BANK_MAX) {
        return IMAGE_BUFFER_REQUEST_UNSUPPORTED_BANK;
    }

    /*
     * Match the vendor request initialization: words 0, 1 and 4 start at
     * zero. The producer reads size/bank at words 2/3 and writes its manager
     * results at words 1/4. Wire word 0 is not the public mapped address.
     */
    put_u32_le(out_body + 0x00, UINT32_C(0));
    put_u32_le(out_body + 0x04, UINT32_C(0));
    put_u32_le(out_body + 0x08, requested_bytes);
    put_u32_le(out_body + 0x0c, bank);
    put_u32_le(out_body + 0x10, UINT32_C(0));
    return IMAGE_BUFFER_REQUEST_OK;
}

image_buffer_request_status image_buffer_request_encode_acquire(
    uint32_t requested_bytes,
    uint32_t bank,
    uint8_t *out_frame,
    size_t frame_capacity,
    size_t *out_frame_size)
{
    uint8_t body[IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE];
    size_t frame_size = 0;
    mcb_frame_status frame_status;
    image_buffer_request_status status;

    if (out_frame == NULL || out_frame_size == NULL) {
        return IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT;
    }

    status = image_buffer_request_encode_acquire_body(requested_bytes, bank,
                                                      body, sizeof(body));
    if (status != IMAGE_BUFFER_REQUEST_OK) {
        return status;
    }

    frame_status = mcb_frame_encode_linux(IMAGE_BUFFER_REQUEST_CATEGORY_ACQUIRE,
                                          IMAGE_BUFFER_REQUEST_ATTRIBUTE_ACQUIRE,
                                          body, sizeof(body),
                                          out_frame, frame_capacity,
                                          &frame_size);
    if (frame_status == MCB_FRAME_INSUFFICIENT_CAPACITY) {
        return IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY;
    }
    if (frame_status != MCB_FRAME_OK) {
        return IMAGE_BUFFER_REQUEST_ENCODE_FAILED;
    }

    *out_frame_size = frame_size;
    return IMAGE_BUFFER_REQUEST_OK;
}

image_buffer_request_status image_buffer_request_encode_release_body(
    uint32_t physical_address,
    uint8_t *out_body,
    size_t body_capacity)
{
    if (out_body == NULL) {
        return IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT;
    }
    if (body_capacity < IMAGE_BUFFER_REQUEST_RELEASE_BODY_SIZE) {
        return IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY;
    }
    if (physical_address == UINT32_C(0) ||
        physical_address == IMAGE_BUFFER_REQUEST_ADDRESS_SENTINEL) {
        return IMAGE_BUFFER_REQUEST_INVALID_ADDRESS;
    }

    put_u32_le(out_body + 0x00, physical_address);
    return IMAGE_BUFFER_REQUEST_OK;
}

image_buffer_request_status image_buffer_request_encode_release(
    uint32_t physical_address,
    uint8_t *out_frame,
    size_t frame_capacity,
    size_t *out_frame_size)
{
    uint8_t body[IMAGE_BUFFER_REQUEST_RELEASE_BODY_SIZE];
    size_t frame_size = 0;
    mcb_frame_status frame_status;
    image_buffer_request_status status;

    if (out_frame == NULL || out_frame_size == NULL) {
        return IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT;
    }

    status = image_buffer_request_encode_release_body(physical_address, body,
                                                      sizeof(body));
    if (status != IMAGE_BUFFER_REQUEST_OK) {
        return status;
    }

    frame_status = mcb_frame_encode_linux(IMAGE_BUFFER_REQUEST_CATEGORY_RELEASE,
                                          IMAGE_BUFFER_REQUEST_ATTRIBUTE_RELEASE,
                                          body, sizeof(body),
                                          out_frame, frame_capacity,
                                          &frame_size);
    if (frame_status == MCB_FRAME_INSUFFICIENT_CAPACITY) {
        return IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY;
    }
    if (frame_status != MCB_FRAME_OK) {
        return IMAGE_BUFFER_REQUEST_ENCODE_FAILED;
    }

    *out_frame_size = frame_size;
    return IMAGE_BUFFER_REQUEST_OK;
}

image_buffer_request_status image_buffer_request_decode_reply(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t *scratch,
    size_t scratch_capacity,
    image_buffer_request_reply *out_reply)
{
    mcb_frame_message message;
    mcb_frame_status frame_status;
    const uint8_t *body;
    image_buffer_request_reply reply;

    if (frame == NULL || scratch == NULL || out_reply == NULL) {
        return IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT;
    }
    if (scratch_capacity < frame_size) {
        return IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY;
    }

    /*
     * The envelope, its route prefix, its declared length and its trailer are
     * checked by the mcb-frame component.  The trailer bytes stay uninterpreted,
     * exactly as that component documents for received frames.
     */
    frame_status = mcb_frame_copy(frame, frame_size, scratch, scratch_capacity,
                                  &message);
    if (frame_status != MCB_FRAME_OK) {
        return IMAGE_BUFFER_REQUEST_MALFORMED_FRAME;
    }
    if (message.category != IMAGE_BUFFER_REQUEST_CATEGORY_REPLY) {
        return IMAGE_BUFFER_REQUEST_UNEXPECTED_CATEGORY;
    }
    if (message.attribute != IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY) {
        return IMAGE_BUFFER_REQUEST_UNEXPECTED_ATTRIBUTE;
    }
    if (message.body_length != IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE) {
        return IMAGE_BUFFER_REQUEST_UNEXPECTED_LENGTH;
    }

    body = message.body;
    reply.wire_word0 = load_u32_le(body + 0x00);
    reply.address_word = load_u32_le(body + 0x04);
    reply.requested_bytes = load_u32_le(body + 0x08);
    reply.bank = load_u32_le(body + 0x0c);
    reply.manager_query_word = load_u32_le(body + 0x10);

    *out_reply = reply;
    return IMAGE_BUFFER_REQUEST_OK;
}

int image_buffer_request_reply_is_refusal(const image_buffer_request_reply *reply)
{
    if (reply == NULL) {
        return 0;
    }
    return reply->address_word == UINT32_C(0) ? 1 : 0;
}
