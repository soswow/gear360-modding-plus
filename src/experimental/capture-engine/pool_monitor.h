#ifndef G360_POOL_MONITOR_H
#define G360_POOL_MONITOR_H

/*
 * Capture-pool lifecycle monitor.
 *
 * Interprets snapshots of the RTOS CCaptureMemoryManager block records
 * (manager + 0xb0 + index * 16, one record per 16 KiB block of the pool at
 * 0xA6000000) and tracks the id-22 RAW reservation (memory slot 28) through
 * reserve -> release. The caller supplies snapshots; this module does no I/O.
 *
 * Observed record layout (live camera, 19 October 2026): word0 = memory slot,
 * word1 = blocks remaining in the run from this block, word2/word3 non-zero on
 * the run head only. Free blocks are all zero.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define G360_POOL_BASE UINT32_C(0xA6000000)
#define G360_POOL_BLOCK_BYTES UINT32_C(0x4000)
#define G360_POOL_BLOCK_COUNT ((size_t)26624)
#define G360_POOL_RECORD_WORDS ((size_t)4)
#define G360_RAW_SLOT UINT32_C(28)
#define G360_RAW_RESERVATION_BLOCKS UINT32_C(4096)

typedef struct g360_pool_run {
    uint32_t first_block;
    uint32_t slot;
    uint32_t block_count;
    uint32_t head_word2;
    uint32_t head_word3;
} g360_pool_run;

/* Physical address of a block index inside the pool. */
uint32_t g360_pool_block_address(uint32_t block);

/*
 * Lists allocation runs in a record snapshot of `block_count` records
 * (4 words each). A run starts at a record with a non-zero slot and a
 * non-zero remaining count; its length is that count, clipped to the table.
 * Returns the number of runs found, which may exceed `capacity`; only the
 * first `capacity` are stored.
 */
size_t g360_pool_list_runs(const uint32_t *records, size_t block_count,
                           g360_pool_run *runs, size_t capacity);

typedef enum g360_raw_phase {
    G360_RAW_IDLE = 0,
    /* A slot-28 run of the full reservation size exists. */
    G360_RAW_RESERVED = 1,
    /* The tracked run no longer exists: the RTOS released the reservation. */
    G360_RAW_RELEASED = 2
} g360_raw_phase;

typedef struct g360_raw_tracker {
    g360_raw_phase phase;
    g360_pool_run reservation;
    uint32_t reserved_at_us;
    uint32_t released_at_us;
} g360_raw_tracker;

void g360_raw_tracker_reset(g360_raw_tracker *tracker);

/*
 * Feeds one snapshot taken at `time_us`. Tracks the first full-size slot-28
 * run that appears after reset; a later reservation is ignored until reset.
 * A run is "the same" only while its first block, slot, length and head
 * words are unchanged, so a release followed by a new reservation at the
 * same address is reported as a release.
 * Returns the phase after the update.
 */
g360_raw_phase g360_raw_tracker_update(g360_raw_tracker *tracker,
                                       const uint32_t *records,
                                       size_t block_count,
                                       uint32_t time_us);

#ifdef __cplusplus
}
#endif

#endif
