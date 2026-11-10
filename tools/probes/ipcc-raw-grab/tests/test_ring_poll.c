/*
 * Host check of the ring watcher's packet walk.
 *
 * The watcher is the one in ipcc_raw_grab.c.  The harness builds a
 * synthetic shared window of the shape the camera's shared window has,
 * writes a record's bytes into a ring, writes the producer's cursor
 * word, and calls the real watcher through the host shim.  There is no
 * second parser in this file: the only things built here are bytes.
 *
 * The window is exactly as long as the shared window, so a read past
 * the end of it is a read past the end of the allocation.
 *
 * What it covers:
 *   - a record whose head, body and trailer straddle the end of the
 *     ring: the bytes handed to the callback must be the record's own
 *     bytes, not the bytes of the ring that follows;
 *   - the callback's pointer must be the program's own copy and not a
 *     pointer into the shared window;
 *   - two records published between two polls: both are delivered;
 *   - a record with a bad outer marker, a bad inner length or a
 *     declared body length that does not match: nothing is delivered
 *     and the loss is counted and reported;
 *   - a frame for another route, or for another category and
 *     attribute: nothing is delivered;
 *   - a cursor outside the ring: refused, and nothing is read;
 *   - a producer that has not written the whole record out: the record
 *     is kept and is delivered once it is whole;
 *   - a callback that refuses a frame: the frame is not counted as
 *     seen.
 *
 * No camera, no network, no device: the program is never entered, only
 * its watcher is.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The program's syscall layer, replaced by the harness.  The watcher's
 * diagnostics go to the error stream, and they are kept here so that the
 * test can see that a loss is reported and not only counted. */
static char notes[16384];
static size_t notes_length;

long ipcc_raw_grab_host_syscall(long nr, long a0, long a1, long a2, long a3,
                                long a4, long a5)
{
    (void)a0;
    (void)a3;
    (void)a4;
    (void)a5;
    if (nr == 4) { /* write */
        const char *data = (const char *)(unsigned long)a1;
        size_t count = (size_t)a2;
        if (notes_length + count + 1u < sizeof(notes)) {
            memcpy(notes + notes_length, data, count);
            notes_length += count;
            notes[notes_length] = '\0';
        }
        return (long)count;
    }
    return -2;
}

/* The host shim in ipcc_raw_grab.c.  It drives the real watcher and
 * hands out the geometry the watcher itself uses. */
extern void ipcc_raw_grab_host_watcher_reset(const unsigned char *window);
extern void ipcc_raw_grab_host_watcher_poll(uint32_t category, uint32_t attribute,
                                            int (*handler)(const unsigned char *,
                                                           uint32_t),
                                            uint32_t *seen);
extern uint32_t ipcc_raw_grab_host_ring_data_offset(int ring);
extern uint32_t ipcc_raw_grab_host_ring_cursor_offset(int ring);
extern uint32_t ipcc_raw_grab_host_ring_capacity(void);
extern uint32_t ipcc_raw_grab_host_window_bytes(void);
extern uint32_t ipcc_raw_grab_host_stat_records_ok(void);
extern uint32_t ipcc_raw_grab_host_stat_records_other(void);
extern uint32_t ipcc_raw_grab_host_stat_records_bad(void);
extern uint32_t ipcc_raw_grab_host_stat_frames_rejected(void);
extern uint32_t ipcc_raw_grab_host_stat_bytes_discarded(void);
extern uint32_t ipcc_raw_grab_host_stat_cursor_errors(void);
extern uint32_t ipcc_raw_grab_host_stat_incomplete(void);

#define CATEGORY_RESULT 0x0081u
#define ATTRIBUTE_STILL 0x0201u
#define ATTRIBUTE_OTHER 0x018cu

#define MAX_FRAMES 4
#define FRAME_CAP 512u

static unsigned char *window;
static uint32_t window_bytes;

static unsigned char frames[MAX_FRAMES][FRAME_CAP];
static uint32_t frame_lengths[MAX_FRAMES];
static int frame_count;
static int refuse;

/* The callback the watcher calls.  It records what it was given and
 * says whether it took the frame. */
