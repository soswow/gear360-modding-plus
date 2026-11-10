#include "mcb_producer_ring.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Contract-level checks for the producer reference model.
 *
 * The model reproduces the stock RTOS writer's behaviour, including the
 * behaviour that is unsafe for a receiver.  These checks describe what the
 * model computes.  They are not a live proof, not a transport test and not a
 * guarantee about a running camera.
 *
 * The fixed vectors below come from the pinned RTOS image through the
 * connected REA/Ghidra session.  The sweeps re-derive an ARITHMETIC property
 * of the model: with the two cursor values taken as given, an admitted write
 * does not overlap the interval [R, W) that the model treats as unread.
 *
 * That property is conditional on the cursor values being truthful, and the
 * model cannot tell whether they are.  producer == consumer is ambiguous
 * between an empty ring and a full ring without an external no-full
 * invariant, so nothing here detects a stale cursor, and nothing here
 * establishes cross-CPU visibility or a live receiver guarantee.
 *
 * The ring size below is a SYNTHESIS COUNTEREXAMPLE INPUT, not an established
 * capacity of the shared control ring.  395 is the ordinary bridge sender's
 * ceiling on one message; the shared ring's live capacity is not established.
 */
#define SYNTHETIC_RING_SIZE 395u
#define BRIDGE_MESSAGE_CEILING 395u

static void check_free_distance(void)
{
    /* FUN_800377dc returns the capacity for equal cursors. */
    assert(mcb_producer_free_distance(SYNTHETIC_RING_SIZE, 0u, 0u) == SYNTHETIC_RING_SIZE);
    /* The countermodel state: N = 395, W = 62, R = 0. */
    assert(mcb_producer_free_distance(SYNTHETIC_RING_SIZE, 62u, 0u) == 333u);
    /* The raw comparison makes the free distance small when R > W. */
    assert(mcb_producer_free_distance(SYNTHETIC_RING_SIZE, 0u, 30u) == 30u);
    assert(mcb_producer_free_distance(SYNTHETIC_RING_SIZE, 62u, 30u) == 363u);
    /* An out-of-range cursor yields zero. */
    assert(mcb_producer_free_distance(SYNTHETIC_RING_SIZE, SYNTHETIC_RING_SIZE, 0u) == 0u);
    assert(mcb_producer_free_distance(SYNTHETIC_RING_SIZE, 0u, SYNTHETIC_RING_SIZE) == 0u);
    assert(mcb_producer_free_distance(0u, 0u, 0u) == 0u);
}

static void check_still_request_extent(void)
{
    mcb_producer_write plan;

    /* The real 48-byte still request composes to a 56-byte message. */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 0u, 0u, 56u, &plan) == MCB_PRODUCER_OK);
    assert(plan.ring_extent == 62u);
    assert(plan.new_producer == 62u);
    assert(plan.collapses == 0);
    assert(plan.clobbers_unread == 0);
    assert(plan.bytes_before_wrap == 62u);
    assert(plan.bytes_after_wrap == 0u);
}

static void check_countermodel_schedule(void)
{
    mcb_producer_write plan;

    /*
     * A consumer that never publishes its cursor leaves R at 0.  The
     * producer then admits the schedule 56, 327, 56 and the third write
     * lands on the bytes the paused reader is copying.
     */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 0u, 0u, 56u, &plan) == MCB_PRODUCER_OK);
    assert(plan.new_producer == 62u);
    assert(plan.collapses == 0);

    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 62u, 0u, 327u, &plan) == MCB_PRODUCER_OK);
    assert(plan.free_distance == 333u);
    assert(plan.new_producer == 0u);
    assert(plan.collapses == 1);
    assert(plan.clobbers_unread == 0);

    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 0u, 0u, 56u, &plan) == MCB_PRODUCER_OK);
    assert(plan.new_producer == 62u);
}

