/* The motion history the kernel keeps (accelerometer and gyroscope, about
 * 200 samples a second), read to find the camera's pose around a shot.
 * Read-only; no capture or SDK call. Layout and axes:
 * target.motion-history-sample-layout-and-axes-20261106.
 *
 * On the camera the history is 8,000,000 bytes of RAM at physical
 * 0x9cd00000 (mapped through /dev/mem) and the write position comes from an
 * ioctl on /dev/accel. Tests point both at plain files instead. */
#ifndef G360_APP_MOTION_H
#define G360_APP_MOTION_H
#include <stddef.h>
#include <stdint.h>
#include "g360_pose.h"
#ifdef __cplusplus
extern "C" {
#endif

#define G360_MOTION_SAMPLE_BYTES 20u
#ifndef G360_MOTION_HISTORY_BYTES
#define G360_MOTION_HISTORY_BYTES 8000000u
#endif
/* The write position runs ahead of the data: the newest slots can still
 * hold last lap's records. Reads stay this many samples (50 ms) behind. */
#define G360_MOTION_LAG 10u
/* A window is at least this many samples (0.25 s) and at most this many (5 s). */
#define G360_MOTION_MIN_WINDOW 50u
#define G360_MOTION_MAX_WINDOW 1000u

typedef struct g360_motion_source {
    const char *history;      /* file or /dev/mem holding the history */
    uint64_t history_offset;  /* where it starts in that file (page aligned) */
    const char *position;     /* /dev/accel (ioctl) or a file of one little-endian u32 */
    int position_ioctl;       /* 1: ioctl on position; 0: read it from the file */
} g360_motion_source;

typedef struct g360_motion {
    const volatile uint32_t *history;
    size_t map_bytes;
    const char *position;
    int position_ioctl;
} g360_motion;

/* 0 on success, -1 if the history cannot be mapped. */
int g360_motion_open(g360_motion *motion, const g360_motion_source *source);
void g360_motion_close(g360_motion *motion);
/* The current write position, or -1. Mark one before and one after a shot. */
int g360_motion_position(const g360_motion *motion, uint32_t *position);
/* Adds the samples between two marks (extended to at least the minimum
 * window, cut to the last maximum) to stats, skipping stale records.
 * Returns the number added. */
uint32_t g360_motion_window(const g360_motion *motion, uint32_t from, uint32_t to,
                            g360_pose_stats *stats);
#ifdef __cplusplus
}
#endif
#endif
