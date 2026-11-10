/*
 * test_ipcc_packet_abi.c - host tests for the pure IPCC packet ABI adapter.
 *
 * No ioctl(), no device, no kernel here: the "kernel responses" are byte
 * buffers that reproduce what the original dispatcher is documented to write
 * back into the request block.
 *
 * Build/run:  make test
 */
#include "ipcc_packet_abi.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void wr_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

/* A destination address that is representable in the 32-bit pointer word. */
#define LOW_PTR   ((void *)(uintptr_t)0x20000000u)

/* The generic contract: the caller's own bounds, any mapped logical
 * selector. */
static void limits_generic(ipcc_pkt_limits *lim)
{
    ipcc_pkt_limits_init(lim, 1u, 0x10000u);
}

/* ---- 1. ABI byte layout ------------------------------------------------ */

static void test_abi_bytes_has_data(void)
{
    uint8_t buf[IPCC_PKT_WIRE_8];
    ipcc_pkt_limits lim;

    limits_generic(&lim);

    memset(buf, 0xa5, sizeof buf);
    CHECK(ipcc_pkt_encode_has_data(buf, IPCC_PKT_SEL_3, &lim) == 0);
    CHECK(buf[0] == 0x03u && buf[1] == 0x00u && buf[2] == 0x00u && buf[3] == 0x00u);
    CHECK(rd_le32(buf + IPCC_PKT_W_CHANNEL) == 0x00000003u);
    /* the output word starts as the sentinel and is write-only */
    CHECK(rd_le32(buf + 4u) == IPCC_PKT_SENTINEL);
}

static void test_abi_bytes_read_frame(void)
{
    uint8_t buf[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;

    limits_generic(&lim);

    memset(buf, 0xa5, sizeof buf);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 0x0102u,
                                     LOW_PTR, 4096u, &lim) == 0);
    CHECK(rd_le32(buf + IPCC_PKT_W_CHANNEL) == 0x00000000u);
    CHECK(rd_le32(buf + IPCC_PKT_W_LEN) == 0x00000102u);
    CHECK(rd_le32(buf + IPCC_PKT_W_REPORTED) == IPCC_PKT_SENTINEL);
    CHECK(rd_le32(buf + IPCC_PKT_W_BUFFER) == 0x20000000u);
    /* little endian, independent of host endianness */
    CHECK(buf[0] == 0x00u && buf[1] == 0x00u && buf[2] == 0x00u && buf[3] == 0x00u);
    CHECK(buf[4] == 0x02u && buf[5] == 0x01u && buf[6] == 0x00u && buf[7] == 0x00u);
    CHECK(buf[12] == 0x00u && buf[13] == 0x00u && buf[14] == 0x00u && buf[15] == 0x20u);
}

/* ---- 2. command word encoding ------------------------------------------ */

static void test_command_encoding(void)
{
    unsigned dir, type, nr, size;
    uint32_t cmd;

    cmd = IPCC_PKT_CMD_HAS_DATA;
    dir = (cmd >> 30) & 3u; size = (cmd >> 16) & 0x3fffu;
    type = (cmd >> 8) & 0xffu; nr = cmd & 0xffu;
    CHECK(dir == 3u);
    CHECK(type == 0x74u);
    CHECK(nr == 4u);
    CHECK(size == IPCC_PKT_WIRE_8);

    cmd = IPCC_PKT_CMD_PEEK_LEN;
    dir = (cmd >> 30) & 3u; size = (cmd >> 16) & 0x3fffu;
    type = (cmd >> 8) & 0xffu; nr = cmd & 0xffu;
    CHECK(dir == 3u);
    CHECK(type == 0x74u);
    CHECK(nr == 5u);
    CHECK(size == IPCC_PKT_WIRE_8);

    cmd = IPCC_PKT_CMD_READ_FRAME;
    dir = (cmd >> 30) & 3u; size = (cmd >> 16) & 0x3fffu;
    type = (cmd >> 8) & 0xffu; nr = cmd & 0xffu;
    CHECK(dir == 3u);
    CHECK(type == 0x74u);
    CHECK(nr == 6u);
    CHECK(size == IPCC_PKT_WIRE_16);

    /* A word that differs only in the encoded size does not select the
     * READ_FRAME branch: it would select some other branch of the same
     * dispatcher, or none at all. The constant itself is what the adapter
     * must use. */
    CHECK(0xc0087406u != IPCC_PKT_CMD_READ_FRAME);
}

/* ---- 3. the logical selector map ---------------------------------------- */

