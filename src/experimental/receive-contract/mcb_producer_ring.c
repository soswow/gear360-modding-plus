#include "mcb_producer_ring.h"

/*
 * The component is freestanding: it includes no libc header, so that the
 * ARMv7 target check compiles without a cross sysroot.  The two structures
 * below are therefore cleared by hand.
 */
static void clear_state(mcb_producer_state *state)
{
    unsigned char *bytes = (unsigned char *)state;
    size_t i;

    for (i = 0u; i < sizeof *state; ++i) {
        bytes[i] = 0u;
    }
}

static void clear_write(mcb_producer_write *write)
{
    unsigned char *bytes = (unsigned char *)write;
    size_t i;

    for (i = 0u; i < sizeof *write; ++i) {
        bytes[i] = 0u;
    }
}

/*
 * Transcription of FUN_800377dc.
 *
 * The source loads the producer cursor W, the consumer cursor R and the
 * capacity N, requires both cursors to be below N, and then returns
 * (R + N) - W when R <= W or R - W when R > W.  Equal cursors therefore
 * return the capacity, which is how the producer reads an empty ring.
 */
size_t mcb_producer_free_distance(size_t capacity, size_t producer, size_t consumer)
{
    if (capacity == 0u) {
        return 0u;
    }
    if (producer >= capacity || consumer >= capacity) {
        return 0u;
    }
    if (consumer <= producer) {
        return (consumer + capacity) - producer;
    }
    return consumer - producer;
}

mcb_producer_status mcb_producer_describe(size_t capacity,
                                          size_t producer,
                                          size_t consumer,
                                          mcb_producer_state *out_state)
{
    mcb_producer_state state;
    size_t free_distance;

    if (out_state == NULL) {
        return MCB_PRODUCER_INVALID_ARGUMENT;
    }
    if (capacity == 0u) {
        return MCB_PRODUCER_INVALID_CAPACITY;
    }
    if (producer >= capacity || consumer >= capacity) {
        return MCB_PRODUCER_INVALID_CURSOR;
    }

    free_distance = mcb_producer_free_distance(capacity, producer, consumer);

    clear_state(&state);
    state.free_distance = free_distance;
    state.occupied = capacity - free_distance;
    state.empty = (producer == consumer) ? 1 : 0;
    state.near_full = (free_distance <= 5u) ? 1 : 0;

    /*
     * The writer admits M when M + 5 < free, that is M <= free - 6.  A
     * message of exactly free - 6 bytes occupies the whole free distance and
     * publishes the producer cursor equal to the consumer cursor.
     */
    if (free_distance > 5u) {
        state.max_admitted_message = free_distance - 6u;
        state.collapse_message_length = free_distance - 6u;
    }

    *out_state = state;
    return MCB_PRODUCER_OK;
}

/* Does the linear interval [start, start + length) meet [lo, hi)? */
static int interval_overlaps(size_t start, size_t length, size_t lo, size_t hi)
{
    if (length == 0u || hi <= lo) {
        return 0;
    }
    return (start < hi) && (lo < start + length);
}

/*
 * Does a write covering the given linear intervals touch the interval the
 * consumer has not consumed?
 *
 * The unread region is ASSUMED to run forward from the consumer cursor to the
 * producer cursor, taken from the two cursor values passed in.  When the
 * producer cursor is numerically below the consumer cursor the region wraps
 * and has two linear parts.
 *
 * This is an overlap calculation against an assumed interval.  It does not
 * establish that the consumer cursor was truthful and it cannot detect a
 * stale cursor, because producer == consumer is ambiguous between an empty
 * ring and a full ring without an external no-full invariant.
 */
static int write_touches_unread(size_t capacity,
                                size_t producer,
                                size_t consumer,
                                const size_t *starts,
                                const size_t *lengths,
                                size_t count)
{
    size_t i;
    int hit = 0;

    for (i = 0u; i < count; ++i) {
        if (lengths[i] == 0u) {
            continue;
        }
        if (producer >= consumer) {
            if (interval_overlaps(starts[i], lengths[i], consumer, producer)) {
                hit = 1;
            }
        } else {
            if (interval_overlaps(starts[i], lengths[i], consumer, capacity)
                || interval_overlaps(starts[i], lengths[i], 0u, producer)) {
                hit = 1;
            }
        }
        if (hit) {
            break;
        }
    }
    return hit;
}

