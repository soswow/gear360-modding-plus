#include "still_request.h"

#include <array>
#include <cassert>

static unsigned cpp_calls;
static uint32_t expected_command = STILL_REQUEST_COMMAND;

static int32_t cpp_submit(uint32_t command, void *buffer, uint32_t length)
{
    ++cpp_calls;
    assert(command == expected_command);
    assert(length == STILL_REQUEST_PAYLOAD_SIZE);
    assert((reinterpret_cast<uintptr_t>(buffer) % 4u) == 0u);
    assert(static_cast<uint8_t *>(buffer)[0x14] == 3);
    return 0;
}

int main()
{
    const still_request_storage_info storage{3, UINT64_C(0x100000002)};
    std::array<uint8_t, STILL_REQUEST_PAYLOAD_SIZE> payload{};
    assert(still_request_encode(payload.data(), payload.size(), &storage) == 0);
    assert(payload[0] == 0x01 && payload[1] == 0x00 &&
           payload[2] == 0x01 && payload[3] == 0x01);
    assert(payload[0x18] == 0x02 && payload[0x1c] == 0x01);
    const still_request_result result = still_request_dispatch(cpp_submit,
                                                               &storage);
    assert(result.status == STILL_REQUEST_ACCEPTED);
    assert(result.vendor_return == 0);
    assert(cpp_calls == 1);

    still_request_options options{};
    still_request_options_device_shutter(&options);
    expected_command = STILL_REQUEST_COMMAND_SHARED_PATH;
    assert(still_request_encode_ex(payload.data(), payload.size(), &storage,
                                    &options) == 0);
    assert(payload[0] == 0x16);
    const still_request_result extended = still_request_dispatch_ex(
        cpp_submit, &storage, &options);
    assert(extended.status == STILL_REQUEST_ACCEPTED);
    assert(cpp_calls == 2);
    return 0;
}