static void test_selector_map(void)
{
    CHECK(ipcc_pkt_selector_valid(IPCC_PKT_SEL_MCB_IN) == 1);
    CHECK(ipcc_pkt_selector_valid(IPCC_PKT_SEL_3) == 1);
    CHECK(ipcc_pkt_selector_valid(IPCC_PKT_SEL_10) == 1);
    CHECK(ipcc_pkt_selector_valid(IPCC_PKT_SEL_11) == 1);
    CHECK(ipcc_pkt_selector_valid(IPCC_PKT_SEL_12) == 1);

    CHECK(ipcc_pkt_selector_handle(IPCC_PKT_SEL_MCB_IN) == 0);
    CHECK(ipcc_pkt_selector_handle(IPCC_PKT_SEL_3) == 2);
    CHECK(ipcc_pkt_selector_handle(IPCC_PKT_SEL_10) == 4);
    CHECK(ipcc_pkt_selector_handle(IPCC_PKT_SEL_11) == 5);
    CHECK(ipcc_pkt_selector_handle(IPCC_PKT_SEL_12) == 6);

    /* the map is sparse: the gaps are not selectors */
    CHECK(ipcc_pkt_selector_valid(1u) == 0);
    CHECK(ipcc_pkt_selector_valid(2u) == 0);
    CHECK(ipcc_pkt_selector_valid(4u) == 0);
    CHECK(ipcc_pkt_selector_valid(0x0fu) == 0);
    CHECK(ipcc_pkt_selector_valid(0x13u) == 0);
    CHECK(ipcc_pkt_selector_valid(0xffffffffu) == 0);
    CHECK(ipcc_pkt_selector_handle(1u) == -1);
    CHECK(ipcc_pkt_selector_handle(0xffffffffu) == -1);
}

static void test_unmapped_selector_refused(void)
{
    uint8_t buf[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;
    static const uint32_t unmapped[] = { 1u, 2u, 4u, 0x0fu, 0x13u, 0x100u, 0xffffffffu };
    size_t i;

    limits_generic(&lim);

    for (i = 0; i < sizeof unmapped / sizeof unmapped[0]; i++) {
        CHECK(ipcc_pkt_encode_has_data(buf, unmapped[i], &lim) == -1);
        CHECK(ipcc_pkt_encode_peek_len(buf, unmapped[i], &lim) == -1);
        CHECK(ipcc_pkt_encode_read_frame(buf, unmapped[i], 16u,
                                         LOW_PTR, 4096u, &lim) == -1);
    }
    /* every mapped selector is accepted */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 16u,
                                     LOW_PTR, 4096u, &lim) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_3, 16u,
                                     LOW_PTR, 4096u, &lim) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_10, 16u,
                                     LOW_PTR, 4096u, &lim) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_11, 16u,
                                     LOW_PTR, 4096u, &lim) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_12, 16u,
                                     LOW_PTR, 4096u, &lim) == 0);
}

/* ---- 4. syscall failure is reported as a failure ------------------------ */

static void test_syscall_failure(void)
{
    uint8_t buf[IPCC_PKT_WIRE_8];
    ipcc_pkt_limits lim;
    ipcc_pkt_result res;

    limits_generic(&lim);

    CHECK(ipcc_pkt_encode_has_data(buf, IPCC_PKT_SEL_3, &lim) == 0);

    /* negative ioctl result */
    CHECK(ipcc_pkt_decode_has_data(buf, -14, IPCC_PKT_SEL_3, &res) == IPCC_PKT_ERR_SYSCALL);
    CHECK(res.status == IPCC_PKT_ERR_SYSCALL);
    CHECK(res.syscall_result == -14);
    CHECK(res.cursor_differs == 0u);
    CHECK(res.observed_output == 0u);
    CHECK(res.output_differs == 0);
    CHECK(res.channel == 0u);

    /* positive ioctl result is not produced by these branches: treat as failure */
    CHECK(ipcc_pkt_decode_has_data(buf, 5, IPCC_PKT_SEL_3, &res) == IPCC_PKT_ERR_SYSCALL);
    CHECK(res.syscall_result == 5);
    CHECK(res.cursor_differs == 0u);
}

/* ---- 5. failure atomicity: no observation survives a failure ------------ */

static void test_failure_output_untouched(void)
{
    uint8_t buf[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;
    ipcc_pkt_result res;

    limits_generic(&lim);

    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 16u,
                                     LOW_PTR, 4096u, &lim) == 0);
    wr_le32(buf + IPCC_PKT_W_REPORTED, 16u); /* a valid-looking output word */

    /* poison the result, then fail the call */
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_read_frame(buf, -14, IPCC_PKT_SEL_MCB_IN, 16u, &res)
          == IPCC_PKT_ERR_SYSCALL);
    CHECK(res.status == IPCC_PKT_ERR_SYSCALL);
    CHECK(res.syscall_result == -14);
    /* no stale or partial observation survives a failure */
    CHECK(res.cursor_differs == 0u);
    CHECK(res.peek_len == 0u);
    CHECK(res.reported == 0u);
    CHECK(res.channel == 0u);
    CHECK(res.observed_output == 0u);
    CHECK(res.output_differs == 0);
}

/* ---- 6. an unchanged output word is a rejected observation -------------- */

