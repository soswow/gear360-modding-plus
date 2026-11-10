#include "g360cap_check.h"

/* Parses one hexadecimal number; returns the position after it, or 0. */
static const char *parse_hex(const char *text, uintptr_t *value)
{
    uintptr_t result = 0;
    int digits = 0;
    for (;; ++text) {
        unsigned digit;
        char c = *text;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10u;
        else break;
        if (result > (UINTPTR_MAX >> 4)) return 0;
        result = (result << 4) | digit;
        ++digits;
    }
    if (digits == 0) return 0;
    *value = result;
    return text;
}

/* Reads one maps line; returns 1 and fills the range and readability. */
static int parse_line(const char *line, uintptr_t *lo, uintptr_t *hi, int *readable)
{
    const char *p = parse_hex(line, lo);
    if (p == 0 || *p != '-') return 0;
    p = parse_hex(p + 1, hi);
    if (p == 0 || *p != ' ' || *hi <= *lo) return 0;
    *readable = p[1] == 'r';
    return 1;
}

int g360_maps_covers(const char *maps_text, uintptr_t start, size_t length)
{
    uintptr_t cursor = start;
    uintptr_t end;
    int pass;

    if (maps_text == 0 || length == 0) return 0;
    end = start + (uintptr_t)length;
    if (end < start) return 0;
    /* The kernel lists mappings in ascending order, so one pass normally
       suffices; extra passes tolerate any order without trusting it. */
    for (pass = 0; pass < 64; ++pass) {
        const char *line = maps_text;
        uintptr_t before = cursor;
        while (*line != '\0') {
            uintptr_t lo;
            uintptr_t hi;
            int readable;
            if (parse_line(line, &lo, &hi, &readable) && lo <= cursor && cursor < hi) {
                if (!readable) return 0;
                cursor = hi;
                if (cursor >= end) return 1;
            }
            while (*line != '\0' && *line != '\n') ++line;
            if (*line == '\n') ++line;
        }
        if (cursor == before) return 0;
    }
    return 0;
}

int g360_maps_physical(const char *maps_text, uintptr_t physical, size_t length,
                       uintptr_t *virtual_out)
{
    const char *line = maps_text;
    uintptr_t physical_end = physical + (uintptr_t)length;

    if (maps_text == 0 || virtual_out == 0 || length == 0 || physical_end < physical) return 0;
    while (*line != '\0') {
        uintptr_t lo;
        uintptr_t hi;
        uintptr_t offset;
        int readable;
        const char *p;
        if (parse_line(line, &lo, &hi, &readable) && readable) {
            /* "lo-hi perms offset dev inode path": skip to the offset. */
            p = line;
            while (*p != ' ' && *p != '\0' && *p != '\n') ++p;     /* range */
            while (*p == ' ') ++p;
            while (*p != ' ' && *p != '\0' && *p != '\n') ++p;     /* perms */
            while (*p == ' ') ++p;
            p = parse_hex(p, &offset);
            if (p != 0 && *p == ' ') {
                int field;
                for (field = 0; field < 2; ++field) {                  /* dev, inode */
                    while (*p == ' ') ++p;
                    while (*p != ' ' && *p != '\0' && *p != '\n') ++p;
                }
                while (*p == ' ') ++p;
                if (p[0] == '/' && p[1] == 'd' && p[2] == 'e' && p[3] == 'v' && p[4] == '/' &&
                    p[5] == 'm' && p[6] == 'e' && p[7] == 'm' &&
                    (p[8] == '\n' || p[8] == '\0' || p[8] == ' ') &&
                    physical >= offset && physical - offset < hi - lo &&
                    physical_end - offset <= hi - lo) {
                    *virtual_out = lo + (physical - offset);
                    return 1;
                }
            }
        }
        while (*line != '\0' && *line != '\n') ++line;
        if (*line == '\n') ++line;
    }
    return 0;
}

int g360_raw_extent(uint32_t width, uint32_t height, uint32_t stride, uint32_t bits,
                    size_t maximum, size_t *size_out)
{
    if (size_out == 0 || width == 0 || height == 0 || bits == 0 || bits > 16u ||
        width > stride / 2u || (size_t)stride > maximum / height) return 0;
    *size_out = (size_t)stride * height;
    return 1;
}

int g360_jpeg_markers_ok(const unsigned char *data, size_t size)
{
    if (data == 0 || size < 4) return 0;
    return data[0] == 0xffu && data[1] == 0xd8u &&
           data[size - 2] == 0xffu && data[size - 1] == 0xd9u;
}