mcb_producer_status mcb_producer_plan_write(size_t capacity,
                                            size_t producer,
                                            size_t consumer,
                                            size_t message_length,
                                            mcb_producer_write *out_write)
{
    mcb_producer_write plan;
    size_t free_distance;
    size_t starts[3];
    size_t lengths[3];
    size_t count;
    size_t copy_length;

    if (out_write == NULL) {
        return MCB_PRODUCER_INVALID_ARGUMENT;
    }
    if (capacity == 0u) {
        return MCB_PRODUCER_INVALID_CAPACITY;
    }
    if (producer >= capacity || consumer >= capacity) {
        return MCB_PRODUCER_INVALID_CURSOR;
    }

    free_distance = mcb_producer_free_distance(capacity, producer, consumer);

    /*
     * The writer's admission: message_length + 5 < free distance.  The first
     * test keeps the addition below from wrapping; a message that large can
     * never be admitted anyway, so refusing it here preserves the writer's own
     * outcome.
     */
    if (message_length > SIZE_MAX - 5u) {
        return MCB_PRODUCER_REFUSED;
    }
    if (message_length + 5u >= free_distance) {
        return MCB_PRODUCER_REFUSED;
    }

    clear_write(&plan);
    plan.free_distance = free_distance;
    /*
     * The admission above already bounds message_length + 6 by the free
     * distance, so the extent cannot wrap here.
     */
    plan.ring_extent = message_length + MCB_PRODUCER_OUTER_HEADER;
    if (plan.ring_extent > SIZE_MAX - producer) {
        return MCB_PRODUCER_INVALID_CAPACITY;
    }
    plan.new_producer = (producer + plan.ring_extent) % capacity;

    count = 0u;

    if (producer < consumer) {
        /*
         * FUN_80037d6c branch at 0x80037f54: the copy length is
         * min(R - 1 - W, M) and the six-byte header is not part of that
         * clamp.  The admission above keeps the payload inside [W, R).
         */
        copy_length = (consumer - 1u - producer) < message_length
                          ? (consumer - 1u - producer)
                          : message_length;
        starts[count] = producer;
        lengths[count] = MCB_PRODUCER_OUTER_HEADER;
        count += 1u;
        starts[count] = producer + MCB_PRODUCER_OUTER_HEADER;
        lengths[count] = copy_length;
        count += 1u;
        plan.bytes_before_wrap = MCB_PRODUCER_OUTER_HEADER + copy_length;
        plan.bytes_after_wrap = 0u;
    } else if (consumer == 0u) {
        /*
         * FUN_80037d6c branch at 0x80037e74: the copy length is
         * min(N - 1 - W, M) and the write is linear, so the header sits at
         * the producer cursor and the payload follows it.
         */
        copy_length = (capacity - 1u - producer) < message_length
                          ? (capacity - 1u - producer)
                          : message_length;
        starts[count] = producer;
        lengths[count] = MCB_PRODUCER_OUTER_HEADER;
        count += 1u;
        starts[count] = producer + MCB_PRODUCER_OUTER_HEADER;
        lengths[count] = copy_length;
        count += 1u;
        plan.bytes_before_wrap = MCB_PRODUCER_OUTER_HEADER + copy_length;
        plan.bytes_after_wrap = 0u;
    } else if (capacity - producer < MCB_PRODUCER_OUTER_HEADER) {
        /*
         * FUN_80037d6c branch at 0x8003802c: the outer header straddles the
         * ring end and the payload is copied in full after the wrap, with no
         * clamp against the consumer cursor.  The admission above keeps that
         * copy inside the consumed region.
         */
        size_t head = capacity - producer;

        starts[count] = producer;
        lengths[count] = head;
        count += 1u;
        starts[count] = 0u;
        lengths[count] = MCB_PRODUCER_OUTER_HEADER - head;
        count += 1u;
        starts[count] = MCB_PRODUCER_OUTER_HEADER - head;
        lengths[count] = message_length;
        count += 1u;
        plan.bytes_before_wrap = head;
        plan.bytes_after_wrap = (MCB_PRODUCER_OUTER_HEADER - head) + message_length;
    } else {
        /*
         * FUN_80037d6c branch at 0x80037fb4: the header and the first
         * payload segment are written linearly, and the wrapped tail is
         * clamped to the consumer cursor.
         *
         * A message that consumes the EXACT free distance takes that clamp
         * one byte short: the tail is limited to R - 1, so one message byte
         * is not copied, while the published cursor still advances by the
         * full requested extent.  That short copy is the stock behaviour and
         * is kept here.
         */
        size_t room = capacity - producer - MCB_PRODUCER_OUTER_HEADER;
        size_t tail = 0u;

        copy_length = room < message_length ? room : message_length;
        starts[count] = producer;
        lengths[count] = MCB_PRODUCER_OUTER_HEADER;
        count += 1u;
        starts[count] = producer + MCB_PRODUCER_OUTER_HEADER;
        lengths[count] = copy_length;
        count += 1u;
        plan.bytes_before_wrap = MCB_PRODUCER_OUTER_HEADER + copy_length;

        if (copy_length < message_length) {
            size_t remaining = message_length - copy_length;
            size_t limit = consumer - 1u;

            tail = remaining < limit ? remaining : limit;
            starts[count] = 0u;
            lengths[count] = tail;
            count += 1u;
            plan.bytes_after_wrap = tail;
        }
    }

    plan.collapses = (plan.new_producer == consumer) ? 1 : 0;
    plan.clobbers_unread =
        write_touches_unread(capacity, producer, consumer, starts, lengths, count);

    *out_write = plan;
    return MCB_PRODUCER_OK;
}

size_t mcb_producer_ring_advance(size_t inner_length)
{
    /*
     * Four outer marker bytes plus the inner frame.
     *
     * The sum is checked before it is formed.  Without the check an
     * inner_length above SIZE_MAX - 4 would wrap and return a small advance,
     * which a caller could then use to move a consumer cursor by the wrong
     * amount.  Every valid result is at least 4, so 0 is returned as the
     * failure marker and is never a valid advance.
     */
    if (inner_length > SIZE_MAX - 4u) {
        return 0u;
    }
    return inner_length + 4u;
}
