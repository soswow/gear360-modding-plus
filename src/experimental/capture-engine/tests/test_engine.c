/*
 * Host checks for the capture-engine components.
 *
 * Optional real-data checks (local files are not in Git):
 *   G360_TIMELINE_LOG=<observer out file>  replay a recorded camera timeline
 *   G360_RAW_FILE=<33,732,608-byte dump>   scan a recorded RAW raster
 */
#include "pool_monitor.h"
#include "raw_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                              \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static uint32_t *new_table(void) {
    return calloc(G360_POOL_BLOCK_COUNT * G360_POOL_RECORD_WORDS, sizeof(uint32_t));
}

/* Writes a run the way the RTOS does: slot, remaining count, head words on the first block. */
static void put_run(uint32_t *records, uint32_t first, uint32_t slot, uint32_t length,
                    uint32_t word2, uint32_t word3) {
    for (uint32_t i = 0; i < length; i++) {
        uint32_t *record = records + (size_t)(first + i) * G360_POOL_RECORD_WORDS;
        record[0] = slot;
        record[1] = length - i;
        record[2] = i == 0 ? word2 : 0;
        record[3] = i == 0 ? word3 : 0;
    }
}

static void clear_blocks(uint32_t *records, uint32_t first, uint32_t length) {
    memset(records + (size_t)first * G360_POOL_RECORD_WORDS, 0,
           (size_t)length * G360_POOL_RECORD_WORDS * sizeof(uint32_t));
}

static void test_runs(void) {
    uint32_t *records = new_table();
    put_run(records, 0, 28, 4096, 0x61, 0x976456);
    put_run(records, 5197, 29, 2048, 0x63, 0x976546);
    put_run(records, 25247, 25, 1377, 0, 0);
    g360_pool_run runs[4];
    size_t count = g360_pool_list_runs(records, G360_POOL_BLOCK_COUNT, runs, 4);
    CHECK(count == 3);
    CHECK(runs[0].first_block == 0 && runs[0].slot == 28 && runs[0].block_count == 4096);
    CHECK(runs[1].first_block == 5197 && runs[1].block_count == 2048);
    CHECK(runs[2].first_block == 25247 && runs[2].block_count == 1377);
    CHECK(g360_pool_block_address(5197) == 0xab134000u);

    /* A remaining count past the end of the table is clipped. */
    clear_blocks(records, 25247, 1377);
    records[(G360_POOL_BLOCK_COUNT - 2) * G360_POOL_RECORD_WORDS] = 7;
    records[(G360_POOL_BLOCK_COUNT - 2) * G360_POOL_RECORD_WORDS + 1] = 100;
    count = g360_pool_list_runs(records, G360_POOL_BLOCK_COUNT, runs, 4);
    CHECK(count == 3);
    CHECK(runs[2].first_block == G360_POOL_BLOCK_COUNT - 2 && runs[2].block_count == 2);
    free(records);
}

static void test_tracker(void) {
    uint32_t *records = new_table();
    g360_raw_tracker tracker;
    g360_raw_tracker_reset(&tracker);
    put_run(records, 9293, 40, 4557, 0x5d, 0x82f37a);
    CHECK(g360_raw_tracker_update(&tracker, records, G360_POOL_BLOCK_COUNT, 100) == G360_RAW_IDLE);

    /* A partial slot-28 run (mid-release snapshot, zero head words) is not a reservation. */
    put_run(records, 6449, 28, 2844, 0, 0);
    CHECK(g360_raw_tracker_update(&tracker, records, G360_POOL_BLOCK_COUNT, 150) == G360_RAW_IDLE);
    clear_blocks(records, 6449, 2844);

    put_run(records, 0, 28, 4096, 0x61, 0x976456);
    CHECK(g360_raw_tracker_update(&tracker, records, G360_POOL_BLOCK_COUNT, 200) == G360_RAW_RESERVED);
    CHECK(tracker.reservation.first_block == 0 && tracker.reserved_at_us == 200);
    CHECK(g360_raw_tracker_update(&tracker, records, G360_POOL_BLOCK_COUNT, 250) == G360_RAW_RESERVED);

    /* Release immediately followed by a new slot-28 reservation at the same address. */
    clear_blocks(records, 0, 4096);
    put_run(records, 0, 28, 4096, 0x70, 0x976683);
    CHECK(g360_raw_tracker_update(&tracker, records, G360_POOL_BLOCK_COUNT, 300) == G360_RAW_RELEASED);
    CHECK(tracker.released_at_us == 300);
    /* Released is terminal until reset. */
    CHECK(g360_raw_tracker_update(&tracker, records, G360_POOL_BLOCK_COUNT, 350) == G360_RAW_RELEASED);
    free(records);
}

static uint32_t read16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t read32(const uint8_t *p) { return read16(p) | (read16(p + 2) << 16); }

