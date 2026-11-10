/*
 * ipcc_packet_abi.h - request/result ABI for the three non-wait IPCC packet
 * ioctls of the original Gear 360 SM-R210 kernel driver.
 *
 * Pure layer: builds request bytes and interprets result bytes. No ioctl(),
 * no open(), no threads, no device access. The caller owns the syscall.
 *
 * Provenance and the full native analysis are in README.md. Evidence:
 * kernel-misc-v6.elf (arm, sha256 611a97a03566e9d017a056434af7513a0d477ee8b69228fa93c3b71e12baf463),
 * inflated kernel hash ebfd2c32ea58c6b3e247294e11cec371272d8ee4c9c093c8382c434ed53d5cf4,
 * VA = fileoff + 0xc0008000, dispatcher 0xc03c9dc8, helpers 0xc03c9034 /
 * 0xc03c8f68 / 0xc03c9180 / 0xc03c90f4, channel selector 0xc03c8ff8.
 * Frozen REA/Ghidra 12.1.3 records; no whole image was available.
 */
#ifndef IPCC_PACKET_ABI_H
#define IPCC_PACKET_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exact command words for the three packet commands. The dispatcher compares
 * the full 32-bit word against a fixed set of branches, so these three APIs
 * must use these constants exactly. A different dir/nr/size encoding of one of
 * these commands does not select its branch; it may select another legitimate
 * branch of the same dispatcher, and a word that matches no branch at all
 * returns 0 without doing anything.
 */
#define IPCC_PKT_CMD_HAS_DATA     0xc0087404u  /* _IOWR('t',4,8)  */
#define IPCC_PKT_CMD_PEEK_LEN     0xc0087405u  /* _IOWR('t',5,8)  */
#define IPCC_PKT_CMD_READ_FRAME   0xc0107406u  /* _IOWR('t',6,16) */

#define IPCC_PKT_WIRE_8           8u
#define IPCC_PKT_WIRE_16          16u

/* Word offsets in the little-endian request blocks. */
#define IPCC_PKT_W_CHANNEL        0u
#define IPCC_PKT_W_LEN            4u
#define IPCC_PKT_W_REPORTED       8u
#define IPCC_PKT_W_BUFFER        12u

/* The frame read stages a 4-byte header in the same destination before the
 * payload copy, so the kernel writes max(4,payload_len) bytes there. */
#define IPCC_PKT_FRAME_HDR_LEN    4u

/* Written into the output word before the call. It is outside every range the
 * observed code can produce, so an unchanged word is a rejected observation. */
#define IPCC_PKT_SENTINEL         0xffffffffu

/*
 * Logical channel selectors. The kernel maps a sparse set of selectors onto
 * internal handles and maps every other value to 0xffffffff, which the
 * dispatcher then passes on unchanged. The adapter refuses unmapped
 * selectors; it does not claim that the kernel rejects them.
 *
 * The policy is adapter-wide and is applied on both sides of the call: the
 * request encoders refuse an unmapped selector before a syscall is issued,
 * and the result decoders refuse an unmapped EXPECTED selector with
 * IPCC_PKT_ERR_ARG, so a decode can never report a successful ABI
 * observation for a channel the adapter declares unmappable.
 */
#define IPCC_PKT_SEL_MCB_IN       0x00u  /* -> internal handle 0 */
#define IPCC_PKT_SEL_3            0x03u  /* -> internal handle 2 */
#define IPCC_PKT_SEL_10           0x10u  /* -> internal handle 4 */
#define IPCC_PKT_SEL_11           0x11u  /* -> internal handle 5 */
#define IPCC_PKT_SEL_12           0x12u  /* -> internal handle 6 */

/* 1 when the selector is one of the mapped values. */
int ipcc_pkt_selector_valid(uint32_t selector);

/* Internal handle for a mapped selector, or -1 when the selector is not
 * mapped. */
int ipcc_pkt_selector_handle(uint32_t selector);

/*
 * Status codes. IPCC_PKT_ERR_ARG is the "refused locally" code: for a request
 * encoder it means no syscall was issued; for a result decoder it means the
 * decode produced no ABI observation (an unmapped expected selector, or a
 * missing argument). It never claims anything about the device.
 */
typedef enum {
    IPCC_PKT_OK = 0,                /* validated ABI observation only        */
    IPCC_PKT_ERR_ARG = 1,           /* refused locally: invalid argument     */
    IPCC_PKT_ERR_SYSCALL = 2,       /* ioctl returned non-zero              */
    IPCC_PKT_ERR_UNCHANGED_OUTPUT = 3, /* output word still holds the sentinel */
    IPCC_PKT_ERR_BAD_OUTPUT = 4,    /* output word outside the observed range*/
    IPCC_PKT_ERR_ECHO = 5           /* an echoed input word does not match  */
} ipcc_pkt_status;

/*
 * Bounds for one request. These are a CALLER CONTRACT, not a device
 * validation: the supplied kernel evidence establishes no ring capacity and
 * no payload limit, and nothing here checks the device. A zero payload bound
 * accepts no frame read; there is no permissive default. These bound one
 * request only and say nothing about the ring's aggregate free-space
 * behaviour.
 */