static void test_unchanged_output(void)
{
    uint8_t buf[IPCC_PKT_WIRE_8];
    ipcc_pkt_limits lim;
    ipcc_pkt_result res;

    limits_generic(&lim);

    CHECK(ipcc_pkt_encode_has_data(buf, IPCC_PKT_SEL_3, &lim) == 0);
    CHECK(ipcc_pkt_decode_has_data(buf, 0, IPCC_PKT_SEL_3, &res)
          == IPCC_PKT_ERR_UNCHANGED_OUTPUT);
    CHECK(res.status == IPCC_PKT_ERR_UNCHANGED_OUTPUT);
    CHECK(res.output_differs == 0);
    CHECK(res.observed_output == 0u);
    CHECK(res.cursor_differs == 0u);
    CHECK(res.channel == 0u);
    CHECK(res.syscall_result == 0);

    /* a changed output word is an observation, not a copy proof */
    wr_le32(buf + 4u, 1u);
    CHECK(ipcc_pkt_decode_has_data(buf, 0, IPCC_PKT_SEL_3, &res) == IPCC_PKT_OK);
    CHECK(res.output_differs == 1);
    CHECK(res.observed_output == 1u);
    CHECK(res.cursor_differs == 1u);
    CHECK(res.channel == IPCC_PKT_SEL_3);

    /* the kernel helper can only produce 0 or 1 */
    wr_le32(buf + 4u, 2u);
    CHECK(ipcc_pkt_decode_has_data(buf, 0, IPCC_PKT_SEL_3, &res)
          == IPCC_PKT_ERR_BAD_OUTPUT);
    CHECK(res.status == IPCC_PKT_ERR_BAD_OUTPUT);
    CHECK(res.observed_output == 0u);
    CHECK(res.output_differs == 0);

    /* cursor equality */
    wr_le32(buf + 4u, 0u);
    CHECK(ipcc_pkt_decode_has_data(buf, 0, IPCC_PKT_SEL_3, &res) == IPCC_PKT_OK);
    CHECK(res.cursor_differs == 0u);
    CHECK(res.output_differs == 1);
}

/* ---- 7. a corrupted echo is refused ------------------------------------- */

static void test_channel_echo(void)
{
    uint8_t buf[IPCC_PKT_WIRE_8];
    ipcc_pkt_limits lim;
    ipcc_pkt_result res;

    limits_generic(&lim);

    CHECK(ipcc_pkt_encode_has_data(buf, IPCC_PKT_SEL_3, &lim) == 0);
    wr_le32(buf + 4u, 1u);
    /* the kernel echoes word 0; a mismatch means the request block was not
     * observed intact */
    wr_le32(buf + IPCC_PKT_W_CHANNEL, 0xdeadbeefu);
    CHECK(ipcc_pkt_decode_has_data(buf, 0, IPCC_PKT_SEL_3, &res) == IPCC_PKT_ERR_ECHO);
    CHECK(res.status == IPCC_PKT_ERR_ECHO);
    CHECK(res.channel == 0u);
    CHECK(res.cursor_differs == 0u);
    CHECK(res.output_differs == 0);
}

/* ---- 8. peek length ---------------------------------------------------- */

static void test_peek_len(void)
{
    uint8_t buf[IPCC_PKT_WIRE_8];
    ipcc_pkt_limits lim;
    ipcc_pkt_result res;

    limits_generic(&lim);

    CHECK(ipcc_pkt_encode_peek_len(buf, IPCC_PKT_SEL_MCB_IN, &lim) == 0);
    wr_le32(buf + 4u, 0x1234u);
    CHECK(ipcc_pkt_decode_peek_len(buf, 0, IPCC_PKT_SEL_MCB_IN, &res) == IPCC_PKT_OK);
    CHECK(res.peek_len == 0x1234u);
    CHECK(res.observed_output == 0x1234u);
    CHECK(res.output_differs == 1);

    /* the peek helper returns a uint16 */
    wr_le32(buf + 4u, 0x00010000u);
    CHECK(ipcc_pkt_decode_peek_len(buf, 0, IPCC_PKT_SEL_MCB_IN, &res)
          == IPCC_PKT_ERR_BAD_OUTPUT);

    /* unchanged output word */
    wr_le32(buf + 4u, IPCC_PKT_SENTINEL);
    CHECK(ipcc_pkt_decode_peek_len(buf, 0, IPCC_PKT_SEL_MCB_IN, &res)
          == IPCC_PKT_ERR_UNCHANGED_OUTPUT);
}

/* ---- 9. the reported byte count is a claim, not a copy proof ------------ */

