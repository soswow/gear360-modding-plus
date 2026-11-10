/*
 * The camera's pose at a shot, from the motion chip, as an XMP packet.
 *
 * Pure: no file, no allocation, no camera call. The caller feeds in the
 * samples of the motion history (accelerometer and gyroscope counts, in the
 * history's own axes) and gets the pose and the XMP packet back.
 *
 * Axes (target.motion-history-sample-layout-and-axes-20261106): +X points
 * away from the buttons, +Y away from the front lens (the one on the
 * screen side; the front lens looks along -Y), +Z down out of the grip when
 * the camera stands upright. An accelerometer at rest reads +1 g on the
 * axis that points up, so an upright camera reads (0, 0, -1) g.
 *
 * Pose, for a viewer that looks out of the front lens with the top of the
 * camera (-Z) up, so that +X is to the viewer's right:
 *   pitch = angle of the front lens axis above the horizon,
 *   roll  = clockwise turn of the camera about that axis, as the viewer
 *           sees it (the horizon turns counterclockwise in the picture,
 *           which is GPano's PoseRollDegrees).
 * Roll is undefined with the lens straight up or down (reported as 0); the
 * "up" vector, which the XMP also carries, stays well defined there.
 */
#ifndef G360_POSE_H
#define G360_POSE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The running sums over the samples of a window. */
typedef struct g360_pose_stats {
    uint32_t samples;
    double accel_sum[3]; /* counts, X Y Z */
    double accel_sq;     /* sum of X*X + Y*Y + Z*Z */
    double gyro_sq;      /* same for the gyroscope */
} g360_pose_stats;

/* What a window says. samples is 0 when there was no window. */
typedef struct g360_pose {
    uint32_t samples;
    double raw_accel[3]; /* mean accelerometer, counts */
    double up[3];        /* calibrated mean accelerometer, g: +1 on the axis pointing up */
    double pitch_deg;    /* -90..90 */
    double roll_deg;     /* -180..180 */
    double gyro_rms_dps; /* rotation rate over the window, degrees per second */
    double accel_std_g;  /* spread of the accelerometer vector over the window */
} g360_pose;

/* The history's counts carry the driver's calibration; see g360_pose.c. */
#define G360_POSE_CALIBRATION "driver-calib_xyz"
#define G360_POSE_GYRO_DPS_PER_COUNT 0.07

void g360_pose_stats_init(g360_pose_stats *stats);
/* One sample: accelerometer X Y Z and gyroscope X Y Z, counts. */
void g360_pose_stats_add(g360_pose_stats *stats, const int16_t accel[3], const int16_t gyro[3]);
/* Returns 0 and fills pose, or -1 with pose zeroed if there are no samples
 * or the accelerometer mean is zero. */
int g360_pose_from_stats(const g360_pose_stats *stats, g360_pose *pose);

/* Enough room for the packet written below. */
#define G360_POSE_XMP_MAX 2048u

/*
 * Writes the XMP packet (UTF-8, NUL-terminated, size without the NUL):
 * GPano:PosePitchDegrees and GPano:PoseRollDegrees for any tool, and our
 * own g360: properties for the stitcher (the calibrated "up" vector, the
 * raw means and the window's stillness). Heading is not written: the camera
 * has no magnetometer. Returns 0, or -1 if the pose is empty or the buffer
 * is too small.
 */
int g360_pose_xmp(const g360_pose *pose, char *out, size_t capacity, size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* G360_POSE_H */
