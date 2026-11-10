/*
 * ipcc_packet_abi.c - pure request/result adapter. See ipcc_packet_abi.h and
 * README.md. No ioctl(), no open(), no threads, no allocation.
 */
#include "ipcc_packet_abi.h"

/*
 * The adapter is freestanding-clean on purpose: it uses no libc function, so
 * it compiles for the target with -ffreestanding and no target sysroot. Only
 * <stdint.h> and <stddef.h> are needed, and clang provides those itself.
 */
static void ipcc_zero(void *dst, size_t n)
{
    unsigned char *p = (unsigned char *)dst;
    size_t i;

    for (i = 0; i < n; i++) {
        p[i] = 0;
    }
}

/* ---- little-endian word helpers (host endianness independent) ---------- */

static void ipcc_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static uint32_t ipcc_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* ---- logical channel selector ------------------------------------------
 * 0xc03c8ff8 maps a sparse set of logical selectors onto internal handles
 * and maps everything else to 0xffffffff. The dispatcher passes the result
 * straight to the per-handle helpers, so an unmapped selector is not
 * rejected by the kernel; refusing it is this adapter's job.
 */

int ipcc_pkt_selector_handle(uint32_t selector)
{
    switch (selector) {
    case IPCC_PKT_SEL_MCB_IN: return 0;
    case IPCC_PKT_SEL_3:      return 2;
    case IPCC_PKT_SEL_10:     return 4;
    case IPCC_PKT_SEL_11:     return 5;
    case IPCC_PKT_SEL_12:     return 6;
    default:                  return -1;
    }
}

int ipcc_pkt_selector_valid(uint32_t selector)
{
    return ipcc_pkt_selector_handle(selector) >= 0 ? 1 : 0;
}

/* ---- bounds helpers ---------------------------------------------------- */

/*
 * The kernel reads the destination pointer from a 32-bit word, and it never
 * range-checks that pointer against the number of bytes it writes. The whole
 * written span must therefore be representable in 32 bits without wrapping.
 * This is representability only; it says nothing about whether the address is
 * accessible by the caller.
 */
static int ipcc_span_fits32(uintptr_t base, size_t span)
{
    uint64_t b = (uint64_t)base;
    uint64_t s = (uint64_t)span;

    if (span == 0u) {
        return 0;
    }
    if (b > 0xffffffffull) {
        return 0;
    }
    /* subtraction form: base + span must not pass 2^32 */
    return s <= (0x100000000ull - b);
}

/* Overflow-safe half-open range overlap test. */
int ipcc_pkt_ranges_overlap(const void *a, size_t alen, const void *b, size_t blen)
{
    uintptr_t ua = (uintptr_t)a;
    uintptr_t ub = (uintptr_t)b;

    if (alen == 0u || blen == 0u) {
        return 0;
    }
    if ((uintptr_t)alen > UINTPTR_MAX - ua) {
        return 1; /* the range cannot be described safely: treat as overlapping */
    }
    if ((uintptr_t)blen > UINTPTR_MAX - ub) {
        return 1;
    }
    return (ua < ub + (uintptr_t)blen) && (ub < ua + (uintptr_t)alen);
}

/* ---- caller policy ----------------------------------------------------- */

void ipcc_pkt_limits_init(ipcc_pkt_limits *out,
                          uint32_t min_payload,
                          uint32_t max_payload)
{
    if (out == NULL) {
        return;
    }
    out->min_payload = min_payload;
    out->max_payload = max_payload;
    out->selector = 0u;
    out->selector_fixed = 0; /* any mapped logical selector */
}

void ipcc_pkt_limits_mcb_incoming(ipcc_pkt_limits *out)
{
    if (out == NULL) {
        return;
    }
    out->min_payload = 10u;  /* inner frame length 10..390 inclusive */
    out->max_payload = 391u;
    out->selector = IPCC_PKT_SEL_MCB_IN;
    out->selector_fixed = 1; /* the caller must choose logical 0 explicitly */
}

/* ---- request encoders --------------------------------------------------- */

/*
 * A request is accepted only with a mapped logical selector, and only with
 * the selector the policy names when the policy pins one. The payload bounds
 * are a caller contract: they are enforced, never validated against the
 * device.
 */
static int ipcc_selector_ok(uint32_t selector, const ipcc_pkt_limits *limits)
{
    if (limits == NULL) {
        return 0;
    }
    if (!ipcc_pkt_selector_valid(selector)) {
        return 0;
    }
    if (limits->selector_fixed && selector != limits->selector) {
        return 0;
    }
    return 1;
}

