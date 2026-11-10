#include "encoded_set.h"

#include <array>
#include <cassert>
#include <cstdint>

namespace
{
/* Synthetic bytes for the linkage test; not camera or firmware data. */
void put_u32_le(std::uint8_t *destination, std::uint32_t value)
{
    destination[0] = static_cast<std::uint8_t>(value & 0xffu);
    destination[1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    destination[2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    destination[3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}
} // namespace

int main()
{
    std::array<std::uint8_t, ENCODED_SET_METADATA_SIZE> constituent{};
    encoded_set_metadata decoded{};

    assert(ENCODED_SET_METADATA_SIZE == 0x228u);
    assert(ENCODED_SET_IMAGE_COUNT == 4u);

    put_u32_le(constituent.data() + ENCODED_SET_TYPE_OFFSET, 0x0a0b0c0du);
    const std::size_t block = ENCODED_SET_IMAGE_MAIN_OFFSET;
    constituent[block + ENCODED_SET_ADDRESS_TYPE_OFFSET] = 0x42;
    put_u32_le(constituent.data() + block + ENCODED_SET_FIRST_PHYSICAL_OFFSET,
               0x11121314u);
    put_u32_le(constituent.data() + block + ENCODED_SET_FIRST_VIRTUAL_OFFSET,
               0x21222324u);
    put_u32_le(constituent.data() + block + ENCODED_SET_BUFFER_SIZE_OFFSET,
               0x31323334u);

    assert(encoded_set_decode(constituent.data(), constituent.size(),
                               &decoded) == ENCODED_SET_OK);
    assert(decoded.set_type == 0x0a0b0c0du);
    assert(decoded.images[ENCODED_SET_IMAGE_MAIN].address_type == 0x42);
    assert(decoded.images[ENCODED_SET_IMAGE_MAIN].first_physical_word ==
           0x11121314u);
    assert(decoded.images[ENCODED_SET_IMAGE_MAIN].first_virtual_word ==
           0x21222324u);
    assert(decoded.images[ENCODED_SET_IMAGE_MAIN].buffer_size_word ==
           0x31323334u);

    /* The status names are visible from C++ with their C names. */
    assert(ENCODED_SET_OK == 0);
    assert(encoded_set_decode(constituent.data(), 4u, &decoded) ==
           ENCODED_SET_INVALID_LENGTH);
    return 0;
}
