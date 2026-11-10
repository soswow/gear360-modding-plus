#include "mcb_ring.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/*
 * Independent model helpers.
 *
 * The occupied set of the ring is derived here by walking forward from the
 * consumer cursor, which is a different derivation from the subtraction used
 * by the planner.  The tests compare invariants, not two copies of the same
 * arithmetic.
 */

#define MODEL_RING_SIZE 395u
#define MODEL_SMALL_RING_MAX 64u

static size_t model_used(size_t capacity, size_t producer, size_t consumer)
{
    size_t index = consumer;
    size_t count = 0;

    if (producer == consumer) {
        return 0;
    }
    do {
        index = (index + 1u == capacity) ? 0u : index + 1u;
        ++count;
    } while (index != producer);
    return count;
}

static size_t model_advance(size_t capacity, size_t start, size_t count)
{
    size_t index = start;
    size_t step;

    for (step = 0; step < count; ++step) {
        index = (index + 1u == capacity) ? 0u : index + 1u;
    }
    return index;
}

/* Marks the bytes the ring already holds, starting at the consumer cursor. */
static void model_fill_occupied(unsigned char *occupied,
                                 size_t capacity,
                                 size_t consumer,
                                 size_t used)
{
    size_t index = consumer;
    size_t step;

    memset(occupied, 0, capacity);
    for (step = 0; step < used; ++step) {
        occupied[index] = 1u;
        index = (index + 1u == capacity) ? 0u : index + 1u;
    }
}

static void fill_sentinel(mcb_ring_admission *plan)
{
    plan->total_bytes = (size_t)0x1111111111111111u;
    plan->start_index = (size_t)0x2222222222222222u;
    plan->first_length = (size_t)0x3333333333333333u;
    plan->second_length = (size_t)0x4444444444444444u;
    plan->new_producer = (size_t)0x5555555555555555u;
}

static void assert_sentinel(const mcb_ring_admission *plan)
{
    assert(plan->total_bytes == (size_t)0x1111111111111111u);
    assert(plan->start_index == (size_t)0x2222222222222222u);
    assert(plan->first_length == (size_t)0x3333333333333333u);
    assert(plan->second_length == (size_t)0x4444444444444444u);
    assert(plan->new_producer == (size_t)0x5555555555555555u);
}

static void test_plan_exact_values(void)
{
    mcb_ring_admission plan;

    /* Empty 395-byte ring, largest outgoing frame: one byte of slack left. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, 0, 388, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 394);
    assert(plan.start_index == 0);
    assert(plan.first_length == 394);
    assert(plan.second_length == 0);
    assert(plan.new_producer == 394);

    /* The packet wraps: it starts at 110 and the next producer is index 5. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 110, 10, 284, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 290);
    assert(plan.start_index == 110);
    assert(plan.first_length == 285);
    assert(plan.second_length == 5);
    assert(plan.new_producer == 5);

    /* The packet ends exactly on the last ring byte: one range, producer 0. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 381, 0, 8, &plan) ==
           MCB_RING_ADMISSION_INSUFFICIENT_SPACE);
    assert_sentinel(&plan);

    /* One byte more free space admits the same 14-byte packet. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 380, 0, 8, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 14);
    assert(plan.start_index == 380);
    assert(plan.first_length == 14);
    assert(plan.second_length == 0);
    assert(plan.new_producer == 394);

    /* Consumer ahead of producer: the free area is the ring tail. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, 394, 8, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 14);
    assert(plan.start_index == 0);
    assert(plan.first_length == 14);
    assert(plan.second_length == 0);
    assert(plan.new_producer == 14);

    /* Only one free byte is left: refused. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 394, 0, 8, &plan) ==
           MCB_RING_ADMISSION_INSUFFICIENT_SPACE);
    assert_sentinel(&plan);
}

static void test_exact_free_is_refused(void)
{
    mcb_ring_admission plan;
    size_t capacity;
    size_t used;

    for (capacity = 16; capacity <= MODEL_RING_SIZE; ++capacity) {
        for (used = 0; used < capacity; ++used) {
            size_t free_bytes = capacity - used;
            size_t producer = (used == 0) ? 0u : used;
            size_t consumer = 0;

            /*
             * The 14-byte packet needs 14 free bytes and must have strictly
             * more than that.
             */
            fill_sentinel(&plan);
            if (free_bytes > 14u) {
                assert(mcb_ring_plan_admission(capacity, producer, consumer, 8,
                                               &plan) == MCB_RING_ADMISSION_OK);
                assert(plan.total_bytes == 14);
                /* One byte of slack survives the admission. */
                assert(model_used(capacity, plan.new_producer, consumer) <=
                       capacity - 1u);
            } else {
                assert(mcb_ring_plan_admission(capacity, producer, consumer, 8,
                                               &plan) ==
                       MCB_RING_ADMISSION_INSUFFICIENT_SPACE);
                assert_sentinel(&plan);
            }

            /*
             * One byte less occupied: the packet fits exactly and still
             * leaves one byte of slack.
             */
            if (used >= 1u && free_bytes >= 14u) {
                size_t smaller_producer = used - 1u;
                size_t smaller_used = model_used(capacity, smaller_producer,
                                                 consumer);
                fill_sentinel(&plan);
                assert(mcb_ring_plan_admission(capacity, smaller_producer,
                                               consumer, 8, &plan) ==
                       MCB_RING_ADMISSION_OK);
                assert(plan.total_bytes == 14);
                assert(model_used(capacity, plan.new_producer, consumer) ==
                       smaller_used + 14u);
                assert(model_used(capacity, plan.new_producer, consumer) <=
                       capacity - 1u);
            }
        }
    }
}

