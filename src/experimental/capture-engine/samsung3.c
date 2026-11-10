#include "samsung3.h"

#include <string.h>

/*
 * From-scratch Samsung3 decoder, reconstructed from the pinned LibRaw
 * reference's documented algorithm. Independent implementation; no copied
 * code. The bit reader mirrors the reference semantics: 32-bit little-endian
 * word refills, bits consumed from the top of the accumulator, and an
 * exhausted stream refills with 0xffffffff (the reference get4 fill value).
 */

typedef struct samsung3_reader {
    const uint8_t *data;
    size_t length;
    size_t pos;      /* byte position of the next unread word */
    uint64_t bitbuf;
    int vbits;
    int exhausted;   /* a refill read past the end */
} samsung3_reader;

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void reader_reset(samsung3_reader *reader)
{
    reader->bitbuf = 0;
    reader->vbits = 0;
}

static uint32_t reader_bits(samsung3_reader *reader, int n)
{
    uint32_t value;

    if (n <= 0) {
        return 0;
    }
    while (reader->vbits < n) {
        uint32_t word;
        if (reader->pos + 4 > reader->length) {
            word = UINT32_C(0xffffffff);
            reader->pos = reader->length;
            reader->exhausted = 1;
        } else {
            word = read_le32(reader->data + reader->pos);
            reader->pos += 4;
        }
        reader->bitbuf = (reader->bitbuf << 32) | word;
        reader->vbits += 32;
    }
    value = (uint32_t)((reader->bitbuf << (64 - reader->vbits)) >> (64 - n));
    reader->vbits -= n;
    return value;
}

