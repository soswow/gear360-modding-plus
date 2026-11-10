#include "mcb_frame.h"
#include "mcb_ring.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

int main()
{
    const std::array<std::uint8_t, MCB_FRAME_MIN_SIZE> frame = {
        0x00, 0x03, 0x34, 0x12, 0x78, 0x56, 0x00, 0x00,
        0xa5, 0x5a
    };
    std::array<std::uint8_t, MCB_FRAME_MIN_SIZE> storage{};
    mcb_frame_message message{};

    assert(mcb_frame_copy(frame.data(), frame.size(), storage.data(),
                          storage.size(), &message) == MCB_FRAME_OK);
    assert(message.category == 0x1234);
    assert(message.attribute == 0x5678);
    assert(message.body == storage.data() + 8);
    assert(message.body_length == 0);
    assert(storage[8] == 0xa5 && storage[9] == 0x5a);

    const std::array<std::uint8_t, 2> body = {0xab, 0xcd};
    std::array<std::uint8_t, MCB_FRAME_TX_MIN_SIZE + 2> encoded{};
    std::size_t encoded_size = 0;
    assert(mcb_frame_encode_linux(0x1234, 0x5678, body.data(), body.size(),
                                 encoded.data(), encoded.size(),
                                 &encoded_size) == MCB_FRAME_OK);
    const std::array<std::uint8_t, MCB_FRAME_TX_MIN_SIZE + 2> expected = {
        0x34, 0x12, 0x78, 0x56, 0x02, 0x00, 0xab, 0xcd, 0x00, 0x00
    };
    assert(encoded_size == encoded.size());
    assert(encoded == expected);

    mcb_ring_admission plan{};
    assert(mcb_ring_plan_admission(395, 110, 10, 284, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 290);
    assert(plan.start_index == 110);
    assert(plan.first_length == 285);
    assert(plan.second_length == 5);
    assert(plan.new_producer == 5);
    assert(mcb_ring_plan_admission(395, 381, 0, 8, &plan) ==
           MCB_RING_ADMISSION_INSUFFICIENT_SPACE);
    return 0;
}