static void test_invalid_arguments(void)
{
    mcb_ring_admission plan;

    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, 0, 8, NULL) ==
           MCB_RING_ADMISSION_INVALID_ARGUMENT);

    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(0, 0, 0, 8, &plan) ==
           MCB_RING_ADMISSION_INVALID_CAPACITY);
    assert_sentinel(&plan);

    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, MODEL_RING_SIZE, 0, 8,
                                   &plan) == MCB_RING_ADMISSION_INVALID_CURSOR);
    assert_sentinel(&plan);

    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, MODEL_RING_SIZE, 8,
                                   &plan) == MCB_RING_ADMISSION_INVALID_CURSOR);
    assert_sentinel(&plan);

    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, MODEL_RING_SIZE + 1u, 0, 8,
                                   &plan) == MCB_RING_ADMISSION_INVALID_CURSOR);
    assert_sentinel(&plan);

    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, MODEL_RING_SIZE + 1u, 8,
                                   &plan) == MCB_RING_ADMISSION_INVALID_CURSOR);
    assert_sentinel(&plan);

    /* Length bounds: 8 and 388 are valid, 7 and 389 are not. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, 0, 0, &plan) ==
           MCB_RING_ADMISSION_INVALID_LENGTH);
    assert_sentinel(&plan);

    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, 0, 7, &plan) ==
           MCB_RING_ADMISSION_INVALID_LENGTH);
    assert_sentinel(&plan);

    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, 0, 389, &plan) ==
           MCB_RING_ADMISSION_INVALID_LENGTH);
    assert_sentinel(&plan);

    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, 0, SIZE_MAX, &plan) ==
           MCB_RING_ADMISSION_INVALID_LENGTH);
    assert_sentinel(&plan);

    /* Even frame lengths are not required. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(MODEL_RING_SIZE, 0, 0, 9, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 15);
    assert(plan.second_length == 0);
}

static void test_size_max_capacity(void)
{
    mcb_ring_admission plan;

    /*
     * Producer at the top of the range: the packet wraps and the addition
     * producer + required would overflow size_t.
     */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(SIZE_MAX, SIZE_MAX - 1u, SIZE_MAX - 10u, 8,
                                   &plan) == MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 14);
    assert(plan.start_index == SIZE_MAX - 1u);
    assert(plan.first_length == 1);
    assert(plan.second_length == 13);
    assert(plan.new_producer == 13);

    /* Consumer at the top of the range, producer at zero. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(SIZE_MAX, 0, SIZE_MAX - 1u, 8, &plan) ==
           MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 14);
    assert(plan.start_index == 0);
    assert(plan.first_length == 14);
    assert(plan.second_length == 0);
    assert(plan.new_producer == 14);

    /* Nearly full: one free byte, refused. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(SIZE_MAX, SIZE_MAX - 1u, 0, 8, &plan) ==
           MCB_RING_ADMISSION_INSUFFICIENT_SPACE);
    assert_sentinel(&plan);

    /* Empty ring at the top of the range. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(SIZE_MAX, SIZE_MAX - 2u, SIZE_MAX - 2u, 8,
                                   &plan) == MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 14);
    assert(plan.start_index == SIZE_MAX - 2u);
    assert(plan.first_length == 2);
    assert(plan.second_length == 12);
    assert(plan.new_producer == 12);

    /* The largest frame at the top of the range. */
    fill_sentinel(&plan);
    assert(mcb_ring_plan_admission(SIZE_MAX, SIZE_MAX - 1u, SIZE_MAX - 1u, 388,
                                   &plan) == MCB_RING_ADMISSION_OK);
    assert(plan.total_bytes == 394);
    assert(plan.first_length == 1);
    assert(plan.second_length == 393);
    assert(plan.new_producer == 393);
}