static int on_frame(const unsigned char *payload, uint32_t inner_len)
{
    /* The frame must be the program's own copy.  A pointer into the
     * shared window would be a pointer into the ring, and the body of a
     * record that wraps is not contiguous there. */
    uintptr_t at = (uintptr_t)payload;
    uintptr_t low = (uintptr_t)window;
    assert(at < low || at >= low + (uintptr_t)window_bytes);
    assert(inner_len <= FRAME_CAP);
    if (frame_count < MAX_FRAMES) {
        memcpy(frames[frame_count], payload, inner_len);
        frame_lengths[frame_count] = inner_len;
    }
    ++frame_count;
    return refuse ? 0 : 1;
}

static void set_cursor(int ring, uint32_t value)
{
    unsigned char *at = window + ipcc_raw_grab_host_ring_cursor_offset(ring);
    at[0] = (unsigned char)(value & 0xffu);
    at[1] = (unsigned char)((value >> 8) & 0xffu);
    at[2] = (unsigned char)((value >> 16) & 0xffu);
    at[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void window_reset(void)
{
    int ring;
    memset(window, 0xa5, window_bytes);
    /* Every ring's cursor starts at the beginning of the ring, so that
     * the ring under test is the only one the watcher looks at. */
    for (ring = 0; ring < 4; ++ring) {
        set_cursor(ring, 0u);
    }
    notes_length = 0u;
    notes[0] = '\0';
    frame_count = 0;
    refuse = 0;
}

static void put_ring(int ring, uint32_t index, const unsigned char *bytes,
                     uint32_t count)
{
    uint32_t data = ipcc_raw_grab_host_ring_data_offset(ring);
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();
    uint32_t start = index % capacity;
    uint32_t first = capacity - start;
    if (first > count) {
        first = count;
    }
    memcpy(window + data + start, bytes, first);
    if (count > first) {
        memcpy(window + data, bytes + first, count - first);
    }
}

/*
 * Builds one ring record: ff 7f, the inner length, and the inner frame
 * 00 03 | category | attribute | body length | body | trailer.  Returns
 * the number of bytes the record occupies in the ring.
 */
static uint32_t build_record(unsigned char *record, uint16_t category,
                             uint16_t attribute, const unsigned char *body,
                             uint32_t body_length)
{
    uint32_t inner = 10u + body_length;
    record[0] = 0xffu;
    record[1] = 0x7fu;
    record[2] = (unsigned char)(inner & 0xffu);
    record[3] = (unsigned char)((inner >> 8) & 0xffu);
    record[4] = 0x00u;
    record[5] = 0x03u;
    record[6] = (unsigned char)(category & 0xffu);
    record[7] = (unsigned char)((category >> 8) & 0xffu);
    record[8] = (unsigned char)(attribute & 0xffu);
    record[9] = (unsigned char)((attribute >> 8) & 0xffu);
    record[10] = (unsigned char)(body_length & 0xffu);
    record[11] = (unsigned char)((body_length >> 8) & 0xffu);
    if (body_length != 0u) {
        memcpy(record + 12, body, body_length);
    }
    record[12 + body_length] = 0x00u;
    record[13 + body_length] = 0x00u;
    return 4u + inner;
}

/* The record's own head straddles the end of the ring. */
static void test_wrapped_head(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 394u;
    uint32_t seen = 0u;
    int index;

    for (index = 0; index < 8; ++index) {
        body[index] = (unsigned char)(0x30 + index);
    }
    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % ipcc_raw_grab_host_ring_capacity());
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 1);
    assert(seen == 1u);
    assert(frame_lengths[0] == 18u);
    assert(memcmp(frames[0], record + 4, 18u) == 0);
    assert(ipcc_raw_grab_host_stat_records_ok() == 1u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 0u);
    assert(ipcc_raw_grab_host_stat_bytes_discarded() == 0u);
    printf("wrapped head: the record's own bytes were delivered\n");
}

/* A long record: the body and the trailer straddle the end of the ring. */
static void test_wrapped_body(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[380];
    uint32_t total;
    uint32_t start = 300u;
    uint32_t seen = 0u;
    uint32_t index;

    for (index = 0u; index < 380u; ++index) {
        body[index] = (unsigned char)(index & 0xffu);
    }
    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 380u);
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % ipcc_raw_grab_host_ring_capacity());
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 1);
    assert(seen == 1u);
    assert(frame_lengths[0] == 390u);
    assert(memcmp(frames[0], record + 4, 390u) == 0);
    assert(ipcc_raw_grab_host_stat_records_ok() == 1u);
    assert(ipcc_raw_grab_host_stat_bytes_discarded() == 0u);
    printf("wrapped body: the body and the trailer were the record's own bytes\n");
}