static void test_reported_is_untrusted(void)
{
    uint8_t buf[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;
    ipcc_pkt_result res;

    limits_generic(&lim);

    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 512u,
                                     LOW_PTR, 4096u, &lim) == 0);
    /* equal cursors: the kernel can report the requested length without
     * copying anything */
    wr_le32(buf + IPCC_PKT_W_REPORTED, 512u);
    CHECK(ipcc_pkt_decode_read_frame(buf, 0, IPCC_PKT_SEL_MCB_IN, 512u, &res)
          == IPCC_PKT_OK);
    CHECK(res.reported == 512u);
    CHECK(res.observed_output == 512u);
    CHECK(res.output_differs == 1);
    /* the adapter offers no "copy confirmed" state at all */

    /* header flag mismatch: the kernel reports 0 */
    wr_le32(buf + IPCC_PKT_W_REPORTED, 0u);
    CHECK(ipcc_pkt_decode_read_frame(buf, 0, IPCC_PKT_SEL_MCB_IN, 512u, &res)
          == IPCC_PKT_OK);
    CHECK(res.reported == 0u);

    /* a value outside {0, payload_len} was not produced by the observed code */
    wr_le32(buf + IPCC_PKT_W_REPORTED, 511u);
    CHECK(ipcc_pkt_decode_read_frame(buf, 0, IPCC_PKT_SEL_MCB_IN, 512u, &res)
          == IPCC_PKT_ERR_BAD_OUTPUT);
    CHECK(res.status == IPCC_PKT_ERR_BAD_OUTPUT);

    /* unchanged output word */
    wr_le32(buf + IPCC_PKT_W_REPORTED, IPCC_PKT_SENTINEL);
    CHECK(ipcc_pkt_decode_read_frame(buf, 0, IPCC_PKT_SEL_MCB_IN, 512u, &res)
          == IPCC_PKT_ERR_UNCHANGED_OUTPUT);

    /* a corrupted length echo is refused */
    wr_le32(buf + IPCC_PKT_W_REPORTED, 512u);
    wr_le32(buf + IPCC_PKT_W_LEN, 511u);
    CHECK(ipcc_pkt_decode_read_frame(buf, 0, IPCC_PKT_SEL_MCB_IN, 512u, &res)
          == IPCC_PKT_ERR_ECHO);
}

/* ---- 10. bounds on the written span (header write included) -------------- */

static void test_header_write_span(void)
{
    uint8_t buf[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;

    limits_generic(&lim);

    /* the frame read stages a 4-byte header in the destination, so a
     * destination of 1 or 3 bytes is too small whatever the length is */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 1u,
                                     LOW_PTR, 1u, &lim) == -1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 3u,
                                     LOW_PTR, 3u, &lim) == -1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 1u,
                                     LOW_PTR, 4u, &lim) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 4u,
                                     LOW_PTR, 4u, &lim) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 5u,
                                     LOW_PTR, 4u, &lim) == -1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 5u,
                                     LOW_PTR, 5u, &lim) == 0);
}

/* ---- 11. 32-bit representability of the written span -------------------- */

static void test_pointer32_span(void)
{
    uint8_t buf[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;

    limits_generic(&lim);

    /* base 0xfffffffe with a 4-byte write would end past 2^32 */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 4u,
                                     (void *)(uintptr_t)0xfffffffeu, 4096u, &lim) == -1);
    /* base 0xffffffff with a 1-byte length still writes the 4-byte header */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 1u,
                                     (void *)(uintptr_t)0xffffffffu, 4096u, &lim) == -1);
    /* the last representable span */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 4u,
                                     (void *)(uintptr_t)0xfffffffcu, 4096u, &lim) == 0);
    /* a 64-bit-only address cannot be carried in a 32-bit pointer word */
    if (sizeof(void *) > 4u) {
        CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 16u,
                                         (void *)(uintptr_t)0x100000000ull,
                                         4096u, &lim) == -1);
    }
}

/* ---- 12. caller policy bounds ------------------------------------------ */

static void test_limits_policy(void)
{
    uint8_t buf[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;
    ipcc_pkt_limits mcb;

    limits_generic(&lim);

    /* a payload length outside the declared range */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 0u,
                                     LOW_PTR, 4096u, &lim) == -1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 0x10000u,
                                     LOW_PTR, 4096u, &lim) == -1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 0xffffu,
                                     LOW_PTR, 0x20000u, &lim) == 0);

    /* a payload length beyond the declared buffer capacity */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 4097u,
                                     LOW_PTR, 4096u, &lim) == -1);

    /* no permissive default: an unset payload bound accepts nothing */
    lim.max_payload = 0u;
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 16u,
                                     LOW_PTR, 4096u, &lim) == -1);

    /* the reviewed MCB policy: inner frame 10..390, and the request must
     * explicitly choose logical selector 0 */
    ipcc_pkt_limits_mcb_incoming(&mcb);
    CHECK(mcb.min_payload == 10u);
    CHECK(mcb.max_payload == 391u);
    CHECK(mcb.selector == IPCC_PKT_SEL_MCB_IN);
    CHECK(mcb.selector_fixed == 1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 10u,
                                     LOW_PTR, 4096u, &mcb) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 390u,
                                     LOW_PTR, 4096u, &mcb) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 9u,
                                     LOW_PTR, 4096u, &mcb) == -1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 391u,
                                     LOW_PTR, 4096u, &mcb) == -1);
    /* another mapped logical selector is not this policy */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_3, 100u,
                                     LOW_PTR, 4096u, &mcb) == -1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_10, 100u,
                                     LOW_PTR, 4096u, &mcb) == -1);
    CHECK(ipcc_pkt_encode_has_data(buf, IPCC_PKT_SEL_3, &mcb) == -1);
    CHECK(ipcc_pkt_encode_peek_len(buf, IPCC_PKT_SEL_11, &mcb) == -1);
    CHECK(ipcc_pkt_encode_has_data(buf, IPCC_PKT_SEL_MCB_IN, &mcb) == 0);
    CHECK(ipcc_pkt_encode_peek_len(buf, IPCC_PKT_SEL_MCB_IN, &mcb) == 0);

    /* the generic contract pins no selector and enforces the caller's bounds */
    ipcc_pkt_limits_init(&lim, 20u, 100u);
    CHECK(lim.selector_fixed == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 20u,
                                     LOW_PTR, 4096u, &lim) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_3, 20u,
                                     LOW_PTR, 4096u, &lim) == 0);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 19u,
                                     LOW_PTR, 4096u, &lim) == -1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 100u,
                                     LOW_PTR, 4096u, &lim) == -1);

    /* a refused request leaves the request block untouched */
    memset(buf, 0x5au, sizeof buf);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 4097u,
                                     LOW_PTR, 4096u, &lim) == -1);
    CHECK(buf[0] == 0x5au && buf[1] == 0x5au);
}