static void test_exhaustive_small_rings(void)
{
    static const size_t frame_sizes[] = {0, 1, 7, 8, 9, 14, 15, 100, 380,
                                         387, 388, 389, 400, SIZE_MAX};
    unsigned char occupied[MODEL_SMALL_RING_MAX];
    mcb_ring_admission plan;
    size_t capacity;
    size_t producer;
    size_t consumer;
    size_t index;

    for (capacity = 1; capacity <= MODEL_SMALL_RING_MAX; ++capacity) {
        for (producer = 0; producer < capacity; ++producer) {
            for (consumer = 0; consumer < capacity; ++consumer) {
                size_t used = model_used(capacity, producer, consumer);
                size_t free_bytes = capacity - used;

                model_fill_occupied(occupied, capacity, consumer, used);

                for (index = 0;
                     index < sizeof(frame_sizes) / sizeof(frame_sizes[0]);
                     ++index) {
                    size_t frame_size = frame_sizes[index];
                    size_t step;
                    int length_ok = (frame_size >= MCB_FRAME_TX_MIN_SIZE &&
                                     frame_size <= MCB_FRAME_TX_MAX_SIZE);
                    size_t required;

                    fill_sentinel(&plan);
                    if (!length_ok) {
                        assert(mcb_ring_plan_admission(capacity, producer,
                                                       consumer, frame_size,
                                                       &plan) ==
                               MCB_RING_ADMISSION_INVALID_LENGTH);
                        assert_sentinel(&plan);
                        continue;
                    }
                    required = frame_size + MCB_RING_OUTER_OVERHEAD;
                    if (required >= free_bytes) {
                        assert(mcb_ring_plan_admission(capacity, producer,
                                                       consumer, frame_size,
                                                       &plan) ==
                               MCB_RING_ADMISSION_INSUFFICIENT_SPACE);
                        assert_sentinel(&plan);
                        continue;
                    }
                    assert(mcb_ring_plan_admission(capacity, producer, consumer,
                                                   frame_size, &plan) ==
                           MCB_RING_ADMISSION_OK);

                    assert(plan.total_bytes == required);
                    assert(plan.start_index == producer);
                    assert(plan.first_length + plan.second_length ==
                           plan.total_bytes);
                    assert(plan.first_length <= capacity - producer);
                    assert(plan.new_producer ==
                           model_advance(capacity, producer, plan.total_bytes));
                    assert(plan.new_producer < capacity);

                    /* The written bytes must not touch an occupied byte. */
                    for (step = 0; step < plan.total_bytes; ++step) {
                        size_t byte_index =
                            model_advance(capacity, producer, step);
                        assert(occupied[byte_index] == 0u);
                    }

                    /*
                     * The plan stays inside the free area and keeps one byte
                     * of slack, so the ring never reaches the ambiguous
                     * full state.
                     */
                    assert(used + plan.total_bytes <= capacity - 1u);
                    assert(model_used(capacity, plan.new_producer, consumer) ==
                           used + plan.total_bytes);
                    assert(plan.new_producer != consumer);
                }
            }
        }
    }
}

static void test_sequential_admission_keeps_slack(void)
{
    unsigned char occupied[MODEL_RING_SIZE];
    mcb_ring_admission plan;
    size_t producer = 0;
    size_t consumer = 0;
    size_t round;

    memset(occupied, 0, sizeof(occupied));

    for (round = 0; round < 64u; ++round) {
        size_t frame_size = 8u + (round * 7u) % 300u;
        size_t required = frame_size + MCB_RING_OUTER_OVERHEAD;
        size_t used = model_used(MODEL_RING_SIZE, producer, consumer);
        size_t free_bytes = MODEL_RING_SIZE - used;
        size_t step;

        fill_sentinel(&plan);
        if (required >= free_bytes) {
            /* The ring is too full for this packet: admission is refused. */
            assert(mcb_ring_plan_admission(MODEL_RING_SIZE, producer, consumer,
                                           frame_size, &plan) ==
                   MCB_RING_ADMISSION_INSUFFICIENT_SPACE);
            assert_sentinel(&plan);
        } else {
            assert(mcb_ring_plan_admission(MODEL_RING_SIZE, producer, consumer,
                                           frame_size, &plan) ==
                   MCB_RING_ADMISSION_OK);
            assert(plan.total_bytes == required);
            assert(plan.total_bytes < free_bytes);

            for (step = 0; step < plan.total_bytes; ++step) {
                size_t byte_index =
                    model_advance(MODEL_RING_SIZE, producer, step);
                assert(occupied[byte_index] == 0u);
                occupied[byte_index] = 1u;
            }
            producer = plan.new_producer;

            /* One byte of slack survives every admission. */
            assert(model_used(MODEL_RING_SIZE, producer, consumer) <=
                   MODEL_RING_SIZE - 1u);
            assert(producer != consumer);
        }

        /* The consumer drains part of the ring every other round. */
        if ((round % 2u) == 1u) {
            size_t half = model_used(MODEL_RING_SIZE, producer, consumer) / 2u;
            for (step = 0; step < half; ++step) {
                occupied[consumer] = 0u;
                consumer = (consumer + 1u == MODEL_RING_SIZE) ? 0u
                                                             : consumer + 1u;
            }
        }
    }
}

int main(void)
{
    test_plan_exact_values();
    test_exact_free_is_refused();
    test_invalid_arguments();
    test_size_max_capacity();
    test_exhaustive_small_rings();
    test_sequential_admission_keeps_slack();
    return 0;
}
