/* File output from caller-owned capture results. No SDK/session calls. */
#ifndef G360_OUTPUT_H
#define G360_OUTPUT_H
#include "g360cap.h"
#include "g360_exif.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Fill the supported fields from the still's copied native Exif record.
 * Absent/invalid fields are left unknown; no requested settings are used. */
void g360_exif_from_still(const g360_still *still, const g360_image *jpeg,
                           g360_exif_info *info);
/* Generate a small header, then stream the existing samples unchanged.
 * Synchronous, fsync before success; no full-frame allocation or copy.
 * Caller retains the frame and MakerNote until return. A future output
 * worker may call this with retained results, independently of capture. */
int g360_save_dng(const g360_frame *frame, const g360_image *maker_note, const char *path);
/* The same with the shot's metadata (g360_dng_header_full): Exif, Samsung
 * IQ MakerNote and an optional description (NULL for none). The header is
 * G360_DNG_FULL_HEADER_BYTES, allocated for the call. */
int g360_save_dng_full(const g360_frame *frame, const g360_image *maker_note,
                       const g360_exif_info *info, const char *description, const char *path);
/* g360_save_dng_full with an XMP packet in the DNG (NULL for none). */
int g360_save_dng_full_xmp(const g360_frame *frame, const g360_image *maker_note,
                           const g360_exif_info *info, const char *description, const char *xmp,
                           const char *path);
/* Insert Exif immediately after SOI, then stream the existing JPEG bytes.
 * For bare encoder JPEGs only. The input buffer is never modified. */
int g360_save_jpeg_exif(const g360_image *jpeg, const g360_image *maker_note,
                        const g360_exif_info *info, const char *path);
/* g360_save_jpeg_exif with an XMP APP1 segment after the Exif one (xmp is a
 * NUL-terminated packet of at most G360_JPEG_XMP_MAX bytes, or NULL). */
#define G360_JPEG_XMP_MAX 4096u
int g360_save_jpeg_exif_xmp(const g360_image *jpeg, const g360_image *maker_note,
                            const g360_exif_info *info, const char *xmp, const char *path);
#ifdef __cplusplus
}
#endif
#endif