static void check_countermodel_refused_with_truthful_cursor(void)
{
    mcb_producer_write plan;

    /* The consumer copies 30 bytes and publishes R = 30. */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 62u, 30u, 327u, &plan) == MCB_PRODUCER_OK);
    assert(plan.new_producer == 0u);
    assert(plan.clobbers_unread == 0);

    /* The third write is now refused: 56 + 5 is not below the free distance. */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 0u, 30u, 56u, &plan) == MCB_PRODUCER_REFUSED);
}

static void check_state(void)
{
    mcb_producer_state state;

    assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, 0u, 0u, &state) == MCB_PRODUCER_OK);
    assert(state.free_distance == SYNTHETIC_RING_SIZE);
    assert(state.occupied == 0u);
    assert(state.empty == 1);
    assert(state.near_full == 0);
    assert(state.max_admitted_message == 389u);
    assert(state.collapse_message_length == 389u);

    assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, 62u, 0u, &state) == MCB_PRODUCER_OK);
    assert(state.free_distance == 333u);
    assert(state.occupied == 62u);
    assert(state.empty == 0);
    assert(state.max_admitted_message == 327u);

    /* A ring with five free bytes cannot take any message. */
    assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, 390u, 0u, &state) == MCB_PRODUCER_OK);
    assert(state.free_distance == 5u);
    assert(state.near_full == 1);
    assert(state.max_admitted_message == 0u);
    assert(state.collapse_message_length == 0u);
}

static void check_collapse_is_the_largest_message(void)
{
    size_t capacity;
    size_t producer;
    size_t consumer;

    for (capacity = 8u; capacity <= 64u; capacity += 8u) {
        for (producer = 0u; producer < capacity; ++producer) {
            for (consumer = 0u; consumer < capacity; ++consumer) {
                mcb_producer_state state;
                mcb_producer_write plan;

                assert(mcb_producer_describe(capacity, producer, consumer, &state) == MCB_PRODUCER_OK);
                if (state.max_admitted_message == 0u) {
                    continue;
                }
                assert(mcb_producer_plan_write(capacity, producer, consumer,
                                               state.max_admitted_message, &plan) == MCB_PRODUCER_OK);
                assert(plan.collapses == 1);
                assert(plan.new_producer == consumer);

                /* One byte more than the free distance is refused. */
                assert(mcb_producer_plan_write(capacity, producer, consumer,
                                               state.max_admitted_message + 1u, &plan)
                       == MCB_PRODUCER_REFUSED);
            }
        }
    }
}

static void check_write_avoids_the_assumed_unread_interval(void)
{
    size_t capacity;
    size_t producer;
    size_t consumer;
    size_t message;

    /*
     * Exhaustive sweep over small rings.
     *
     * What is checked: an admitted write does not overlap the interval
     * [R, W) that the model treats as unread, given the cursor values passed
     * in.  What is NOT checked: that those cursor values were truthful.  The
     * model cannot detect a stale cursor, because equal cursors are ambiguous
     * between an empty ring and a full ring.
     */
    for (capacity = 4u; capacity <= 64u; capacity += 4u) {
        for (producer = 0u; producer < capacity; ++producer) {
            for (consumer = 0u; consumer < capacity; ++consumer) {
                for (message = 0u; message <= BRIDGE_MESSAGE_CEILING; ++message) {
                    mcb_producer_write plan;
                    mcb_producer_status status =
                        mcb_producer_plan_write(capacity, producer, consumer, message, &plan);

                    if (status != MCB_PRODUCER_OK) {
                        assert(status == MCB_PRODUCER_REFUSED);
                        continue;
                    }
                    assert(plan.clobbers_unread == 0);
                }
            }
        }
    }

    /*
     * The synthesis ring: the written set grows monotonically with the
     * message length, so the largest admitted message covers every smaller
     * one.
     */
    for (producer = 0u; producer < SYNTHETIC_RING_SIZE; ++producer) {
        for (consumer = 0u; consumer < SYNTHETIC_RING_SIZE; ++consumer) {
            mcb_producer_state state;
            mcb_producer_write plan;

            assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, producer, consumer, &state)
                   == MCB_PRODUCER_OK);
            if (state.max_admitted_message == 0u) {
                continue;
            }
            assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, producer, consumer, 0u, &plan)
                   == MCB_PRODUCER_OK);
            assert(plan.clobbers_unread == 0);
            assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, producer, consumer,
                                           state.max_admitted_message, &plan)
                   == MCB_PRODUCER_OK);
            assert(plan.clobbers_unread == 0);
        }
    }
}

