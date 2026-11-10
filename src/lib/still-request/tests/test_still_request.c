#include "still_request.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static const uint8_t expected[48] = {
    0x01, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
    0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* The same request with the camera's own command word. */
static const uint8_t expected_shared_path[48] = {
    0x16, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
    0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Cap 3, Max 256: the command word is the plain path. */
static const uint8_t expected_cap3_max256[48] = {
    0x01, 0x00, 0x01, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
    0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned invocation_count;
static uint32_t received_command;
static uint32_t received_length;
static uint8_t received_payload[48];
static int32_t configured_vendor_return;

static int32_t fake_submit(uint32_t command, void *buffer, uint32_t length)
{
    ++invocation_count;
    received_command = command;
    received_length = length;
    assert(length == sizeof(received_payload));
    assert(((uintptr_t)buffer % 4u) == 0u);
    memcpy(received_payload, buffer, sizeof(received_payload));
    return configured_vendor_return;
}

static void test_encoder(void)
{
    const still_request_storage_info storage = {
        UINT32_C(0x12345678), UINT64_C(0x0123456789abcdef)
    };
    uint8_t output[48];

    memset(output, 0xa5, sizeof(output));
    assert(still_request_encode(output, sizeof(output), &storage) == 0);
    assert(memcmp(output, expected, sizeof(expected)) == 0);
}

static void test_invalid_encoder_arguments(void)
{
    const still_request_storage_info storage = {7, UINT64_C(9)};
    uint8_t output[48];
    uint8_t original[48];

    memset(output, 0x5a, sizeof(output));
    memcpy(original, output, sizeof(output));
    assert(still_request_encode(output, sizeof(output) - 1, &storage) == -1);
    assert(memcmp(output, original, sizeof(original)) == 0);
    assert(still_request_encode(output, sizeof(output), NULL) == -1);
    assert(memcmp(output, original, sizeof(original)) == 0);
    assert(still_request_encode(NULL, sizeof(output), &storage) == -1);
    assert(memcmp(output, original, sizeof(original)) == 0);
}

static void test_encoder_options(void)
{
    const still_request_storage_info storage = {
        UINT32_C(0x12345678), UINT64_C(0x0123456789abcdef)
    };
    still_request_options options;
    uint8_t output[48];

    /* A null options pointer keeps the reviewed bytes. */
    memset(output, 0xa5, sizeof(output));
    assert(still_request_encode_ex(output, sizeof(output), &storage, NULL) == 0);
    assert(memcmp(output, expected, sizeof(expected)) == 0);

    /* An all-zero options structure is the same request. */
    memset(&options, 0, sizeof(options));
    memset(output, 0xa5, sizeof(output));
    assert(still_request_encode_ex(output, sizeof(output), &storage, &options) == 0);
    assert(memcmp(output, expected, sizeof(expected)) == 0);

    /* The camera's own command word changes nothing but those bytes. */
    still_request_options_device_shutter(&options);
    assert(options.command == STILL_REQUEST_COMMAND_SHARED_PATH);
    assert(options.cap == STILL_REQUEST_CAP_DEFAULT);
    assert(options.max == STILL_REQUEST_MAX_DEFAULT);
    memset(output, 0xa5, sizeof(output));
    assert(still_request_encode_ex(output, sizeof(output), &storage, &options) == 0);
    assert(memcmp(output, expected_shared_path, sizeof(expected_shared_path)) == 0);

    /* Explicit Cap and Max. */
    options.command = STILL_REQUEST_COMMAND_PLAIN_PATH;
    options.cap = 3u;
    options.max = 256u;
    memset(output, 0xa5, sizeof(output));
    assert(still_request_encode_ex(output, sizeof(output), &storage, &options) == 0);
    assert(memcmp(output, expected_cap3_max256, sizeof(expected_cap3_max256)) == 0);

    /* A Max below Cap is refused and leaves the output untouched. */
    options.cap = 4u;
    options.max = 3u;
    memset(output, 0x5a, sizeof(output));
    {
        uint8_t original[48];
        memcpy(original, output, sizeof(original));
        assert(still_request_encode_ex(output, sizeof(output), &storage, &options) == -1);
        assert(memcmp(output, original, sizeof(original)) == 0);
    }

    /* Invalid arguments still refuse before anything is written. */
    assert(still_request_encode_ex(output, sizeof(output) - 1, &storage, &options) == -1);
    assert(still_request_encode_ex(output, sizeof(output), NULL, &options) == -1);
    assert(still_request_encode_ex(NULL, sizeof(output), &storage, &options) == -1);
}

static void test_dispatch(void)
{
    const still_request_storage_info storage = {
        UINT32_C(0x12345678), UINT64_C(0x0123456789abcdef)
    };
    still_request_result result;

    invocation_count = 0;
    configured_vendor_return = 0;
    result = still_request_dispatch(fake_submit, &storage);
    assert(result.status == STILL_REQUEST_ACCEPTED);
    assert(result.vendor_return == 0);
    assert(invocation_count == 1);
    assert(received_command == STILL_REQUEST_COMMAND);
    assert(received_length == 48);
    assert(memcmp(received_payload, expected, sizeof(expected)) == 0);

    invocation_count = 0;
    configured_vendor_return = -37;
    result = still_request_dispatch(fake_submit, &storage);
    assert(result.status == STILL_REQUEST_INDETERMINATE);
    assert(result.vendor_return == -37);
    assert(invocation_count == 1);

    invocation_count = 0;
    result = still_request_dispatch(NULL, &storage);
    assert(result.status == STILL_REQUEST_INVALID_ARGUMENT);
    assert(result.vendor_return == 0);
    assert(invocation_count == 0);
    result = still_request_dispatch(fake_submit, NULL);
    assert(result.status == STILL_REQUEST_INVALID_ARGUMENT);
    assert(invocation_count == 0);
}

static void test_dispatch_options(void)
{
    const still_request_storage_info storage = {
        UINT32_C(0x12345678), UINT64_C(0x0123456789abcdef)
    };
    still_request_options options;
    still_request_result result;

    /* The effective command word is the one handed to the vendor call. */
    still_request_options_device_shutter(&options);
    invocation_count = 0;
    configured_vendor_return = 0;
    result = still_request_dispatch_ex(fake_submit, &storage, &options);
    assert(result.status == STILL_REQUEST_ACCEPTED);
    assert(invocation_count == 1);
    assert(received_command == STILL_REQUEST_COMMAND_SHARED_PATH);
    assert(received_length == 48);
    assert(memcmp(received_payload, expected_shared_path, sizeof(expected_shared_path)) == 0);

    /* A refused request never reaches the vendor call. */
    options.cap = 4u;
    options.max = 3u;
    invocation_count = 0;
    result = still_request_dispatch_ex(fake_submit, &storage, &options);
    assert(result.status == STILL_REQUEST_INVALID_ARGUMENT);
    assert(result.vendor_return == 0);
    assert(invocation_count == 0);

    /* A null options pointer keeps the reviewed behaviour. */
    invocation_count = 0;
    result = still_request_dispatch_ex(fake_submit, &storage, NULL);
    assert(result.status == STILL_REQUEST_ACCEPTED);
    assert(invocation_count == 1);
    assert(received_command == STILL_REQUEST_COMMAND);
    assert(memcmp(received_payload, expected, sizeof(expected)) == 0);

    /* Invalid arguments are refused before the vendor call. */
    invocation_count = 0;
    result = still_request_dispatch_ex(NULL, &storage, &options);
    assert(result.status == STILL_REQUEST_INVALID_ARGUMENT);
    assert(invocation_count == 0);
    result = still_request_dispatch_ex(fake_submit, NULL, &options);
    assert(result.status == STILL_REQUEST_INVALID_ARGUMENT);
    assert(invocation_count == 0);
}

int main(void)
{
    test_encoder();
    test_invalid_encoder_arguments();
    test_encoder_options();
    test_dispatch();
    test_dispatch_options();
    return 0;
}