int ipcc_pkt_encode_has_data(uint8_t out[IPCC_PKT_WIRE_8],
                             uint32_t selector,
                             const ipcc_pkt_limits *limits)
{
    if (out == NULL) {
        return -1;
    }
    if (!ipcc_selector_ok(selector, limits)) {
        return -1;
    }
    ipcc_zero(out, IPCC_PKT_WIRE_8);
    ipcc_put_le32(out + IPCC_PKT_W_CHANNEL, selector);
    ipcc_put_le32(out + 4u, IPCC_PKT_SENTINEL);
    return 0;
}

int ipcc_pkt_encode_peek_len(uint8_t out[IPCC_PKT_WIRE_8],
                             uint32_t selector,
                             const ipcc_pkt_limits *limits)
{
    if (out == NULL) {
        return -1;
    }
    if (!ipcc_selector_ok(selector, limits)) {
        return -1;
    }
    ipcc_zero(out, IPCC_PKT_WIRE_8);
    ipcc_put_le32(out + IPCC_PKT_W_CHANNEL, selector);
    ipcc_put_le32(out + 4u, IPCC_PKT_SENTINEL);
    return 0;
}

int ipcc_pkt_encode_read_frame(uint8_t out[IPCC_PKT_WIRE_16],
                               uint32_t selector,
                               uint32_t payload_len,
                               const void *payload_buffer,
                               size_t payload_capacity,
                               const ipcc_pkt_limits *limits)
{
    size_t span;

    if (out == NULL || payload_buffer == NULL || limits == NULL) {
        return -1;
    }
    if (!ipcc_selector_ok(selector, limits)) {
        return -1;
    }
    if (limits->max_payload == 0u || limits->min_payload > limits->max_payload) {
        return -1; /* no proven payload bound: accept nothing */
    }
    if (payload_len < limits->min_payload || payload_len >= limits->max_payload) {
        return -1;
    }
    /*
     * The frame read stages a 4-byte header in the destination before the
     * payload copy, so the kernel writes max(4,payload_len) bytes there. A
     * bounded write needs the whole written span, not just payload_len.
     */
    span = (payload_len > (uint32_t)IPCC_PKT_FRAME_HDR_LEN)
             ? (size_t)payload_len
             : (size_t)IPCC_PKT_FRAME_HDR_LEN;
    if (span > payload_capacity) {
        return -1;
    }
    /*
     * The kernel writes the payload first and copies the request block back
     * afterwards, so an overlap would let the write-back clobber payload
     * bytes.
     */
    if (ipcc_pkt_ranges_overlap(out, IPCC_PKT_WIRE_16, payload_buffer, span)) {
        return -1;
    }
    if (!ipcc_span_fits32((uintptr_t)payload_buffer, span)) {
        return -1; /* the written span is not representable in a 32-bit word */
    }

    ipcc_zero(out, IPCC_PKT_WIRE_16);
    ipcc_put_le32(out + IPCC_PKT_W_CHANNEL, selector);
    ipcc_put_le32(out + IPCC_PKT_W_LEN, payload_len);
    ipcc_put_le32(out + IPCC_PKT_W_REPORTED, IPCC_PKT_SENTINEL);
    ipcc_put_le32(out + IPCC_PKT_W_BUFFER, (uint32_t)(uintptr_t)payload_buffer);
    return 0;
}

/* ---- result decoders ---------------------------------------------------- */

static void ipcc_result_clear(ipcc_pkt_result *out)
{
    ipcc_zero(out, sizeof(*out));
}

/*
 * Snapshot the input words before touching the result: the caller may pass a
 * result structure that overlaps the input block.
 */
static void ipcc_snapshot(const uint8_t *in, uint32_t *w0, uint32_t *w1, uint32_t *w2)
{
    *w0 = ipcc_get_le32(in + IPCC_PKT_W_CHANNEL);
    *w1 = ipcc_get_le32(in + 4u);
    if (w2 != NULL) {
        *w2 = ipcc_get_le32(in + IPCC_PKT_W_REPORTED);
    }
}

/*
 * Common prologue. The expected selector is checked first: it is a local
 * contract, and a selector that the adapter itself declares unmappable
 * carries no meaningful ABI observation whatever the kernel echoed back. Such
 * a decode is refused with IPCC_PKT_ERR_ARG, exactly as the encoders refuse an
 * unmapped selector before any syscall is issued. Every observation field
 * stays zero; the raw ioctl return is still reported in syscall_result.
 *
 * A non-zero ioctl return is rejected. The reviewed branches convert
 * nonnegative output-copy helper results to zero and propagate negative
 * values. Those numeric results do not authenticate complete copying.
 */
