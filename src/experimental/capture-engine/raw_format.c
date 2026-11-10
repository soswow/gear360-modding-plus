#include "raw_format.h"

int g360_raw_scan(const uint8_t *samples, size_t length, g360_raw_statistics *statistics) {
    if (samples == NULL || statistics == NULL || length != G360_RAW_BYTES) {
        return -1;
    }
    uint32_t minimum = 0xffff;
    uint32_t maximum = 0;
    uint32_t bits = 0;
    size_t misaligned = 0;
    const uint32_t low_mask = (1u << G360_RAW_ALIGN_SHIFT) - 1u;
    for (size_t offset = 0; offset < length; offset += 2) {
        uint32_t value = (uint32_t)samples[offset] | ((uint32_t)samples[offset + 1] << 8);
        if (value < minimum) {
            minimum = value;
        }
        if (value > maximum) {
            maximum = value;
        }
        bits |= value;
        if (value & low_mask) {
            misaligned++;
        }
    }
    statistics->minimum = minimum;
    statistics->maximum = maximum;
    statistics->bits_seen = bits;
    statistics->misaligned_samples = misaligned;
    return 0;
}

/* Little-endian byte writer with overflow tracking. */
typedef struct writer {
    uint8_t *data;
    size_t capacity;
    size_t length;
    int overflow;
} writer;

static void put8(writer *w, size_t at, uint8_t value) {
    if (at < w->capacity) {
        w->data[at] = value;
    } else {
        w->overflow = 1;
    }
}

static void put16(writer *w, size_t at, uint32_t value) {
    put8(w, at, (uint8_t)value);
    put8(w, at + 1, (uint8_t)(value >> 8));
}

static void put32(writer *w, size_t at, uint32_t value) {
    put16(w, at, value & 0xffffu);
    put16(w, at + 2, value >> 16);
}

/* Appends a payload to the extra-data area and returns its offset. */
static size_t append(writer *w, const void *bytes, size_t count) {
    size_t at = w->length;
    const uint8_t *source = (const uint8_t *)bytes;
    for (size_t i = 0; i < count; i++) {
        put8(w, at + i, source[i]);
    }
    w->length += count;
    return at;
}

static size_t append_text(writer *w, const char *text, uint32_t *count) {
    size_t length = 0;
    while (text[length]) {
        length++;
    }
    *count = (uint32_t)length + 1u;
    return append(w, text, length + 1);
}

static size_t append_rational(writer *w, uint32_t numerator, uint32_t denominator) {
    size_t at = w->length;
    put32(w, at, numerator);
    put32(w, at + 4, denominator);
    w->length += 8;
    return at;
}

static size_t append_srational(writer *w, int32_t numerator, uint32_t denominator) {
    return append_rational(w, (uint32_t)numerator, denominator);
}

#define RATIONAL_SCALE 10000000

static int32_t scaled(double value) {
    double product = value * RATIONAL_SCALE;
    return (int32_t)(product < 0 ? product - 0.5 : product + 0.5);
}

typedef struct entry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value; /* inline value or payload offset */
} entry;

enum { TYPE_BYTE = 1, TYPE_ASCII = 2, TYPE_SHORT = 3, TYPE_LONG = 4, TYPE_RATIONAL = 5, TYPE_SRATIONAL = 10 };

/* Uncalibrated placeholder: XYZ-to-sRGB stored as XYZ-to-camera. */
static const double xyz_to_camera[9] = {
    3.2404542, -1.5371385, -0.4985314,
    -0.9692660, 1.8760108, 0.0415560,
    0.0556434, -0.2040259, 1.0572252,
};

#define ENTRY_COUNT 34u

