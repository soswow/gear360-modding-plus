/* Durable names for the camera app; no capture/SDK calls. */
#ifndef G360_APP_STORAGE_H
#define G360_APP_STORAGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define G360_STORAGE_STEM_BYTES 64u
#define G360_STORAGE_PATH_BYTES 512u
#define G360_STORAGE_DATETIME_BYTES 20u
#define G360_STORAGE_MAX_COUNTER 99999u

typedef enum g360_storage_status {
    G360_STORAGE_OK = 0,
    G360_STORAGE_ARGUMENT = 1,
    G360_STORAGE_IO = 2,
    G360_STORAGE_BUSY = 3,
    G360_STORAGE_CORRUPT = 4,
    G360_STORAGE_EXHAUSTED = 5,
    G360_STORAGE_CLOCK = 6,
    G360_STORAGE_LATCHED = 7
} g360_storage_status;

typedef struct g360_storage {
    char photos[448];
    char state[448];
    int lock_fd;
    int counter_fd[2];
    uint32_t counter;
    unsigned slot;
    int opened;
    int latched;
} g360_storage;

/* root's parent must exist. Creates root/photos and root/state. One process
 * holds state/storage.lock. Scans existing photo names once at open, then
 * keeps two versioned/checksummed, fsynced counter slots without renames.
 * A missing state can be recovered from photo names; two corrupt slots are
 * refused. The counter does not reset on a clock change or restart. */
int g360_storage_open(g360_storage *storage, const char *root);

/* Allocates a durable sequence before reserving the requested empty output
 * files with O_EXCL. jpg or dng may be NULL, but not both. Caller may replace only
 * these reserved files with completed JPEG/DNG bytes. Never deletes or truncates an existing photograph. Timestamp uses the
 * camera's local wall clock. Failed/interrupted captures can leave gaps and
 * empty reserved files; sequence99999 is the last and never wraps.
 * All output buffers must have the sizes defined above and be disjoint.
 * After an IO failure the storage session is latched; close/reopen to recover. */
int g360_storage_reserve(g360_storage *storage, char stem[64], char jpg[512],
                          char dng[512], char exif_datetime[20]);
/* Same operation with a caller-supplied YYYYMMDD-HHMMSS timestamp; useful for
 * deterministic scheduling/tests. Exactly15 characters and a trailing NUL. */
int g360_storage_reserve_at(g360_storage *storage, const char timestamp[16],
                             char stem[64], char jpg[512], char dng[512],
                             char exif_datetime[20]);
/* Same sequence and O_EXCL rules, reserving only the DNG name.
 * The counter scan treats the DNG like any photo. */
int g360_storage_reserve_dng_at(g360_storage *storage, const char timestamp[16],
                                 char stem[64], char dng[512]);
/* The camera's local wall clock as YYYYMMDD-HHMMSS (15 characters + NUL). */
int g360_storage_timestamp(char timestamp[16]);
void g360_storage_close(g360_storage *storage);
const char *g360_storage_status_text(int status);
#ifdef __cplusplus
}
#endif
#endif