static void test_dng_header(void) {
    uint8_t header[G360_DNG_HEADER_CAPACITY];
    g360_dng_parameters parameters = {4105, 0.544, 0.578, NULL};
    size_t length = g360_dng_write_header(header, sizeof(header), &parameters);
    CHECK(length > 0 && length % 16 == 0);
    CHECK(header[0] == 'I' && header[1] == 'I' && read16(header + 2) == 42);
    uint32_t ifd = read32(header + 4);
    uint32_t entries = read16(header + ifd);
    uint32_t previous_tag = 0;
    uint32_t strip_offset = 0, strip_bytes = 0, white = 0, cfa = 0;
    for (uint32_t i = 0; i < entries; i++) {
        const uint8_t *entry = header + ifd + 2 + i * 12;
        uint32_t tag = read16(entry);
        CHECK(tag > previous_tag);
        previous_tag = tag;
        if (tag == 273) strip_offset = read32(entry + 8);
        if (tag == 279) strip_bytes = read32(entry + 8);
        if (tag == 50717) white = read32(entry + 8);
        if (tag == 33422) cfa = read32(entry + 8);
        /* Every out-of-line payload must lie inside the header. */
        uint32_t type = read16(entry + 2), count = read32(entry + 4);
        uint32_t size = (type == 5 || type == 10) ? 8 : (type == 4 ? 4 : (type == 3 ? 2 : 1));
        if ((uint64_t)size * count > 4) {
            CHECK(read32(entry + 8) + size * count <= length);
        }
    }
    CHECK(read32(header + ifd + 2 + entries * 12) == 0);
    CHECK(strip_offset == length);
    CHECK(strip_bytes == G360_RAW_BYTES);
    CHECK(white == 65472u);
    CHECK(cfa == 0x01020001u);
    CHECK(g360_dng_write_header(header, 64, &parameters) == 0);
}

static void test_scan_synthetic(void) {
    uint8_t *raster = malloc(G360_RAW_BYTES);
    for (size_t i = 0; i < G360_RAW_BYTES; i += 2) {
        uint32_t value = (uint32_t)((i / 2) % 1024) << 6;
        raster[i] = (uint8_t)value;
        raster[i + 1] = (uint8_t)(value >> 8);
    }
    g360_raw_statistics statistics;
    CHECK(g360_raw_scan(raster, G360_RAW_BYTES, &statistics) == 0);
    CHECK(statistics.minimum == 0 && statistics.maximum == 65472u);
    CHECK(statistics.bits_seen == 0xffc0u && statistics.misaligned_samples == 0);
    raster[1001] |= 0;
    raster[1000] |= 1;
    CHECK(g360_raw_scan(raster, G360_RAW_BYTES, &statistics) == 0 && statistics.misaligned_samples == 1);
    CHECK(g360_raw_scan(raster, G360_RAW_BYTES - 2, &statistics) == -1);
    free(raster);
}

/* Replays BASE/REC lines from a capture-timeline-observer log through the tracker. */
static void replay_timeline(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "cannot open %s\n", path);
        failures++;
        return;
    }
    uint32_t *records = new_table();
    g360_raw_tracker tracker;
    g360_raw_tracker_reset(&tracker);
    char line[512];
    unsigned long last_time = 0;
    int have_time = 0;
    while (fgets(line, sizeof(line), file)) {
        unsigned long first, last, time;
        unsigned int w[4], n[4];
        if (sscanf(line, "BASE %lu %lu %x %x %x %x", &first, &last, &w[0], &w[1], &w[2], &w[3]) == 6) {
            for (unsigned long i = first; i <= last && i < G360_POOL_BLOCK_COUNT; i++) {
                for (int k = 0; k < 4; k++) records[i * 4 + k] = w[k];
            }
        } else if (sscanf(line, "T %lu REC %lu %lu %x %x %x %x -> %x %x %x %x", &time, &first, &last,
                          &w[0], &w[1], &w[2], &w[3], &n[0], &n[1], &n[2], &n[3]) == 11) {
            /* All changes seen in one poll share a time; evaluate when the time advances. */
            if (have_time && time != last_time) {
                g360_raw_tracker_update(&tracker, records, G360_POOL_BLOCK_COUNT, (uint32_t)last_time);
            }
            for (unsigned long i = first; i <= last && i < G360_POOL_BLOCK_COUNT; i++) {
                for (int k = 0; k < 4; k++) records[i * 4 + k] = n[k];
            }
            last_time = time;
            have_time = 1;
        }
    }
    if (have_time) {
        g360_raw_tracker_update(&tracker, records, G360_POOL_BLOCK_COUNT, (uint32_t)last_time);
    }
    fclose(file);
    printf("replay %s: phase %d, reserved at %u us (block %u, 0x%08x), released at %u us\n", path,
           tracker.phase, tracker.reserved_at_us, tracker.reservation.first_block,
           g360_pool_block_address(tracker.reservation.first_block), tracker.released_at_us);
    CHECK(tracker.phase == G360_RAW_RELEASED);
    CHECK(tracker.released_at_us > tracker.reserved_at_us);
    free(records);
}

static void scan_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "cannot open %s\n", path);
        failures++;
        return;
    }
    uint8_t *raster = malloc(G360_RAW_BYTES + 1);
    size_t length = fread(raster, 1, G360_RAW_BYTES + 1, file);
    fclose(file);
    g360_raw_statistics statistics;
    CHECK(g360_raw_scan(raster, length, &statistics) == 0);
    printf("scan %s: min %u max %u bits 0x%04x misaligned %zu\n", path, statistics.minimum,
           statistics.maximum, statistics.bits_seen, statistics.misaligned_samples);
    CHECK(statistics.misaligned_samples == 0);
    free(raster);
}

int main(void) {
    test_runs();
    test_tracker();
    test_dng_header();
    test_scan_synthetic();
    const char *log = getenv("G360_TIMELINE_LOG");
    if (log && *log) {
        replay_timeline(log);
    }
    const char *raw = getenv("G360_RAW_FILE");
    if (raw && *raw) {
        scan_file(raw);
    }
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("capture-engine host checks passed");
    return 0;
}