/* ---- 13. aliasing rules ------------------------------------------------- */

static void test_aliasing_rules(void)
{
    uint8_t buf[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;

    limits_generic(&lim);

    /* the destination overlapping the request block */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 32u, buf, 64u, &lim) == -1);
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 32u, buf + 8, 64u, &lim) == -1);

    /* a disjoint destination is fine */
    CHECK(ipcc_pkt_encode_read_frame(buf, IPCC_PKT_SEL_MCB_IN, 32u, LOW_PTR, 64u, &lim) == 0);

    /* the rule itself, with synthetic 32-bit addresses */
    CHECK(ipcc_pkt_ranges_overlap((void *)(uintptr_t)0x1000u, 16u,
                                  (void *)(uintptr_t)0x1008u, 64u) == 1);
    CHECK(ipcc_pkt_ranges_overlap((void *)(uintptr_t)0x1000u, 16u,
                                  (void *)(uintptr_t)0x0ff8u, 16u) == 1);
    /* touching ranges do not overlap */
    CHECK(ipcc_pkt_ranges_overlap((void *)(uintptr_t)0x1000u, 16u,
                                  (void *)(uintptr_t)0x1010u, 64u) == 0);
    CHECK(ipcc_pkt_ranges_overlap((void *)(uintptr_t)0x1010u, 64u,
                                  (void *)(uintptr_t)0x1000u, 16u) == 0);
    /* a zero-length range never overlaps */
    CHECK(ipcc_pkt_ranges_overlap((void *)(uintptr_t)0x1000u, 0u,
                                  (void *)(uintptr_t)0x1000u, 64u) == 0);
}

/* ---- 14. decoding with an aliased or misaligned input block ------------- */

static void test_decode_alias_and_misalign(void)
{
    uint8_t buf[IPCC_PKT_WIRE_8 + 4];
    ipcc_pkt_limits lim;
    ipcc_pkt_result res;
    union {
        uint8_t bytes[64];
        ipcc_pkt_result res;
    } alias;

    limits_generic(&lim);

    /* misaligned input block: the decoder reads byte-wise */
    memset(buf, 0, sizeof buf);
    wr_le32(buf + 1, IPCC_PKT_SEL_3);  /* selector word at offset 1 */
    wr_le32(buf + 5, 1u);               /* output word at offset 5 */
    CHECK(ipcc_pkt_decode_has_data(buf + 1, 0, IPCC_PKT_SEL_3, &res) == IPCC_PKT_OK);
    CHECK(res.cursor_differs == 1u);
    CHECK(res.channel == IPCC_PKT_SEL_3);

    /* input block aliased with the result structure: the decoder snapshots
     * the input words before it clears the result */
    memset(&alias, 0, sizeof alias);
    wr_le32(alias.bytes + IPCC_PKT_W_CHANNEL, IPCC_PKT_SEL_3);
    wr_le32(alias.bytes + 4u, 1u);
    CHECK(ipcc_pkt_decode_has_data(alias.bytes, 0, IPCC_PKT_SEL_3, &alias.res)
          == IPCC_PKT_OK);
    CHECK(alias.res.status == IPCC_PKT_OK);
    CHECK(alias.res.channel == IPCC_PKT_SEL_3);
    CHECK(alias.res.cursor_differs == 1u);
    CHECK(alias.res.output_differs == 1);
}

/* ---- 15. the peek value is the natural payload length ------------------ */

static void test_peek_to_read_planning(void)
{
    uint8_t peek[IPCC_PKT_WIRE_8];
    uint8_t frame[IPCC_PKT_WIRE_16];
    ipcc_pkt_limits lim;
    ipcc_pkt_result res;

    limits_generic(&lim);

    CHECK(ipcc_pkt_encode_peek_len(peek, IPCC_PKT_SEL_MCB_IN, &lim) == 0);
    wr_le32(peek + 4u, 900u);
    CHECK(ipcc_pkt_decode_peek_len(peek, 0, IPCC_PKT_SEL_MCB_IN, &res) == IPCC_PKT_OK);
    CHECK(res.peek_len == 900u);

    /* the peeked length is what the frame read request carries */
    CHECK(ipcc_pkt_encode_read_frame(frame, IPCC_PKT_SEL_MCB_IN, res.peek_len,
                                     LOW_PTR, 4096u, &lim) == 0);
    CHECK(rd_le32(frame + IPCC_PKT_W_LEN) == 900u);

    /* a peeked length that does not fit the caller's buffer is refused */
    CHECK(ipcc_pkt_encode_read_frame(frame, IPCC_PKT_SEL_MCB_IN, 900u,
                                     LOW_PTR, 512u, &lim) == -1);
}