static void check_wrap_branches(void)
{
    mcb_producer_write plan;

    /* N - W < 6: the outer header straddles the ring end. */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 392u, 100u, 20u, &plan) == MCB_PRODUCER_OK);
    assert(plan.bytes_before_wrap == 3u);
    assert(plan.bytes_after_wrap == 3u + 20u);
    assert(plan.clobbers_unread == 0);

    /* A message that fits after the header is written in one piece. */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 100u, 50u, 40u, &plan) == MCB_PRODUCER_OK);
    assert(plan.bytes_before_wrap == 46u);
    assert(plan.bytes_after_wrap == 0u);
    assert(plan.clobbers_unread == 0);
}

/*
 * The exact-free boundary on the wrap branch whose header fits before the
 * boundary.  The stock writer admits a message that consumes the exact free
 * distance, takes the tail clamp one byte short, and still publishes the full
 * requested extent.  The model keeps that behaviour; it is not corrected
 * here.
 */
static void check_exact_free_wrap_short_copy(void)
{
    mcb_producer_write plan;
    mcb_producer_state state;

    /* N = 395, W = 100, R = 50: the free distance is 345. */
    assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, 100u, 50u, &state) == MCB_PRODUCER_OK);
    assert(state.free_distance == 345u);
    assert(state.max_admitted_message == 339u);

    /* One byte below the exact free distance: no short copy. */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 100u, 50u, 338u, &plan) == MCB_PRODUCER_OK);
    assert(plan.ring_extent == 344u);
    assert(plan.bytes_before_wrap == 295u);
    assert(plan.bytes_after_wrap == 49u);
    assert(plan.bytes_before_wrap + plan.bytes_after_wrap == plan.ring_extent);
    assert(plan.collapses == 0);
    assert(plan.clobbers_unread == 0);

    /*
     * The exact free distance: admitted, one byte short in the copy, the
     * published cursor equal to the consumer cursor, so the ring collapses.
     */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 100u, 50u, 339u, &plan) == MCB_PRODUCER_OK);
    assert(plan.ring_extent == 345u);
    assert(plan.bytes_before_wrap == 295u);
    assert(plan.bytes_after_wrap == 49u);
    assert(plan.bytes_before_wrap + plan.bytes_after_wrap == plan.ring_extent - 1u);
    assert(plan.new_producer == 50u);
    assert(plan.collapses == 1);
    assert(plan.clobbers_unread == 0);

    /* One byte more than the exact free distance is refused. */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 100u, 50u, 340u, &plan)
           == MCB_PRODUCER_REFUSED);
}

/*
 * Equal cursors are ambiguous.  The model reads producer == consumer as empty,
 * and a ring the producer has just collapsed into the reader is described the
 * same way as a fresh empty ring.  The model therefore cannot tell a full ring
 * from an empty one, and cannot detect a stale cursor.
 */
static void check_equal_cursors_are_ambiguous(void)
{
    mcb_producer_state before;
    mcb_producer_state after;
    mcb_producer_write plan;

    assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, 0u, 0u, &before) == MCB_PRODUCER_OK);

    /* A write that collapses the ring onto the consumer cursor. */
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 62u, 0u, 327u, &plan) == MCB_PRODUCER_OK);
    assert(plan.collapses == 1);
    assert(plan.new_producer == 0u);

    /* The collapsed ring is described exactly like the fresh empty ring. */
    assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, 0u, 0u, &after) == MCB_PRODUCER_OK);
    assert(after.free_distance == before.free_distance);
    assert(after.occupied == before.occupied);
    assert(after.empty == before.empty);
    assert(after.empty == 1);
    assert(after.max_admitted_message == before.max_admitted_message);
}

