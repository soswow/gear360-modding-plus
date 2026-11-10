/*
 * Pure checks used by g360cap before it touches memory it did not allocate.
 * No I/O and no allocation: the caller supplies the /proc/self/maps text.
 */
#ifndef G360CAP_CHECK_H
#define G360CAP_CHECK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 1 when every byte of [start, start + length) lies in readable mappings
 * listed in maps_text (the /proc/self/maps format: "lo-hi perms ..."), which
 * may be several adjacent mappings. 0 for length 0, for an overflowing range,
 * or when any byte is in a gap or a mapping without read permission.
 */
int g360_maps_covers(const char *maps_text, uintptr_t start, size_t length);

/*
 * Finds where physical memory [physical, physical + length) is mapped in
 * this process: a readable line of maps_text whose path is /dev/mem (its
 * offset field is then the physical address) and which covers the whole
 * range. Returns 1 and sets *virtual_out to the address of the first byte,
 * else 0 (no such line, the range spans two lines, length 0, or overflow).
 */
int g360_maps_physical(const char *maps_text, uintptr_t physical, size_t length,
                       uintptr_t *virtual_out);

/* Validates a 16-bit-word RAW layout and computes stride * height without
 * overflowing. All dimensions and bits (1..16) must be nonzero, each row
 * must contain width samples, and the byte extent must not exceed maximum.
 * On failure, *size_out is unchanged. */
int g360_raw_extent(uint32_t width, uint32_t height, uint32_t stride, uint32_t bits,
                    size_t maximum, size_t *size_out);

/* 1 when data[0..1] is FF D8 and data[size-2..size-1] is FF D9 (size >= 4). */
int g360_jpeg_markers_ok(const unsigned char *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* G360CAP_CHECK_H */
