#ifndef IMAGE_BUFFER_REQUEST_H
#define IMAGE_BUFFER_REQUEST_H

#include "mcb_frame.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure image-buffer request/reply component.
 *
 * It encodes one acquire request, one release request and decodes one reply
 * body.  It performs no device I/O, no allocation, no transport, no retry and
 * no target-memory I/O.  The caller owns every buffer.
 *
 * Source facts (all `status: accepted` in ../facts/contracts/raw-buffer.yaml unless noted):
 *
 *   raw.image-buffer-client-wire
 *     The pinned Linux RequestImageBufferMemory and Ex initialize five u32
 *     words, put the requested bytes in word 2 and, for Ex, the bank argument
 *     in word 3.  GetCaptureData sends category 0x80, attribute 0x018c and all
 *     twenty bytes.  Release passes the public address through the inverse
 *     converter and sends it with attribute 0x018d.
 *
 *   raw.image-buffer-request-producer
 *     The RTOS handler reads the byte count at body + 8 and the bank at
 *     body + 0x0c, writes the allocation result to body + 4 and the manager
 *     query to body + 0x10, then answers with category 0x81, attribute 0x018c
 *     and a fixed length of 20.
 *
 *   raw.image-buffer-release-dispatch
 *     The category 0x81 attribute 0x018d entry admits a 4-byte body and passes
 *     the supplied u32 address to the manager.  No reply is constructed.
 *
 * The word 0 and word 4 request fields are zero in the request.  Their meaning
 * on the wire is not established, so this component writes zeros and treats the
 * reply's word 0 and word 4 as opaque words.
 */

/* Outgoing Linux MCB frame identifiers. */
#define IMAGE_BUFFER_REQUEST_CATEGORY_ACQUIRE UINT16_C(0x0080)
#define IMAGE_BUFFER_REQUEST_ATTRIBUTE_ACQUIRE UINT16_C(0x018c)
#define IMAGE_BUFFER_REQUEST_CATEGORY_REPLY UINT16_C(0x0081)
#define IMAGE_BUFFER_REQUEST_ATTRIBUTE_REPLY UINT16_C(0x018c)
#define IMAGE_BUFFER_REQUEST_CATEGORY_RELEASE UINT16_C(0x0081)
#define IMAGE_BUFFER_REQUEST_ATTRIBUTE_RELEASE UINT16_C(0x018d)

/* Body sizes in bytes.  Both bodies are little-endian u32 words. */
#define IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE ((size_t)20u)
#define IMAGE_BUFFER_REQUEST_RELEASE_BODY_SIZE ((size_t)4u)

/* Complete outgoing frame sizes: body plus the mcb-frame TX envelope. */
#define IMAGE_BUFFER_REQUEST_ACQUIRE_FRAME_SIZE \
    (IMAGE_BUFFER_REQUEST_ACQUIRE_BODY_SIZE + MCB_FRAME_TX_MIN_SIZE)
#define IMAGE_BUFFER_REQUEST_RELEASE_FRAME_SIZE \
    (IMAGE_BUFFER_REQUEST_RELEASE_BODY_SIZE + MCB_FRAME_TX_MIN_SIZE)

/*
 * Bank selector values observed in the pinned Linux client.  The native
 * producer maps bank 1 and bank 2 to one manager slot and every other value
 * to another slot whose interval semantics are not established, so this
 * component exposes only the three known selectors and refuses the rest.
 */
#define IMAGE_BUFFER_REQUEST_BANK_DEFAULT UINT32_C(0)
#define IMAGE_BUFFER_REQUEST_BANK_FIRST UINT32_C(1)
#define IMAGE_BUFFER_REQUEST_BANK_SECOND UINT32_C(2)
#define IMAGE_BUFFER_REQUEST_BANK_MAX UINT32_C(2)

/*
 * The native manager counts free space in 16 KiB units.  A request size that
 * the native side rounds up must not wrap a u32, so the rounding addition
 * (requested_bytes + 0x3fff) has to fit in 32 bits.  The largest accepted
 * requested size is therefore 0xffffc000 bytes.
 */
#define IMAGE_BUFFER_REQUEST_NATIVE_ROUND_UNIT UINT32_C(0x4000)
#define IMAGE_BUFFER_REQUEST_NATIVE_ROUND_MASK UINT32_C(0x3fff)
#define IMAGE_BUFFER_REQUEST_MAX_REQUESTED_BYTES \
    (UINT32_MAX - IMAGE_BUFFER_REQUEST_NATIVE_ROUND_MASK)

/*
 * The producer replaces the no-allocation sentinel 0xbfffffff with zero in
 * the reply, so a zero address means "no allocation result" and the sentinel
 * itself is not a releasable address.
 */
#define IMAGE_BUFFER_REQUEST_ADDRESS_SENTINEL UINT32_C(0xbfffffff)