static void check_invalid_arguments(void)
{
    mcb_producer_write plan;
    mcb_producer_state state;

    memset(&plan, 0xa5, sizeof plan);
    memset(&state, 0xa5, sizeof state);

    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 0u, 0u, 56u, NULL) == MCB_PRODUCER_INVALID_ARGUMENT);
    assert(mcb_producer_plan_write(0u, 0u, 0u, 56u, &plan) == MCB_PRODUCER_INVALID_CAPACITY);
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, SYNTHETIC_RING_SIZE, 0u, 56u, &plan)
           == MCB_PRODUCER_INVALID_CURSOR);
    assert(mcb_producer_plan_write(SYNTHETIC_RING_SIZE, 0u, SYNTHETIC_RING_SIZE, 56u, &plan)
           == MCB_PRODUCER_INVALID_CURSOR);
    assert(plan.ring_extent == (size_t)0xa5a5a5a5a5a5a5a5ull);

    assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, 0u, 0u, NULL) == MCB_PRODUCER_INVALID_ARGUMENT);
    assert(mcb_producer_describe(0u, 0u, 0u, &state) == MCB_PRODUCER_INVALID_CAPACITY);
    assert(mcb_producer_describe(SYNTHETIC_RING_SIZE, SYNTHETIC_RING_SIZE, 0u, &state)
           == MCB_PRODUCER_INVALID_CURSOR);
    assert(state.free_distance == (size_t)0xa5a5a5a5a5a5a5a5ull);
}

static void check_ring_advance(void)
{
    /* The ordinary control frame: inner size 58, ring advance 62. */
    assert(mcb_producer_ring_advance(58u) == 62u);
    assert(mcb_producer_ring_advance(0u) == 4u);
    assert(mcb_producer_ring_advance(1u) == 5u);
    assert(mcb_producer_ring_advance(2u) == 6u);
    assert(mcb_producer_ring_advance(3u) == 7u);
}

/*
 * The advance overflow boundary.  An inner length above SIZE_MAX - 4 cannot be
 * advanced; the function reports 0.  Every valid result is at least 4, so 0
 * separates the failure from every valid advance.
 */
static void check_ring_advance_overflow(void)
{
    /* The largest inner length that can still be advanced. */
    assert(mcb_producer_ring_advance(SIZE_MAX - 4u) == SIZE_MAX);
    assert(mcb_producer_ring_advance(SIZE_MAX - 5u) == SIZE_MAX - 1u);

    /* One byte past the boundary and beyond: reported as failure. */
    assert(mcb_producer_ring_advance(SIZE_MAX - 3u) == 0u);
    assert(mcb_producer_ring_advance(SIZE_MAX - 2u) == 0u);
    assert(mcb_producer_ring_advance(SIZE_MAX - 1u) == 0u);
    assert(mcb_producer_ring_advance(SIZE_MAX) == 0u);

    /* No valid advance is zero, so zero is never confused with a result. */
    assert(mcb_producer_ring_advance(0u) != 0u);
    assert(mcb_producer_ring_advance(58u) != 0u);
    assert(mcb_producer_ring_advance(SIZE_MAX - 4u) != 0u);
}

int main(void)
{
    check_free_distance();
    check_still_request_extent();
    check_countermodel_schedule();
    check_countermodel_refused_with_truthful_cursor();
    check_state();
    check_collapse_is_the_largest_message();
    check_write_avoids_the_assumed_unread_interval();
    check_wrap_branches();
    check_exact_free_wrap_short_copy();
    check_equal_cursors_are_ambiguous();
    check_invalid_arguments();
    check_ring_advance();
    check_ring_advance_overflow();
    return 0;
}
