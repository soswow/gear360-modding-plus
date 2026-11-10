#ifndef STILL_REQUEST_H
#define STILL_REQUEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Observed command variants. The legacy entry points retain 0x01010001.
 * A saved 22 October 2026 camera run submitted 0x01010016 with Cap=Max=1.
 * The firmware's STL command-name table identifies the low byte of the
 * command as the E_CAP_CMD_STL_ index: 0x01010001 is STL_NORMAL (index 1)
 * and 0x01010016 is STL_LOCK3A_NORMAL (index 22), the mm capture type 21
 * (LOCK3A_CAPTURE) that FUN_00042e18 maps onto it. The historical
 * "device shutter / shared path" name for 0x01010016 mislabels the command;
 * it is a lock-3A-then-capture still, not a shutter mechanism. See
 * next-edge/still-path/CAPTURE-TYPE-SELECTION.md.
 * Its RTOS receiver/controller-record path is source-identified; supported
 * count ranges, live execution and completion remain unproved.
 * This component only encodes bytes and invokes a caller-supplied submitter.
 */
#define STILL_REQUEST_COMMAND_PLAIN_PATH UINT32_C(0x01010001)
#define STILL_REQUEST_COMMAND_SHARED_PATH UINT32_C(0x01010016)

/* The command the plain entry points use.  Unchanged from the reviewed
 * component; a caller that wants the camera's own word asks for it
 * explicitly. */
#define STILL_REQUEST_COMMAND STILL_REQUEST_COMMAND_PLAIN_PATH

#define STILL_REQUEST_CAP_DEFAULT UINT32_C(1)
#define STILL_REQUEST_MAX_DEFAULT UINT32_C(1)

#define STILL_REQUEST_PAYLOAD_SIZE ((size_t)48)

typedef struct still_request_storage_info {
    /* Values prepared by the caller's camera storage accounting. */
    uint32_t remaining_count;
    /* Available bytes after the caller's camera-specific storage reserve. */
    uint64_t free_space_bytes;
} still_request_storage_info;

/*
 * Optional request fields.  A zero field means "use the built-in default", so
 * an all-zero structure encodes the legacy request. Non-default Cap/Max
 * values are wire inputs, not a verified multi-capture capability. The
 * max >= cap check is a local policy, not a recovered RTOS requirement.
 */
typedef struct still_request_options {
    /* Caller-supplied command word; 0 selects STILL_REQUEST_COMMAND. */
    uint32_t command;
    /* Cap word; 0 selects STILL_REQUEST_CAP_DEFAULT. */
    uint32_t cap;
    /* Max word; 0 selects STILL_REQUEST_MAX_DEFAULT. */
    uint32_t max;
} still_request_options;

/* Selects the observed command 0x01010016 with Cap=Max=1.
 * A null pointer is ignored. */
void still_request_options_device_shutter(still_request_options *options);

typedef enum still_request_status {
    STILL_REQUEST_INVALID_ARGUMENT = 0,
    /* Transport accepted the request; no frame completion is implied. */
    STILL_REQUEST_ACCEPTED = 1,
    /* The command may still be queued. Do not retry automatically. */
    STILL_REQUEST_INDETERMINATE = 2
} still_request_status;

typedef struct still_request_result {
    still_request_status status;
    int32_t vendor_return;
} still_request_result;

/* Compatible with the caller's initialized CaptureStillImage export. */
typedef int32_t (*still_request_submit_fn)(uint32_t command,
                                          void *buffer,
                                          uint32_t length);

/* Returns 0 after encoding exactly 48 bytes, or -1 for invalid arguments.
 * Invalid arguments leave output untouched. */
int still_request_encode(
    uint8_t *output,
    size_t output_capacity,
    const still_request_storage_info *storage);

/* As still_request_encode, with an explicit command word, Cap and Max.
 * A null options pointer selects the built-in defaults, which are the same
 * bytes still_request_encode writes.  Returns -1 for invalid arguments and
 * for a Max below Cap; in both cases output is left untouched. */
int still_request_encode_ex(
    uint8_t *output,
    size_t output_capacity,
    const still_request_storage_info *storage,
    const still_request_options *options);

/* submit must synchronously consume or copy all 48 bytes before returning.
 * The caller supplies its already initialized 32-bit ARM vendor context and
 * owns context setup and submission serialization; this function does no init.
 * Mode, liveview and 3A/capture preparation also remain caller-owned.
 * Payload storage is at least 4-byte aligned. Valid input invokes submit once.
 * The command word passed to submit is the effective one. */
still_request_result still_request_dispatch(
    still_request_submit_fn submit,
    const still_request_storage_info *storage);

/* As still_request_dispatch, with an explicit command word, Cap and Max.
 * A null options pointer selects the built-in defaults. */
still_request_result still_request_dispatch_ex(
    still_request_submit_fn submit,
    const still_request_storage_info *storage,
    const still_request_options *options);

#ifdef __cplusplus
}
#endif

#endif