typedef enum image_buffer_request_status {
    IMAGE_BUFFER_REQUEST_OK = 0,
    /* A null pointer or a missing output object. */
    IMAGE_BUFFER_REQUEST_INVALID_ARGUMENT = 1,
    /* The caller buffer is smaller than the result needs. */
    IMAGE_BUFFER_REQUEST_INSUFFICIENT_CAPACITY = 2,
    /* The requested size is zero. */
    IMAGE_BUFFER_REQUEST_INVALID_SIZE = 3,
    /* The requested size would wrap when the native side rounds it up. */
    IMAGE_BUFFER_REQUEST_ROUNDING_OVERFLOW = 4,
    /* The bank selector is not one of the three known values. */
    IMAGE_BUFFER_REQUEST_UNSUPPORTED_BANK = 5,
    /* The release address is zero or the no-allocation sentinel. */
    IMAGE_BUFFER_REQUEST_INVALID_ADDRESS = 6,
    /* The reply envelope is not a complete, well formed mcb-frame. */
    IMAGE_BUFFER_REQUEST_MALFORMED_FRAME = 7,
    /* The reply category is not 0x81. */
    IMAGE_BUFFER_REQUEST_UNEXPECTED_CATEGORY = 8,
    /* The reply attribute is not 0x018c. */
    IMAGE_BUFFER_REQUEST_UNEXPECTED_ATTRIBUTE = 9,
    /* The reply body is not exactly 20 bytes. */
    IMAGE_BUFFER_REQUEST_UNEXPECTED_LENGTH = 10,
    /* The mcb-frame encoder refused the body for another reason. */
    IMAGE_BUFFER_REQUEST_ENCODE_FAILED = 11
} image_buffer_request_status;

/*
 * The five words of a decoded reply body.  Every field is the raw wire word
 * at the same offset.  The source-bound uses of the words are described below; decoding does not
 * establish a remote allocation or ownership.
 *
 *   wire_word0        word 0, offset 0x00.  The pinned Linux client reads the
 *                     allocation result from word 1 and converts it into its
 *                     public word 0, so this is not the public return value.
 *   address_word      word 1, offset 0x04.  The allocation result written by
 *                     the RTOS producer.  A nonzero value is not established
 *                     to be an owned allocation, and the manager behaviour
 *                     behind the release slot is not proven.
 *   requested_bytes   word 2, offset 0x08.  Echo of the request.  An echoed
 *                     size does not establish which request this answers.
 *   bank              word 3, offset 0x0c.  Echo of the request.  An echoed
 *                     bank does not establish which request this answers.
 *   manager_query_word
 *                     word 4, offset 0x10.  The manager free-space query.  Under
 *                     the conditional manager binding it is the largest
 *                     contiguous free run in bytes counted in 16 KiB units; it
 *                     is not a pool size, an image stride or a geometry.
 */
typedef struct image_buffer_request_reply {
    uint32_t wire_word0;
    uint32_t address_word;
    uint32_t requested_bytes;
    uint32_t bank;
    uint32_t manager_query_word;
} image_buffer_request_reply;

/*
 * Encode the 20-byte acquire body: five little-endian u32 words
 * [0, 0, requested_bytes, bank, 0].
 *
 * Checks are applied in this order: zero size, rounding overflow, bank.  A
 * zero requested size, a size above 0xffffc000 and a bank above 2 are refused.
 * No image geometry, lease or pool limit is imposed.  On failure the body is
 * left untouched.
 */
image_buffer_request_status image_buffer_request_encode_acquire_body(
    uint32_t requested_bytes,
    uint32_t bank,
    uint8_t *out_body,
    size_t body_capacity);

/*
 * Encode the complete outgoing acquire frame: category 0x80, attribute
 * 0x018c, the 20-byte body and the two zero trailer bytes of the mcb-frame
 * TX envelope.  The result is 28 bytes.
 *
 * out_frame_size receives the encoded size and is left untouched on failure.
 * It must not overlap out_frame.
 */
image_buffer_request_status image_buffer_request_encode_acquire(
    uint32_t requested_bytes,
    uint32_t bank,
    uint8_t *out_frame,
    size_t frame_capacity,
    size_t *out_frame_size);

/*
 * Encode the 4-byte release body: one little-endian u32 physical address.
 *
 * A zero address and the sentinel 0xbfffffff are refused.  No pool bound is
 * invented, so any other nonzero address is encoded.  On failure the body is
 * left untouched.
 */
image_buffer_request_status image_buffer_request_encode_release_body(
    uint32_t physical_address,
    uint8_t *out_body,
    size_t body_capacity);

/*
 * Encode the complete outgoing release frame: category 0x81, attribute
 * 0x018d, the 4-byte body and the two zero trailer bytes of the mcb-frame
 * TX envelope.  The result is 12 bytes.
 *
 * out_frame_size receives the encoded size and is left untouched on failure.
 * It must not overlap out_frame.
 */
image_buffer_request_status image_buffer_request_encode_release(
    uint32_t physical_address,
    uint8_t *out_frame,
    size_t frame_capacity,
    size_t *out_frame_size);

/*
 * Decode one reply frame.
 *
 * frame points at a complete received mcb-frame, that is the bytes the kernel
 * left after consuming its four-byte outer header: the 00 03 route prefix,
 * the category, attribute and body length, the body and the two trailer
 * bytes.  The frame must be category 0x81, attribute 0x018c and carry exactly
 * 20 body bytes.
 *
 * scratch is caller storage of at least frame_size bytes; it may overlap
 * frame.  The decoded words are copied out of scratch, so the scratch buffer
 * need not be retained after the call.  out_reply is left untouched on
 * failure. scratch may be modified on failure after envelope validation.
 *
 * A reply whose address word is zero is a syntactically valid refusal, not an
 * allocation.  The decode never establishes that a reply answers any request.
 */
image_buffer_request_status image_buffer_request_decode_reply(
    const uint8_t *frame,
    size_t frame_size,
    uint8_t *scratch,
    size_t scratch_capacity,
    image_buffer_request_reply *out_reply);

/*
 * Returns 1 when the reply carries no allocation result, that is when its
 * address word is zero.  The producer replaces its no-allocation sentinel with
 * zero, so zero is the refusal encoding.  Returns 0 for a null pointer.
 *
 * A refusal is not an owned allocation and does not establish that any
 * request was answered.
 */
int image_buffer_request_reply_is_refusal(const image_buffer_request_reply *reply);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_BUFFER_REQUEST_H */
