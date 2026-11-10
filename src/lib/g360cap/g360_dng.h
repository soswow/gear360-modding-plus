/* A DNG header for the owned, whole-frame copy made by g360cap. */
#ifndef G360_DNG_H
#define G360_DNG_H

#include "g360cap.h"
#include "g360_exif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Current fixed TIFF tag set, including padding up to the first sample. */
#define G360_DNG_HEADER_BYTES ((size_t)588u)

/*
 * Build the metadata only. Write header[0..*size), followed by the unchanged
 * frame->image.data[0..frame->image.size), to obtain the DNG. No allocation,
 * sample scan/copy, file I/O or camera operation occurs here; an owned still
 * can therefore be queued for later writing while capture continues.
 *
 * The frame must be the copied, validated 5824 x 2896 whole frame, stride
 * 11648, 16-bit little-endian words. The copied native maker-note buffer must
 * contain its IQ block. Missing/invalid colour data is refused, rather than
 * replaced by a guessed matrix. The caller keeps both inputs alive during
 * this call. The output must not overlap either input. Capacity must be at
 * least G360_DNG_HEADER_BYTES; *size is zero on failure. Returns G360_OK,
 * G360_E_ARGUMENT (pointers/capacity), or G360_E_IMAGE (layout/colour data).
 *
 * Tags and rational rounding match dng/make_dng.py, including its Software
 * string, retained for byte-for-byte comparison with that reference writer.
 * P2 as D65 remains an assumption of that shared colour model.
 */
int g360_dng_header(const g360_frame *frame, const g360_image *maker_note,
                    unsigned char *header, size_t capacity, size_t *size);

/* The full header: the samples start at this fixed offset, so every such DNG
 * has the same size (header + frame). */
#define G360_DNG_FULL_HEADER_BYTES ((size_t)40960u)

/*
 * The same DNG with the shot's metadata, matching make_dng.build_full()
 * byte for byte. Adds to IFD0: Orientation 1, LensInfo (1.2 mm f/2.2), an
 * Exif IFD and, when given, DateTime and ImageDescription. The Exif IFD has
 * ExposureTime, ExposureProgram, ISO, DateTimeOriginal/Digitized and
 * WhiteBalance when info knows them (as g360_exif_app1 does), FNumber 2.2,
 * FocalLength 1.2 mm, FocalLengthIn35mmFilm 6, ExifVersion 0230 and the
 * camera's IQ records as a little-endian Samsung Type 2 MakerNote (tags
 * 0x0001 "0100", 0x0060 AE, 0x00a0 and 0x00a1 AWB, 0x00c0 colour; offsets
 * relative to the MakerNote). description (NULL or "" for none) is at most
 * 63 printable ASCII characters; info->datetime is "" or
 * "YYYY:MM:DD HH:MM:SS". The header is zero-padded to
 * G360_DNG_FULL_HEADER_BYTES, which capacity must reach. Same inputs,
 * refusals and no-allocation rule as g360_dng_header; G360_E_ARGUMENT also
 * for a NULL info or a bad datetime/description.
 */
int g360_dng_header_full(const g360_frame *frame, const g360_image *maker_note,
                         const g360_exif_info *info, const char *description,
                         unsigned char *header, size_t capacity, size_t *size);

/* The most XMP the header takes (bytes, without the NUL). */
#define G360_DNG_XMP_MAX ((size_t)4096u)

/*
 * g360_dng_header_full plus an XMP packet (TIFF tag 700, IFD0). xmp is a
 * NUL-terminated string of 1..G360_DNG_XMP_MAX bytes, or NULL for none (the
 * result is then byte for byte g360_dng_header_full's). G360_E_ARGUMENT for
 * an empty or longer packet, or when the header would overflow.
 */
int g360_dng_header_full_xmp(const g360_frame *frame, const g360_image *maker_note,
                             const g360_exif_info *info, const char *description, const char *xmp,
                             unsigned char *header, size_t capacity, size_t *size);

#ifdef __cplusplus
}
#endif
#endif