int g360_samsung3_decode(const uint8_t *input, size_t input_length,
                         uint32_t width, uint32_t height,
                         size_t data_offset,
                         g360_samsung3_header *header,
                         uint16_t *output, size_t output_capacity)
{
    samsung3_reader reader;
    size_t maxpixidx;
    uint32_t row;

    if (input == NULL || output == NULL || header == NULL) {
        return -1;
    }
    if (width == 0 || height == 0 || input_length < G360_SAMSUNG3_HEADER_BYTES) {
        return -1;
    }
    maxpixidx = (size_t)width * height;
    if (output_capacity < maxpixidx * sizeof(uint16_t)) {
        return -1;
    }

    memset(&reader, 0, sizeof(reader));
    reader.data = input;
    reader.length = input_length;

    /* The 128-bit logical header, in the reference's field order. */
    reader_reset(&reader);
    (void)reader_bits(&reader, 16); /* ignored */
    (void)reader_bits(&reader, 4);  /* ignored */
    header->bit_depth = reader_bits(&reader, 4) + 1;
    if (header->bit_depth > 16) {
        return -2;
    }
    header->datamax = (UINT32_C(1) << header->bit_depth) - 1;
    (void)reader_bits(&reader, 8);  /* ignored */
    (void)reader_bits(&reader, 16); /* ignored */
    (void)reader_bits(&reader, 16); /* ignored */
    (void)reader_bits(&reader, 16); /* ignored */
    (void)reader_bits(&reader, 4);  /* ignored */
    header->optflags = reader_bits(&reader, 4);
    (void)reader_bits(&reader, 16); /* ignored */
    (void)reader_bits(&reader, 8);  /* ignored */
    (void)reader_bits(&reader, 2);  /* ignored */
    header->init_val = (uint16_t)reader_bits(&reader, 14);

    for (row = 0; row < height; ++row) {
        uint32_t col;
        int motion = 7;
        int scale = 0;
        uint32_t diff_bits_mode[3][2];
        uint32_t i;

        /*
         * Row alignment: forward-seek so the byte position is congruent to
         * data_offset modulo 16, exactly as the reference does, then drop the
         * remaining buffered bits.
         */
        reader.pos += ((data_offset - reader.pos) & 15u);
        reader_reset(&reader);

        for (i = 0; i < 3; ++i) {
            diff_bits_mode[i][0] = diff_bits_mode[i][1] = (row < 2) ? 7u : 4u;
        }

        for (col = 0; col + 15 < width; col += 16) {
            uint32_t diff_bits[4] = {0, 0, 0, 0};

            if ((header->optflags & 4u) == 0 && (col & 63u) == 0) {
                static const int scale_values[3] = {0, -2, 2};
                int code = (int)reader_bits(&reader, 2);
                if (code < 3) {
                    scale += scale_values[code];
                } else {
                    scale = (int)reader_bits(&reader, 12);
                }
            }

            if ((header->optflags & 2u) != 0) {
                motion = reader_bits(&reader, 1) ? 3 : 7;
            } else if (reader_bits(&reader, 1) == 0) {
                motion = (int)reader_bits(&reader, 3);
            }
            /* otherwise the previous motion value stays */

            if (motion == 7) {
                for (i = 0; i < 16; ++i) {
                    uint16_t prediction = (col == 0)
                        ? header->init_val
                        : output[(size_t)row * width + col + i - 2];
                    output[(size_t)row * width + col + i] = prediction;
                }
            } else {
                static const int motion_offset[7] = {-4, -2, -2, 0, 0, 2, 4};
                static const int motion_average[7] = {0, 0, 1, 0, 1, 0, 0};
                if (row < 2) {
                    return -2; /* previous-row lookup on the first two rows */
                }
                for (i = 0; i < 16; ++i) {
                    int refrow = ((row + i) & 1u) ? (int)row - 2 : (int)row - 1;
                    int refcol = (int)col + (int)i + motion_offset[motion] +
                                 (int)(1 - ((row + i) & 1u)) * (1 - 2 * (int)(i & 1u));
                    size_t refidx = (size_t)refrow * width + (size_t)refcol;
                    if ((int)refcol < 0 || refidx >= maxpixidx) {
                        return -2;
                    }
                    if (motion_average[motion]) {
                        size_t refidx2 = (size_t)refrow * width + (size_t)(refcol + 2);
                        if (refidx2 >= maxpixidx) {
                            return -2;
                        }
                        output[(size_t)row * width + col + i] =
                            (uint16_t)(((uint32_t)output[refidx] +
                                        (uint32_t)output[refidx2] + 1u) >> 1);
                    } else {
                        output[(size_t)row * width + col + i] = output[refidx];
                    }
                }
            }

            if ((header->optflags & 1u) != 0 || reader_bits(&reader, 1) == 0) {
                uint32_t flags[4];
                for (i = 0; i < 4; ++i) {
                    flags[i] = reader_bits(&reader, 2);
                }
                for (i = 0; i < 4; ++i) {
                    uint32_t colornum = (row & 1u) ? (i >> 1)
                                                   : ((i >> 1) + 2) % 3;
                    switch (flags[i]) {
                    case 0:
                        diff_bits[i] = diff_bits_mode[colornum][0];
                        break;
                    case 1:
                        diff_bits[i] = diff_bits_mode[colornum][0] + 1;
                        break;
                    case 2:
                        diff_bits[i] = diff_bits_mode[colornum][0] - 1;
                        break;
                    default:
                        diff_bits[i] = reader_bits(&reader, 4);
                        break;
                    }
                    diff_bits_mode[colornum][0] = diff_bits_mode[colornum][1];
                    diff_bits_mode[colornum][1] = diff_bits[i];
                    if (diff_bits[i] > header->bit_depth + 1) {
                        return -2;
                    }
                }
            }

            for (i = 0; i < 16; ++i) {
                uint32_t len = diff_bits[i >> 2];
                int diff;
                int rcol;
                size_t bidx;
                int value;

                if (len == 0) {
                    diff = 0;
                } else {
                    int sign_mask = 1 << (int)(len - 1);
                    diff = ((int)(reader_bits(&reader, (int)len) ^
                                  (uint32_t)sign_mask) - sign_mask) *
                               (scale * 2 + 1) + scale;
                }
                rcol = (row & 1u)
                    ? (int)col + (int)((i & 0x7u) << 1) + 1 - (int)(i >> 3)
                    : (int)col + (int)((i & 0x7u) << 1) + (int)(i >> 3);
                bidx = (size_t)row * width + (size_t)rcol;
                if (rcol < 0 || bidx >= maxpixidx) {
                    return -2;
                }
                value = (int)output[bidx] + diff;
                output[bidx] = (uint16_t)(value < 0 ? 0 :
                                          (value > (int)header->datamax
                                               ? (int)header->datamax : value));
            }
        }
    }

    /* An exhausted stream is not itself an error; the caller checks the
     * header and the raster. */
    return 0;
}
