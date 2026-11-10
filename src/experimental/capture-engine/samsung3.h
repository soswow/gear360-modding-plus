#ifndef G360_SAMSUNG3_H
#define G360_SAMSUNG3_H

/*
 * From-scratch decoder for the Samsung3 lossless-compressed RAW stream, the
 * format the still pipeline's LRC (Low Raw Compression) stage emits.
 *
 * The format is reconstructed from the pinned public LibRaw reference
 * (revision 2caab57e, samsung3_load_raw) and is implemented independently
 * here; no LibRaw code is copied. See
 * output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/RAW-PROVENANCE-LRC.md.
 *
 * Stream shape: a 128-bit logical header (depth code, option flags, initial
 * sample), then one bit-packed section per row, aligned to a 16-byte boundary
 * relative to the data offset. Rows are decoded in 16-pixel blocks with a
 * previous-pixel or previous-row predictor, an adaptive per-colour difference
 * length, and a scale factor.
 *
 * NOT validated against camera bytes: the equivalence of the SM-R210 LRC
 * output and this format is unproven (the same open item recorded in
 * external.libraw-samsung3-input-contract). The decoded samples are the
 * datamax-clipped values; the 10-bit-to-16-bit shift (bits 6..15) observed in
 * the measured raster is a caller-side step, not part of this decoder.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The logical header's 128 bits sum to 16 bytes. */
#define G360_SAMSUNG3_HEADER_BYTES ((size_t)16)

typedef struct g360_samsung3_header {
    /* depth code + 1, from the header's depth nibble. */
    uint32_t bit_depth;
    /* (1 << bit_depth) - 1. */
    uint32_t datamax;
    /* The 4-bit option field: bit0 forces the difference-length flags,
     * bit1 forces short motion codes, bit2 disables the scale step. */
    uint32_t optflags;
    /* The 14-bit initial sample. */
    uint16_t init_val;
} g360_samsung3_header;

/*
 * Decode a Samsung3-compressed stream into a row-major little-endian 16-bit
 * raster of `width` by `height` samples.
 *
 * `data_offset` is the stream position the row alignment is relative to; for
 * a bare stream whose data begins immediately after the 16-byte header, pass
 * G360_SAMSUNG3_HEADER_BYTES. A TIFF-embedded strip passes its strip offset.
 *
 * Returns 0 on success and fills `header` and `output` (output_capacity must
 * be at least width * height * 2 bytes). Returns -1 for invalid arguments,
 * -2 for a corrupt stream (out-of-range motion, difference length, or a
 * reference index outside the raster).
 */
int g360_samsung3_decode(const uint8_t *input, size_t input_length,
                         uint32_t width, uint32_t height,
                         size_t data_offset,
                         g360_samsung3_header *header,
                         uint16_t *output, size_t output_capacity);

#ifdef __cplusplus
}
#endif

#endif /* G360_SAMSUNG3_H */
