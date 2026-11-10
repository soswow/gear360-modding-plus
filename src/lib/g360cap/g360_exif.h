/*
 * The Exif + Samsung MakerNote APP1 segment for one Gear 360 still.
 *
 * The camera's stock app writes an APP1 segment into every JPEG it takes.
 * It holds a big-endian TIFF with IFD0 (Make SAMSUNG, Model SM-R210), an
 * Exif IFD and a Samsung "Type 2" MakerNote whose value offsets are
 * relative to the MakerNote's own start (tag 0x0001 = "0100"). The
 * MakerNote carries the camera's image-processing records under the stock
 * tags: 0x00a0 and 0x00a1 (AWB, already XOR-masked with 0xc8 in the
 * buffer), 0x0060 (AE) and 0x00c0 (colour).
 *
 * The formats are described in reverse-engineering/dng/README.md and
 * reverse-engineering/dng/stock_iq.py reads them back.
 *
 * This module is pure: it builds the segment's bytes into a buffer the
 * caller owns. It allocates nothing, opens no file and copies no JPEG.
 * The caller splices the returned bytes into the JPEG after SOI.
 */
#ifndef G360_EXIF_H
#define G360_EXIF_H

#include <stddef.h>
#include <stdint.h>

#include "g360cap.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the segment should say about the still.
 *
 * Zero means "not known" for the exposure and the ISO: the tag is left
 * out of the Exif IFD rather than guessed. The exposure is a rational,
 * exposure_numerator / exposure_denominator; the two are either both
 * zero (unknown, tag omitted) or both nonzero.
 *
 * The exposure program and the white balance use G360_EXIF_UNKNOWN
 * (0xffff) for "not known": the tag is omitted. Any other value is
 * written as it stands.
 *
 * The date is either empty (the date tags are omitted) or exactly
 * "YYYY:MM:DD HH:MM:SS" (19 characters, NUL-terminated in the 20-byte
 * field). Anything else is refused.
 */
typedef struct g360_exif_info {
    uint32_t width;                 /* the still's width in pixels */
    uint32_t height;                /* the still's height in pixels */
    uint32_t exposure_numerator;    /* exposure time's numerator */
    uint32_t exposure_denominator;  /* exposure time's denominator */
    uint32_t iso;                   /* ISO speed; 0 means unknown */
    uint32_t exposure_program;      /* Exif ExposureProgram; G360_EXIF_UNKNOWN means unknown */
    uint32_t white_balance;         /* Exif WhiteBalance; G360_EXIF_UNKNOWN means unknown */
    char datetime[20];              /* "YYYY:MM:DD HH:MM:SS" or empty */
} g360_exif_info;

/* "Not known" for the exposure program and the white balance. */
#define G360_EXIF_UNKNOWN 0xffffu

/*
 * The buffer size a caller needs for g360_exif_app1's output. The segment
 * with the full MakerNote (the AWB record twice, the AE record and the
 * colour record) is about 34 kB, so 40,000 bytes is enough with room to
 * spare. Any capacity at least as large as the segment works.
 */
#define G360_EXIF_APP1_MAX_SIZE 40000u

/*
 * The JPEG segment length field is 16 bits, so a segment's payload cannot
 * be longer than this (the field counts itself and the payload, not the
 * two marker bytes).
 */
#define G360_EXIF_APP1_MAX_SEGMENT 65535u

/*
 * Builds the whole APP1 segment, marker and length field included: the
 * bytes are FF E1, the big-endian length, "Exif\0\0" and the TIFF block.
 *
 * maker_note is the camera's maker-note buffer (g360_still.maker_note):
 * its data must be a copy (G360_IMAGE_COPIED) of at least the IQ block,
 * and the AWB, AE and colour records it holds must carry the signatures
 * the camera writes. The records are copied into the MakerNote as they
 * are; nothing is read past the IQ block and nothing is invented.
 *
 * info says what the Exif IFD should contain; see g360_exif_info.
 *
 * output receives the segment and must have room for it; capacity is the
 * number of bytes output can hold. size receives the number of bytes
 * written, and is set to zero when the call fails.
 *
 * Returns G360_OK, G360_E_ARGUMENT for a null pointer, a capacity that is
 * too small or an out-of-range field, and G360_E_IMAGE for a maker-note
 * buffer that is absent, not a copy, too short or carries a record with
 * the wrong signature.
 */
int g360_exif_app1(const g360_image *maker_note, const g360_exif_info *info,
                   unsigned char *output, size_t capacity, size_t *size);

/*
 * The real exposure time of a still taken with manual shutter TV, for
 * exposure_numerator / exposure_denominator. The firmware's own capture
 * record holds the nearest third-stop name instead (1/30 for 1/34.9 s), up
 * to 0.16 EV off; trial RANGE14 (9 November 2026) found the RAW follows
 * this time to 0.013 EV RMS from 1/6 to 1/12,634 s:
 *
 *   2^(-TV/16) s, at most 166 ms (the stream cap: 4 VideoFull frames less
 *   6 lines, trial L1) and at least 78.8 us (6 sensor lines of 13.13 us,
 *   trial F1), then 5 us less (the shortfall trials ENDS1/ENDS2 measured).
 *
 * The numerator counts tenths of a microsecond over a denominator of
 * 10,000,000, reduced: TV 41 is 33199/200000 (165.995 ms), TV 218 is
 * 371/5000000 (74.2 us). Pure integer arithmetic; any TV is accepted.
 */
void g360_exif_exposure_from_tv(int32_t tv, uint32_t *numerator, uint32_t *denominator);

#ifdef __cplusplus
}
#endif

#endif /* G360_EXIF_H */
