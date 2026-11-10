#include "image_buffer_request.h"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    std::array<uint8_t, IMAGE_BUFFER_REQUEST_ACQUIRE_FRAME_SIZE> frame{};
    std::array<uint8_t, MCB_FRAME_MAX_SIZE> scratch{};
    std::array<uint8_t, IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE> body{};
    image_buffer_request_reply reply{};
    std::size_t frame_size = 0;

    assert(image_buffer_request_encode_acquire(UINT32_C(0x00100000),
                                               IMAGE_BUFFER_REQUEST_BANK_SECOND,
                                               frame.data(), frame.size(),
                                               &frame_size) ==
           IMAGE_BUFFER_REQUEST_OK);
    assert(frame_size == IMAGE_BUFFER_REQUEST_ACQUIRE_FRAME_SIZE);
    assert(frame[0] == 0x80 && frame[1] == 0x00);
    assert(frame[2] == 0x8c && frame[3] == 0x01);
    assert(frame[4] == 0x14 && frame[5] == 0x00);
    assert(frame[0x12] == 0x02); /* body word 3: the bank */

    assert(image_buffer_request_encode_acquire_body(UINT32_C(0x00100000),
                                                    IMAGE_BUFFER_REQUEST_BANK_FIRST,
                                                    body.data(), body.size()) ==
           IMAGE_BUFFER_REQUEST_OK);
    assert(body[0x08] == 0x00 && body[0x09] == 0x00 &&
           body[0x0a] == 0x10 && body[0x0b] == 0x00);
    assert(body[0x0c] == 0x01);

    /* The decode is callable from C++ with the same reply structure. */
    std::array<uint8_t, MCB_FRAME_MAX_SIZE> reply_frame{};
    reply_frame[0] = 0x00;
    reply_frame[1] = 0x03;
    reply_frame[2] = 0x81;
    reply_frame[3] = 0x00;
    reply_frame[4] = 0x8c;
    reply_frame[5] = 0x01;
    reply_frame[6] = 0x14;
    reply_frame[7] = 0x00;
    reply_frame[8 + 0x04] = 0x00; /* zero address: a refusal */
    assert(image_buffer_request_decode_reply(reply_frame.data(),
                                             30u, /* 8 header + 20 body + 2 trailer */
                                             scratch.data(), scratch.size(),
                                             &reply) == IMAGE_BUFFER_REQUEST_OK);
    assert(reply.address_word == 0u);
    assert(image_buffer_request_reply_is_refusal(&reply) == 1);

    /* The enum values are visible from C++ with their C names. */
    assert(IMAGE_BUFFER_REQUEST_OK == 0);
    assert(IMAGE_BUFFER_REQUEST_UNEXPECTED_CATEGORY ==
           IMAGE_BUFFER_REQUEST_UNEXPECTED_CATEGORY);
    return 0;
}
