#include "still_request.h"

static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & UINT32_C(0xff));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xff));
}

static void put_u64_le(uint8_t *destination, uint64_t value)
{
    size_t index;
    for (index = 0; index < 8; ++index) {
        destination[index] = (uint8_t)(value & UINT64_C(0xff));
        value >>= 8;
    }
}

static void resolve_options(const still_request_options *options,
                            uint32_t *command,
                            uint32_t *cap,
                            uint32_t *max)
{
    *command = STILL_REQUEST_COMMAND;
    *cap = STILL_REQUEST_CAP_DEFAULT;
    *max = STILL_REQUEST_MAX_DEFAULT;
    if (options == NULL) {
        return;
    }
    if (options->command != 0u) {
        *command = options->command;
    }
    if (options->cap != 0u) {
        *cap = options->cap;
    }
    if (options->max != 0u) {
        *max = options->max;
    }
}

void still_request_options_device_shutter(still_request_options *options)
{
    if (options == NULL) {
        return;
    }
    options->command = STILL_REQUEST_COMMAND_SHARED_PATH;
    options->cap = STILL_REQUEST_CAP_DEFAULT;
    options->max = STILL_REQUEST_MAX_DEFAULT;
}

int still_request_encode_ex(
    uint8_t *output,
    size_t output_capacity,
    const still_request_storage_info *storage,
    const still_request_options *options)
{
    uint8_t encoded[STILL_REQUEST_PAYLOAD_SIZE] = {0};
    uint32_t command;
    uint32_t cap;
    uint32_t max;
    size_t index;

    if (output == NULL || storage == NULL ||
        output_capacity < STILL_REQUEST_PAYLOAD_SIZE) {
        return -1;
    }

    resolve_options(options, &command, &cap, &max);
    if (max < cap) {
        return -1;
    }

    put_u32_le(encoded + 0x00, command);
    put_u32_le(encoded + 0x04, cap);
    put_u32_le(encoded + 0x08, max);
    put_u32_le(encoded + 0x14, storage->remaining_count);
    put_u64_le(encoded + 0x18, storage->free_space_bytes);
    for (index = 0; index < sizeof(encoded); ++index) {
        output[index] = encoded[index];
    }
    return 0;
}

int still_request_encode(
    uint8_t *output,
    size_t output_capacity,
    const still_request_storage_info *storage)
{
    return still_request_encode_ex(output, output_capacity, storage, NULL);
}

still_request_result still_request_dispatch_ex(
    still_request_submit_fn submit,
    const still_request_storage_info *storage,
    const still_request_options *options)
{
    uint32_t payload_words[STILL_REQUEST_PAYLOAD_SIZE / sizeof(uint32_t)];
    uint8_t *payload = (uint8_t *)payload_words;
    still_request_result result;
    uint32_t command;
    uint32_t cap;
    uint32_t max;

    result.status = STILL_REQUEST_INVALID_ARGUMENT;
    result.vendor_return = 0;
    if (submit == NULL || storage == NULL) {
        return result;
    }
    resolve_options(options, &command, &cap, &max);
    if (still_request_encode_ex(payload,
                                sizeof(payload_words),
                                storage,
                                options) != 0) {
        return result;
    }

    result.vendor_return = submit(command,
                                  payload,
                                  (uint32_t)STILL_REQUEST_PAYLOAD_SIZE);
    result.status = result.vendor_return == 0
                        ? STILL_REQUEST_ACCEPTED
                        : STILL_REQUEST_INDETERMINATE;
    return result;
}

still_request_result still_request_dispatch(
    still_request_submit_fn submit,
    const still_request_storage_info *storage)
{
    return still_request_dispatch_ex(submit, storage, NULL);
}
