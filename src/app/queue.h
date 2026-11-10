/* Sweep frames on the camera's built-in flash, moved to the card afterwards.
 *
 * The flash's data partition (/opt/usr, ext4) writes about 42 MB/s, the card
 * about 16.5 MB/s (output/speed-20261105/RESULT.md). A sweep saves each DNG and optional JPEG
 * here before the next capture, then moves the files to the card.
 *
 * Queue layout, one directory:
 *   STAMP-K.dng   frame K (1..G360_QUEUE_MAX_FRAMES) of the sweep started at
 *                 STAMP (YYYYMMDD-HHMMSS, the camera's local wall clock)
 *   STAMP-K.jpg   optional JPEG from the same frame
 *   STAMP.csv     the sweep's exposures, one line added per captured frame
 * Other names are left alone. No capture/SDK calls. */
#ifndef G360_APP_QUEUE_H
#define G360_APP_QUEUE_H
#include <stdint.h>
#include "storage.h"
#ifdef __cplusplus
extern "C" {
#endif
#define G360_QUEUE_MAX_FRAMES 16u
/* A complete frame file: the DNG header with the shot's metadata
 * (G360_DNG_FULL_HEADER_BYTES) and the whole 5824 x 2896 16-bit frame.
 * Frames queued by builds before 6 November have the plain 588-byte header
 * and are accepted too. Tests override both. */
#ifndef G360_QUEUE_FRAME_BYTES
#define G360_QUEUE_FRAME_BYTES (UINT64_C(40960) + UINT64_C(33732608))
#endif
#ifndef G360_QUEUE_OLD_FRAME_BYTES
#define G360_QUEUE_OLD_FRAME_BYTES (UINT64_C(588) + UINT64_C(33732608))
#endif

/* Camera JPEG payload limit plus room for Exif and XMP APP1 segments. */
#define G360_QUEUE_JPEG_MAX_BYTES (UINT64_C(100663296) + UINT64_C(65536) + UINT64_C(4096))

typedef enum g360_queue_status {
    G360_QUEUE_OK = 0,
    G360_QUEUE_ARGUMENT = 1,
    G360_QUEUE_IO = 2,
    G360_QUEUE_EXISTS = 3,
    G360_QUEUE_STORAGE = 4,
    G360_QUEUE_STOPPED = 5
} g360_queue_status;

typedef struct g360_queue {
    char dir[400];
    int opened;
} g360_queue;

/* One frame's exposure. tv/iso are what was asked for (tv in the camera's
 * units, 16 * log2(1/seconds)); the others come from the still's own Exif
 * record, 0 when it was not available. */
typedef struct g360_queue_info {
    uint32_t frame;
    int32_t tv;
    uint32_t iso;
    uint32_t exposure_numerator;
    uint32_t exposure_denominator;
    uint32_t actual_iso;
} g360_queue_info;

typedef struct g360_queue_hooks {
    void *context;
    /* Before each frame is moved: frames processed so far, total frames,
     * and the bytes of all complete outputs. Nonzero leaves it queued. */
    int (*before)(void *context, unsigned done, unsigned total, uint64_t bytes);
    /* After a frame is on the card: its stem and the bytes written. */
    void (*after)(void *context, const char *stem, uint64_t bytes);
} g360_queue_hooks;

/* dir's parent must exist; dir is created if missing. */
int g360_queue_open(g360_queue *queue, const char *dir);
/* Creates dir/STAMP-FRAME.dng empty with O_EXCL and returns its path. An
 * existing file is never reused (G360_QUEUE_EXISTS). */
int g360_queue_create_frame(const g360_queue *queue, const char stamp[16], unsigned frame,
                            char path[512]);
/* Same reservation for dir/STAMP-FRAME.jpg. Both outputs use the same
 * frame number; each is recovered independently if its partner is partial. */
int g360_queue_create_jpeg(const g360_queue *queue, const char stamp[16], unsigned frame,
                           char path[512]);
/* Appends one frame's line to dir/STAMP.csv (created with its header when
 * new), then fsyncs it (and the directory, when created). The sweep calls it
 * as soon as a frame is captured, so a crash keeps every captured frame's
 * exposure; a half-written last line is ignored when read. */
int g360_queue_append_info(const g360_queue *queue, const char stamp[16], const g360_queue_info *info);
/* The oldest queued sweep: 1 with *stamp set, 0 if the queue is empty, -1 on
 * an IO error. */
int g360_queue_oldest(const g360_queue *queue, char stamp[16]);
/* Moves one sweep to the card, frame by frame in order. For each frame,
 * reserve one stem for its complete DNG and/or JPEG, copy and fsync all
 * outputs, append one exposure-list row per output (fsynced), then delete
 * the queued sources. A crash can duplicate a frame but never lose its only
 * complete copy. An orphan complete output is recovered on its own.
 * DNGs must have a supported complete size; JPEGs need SOI/EOI markers and
 * fit G360_QUEUE_JPEG_MAX_BYTES. Partial outputs are deleted; *dropped counts
 * frames with at least one partial output, *moved frames with any complete
 * output (one frame may count in both). STAMP.csv is deleted after the last
 * frame. Card bytes written are added to *written. */
int g360_queue_move(const g360_queue *queue, const char stamp[16], g360_storage *storage,
                    const g360_queue_hooks *hooks, uint64_t *written, unsigned *moved,
                    unsigned *dropped);
/* Appends one frame's line to photos/STAMP-sweep.csv (created with its
 * header line when new), then fsyncs it. file is the frame's name in
 * photos/ (STEM.dng or STEM.jpg). known 0 leaves
 * the exposure fields empty. Sweeps saved straight to the card use it too.
 * Returns 0 or -1; *bytes (if not NULL) gets the bytes written. */
int g360_sweep_list_append(const g360_storage *storage, const char stamp[16], const char *file,
                           unsigned frame, const g360_queue_info *info, int known, uint64_t *bytes);
const char *g360_queue_status_text(int status);
#ifdef __cplusplus
}
#endif
#endif
