#ifndef G360_RAW_FORMAT_H
#define G360_RAW_FORMAT_H

/*
 * Measured layout of the SM-R210 still RAW and an uncompressed DNG wrapper.
 *
 * Measured from one 18 October 2026 dump (SHA-256 5af26b98...d780): 5824 x 2896
 * little-endian 16-bit samples, stride 11648 bytes, 33,732,608 bytes; every
 * sample a multiple of 64 (10 significant bits in bits 6..15); GRBG on the
 * right lens. Left-lens red/blue order, crop/padding and sensor precision are
 * not established. The DNG colour matrix is an uncalibrated placeholder.
 * No I/O here: callers write the header and then the unchanged samples.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define G360_RAW_WIDTH UINT32_C(5824)
#define G360_RAW_HEIGHT UINT32_C(2896)
#define G360_RAW_STRIDE UINT32_C(11648)
#define G360_RAW_BYTES ((size_t)G360_RAW_STRIDE * G360_RAW_HEIGHT)
#define G360_RAW_SIGNIFICANT_BITS 10u
#define G360_RAW_ALIGN_SHIFT 6u
#define G360_RAW_WHITE_LEVEL (UINT32_C(1023) << G360_RAW_ALIGN_SHIFT)

typedef struct g360_raw_statistics {
    uint32_t minimum;
    uint32_t maximum;
    /* Bitwise OR of all samples; 0xffc0 for full 10-bit high-aligned data. */
    uint32_t bits_seen;
    /* Samples with any of the low ALIGN_SHIFT bits set. */
    size_t misaligned_samples;
} g360_raw_statistics;

/*
 * Scans a little-endian 16-bit raster of `length` bytes. Returns 0 and fills
 * `statistics` when `length` equals G360_RAW_BYTES; -1 otherwise.
 */
int g360_raw_scan(const uint8_t *samples, size_t length, g360_raw_statistics *statistics);

typedef struct g360_dng_parameters {
    /* Stored-sample black level (same units as the samples). */
    uint32_t black_level;
    /* AsShotNeutral as ratios with green = 1; zero means 1. */
    double neutral_red;
    double neutral_blue;
    const char *description;
} g360_dng_parameters;

/*
 * Writes a little-endian DNG header (TIFF header, one IFD, tag payloads) for
 * a G360_RAW_BYTES raster that the caller appends immediately after it.
 * Returns the header length (also the raster's StripOffset), or 0 if
 * `capacity` is too small. The header is at most G360_DNG_HEADER_CAPACITY.
 */
#define G360_DNG_HEADER_CAPACITY ((size_t)4096)
size_t g360_dng_write_header(uint8_t *output, size_t capacity,
                             const g360_dng_parameters *parameters);

#ifdef __cplusplus
}
#endif

#endif
