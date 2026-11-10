#ifndef MCB_FRAME_H
#define MCB_FRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The caller passes the bytes after the four-byte outer header consumed by
 * the kernel.  The inner representation is:
 *
 *   00 03 | category u16 LE | attribute u16 LE | payload length u16 LE |
 *   payload bytes | two uninterpreted trailer bytes
 */
#define MCB_FRAME_MIN_SIZE ((size_t)10u)
#define MCB_FRAME_MAX_SIZE ((size_t)390u)
#define MCB_FRAME_MAX_PAYLOAD ((size_t)380u)

/* Outgoing Linux MCB frame bounds: 6-byte header, body, 2 zero trailer bytes. */
#define MCB_FRAME_TX_MIN_SIZE ((size_t)8u)
#define MCB_FRAME_TX_MAX_SIZE ((size_t)388u)

typedef enum mcb_frame_status {
    MCB_FRAME_OK = 0,
    MCB_FRAME_INVALID_ARGUMENT = 1,
    MCB_FRAME_INVALID_SIZE = 2,
    MCB_FRAME_INVALID_PREFIX = 3,
    MCB_FRAME_INVALID_LENGTH = 4,
    MCB_FRAME_INSUFFICIENT_CAPACITY = 5
} mcb_frame_status;

typedef struct mcb_frame_message {
    uint16_t category;
    uint16_t attribute;
    const uint8_t *body;
    size_t body_length;
} mcb_frame_message;

/*
 * Validate and copy one complete inner frame into caller_storage.
 *
 * On success, caller_storage contains the complete frame, including its two
 * trailer bytes.  out_message->body points into caller_storage at byte 8 and
 * remains valid while that storage is retained and unmodified by the caller.
 * Category and attribute are copied as little-endian u16 values.  Unknown
 * values are accepted because this function performs syntax checks only.
 *
 * wire and caller_storage may overlap; the copy uses memmove.  The metadata
 * object out_message must not overlap wire or caller_storage.  This is a
 * caller precondition, so the function does not attempt to detect arbitrary
 * pointer overlap.  No argument or output is changed on failure.
 */
mcb_frame_status mcb_frame_copy(const uint8_t *wire,
                                size_t wire_size,
                                uint8_t *caller_storage,
                                size_t storage_capacity,
                                mcb_frame_message *out_message);

/*
 * Encode one outgoing Linux MCB frame into caller_storage:
 * category u16 LE | attribute u16 LE | body length u16 LE | body | 00 00.
 * The 00 03 receive route prefix is not part of this outgoing buffer; the
 * kernel supplies its outer framing. Unknown category and attribute values
 * are accepted. A NULL body is valid only when body_length is zero.
 *
 * The encoded size is body_length + 8 and body_length may not exceed the
 * shared conservative maximum of 380 bytes. caller_storage may be misaligned
 * and may overlap body in any direction, including exact in-place use. The
 * body is moved before header and trailer bytes are written. out_frame_size
 * must not overlap body or caller_storage; this is a caller precondition.
 * No argument, destination byte, or output size is changed on failure.
 */
mcb_frame_status mcb_frame_encode_linux(uint16_t category,
                                        uint16_t attribute,
                                        const uint8_t *body,
                                        size_t body_length,
                                        uint8_t *caller_storage,
                                        size_t storage_capacity,
                                        size_t *out_frame_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MCB_FRAME_H */
