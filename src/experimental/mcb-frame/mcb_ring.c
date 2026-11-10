#include "mcb_ring.h"

mcb_ring_admission_status mcb_ring_plan_admission(size_t capacity,
                                                  size_t producer,
                                                  size_t consumer,
                                                  size_t frame_size,
                                                  mcb_ring_admission *out_plan)
{
    size_t used;
    size_t free_bytes;
    size_t required;
    size_t first_length;
    size_t new_producer;

    if (out_plan == NULL) {
        return MCB_RING_ADMISSION_INVALID_ARGUMENT;
    }
    if (capacity == 0) {
        return MCB_RING_ADMISSION_INVALID_CAPACITY;
    }
    if (producer >= capacity || consumer >= capacity) {
        return MCB_RING_ADMISSION_INVALID_CURSOR;
    }
    if (frame_size < MCB_FRAME_TX_MIN_SIZE || frame_size > MCB_FRAME_TX_MAX_SIZE) {
        return MCB_RING_ADMISSION_INVALID_LENGTH;
    }

    /* frame_size is bounded above, so the addition cannot overflow. */
    required = frame_size + MCB_RING_OUTER_OVERHEAD;

    /*
     * used = (producer + capacity - consumer) mod capacity, evaluated
     * without the overflowing producer + capacity term.  The two branches
     * differ exactly at producer == consumer, where used is 0.
     */
    if (producer >= consumer) {
        used = producer - consumer;
    } else {
        used = capacity - (consumer - producer);
    }
    /* used is 0 .. capacity-1, so free_bytes is 1 .. capacity. */
    free_bytes = capacity - used;

    /*
     * Strictly less than free.  Equality is refused so that one byte of
     * slack always remains and W == R keeps meaning "empty".
     */
    if (required >= free_bytes) {
        return MCB_RING_ADMISSION_INSUFFICIENT_SPACE;
    }

    /*
     * required < free_bytes <= capacity, so capacity - required is at least 1
     * and cannot wrap.  The comparison below is the overflow free form of
     * "producer + required >= capacity": it holds also in the exact tail
     * case, where the packet ends on the last ring byte and the new producer
     * is 0.
     */
    if (producer >= capacity - required) {
        first_length = capacity - producer;
        new_producer = producer - (capacity - required);
    } else {
        first_length = required;
        new_producer = producer + required;
    }

    out_plan->total_bytes = required;
    out_plan->start_index = producer;
    out_plan->first_length = first_length;
    out_plan->second_length = required - first_length;
    out_plan->new_producer = new_producer;
    return MCB_RING_ADMISSION_OK;
}