typedef struct {
    uint32_t min_payload;   /* inclusive lower bound on payload_len       */
    uint32_t max_payload;   /* exclusive upper bound on payload_len       */
    uint32_t selector;      /* logical selector this policy applies to    */
    int      selector_fixed;/* 1 = a request must use exactly `selector`  */
} ipcc_pkt_limits;

/*
 * Generic caller contract: the caller's own payload bounds, any mapped
 * logical selector. The caller is responsible for the bounds; the adapter
 * only enforces what the caller declared.
 */
void ipcc_pkt_limits_init(ipcc_pkt_limits *out,
                          uint32_t min_payload,
                          uint32_t max_payload);

/*
 * Reviewed caller policy for the ordinary incoming MCB: inner frame length
 * 10..390 inclusive, and the request must explicitly choose logical
 * selector IPCC_PKT_SEL_MCB_IN. These values come from the parent review of
 * the incoming MCB frame range, not from the frozen kernel packet.
 */
void ipcc_pkt_limits_mcb_incoming(ipcc_pkt_limits *out);

/*
 * Failure atomicity: unless status == IPCC_PKT_OK every observation field
 * below is zero and carries no meaning. Only status and syscall_result are
 * meaningful in a failing result. This holds for every failure, including a
 * NULL input block and including the refusal of an unmapped expected
 * selector: with a valid result pointer the decoder clears the result and
 * reports IPCC_PKT_ERR_ARG. A NULL result pointer has nothing to write.
 *
 * IPCC_PKT_OK means only that the ioctl returned 0 and the output word was
 * observed to change. It is a pure ABI observation: it is not a statement
 * that any byte was copied coherently, that a peer is producing, that this
 * caller owns the data, or that the data is usable.
 */
typedef struct {
    ipcc_pkt_status status;
    int32_t         syscall_result;  /* raw ioctl return value               */
    uint32_t        channel;         /* echoed selector word                 */
    uint32_t        cursor_differs;  /* HAS_DATA: the two ring cursors differ */
    uint32_t        peek_len;        /* PEEK_LEN: uint16 frame-header field  */
    uint32_t        reported;        /* READ_FRAME: kernel byte-count claim  */
    uint32_t        observed_output; /* the output word as observed          */
    int             output_differs;  /* 1 = output word differs from sentinel*/
} ipcc_pkt_result;

/* Request encoders. Return 0 on success; on refusal they return -1 and leave
 * the request block untouched. The selector must be a mapped logical
 * selector and must match a policy that pins one; the frame read
 * additionally enforces the payload bounds. */
int ipcc_pkt_encode_has_data(uint8_t out[IPCC_PKT_WIRE_8],
                             uint32_t selector,
                             const ipcc_pkt_limits *limits);

int ipcc_pkt_encode_peek_len(uint8_t out[IPCC_PKT_WIRE_8],
                             uint32_t selector,
                             const ipcc_pkt_limits *limits);

int ipcc_pkt_encode_read_frame(uint8_t out[IPCC_PKT_WIRE_16],
                               uint32_t selector,
                               uint32_t payload_len,
                               const void *payload_buffer,
                               size_t payload_capacity,
                               const ipcc_pkt_limits *limits);

/*
 * Result decoders. `in` is the request block after the ioctl returned.
 * `syscall_result` is the raw ioctl return and `selector` the requested
 * logical selector. The input block may overlap the result structure: the
 * decoder snapshots the input words before it clears the result. Misaligned
 * input blocks are read byte-wise and are supported.
 *
 * The expected selector must be a mapped logical selector. An unmapped one is
 * refused with IPCC_PKT_ERR_ARG before any echo or output word is interpreted:
 * the adapter declares such a channel unmappable, so no ABI observation can be
 * meaningful for it, whatever the kernel echoed back. The refusal clears every
 * observation field; syscall_result still carries the raw ioctl return. The
 * refusal therefore takes precedence over the echo mismatch, the unchanged
 * output word and the output range checks, which keep their documented
 * behaviour for a mapped selector.
 *
 * The raw ioctl return is numeric: the observed branches return 0 when the
 * copy helper returned a nonnegative value and propagate a negative helper
 * value unchanged. The helper bodies are not in the packet, so neither sign
 * is shown to mean a particular copy outcome, and a nonnegative helper value
 * is not evidence that a copy completed.
 */
ipcc_pkt_status ipcc_pkt_decode_has_data(const uint8_t *in,
                                          int32_t syscall_result,
                                          uint32_t selector,
                                          ipcc_pkt_result *out);

ipcc_pkt_status ipcc_pkt_decode_peek_len(const uint8_t *in,
                                          int32_t syscall_result,
                                          uint32_t selector,
                                          ipcc_pkt_result *out);

ipcc_pkt_status ipcc_pkt_decode_read_frame(const uint8_t *in,
                                            int32_t syscall_result,
                                            uint32_t selector,
                                            uint32_t payload_len,
                                            ipcc_pkt_result *out);

/* Aliasing rule used by the frame read: the request block and the written
 * span of the destination must be disjoint. Exposed so a caller can apply
 * the same rule. */
int ipcc_pkt_ranges_overlap(const void *a, size_t alen, const void *b, size_t blen);

#ifdef __cplusplus
}
#endif

#endif /* IPCC_PACKET_ABI_H */