static ipcc_pkt_status ipcc_result_prologue(ipcc_pkt_result *out,
                                             int32_t syscall_result,
                                             uint32_t w_channel,
                                             uint32_t selector)
{
    ipcc_result_clear(out);
    out->syscall_result = syscall_result;
    if (!ipcc_pkt_selector_valid(selector)) {
        /* refused locally: no observation, no syscall semantics claimed */
        out->status = IPCC_PKT_ERR_ARG;
        return out->status;
    }
    if (syscall_result != 0) {
        out->status = IPCC_PKT_ERR_SYSCALL;
        return out->status;
    }
    if (w_channel != selector) {
        /* the kernel echoes the input words it copied in; a mismatch means
         * the request block was not observed intact */
        out->status = IPCC_PKT_ERR_ECHO;
        return out->status;
    }
    out->channel = w_channel;
    out->status = IPCC_PKT_OK;
    return out->status;
}

ipcc_pkt_status ipcc_pkt_decode_has_data(const uint8_t *in,
                                          int32_t syscall_result,
                                          uint32_t selector,
                                          ipcc_pkt_result *out)
{
    uint32_t w0, w1;

    if (out == NULL) {
        return IPCC_PKT_ERR_ARG; /* nothing to write */
    }
    if (in == NULL) {
        /* failure atomicity also covers a missing input block */
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_ARG;
        out->syscall_result = 0;
        return out->status;
    }
    ipcc_snapshot(in, &w0, &w1, NULL);
    if (ipcc_result_prologue(out, syscall_result, w0, selector) != IPCC_PKT_OK) {
        return out->status;
    }

    if (w1 == IPCC_PKT_SENTINEL) {
        /* The output word was not observed to change. This is a rejected
         * observation, not a diagnosis of which kernel path ran. */
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_UNCHANGED_OUTPUT;
        out->syscall_result = 0;
        return out->status;
    }
    out->observed_output = w1;
    out->output_differs = 1;
    if (w1 > 1u) {
        /* the helper returns 0 or 1 only */
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_BAD_OUTPUT;
        out->syscall_result = 0;
        return out->status;
    }
    /* the two ring cursors differ; this is a cursor inequality, not a peer
     * readiness or frame completeness signal */
    out->cursor_differs = w1;
    return IPCC_PKT_OK;
}

ipcc_pkt_status ipcc_pkt_decode_peek_len(const uint8_t *in,
                                          int32_t syscall_result,
                                          uint32_t selector,
                                          ipcc_pkt_result *out)
{
    uint32_t w0, w1;

    if (out == NULL) {
        return IPCC_PKT_ERR_ARG; /* nothing to write */
    }
    if (in == NULL) {
        /* failure atomicity also covers a missing input block */
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_ARG;
        out->syscall_result = 0;
        return out->status;
    }
    ipcc_snapshot(in, &w0, &w1, NULL);
    if (ipcc_result_prologue(out, syscall_result, w0, selector) != IPCC_PKT_OK) {
        return out->status;
    }

    if (w1 == IPCC_PKT_SENTINEL) {
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_UNCHANGED_OUTPUT;
        out->syscall_result = 0;
        return out->status;
    }
    out->observed_output = w1;
    out->output_differs = 1;
    if (w1 > 0xffffu) {
        /* the helper returns a uint16 */
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_BAD_OUTPUT;
        out->syscall_result = 0;
        return out->status;
    }
    out->peek_len = w1;
    return IPCC_PKT_OK;
}

ipcc_pkt_status ipcc_pkt_decode_read_frame(const uint8_t *in,
                                            int32_t syscall_result,
                                            uint32_t selector,
                                            uint32_t payload_len,
                                            ipcc_pkt_result *out)
{
    uint32_t w0, w1, w2;

    if (out == NULL) {
        return IPCC_PKT_ERR_ARG; /* nothing to write */
    }
    if (in == NULL) {
        /* failure atomicity also covers a missing input block */
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_ARG;
        out->syscall_result = 0;
        return out->status;
    }
    ipcc_snapshot(in, &w0, &w1, &w2);
    if (ipcc_result_prologue(out, syscall_result, w0, selector) != IPCC_PKT_OK) {
        return out->status;
    }

    if (w1 != payload_len) {
        /* the kernel never modifies the length word, so a mismatch means the
         * request block was not observed intact */
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_ECHO;
        out->syscall_result = 0;
        return out->status;
    }
    if (w2 == IPCC_PKT_SENTINEL) {
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_UNCHANGED_OUTPUT;
        out->syscall_result = 0;
        return out->status;
    }
    out->observed_output = w2;
    out->output_differs = 1;
    /*
     * The frame-read helper returns either 0 (header flag mismatch) or the
     * length it was asked for. Any other value was not produced by the
     * observed code. A reported length is the kernel's claim; it is not
     * evidence that the bytes arrived.
     */
    if (w2 != 0u && w2 != payload_len) {
        ipcc_result_clear(out);
        out->status = IPCC_PKT_ERR_BAD_OUTPUT;
        out->syscall_result = 0;
        return out->status;
    }
    out->reported = w2;
    return IPCC_PKT_OK;
}