/* ---- 16. failure atomicity for a missing input block -------------------- */

static void test_null_input_failure_atomicity(void)
{
    ipcc_pkt_result res;
    int rc;

    /* a NULL input block must still clear the result and report the
     * argument error, for all three decoders */
    memset(&res, 0xaa, sizeof res);
    rc = (int)ipcc_pkt_decode_has_data(NULL, 0, IPCC_PKT_SEL_3, &res);
    CHECK(rc == (int)IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    CHECK(res.syscall_result == 0);
    CHECK(res.channel == 0u);
    CHECK(res.cursor_differs == 0u);
    CHECK(res.peek_len == 0u);
    CHECK(res.reported == 0u);
    CHECK(res.observed_output == 0u);
    CHECK(res.output_differs == 0);

    memset(&res, 0xaa, sizeof res);
    rc = (int)ipcc_pkt_decode_peek_len(NULL, 0, IPCC_PKT_SEL_3, &res);
    CHECK(rc == (int)IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    CHECK(res.syscall_result == 0);
    CHECK(res.channel == 0u);
    CHECK(res.cursor_differs == 0u);
    CHECK(res.peek_len == 0u);
    CHECK(res.reported == 0u);
    CHECK(res.observed_output == 0u);
    CHECK(res.output_differs == 0);

    memset(&res, 0xaa, sizeof res);
    rc = (int)ipcc_pkt_decode_read_frame(NULL, 0, IPCC_PKT_SEL_3, 16u, &res);
    CHECK(rc == (int)IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    CHECK(res.syscall_result == 0);
    CHECK(res.channel == 0u);
    CHECK(res.cursor_differs == 0u);
    CHECK(res.peek_len == 0u);
    CHECK(res.reported == 0u);
    CHECK(res.observed_output == 0u);
    CHECK(res.output_differs == 0);

    /* a NULL result pointer has nothing to write and must not be touched */
    CHECK((int)ipcc_pkt_decode_has_data(NULL, 0, IPCC_PKT_SEL_3, NULL)
          == (int)IPCC_PKT_ERR_ARG);
    CHECK((int)ipcc_pkt_decode_peek_len(NULL, 0, IPCC_PKT_SEL_3, NULL)
          == (int)IPCC_PKT_ERR_ARG);
    CHECK((int)ipcc_pkt_decode_read_frame(NULL, 0, IPCC_PKT_SEL_3, 16u, NULL)
          == (int)IPCC_PKT_ERR_ARG);
}

/* ---- 17. the result side applies the sparse-selector policy too ----------- */

/* The sparse map: five mapped selectors, everything else unmapped. */
static const uint32_t g_mapped_sel[] = {
    IPCC_PKT_SEL_MCB_IN, IPCC_PKT_SEL_3, IPCC_PKT_SEL_10,
    IPCC_PKT_SEL_11, IPCC_PKT_SEL_12
};
static const uint32_t g_unmapped_sel[] = {
    0x1u, 0x2u, 0x4u, 0x0fu, 0x13u, 0x99u, 0xffffffffu
};

#define N_MAPPED_SEL   (sizeof g_mapped_sel / sizeof g_mapped_sel[0])
#define N_UNMAPPED_SEL (sizeof g_unmapped_sel / sizeof g_unmapped_sel[0])

/* No observation may survive a failure: every observation field is zero. */
static void check_no_observation(const ipcc_pkt_result *res)
{
    CHECK(res->channel == 0u);
    CHECK(res->cursor_differs == 0u);
    CHECK(res->peek_len == 0u);
    CHECK(res->reported == 0u);
    CHECK(res->observed_output == 0u);
    CHECK(res->output_differs == 0);
}

/*
 * Request blocks that are completely consistent for the given selector: the
 * channel word echoes the selector, the output word holds a value the helper
 * could have produced, and the length word matches the requested length. The
 * selector is then the only thing wrong with it.
 *
 * The 8-byte commands put the output word at offset 4; the 16-byte frame read
 * puts the requested length there and the output word at offset 8.
 */
static void build_block_8(uint8_t *blk, uint32_t selector, uint32_t output)
{
    memset(blk, 0, IPCC_PKT_WIRE_8);
    wr_le32(blk + IPCC_PKT_W_CHANNEL, selector);
    wr_le32(blk + 4u, output);
}

static void build_block_16(uint8_t *blk, uint32_t selector, uint32_t len)
{
    memset(blk, 0, IPCC_PKT_WIRE_16);
    wr_le32(blk + IPCC_PKT_W_CHANNEL, selector);
    wr_le32(blk + IPCC_PKT_W_LEN, len);
    wr_le32(blk + IPCC_PKT_W_REPORTED, len);
    wr_le32(blk + IPCC_PKT_W_BUFFER, 0x20000000u);
}

static void test_result_side_unmapped_selector_refused(void)
{
    uint8_t blk[IPCC_PKT_WIRE_16];
    ipcc_pkt_result res;
    size_t i;

    for (i = 0; i < N_UNMAPPED_SEL; i++) {
        uint32_t sel = g_unmapped_sel[i];

        /* HAS_DATA: the output word holds 1, a value the helper produces */
        build_block_8(blk, sel, 1u);
        memset(&res, 0xaa, sizeof res);
        CHECK(ipcc_pkt_decode_has_data(blk, 0, sel, &res) == IPCC_PKT_ERR_ARG);
        CHECK(res.status == IPCC_PKT_ERR_ARG);
        CHECK(res.syscall_result == 0);
        check_no_observation(&res);

        /* PEEK_LEN: the output word holds a uint16 frame-header value */
        build_block_8(blk, sel, 0x1234u);
        memset(&res, 0xaa, sizeof res);
        CHECK(ipcc_pkt_decode_peek_len(blk, 0, sel, &res) == IPCC_PKT_ERR_ARG);
        CHECK(res.status == IPCC_PKT_ERR_ARG);
        CHECK(res.syscall_result == 0);
        check_no_observation(&res);

        /* READ_FRAME: the length echo and the reported byte count are both
         * exactly what the caller asked for */
        build_block_16(blk, sel, 16u);
        memset(&res, 0xaa, sizeof res);
        CHECK(ipcc_pkt_decode_read_frame(blk, 0, sel, 16u, &res) == IPCC_PKT_ERR_ARG);
        CHECK(res.status == IPCC_PKT_ERR_ARG);
        CHECK(res.syscall_result == 0);
        check_no_observation(&res);
    }
}

static void test_result_side_unmapped_raw_return_kept(void)
{
    uint8_t blk[IPCC_PKT_WIRE_16];
    ipcc_pkt_result res;

    build_block_8(blk, 0x99u, 1u);

    /* the refusal does not depend on the ioctl return: a negative helper
     * value is still the raw return, not an observation */
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_has_data(blk, -14, 0x99u, &res) == IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    CHECK(res.syscall_result == -14);
    check_no_observation(&res);

    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_peek_len(blk, -14, 0x99u, &res) == IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    CHECK(res.syscall_result == -14);
    check_no_observation(&res);

    build_block_16(blk, 0x99u, 16u);
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_read_frame(blk, -14, 0x99u, 16u, &res) == IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    CHECK(res.syscall_result == -14);
    check_no_observation(&res);

    /* a positive raw return is reported unchanged as well */
    build_block_8(blk, 0x99u, 1u);
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_has_data(blk, 5, 0x99u, &res) == IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    CHECK(res.syscall_result == 5);
    check_no_observation(&res);
}

static void test_result_side_mapped_selector_still_ok(void)
{
    uint8_t blk[IPCC_PKT_WIRE_16];
    ipcc_pkt_result res;
    size_t i;

    for (i = 0; i < N_MAPPED_SEL; i++) {
        uint32_t sel = g_mapped_sel[i];

        build_block_8(blk, sel, 1u);
        memset(&res, 0xaa, sizeof res);
        CHECK(ipcc_pkt_decode_has_data(blk, 0, sel, &res) == IPCC_PKT_OK);
        CHECK(res.status == IPCC_PKT_OK);
        CHECK(res.syscall_result == 0);
        CHECK(res.channel == sel);
        CHECK(res.cursor_differs == 1u);
        CHECK(res.observed_output == 1u);
        CHECK(res.output_differs == 1);

        build_block_8(blk, sel, 0x1234u);
        memset(&res, 0xaa, sizeof res);
        CHECK(ipcc_pkt_decode_peek_len(blk, 0, sel, &res) == IPCC_PKT_OK);
        CHECK(res.status == IPCC_PKT_OK);
        CHECK(res.channel == sel);
        CHECK(res.peek_len == 0x1234u);
        CHECK(res.observed_output == 0x1234u);
        CHECK(res.output_differs == 1);

        build_block_16(blk, sel, 16u);
        memset(&res, 0xaa, sizeof res);
        CHECK(ipcc_pkt_decode_read_frame(blk, 0, sel, 16u, &res) == IPCC_PKT_OK);
        CHECK(res.status == IPCC_PKT_OK);
        CHECK(res.channel == sel);
        CHECK(res.reported == 16u);
        CHECK(res.observed_output == 16u);
        CHECK(res.output_differs == 1);
    }
}

static void test_result_side_refusal_precedence(void)
{
    uint8_t blk[IPCC_PKT_WIRE_16];
    ipcc_pkt_result res;

    /* an unmapped expected selector is refused before the echo comparison:
     * a mismatching channel word does not turn the refusal into an echo
     * error */
    build_block_8(blk, 0x99u, 1u);
    wr_le32(blk + IPCC_PKT_W_CHANNEL, 0xdeadbeefu);
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_has_data(blk, 0, 0x99u, &res) == IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    check_no_observation(&res);

    /* and before the unchanged-output check */
    build_block_8(blk, 0x99u, IPCC_PKT_SENTINEL);
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_has_data(blk, 0, 0x99u, &res) == IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    check_no_observation(&res);

    /* a mapped expected selector keeps the documented rules: an unchanged
     * output word is still the unchanged-output error */
    build_block_8(blk, IPCC_PKT_SEL_3, IPCC_PKT_SENTINEL);
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_has_data(blk, 0, IPCC_PKT_SEL_3, &res)
          == IPCC_PKT_ERR_UNCHANGED_OUTPUT);
    CHECK(res.status == IPCC_PKT_ERR_UNCHANGED_OUTPUT);
    check_no_observation(&res);

    /* and an echoed channel word that is not the requested one is still an
     * echo error, even when that echoed word is itself unmapped */
    build_block_8(blk, 0x99u, 1u);
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_has_data(blk, 0, IPCC_PKT_SEL_3, &res) == IPCC_PKT_ERR_ECHO);
    CHECK(res.status == IPCC_PKT_ERR_ECHO);
    check_no_observation(&res);

    /* the length echo of a frame read keeps its own rule for a mapped
     * selector */
    build_block_16(blk, IPCC_PKT_SEL_3, 16u);
    wr_le32(blk + IPCC_PKT_W_LEN, 15u);
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_read_frame(blk, 0, IPCC_PKT_SEL_3, 16u, &res)
          == IPCC_PKT_ERR_ECHO);
    check_no_observation(&res);
}