/* Two records published between two polls. */
static void test_two_packets(void)
{
    unsigned char first[FRAME_CAP];
    unsigned char second[FRAME_CAP];
    unsigned char body_a[12];
    unsigned char body_b[6];
    uint32_t total_a;
    uint32_t total_b;
    uint32_t start = 100u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();
    uint32_t seen = 0u;
    int index;

    for (index = 0; index < 12; ++index) {
        body_a[index] = (unsigned char)(0x11 + index);
    }
    for (index = 0; index < 6; ++index) {
        body_b[index] = (unsigned char)(0x77 + index);
    }
    window_reset();
    total_a = build_record(first, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                           body_a, 12u);
    total_b = build_record(second, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                           body_b, 6u);
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, first, total_a);
    put_ring(0, (start + total_a) % capacity, second, total_b);
    set_cursor(0, (start + total_a + total_b) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 2);
    assert(seen == 2u);
    assert(frame_lengths[0] == 22u);
    assert(frame_lengths[1] == 16u);
    assert(memcmp(frames[0], first + 4, 22u) == 0);
    assert(memcmp(frames[1], second + 4, 16u) == 0);
    assert(ipcc_raw_grab_host_stat_records_ok() == 2u);
    printf("two packets in one advance: both were delivered\n");
}

/* A record whose outer marker is wrong: the record's length is unknown,
 * so the rest of the ring is lost, and the loss is counted and said. */
static void test_bad_marker(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    record[0] = 0x00u;
    record[1] = 0x00u;
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % ipcc_raw_grab_host_ring_capacity());
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 1u);
    assert(ipcc_raw_grab_host_stat_bytes_discarded() == total);
    assert(strstr(notes, "discarded") != NULL);
    /* The same bytes are not reported again: the cursor moved on. */
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);
    assert(ipcc_raw_grab_host_stat_records_bad() == 1u);
    printf("bad marker: nothing delivered, the loss was counted and said\n");
}

/* A record whose inner length cannot hold a frame. */
static void test_bad_inner_length(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    record[2] = 0x09u; /* one byte short of the smallest frame */
    record[3] = 0x00u;
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 1u);
    assert(ipcc_raw_grab_host_stat_bytes_discarded() == total);
    printf("bad inner length: nothing delivered, the loss was counted\n");
}

/* A record whose inner length is larger than a frame can be. */
static void test_oversized_inner_length(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    record[2] = 0xf1u; /* larger than the biggest frame */
    record[3] = 0x01u;
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 1u);
    printf("oversized inner length: nothing delivered\n");
}

/* A record whose frame declares a body length that does not match it.
 * The record's own length is known, so the walk goes on and nothing is
 * lost. */
static void test_bad_declared_length(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    record[10] = 0x05u; /* the frame says five bytes, the record says eight */
    record[11] = 0x00u;
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 1u);
    assert(ipcc_raw_grab_host_stat_bytes_discarded() == 0u);
    printf("bad declared length: nothing delivered, nothing lost\n");
}

/* A frame that is not for the Linux side. */
static void test_other_route(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    record[5] = 0x04u; /* not the route to Linux */
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 1u);
    assert(ipcc_raw_grab_host_stat_bytes_discarded() == 0u);
    printf("other route: nothing delivered\n");
}

/* A frame for another attribute, and a frame for another category. */
static void test_other_attribute(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_OTHER,
                         body, 8u);
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_records_other() == 1u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 0u);

    window_reset();
    total = build_record(record, (uint16_t)0x0082u, (uint16_t)ATTRIBUTE_STILL, body, 8u);
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_records_other() == 1u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 0u);
    printf("other attribute and other category: nothing delivered\n");
}

/* A cursor that is not a position in the ring. */
static void test_invalid_cursor(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    put_ring(0, start, record, total);
    set_cursor(0, 0xffffffffu);
    ipcc_raw_grab_host_watcher_reset(window);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_cursor_errors() == 1u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 0u);
    assert(strstr(notes, "cursor out of range") != NULL);

    /* The end of the ring is not a position in it either. */
    window_reset();
    set_cursor(0, capacity);
    ipcc_raw_grab_host_watcher_reset(window);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);
    assert(frame_count == 0);
    assert(ipcc_raw_grab_host_stat_cursor_errors() == 1u);
    printf("invalid cursor: refused, and nothing was read\n");
}

