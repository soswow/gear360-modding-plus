#include "pool_monitor.h"

uint32_t g360_pool_block_address(uint32_t block) {
    return G360_POOL_BASE + block * G360_POOL_BLOCK_BYTES;
}

size_t g360_pool_list_runs(const uint32_t *records, size_t block_count,
                           g360_pool_run *runs, size_t capacity) {
    size_t found = 0;
    size_t index = 0;
    while (index < block_count) {
        const uint32_t *record = records + index * G360_POOL_RECORD_WORDS;
        uint32_t slot = record[0];
        uint32_t remaining = record[1];
        if (slot == 0 || remaining == 0) {
            index++;
            continue;
        }
        /* A corrupt or mid-update count must not run past the table. */
        if (remaining > block_count - index) {
            remaining = (uint32_t)(block_count - index);
        }
        if (found < capacity) {
            runs[found].first_block = (uint32_t)index;
            runs[found].slot = slot;
            runs[found].block_count = remaining;
            runs[found].head_word2 = record[2];
            runs[found].head_word3 = record[3];
        }
        found++;
        index += remaining;
    }
    return found;
}

void g360_raw_tracker_reset(g360_raw_tracker *tracker) {
    tracker->phase = G360_RAW_IDLE;
    tracker->reservation.first_block = 0;
    tracker->reservation.slot = 0;
    tracker->reservation.block_count = 0;
    tracker->reservation.head_word2 = 0;
    tracker->reservation.head_word3 = 0;
    tracker->reserved_at_us = 0;
    tracker->released_at_us = 0;
}

static int same_run(const g360_pool_run *a, const g360_pool_run *b) {
    return a->first_block == b->first_block && a->slot == b->slot &&
           a->block_count == b->block_count && a->head_word2 == b->head_word2 &&
           a->head_word3 == b->head_word3;
}

static int is_full_raw_reservation(const g360_pool_run *run) {
    return run->slot == G360_RAW_SLOT &&
           run->block_count == G360_RAW_RESERVATION_BLOCKS &&
           (run->head_word2 != 0 || run->head_word3 != 0);
}

g360_raw_phase g360_raw_tracker_update(g360_raw_tracker *tracker,
                                       const uint32_t *records,
                                       size_t block_count,
                                       uint32_t time_us) {
    if (tracker->phase == G360_RAW_RELEASED) {
        return tracker->phase;
    }
    /* Walk runs one at a time so no large buffer is needed. */
    size_t index = 0;
    int still_present = 0;
    while (index < block_count) {
        const uint32_t *record = records + index * G360_POOL_RECORD_WORDS;
        if (record[0] == 0 || record[1] == 0) {
            index++;
            continue;
        }
        g360_pool_run run;
        uint32_t remaining = record[1];
        if (remaining > block_count - index) {
            remaining = (uint32_t)(block_count - index);
        }
        run.first_block = (uint32_t)index;
        run.slot = record[0];
        run.block_count = remaining;
        run.head_word2 = record[2];
        run.head_word3 = record[3];
        if (tracker->phase == G360_RAW_IDLE && is_full_raw_reservation(&run)) {
            tracker->reservation = run;
            tracker->reserved_at_us = time_us;
            tracker->phase = G360_RAW_RESERVED;
            return tracker->phase;
        }
        if (tracker->phase == G360_RAW_RESERVED && same_run(&run, &tracker->reservation)) {
            still_present = 1;
            break;
        }
        index += remaining;
    }
    if (tracker->phase == G360_RAW_RESERVED && !still_present) {
        tracker->released_at_us = time_us;
        tracker->phase = G360_RAW_RELEASED;
    }
    return tracker->phase;
}
