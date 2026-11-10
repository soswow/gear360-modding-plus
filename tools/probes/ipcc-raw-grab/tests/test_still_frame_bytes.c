/* Independent golden vector from the reviewed stock still's 48-byte body.
 * No device access: this checks the encoder integration, not execution.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mcb_frame.h"
#include "still_request.h"

int main(void)
{
    const uint8_t expected_body[48] = {
        0x16,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,
        0x01,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00, 0x90,0x09,0x00,0x00,
        0x00,0x00,0xad,0xbd, 0x01,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00
    };
    const uint8_t header[6] = {0x83,0x00,0x01,0x02,0x30,0x00};
    still_request_options options = {STILL_REQUEST_COMMAND_SHARED_PATH, 1, 1};
    still_request_storage_info storage = {2448, UINT64_C(7477198848)};
    uint8_t body[48], frame[56];
    size_t length = 0;

    assert(still_request_encode_ex(body, sizeof(body), &storage, &options) == 0);
    assert(memcmp(body, expected_body, sizeof(body)) == 0);
    assert(mcb_frame_encode_linux(0x83, 0x0201, body, sizeof(body),
                                  frame, sizeof(frame), &length) == MCB_FRAME_OK);
    assert(length == sizeof(frame));
    assert(memcmp(frame, header, sizeof(header)) == 0);
    assert(memcmp(frame + 6, expected_body, sizeof(body)) == 0);
    assert(frame[54] == 0 && frame[55] == 0);
    puts("reviewed still vector: PASS");
    return 0;
}