/* A producer that has not written the whole record out yet. */
static void test_invalid_initial_baseline(void)
{
    unsigned char record[FRAME_CAP];
    const unsigned char body[4] = {1, 2, 3, 4};
    uint32_t start = 100u, seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();
    uint32_t total;

    window_reset();
    set_cursor(0, 0xffffffffu);
    ipcc_raw_grab_host_watcher_reset(window);
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);
    assert(frame_count == 0 && seen == 0);
    assert(ipcc_raw_grab_host_stat_cursor_errors() == 1);

    total = build_record(record, CATEGORY_RESULT, ATTRIBUTE_STILL, body, sizeof(body));
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);
    assert(frame_count == 1 && seen == 1);
    assert(ipcc_raw_grab_host_stat_cursor_errors() == 1);
    assert(memcmp(frames[0], record + 4, total - 4) == 0);
    puts("invalid baseline: no fabricated event; new baseline works");
}

static void test_incomplete_record(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    put_ring(0, start, record, total);
    /* The producer has published ten bytes of the record so far. */
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    set_cursor(0, (start + 10u) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_incomplete() == 1u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 0u);

    /* The record is kept: once it is whole it is delivered. */
    set_cursor(0, (start + total) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);
    assert(frame_count == 1);
    assert(seen == 1u);
    assert(memcmp(frames[0], record + 4, 18u) == 0);
    printf("incomplete record: kept, and delivered once it was whole\n");
}

/* Nothing was published: the cursor did not move. */
static void test_no_advance(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    /* The cursor stays where it was: the producer has published
     * nothing. */
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 0);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_records_ok() == 0u);
    assert(ipcc_raw_grab_host_stat_records_other() == 0u);
    assert(ipcc_raw_grab_host_stat_records_bad() == 0u);
    assert(ipcc_raw_grab_host_stat_frames_rejected() == 0u);
    assert(ipcc_raw_grab_host_stat_bytes_discarded() == 0u);
    assert(ipcc_raw_grab_host_stat_cursor_errors() == 0u);
    assert(ipcc_raw_grab_host_stat_incomplete() == 0u);
    printf("no advance: nothing delivered and nothing counted\n");
}

/* A callback that refuses the frame: the frame is not seen. */
static void test_refused_frame(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 100u;
    uint32_t seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();

    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    set_cursor(0, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(0, start, record, total);
    set_cursor(0, (start + total) % capacity);
    refuse = 1;
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 1);
    assert(seen == 0u);
    assert(ipcc_raw_grab_host_stat_records_ok() == 0u);
    assert(ipcc_raw_grab_host_stat_frames_rejected() == 1u);
    printf("refused frame: the frame was not counted as seen\n");
}

/* A ring other than the first, to show that the geometry the watcher
 * uses is the geometry the fixture is built on. */
static void test_second_ring(void)
{
    unsigned char record[FRAME_CAP];
    unsigned char body[8];
    uint32_t total;
    uint32_t start = 390u;
    uint32_t seen = 0u;
    uint32_t capacity = ipcc_raw_grab_host_ring_capacity();
    int index;

    for (index = 0; index < 8; ++index) {
        body[index] = (unsigned char)(0x90 + index);
    }
    window_reset();
    total = build_record(record, (uint16_t)CATEGORY_RESULT, (uint16_t)ATTRIBUTE_STILL,
                         body, 8u);
    set_cursor(3, start);
    ipcc_raw_grab_host_watcher_reset(window);
    put_ring(3, start, record, total);
    set_cursor(3, (start + total) % capacity);
    ipcc_raw_grab_host_watcher_poll(CATEGORY_RESULT, ATTRIBUTE_STILL, on_frame, &seen);

    assert(frame_count == 1);
    assert(seen == 1u);
    assert(memcmp(frames[0], record + 4, 18u) == 0);
    printf("second ring: the record was delivered\n");
}

int main(void)
{
    window_bytes = ipcc_raw_grab_host_window_bytes();
    window = (unsigned char *)malloc(window_bytes != 0u ? window_bytes : 1u);
    assert(window != NULL);

    test_wrapped_head();
    test_wrapped_body();
    test_two_packets();
    test_bad_marker();
    test_bad_inner_length();
    test_oversized_inner_length();
    test_bad_declared_length();
    test_other_route();
    test_other_attribute();
    test_invalid_cursor();
    test_invalid_initial_baseline();
    test_incomplete_record();
    test_no_advance();
    test_refused_frame();
    test_second_ring();

    free(window);
    printf("ring poll: PASS\n");
    return 0;
}