static void test_result_side_unmapped_alias_and_null(void)
{
    union {
        uint8_t bytes[64];
        ipcc_pkt_result res;
    } alias;
    ipcc_pkt_result res;

    /* the input block aliased with the result structure: the refusal must
     * not depend on the order of the snapshot and the clear */
    memset(&alias, 0xaa, sizeof alias);
    wr_le32(alias.bytes + IPCC_PKT_W_CHANNEL, 0x99u);
    wr_le32(alias.bytes + 4u, 1u);
    CHECK(ipcc_pkt_decode_has_data(alias.bytes, 0, 0x99u, &alias.res)
          == IPCC_PKT_ERR_ARG);
    CHECK(alias.res.status == IPCC_PKT_ERR_ARG);
    CHECK(alias.res.syscall_result == 0);
    check_no_observation(&alias.res);

    memset(&alias, 0xaa, sizeof alias);
    wr_le32(alias.bytes + IPCC_PKT_W_CHANNEL, 0x99u);
    wr_le32(alias.bytes + IPCC_PKT_W_LEN, 16u);
    wr_le32(alias.bytes + IPCC_PKT_W_REPORTED, 16u);
    CHECK(ipcc_pkt_decode_read_frame(alias.bytes, 0, 0x99u, 16u, &alias.res)
          == IPCC_PKT_ERR_ARG);
    CHECK(alias.res.status == IPCC_PKT_ERR_ARG);
    check_no_observation(&alias.res);

    /* a misaligned input block is refused the same way */
    memset(&alias, 0xaa, sizeof alias);
    wr_le32(alias.bytes + 1, 0x99u);
    wr_le32(alias.bytes + 5, 1u);
    memset(&res, 0xaa, sizeof res);
    CHECK(ipcc_pkt_decode_has_data(alias.bytes + 1, 0, 0x99u, &res)
          == IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    check_no_observation(&res);

    /* a NULL result pointer has nothing to write */
    CHECK((int)ipcc_pkt_decode_has_data(NULL, 0, 0x99u, NULL)
          == (int)IPCC_PKT_ERR_ARG);
    CHECK((int)ipcc_pkt_decode_peek_len(NULL, 0, 0x99u, NULL)
          == (int)IPCC_PKT_ERR_ARG);
    CHECK((int)ipcc_pkt_decode_read_frame(NULL, 0, 0x99u, 16u, NULL)
          == (int)IPCC_PKT_ERR_ARG);

    /* a NULL input block is still an argument error */
    memset(&res, 0xaa, sizeof res);
    CHECK((int)ipcc_pkt_decode_has_data(NULL, 0, 0x99u, &res)
          == (int)IPCC_PKT_ERR_ARG);
    CHECK(res.status == IPCC_PKT_ERR_ARG);
    CHECK(res.syscall_result == 0);
    check_no_observation(&res);
}

int main(void)
{
    test_abi_bytes_has_data();
    test_abi_bytes_read_frame();
    test_command_encoding();
    test_selector_map();
    test_unmapped_selector_refused();
    test_syscall_failure();
    test_failure_output_untouched();
    test_unchanged_output();
    test_channel_echo();
    test_peek_len();
    test_reported_is_untrusted();
    test_header_write_span();
    test_pointer32_span();
    test_limits_policy();
    test_aliasing_rules();
    test_decode_alias_and_misalign();
    test_peek_to_read_planning();
    test_null_input_failure_atomicity();
    test_result_side_unmapped_selector_refused();
    test_result_side_unmapped_raw_return_kept();
    test_result_side_mapped_selector_still_ok();
    test_result_side_refusal_precedence();
    test_result_side_unmapped_alias_and_null();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