size_t g360_dng_write_header(uint8_t *output, size_t capacity,
                             const g360_dng_parameters *parameters) {
    if (output == NULL || parameters == NULL) {
        return 0;
    }
    writer w = {output, capacity, 0, 0};
    /* TIFF header, then the IFD at offset 8, then payloads after the IFD. */
    const size_t ifd_offset = 8;
    const size_t ifd_size = 2 + ENTRY_COUNT * 12 + 4;
    w.length = ifd_offset + ifd_size;

    const char *description = parameters->description ? parameters->description
                                                        : "Samsung Gear 360 SM-R210 still RAW";
    uint32_t description_count, make_count, model_count, software_count, unique_count;
    size_t description_at = append_text(&w, description, &description_count);
    size_t make_at = append_text(&w, "Samsung", &make_count);
    size_t model_at = append_text(&w, "SM-R210", &model_count);
    size_t software_at = append_text(&w, "g360 capture engine", &software_count);
    size_t unique_at = append_text(&w, "Samsung Gear 360 SM-R210", &unique_count);
    size_t x_resolution_at = append_rational(&w, 72, 1);
    size_t y_resolution_at = append_rational(&w, 72, 1);
    size_t black_at = append_rational(&w, parameters->black_level, 1);
    size_t matrix_at = w.length;
    for (int i = 0; i < 9; i++) {
        append_srational(&w, scaled(xyz_to_camera[i]), RATIONAL_SCALE);
    }
    double red = parameters->neutral_red > 0 ? parameters->neutral_red : 1.0;
    double blue = parameters->neutral_blue > 0 ? parameters->neutral_blue : 1.0;
    size_t neutral_at = append_rational(&w, (uint32_t)scaled(red), RATIONAL_SCALE);
    append_rational(&w, RATIONAL_SCALE, RATIONAL_SCALE);
    append_rational(&w, (uint32_t)scaled(blue), RATIONAL_SCALE);
    size_t baseline_at = append_srational(&w, 0, RATIONAL_SCALE);
    size_t active_area_at = w.length;
    put32(&w, w.length, 0);
    put32(&w, w.length + 4, 0);
    put32(&w, w.length + 8, G360_RAW_HEIGHT);
    put32(&w, w.length + 12, G360_RAW_WIDTH);
    w.length += 16;
    /* Raster starts 16-byte aligned after the payloads. */
    size_t image_offset = (w.length + 15u) & ~(size_t)15u;
    while (w.length < image_offset) {
        put8(&w, w.length, 0);
        w.length++;
    }

    /* Inline SHORT pairs and BYTE quads are packed little-endian into the value word. */
    const entry entries[ENTRY_COUNT] = {
        {254, TYPE_LONG, 1, 0},
        {256, TYPE_LONG, 1, G360_RAW_WIDTH},
        {257, TYPE_LONG, 1, G360_RAW_HEIGHT},
        {258, TYPE_SHORT, 1, 16},
        {259, TYPE_SHORT, 1, 1},
        {262, TYPE_SHORT, 1, 32803},
        {270, TYPE_ASCII, description_count, (uint32_t)description_at},
        {271, TYPE_ASCII, make_count, (uint32_t)make_at},
        {272, TYPE_ASCII, model_count, (uint32_t)model_at},
        {273, TYPE_LONG, 1, (uint32_t)image_offset},
        {274, TYPE_SHORT, 1, 1},
        {277, TYPE_SHORT, 1, 1},
        {278, TYPE_LONG, 1, G360_RAW_HEIGHT},
        {279, TYPE_LONG, 1, (uint32_t)G360_RAW_BYTES},
        {282, TYPE_RATIONAL, 1, (uint32_t)x_resolution_at},
        {283, TYPE_RATIONAL, 1, (uint32_t)y_resolution_at},
        {284, TYPE_SHORT, 1, 1},
        {296, TYPE_SHORT, 1, 2},
        {305, TYPE_ASCII, software_count, (uint32_t)software_at},
        {33421, TYPE_SHORT, 2, 2u | (2u << 16)},
        /* G R / B G, measured on the right lens. */
        {33422, TYPE_BYTE, 4, 1u | (0u << 8) | (2u << 16) | (1u << 24)},
        {50706, TYPE_BYTE, 4, 1u | (4u << 8)},
        {50707, TYPE_BYTE, 4, 1u | (1u << 8)},
        {50708, TYPE_ASCII, unique_count, (uint32_t)unique_at},
        {50710, TYPE_BYTE, 3, 0u | (1u << 8) | (2u << 16)},
        {50711, TYPE_SHORT, 1, 1},
        {50713, TYPE_SHORT, 2, 1u | (1u << 16)},
        {50714, TYPE_RATIONAL, 1, (uint32_t)black_at},
        {50717, TYPE_LONG, 1, G360_RAW_WHITE_LEVEL},
        {50721, TYPE_SRATIONAL, 9, (uint32_t)matrix_at},
        {50728, TYPE_RATIONAL, 3, (uint32_t)neutral_at},
        {50730, TYPE_SRATIONAL, 1, (uint32_t)baseline_at},
        /* D65 */
        {50778, TYPE_SHORT, 1, 21},
        {50829, TYPE_LONG, 4, (uint32_t)active_area_at},
    };

    put8(&w, 0, 'I');
    put8(&w, 1, 'I');
    put16(&w, 2, 42);
    put32(&w, 4, (uint32_t)ifd_offset);
    put16(&w, ifd_offset, ENTRY_COUNT);
    for (uint32_t i = 0; i < ENTRY_COUNT; i++) {
        size_t at = ifd_offset + 2 + i * 12u;
        put16(&w, at, entries[i].tag);
        put16(&w, at + 2, entries[i].type);
        put32(&w, at + 4, entries[i].count);
        put32(&w, at + 8, entries[i].value);
    }
    put32(&w, ifd_offset + 2 + ENTRY_COUNT * 12u, 0);
    if (w.overflow) {
        return 0;
    }
    return image_offset;
}
