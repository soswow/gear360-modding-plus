#include "g360_pose.h"

#include "capture_libc.h"

#if defined(__APPLE__)
#include <math.h>
#else
/* The camera build has no headers; libm is linked. */
double sqrt(double x);
double asin(double x);
double atan2(double y, double x);
#endif

/*
 * The history already has the driver's own calibration subtracted
 * (calib_xyz, target.linux-peripherals-survey-20261106), and the live view
 * used it as it is. So no offset of ours: the counts are the driver's, scaled
 * by the chip's sensitivity (122 ug per count, 8197 counts per g). The six
 * hand-held face readings of 6 November still left about +50 counts on Y and
 * +240 on Z (up to 1.7 degrees); that is not corrected here. The raw means
 * in the XMP let a later calibration redo old photos.
 */
#define COUNTS_PER_G 8197.0

#define RAD_TO_DEG 57.29577951308232

void g360_pose_stats_init(g360_pose_stats *stats)
{
    memset(stats, 0, sizeof *stats);
}

void g360_pose_stats_add(g360_pose_stats *stats, const int16_t accel[3], const int16_t gyro[3])
{
    int k;
    ++stats->samples;
    for (k = 0; k < 3; ++k) {
        stats->accel_sum[k] += accel[k];
        stats->accel_sq += (double)accel[k] * accel[k];
        stats->gyro_sq += (double)gyro[k] * gyro[k];
    }
}

int g360_pose_from_stats(const g360_pose_stats *stats, g360_pose *pose)
{
    double norm, forward, spread;
    int k;
    memset(pose, 0, sizeof *pose);
    if (stats == 0 || stats->samples == 0) return -1;
    pose->samples = stats->samples;
    for (k = 0; k < 3; ++k) {
        pose->raw_accel[k] = stats->accel_sum[k] / stats->samples;
        pose->up[k] = pose->raw_accel[k] / COUNTS_PER_G;
    }
    norm = sqrt(pose->up[0] * pose->up[0] + pose->up[1] * pose->up[1] + pose->up[2] * pose->up[2]);
    if (norm < 1e-6) { memset(pose, 0, sizeof *pose); return -1; }
    forward = -pose->up[1] / norm; /* the front lens looks along -Y */
    if (forward > 1.0) forward = 1.0;
    if (forward < -1.0) forward = -1.0;
    pose->pitch_deg = asin(forward) * RAD_TO_DEG;
    /* Right of the viewer is +X, the top of the camera -Z. Rolled
       clockwise, the right side goes down and "up" leans toward -X. */
    if (sqrt(pose->up[0] * pose->up[0] + pose->up[2] * pose->up[2]) > 0.02 * norm)
        pose->roll_deg = atan2(-pose->up[0], -pose->up[2]) * RAD_TO_DEG;
    pose->gyro_rms_dps = sqrt(stats->gyro_sq / stats->samples) * G360_POSE_GYRO_DPS_PER_COUNT;
    {
        double mean_sq = 0.0, per_sample = stats->accel_sq / stats->samples;
        for (k = 0; k < 3; ++k) mean_sq += pose->raw_accel[k] * pose->raw_accel[k];
        spread = per_sample - mean_sq;
        pose->accel_std_g = spread > 0.0 ? sqrt(spread) / COUNTS_PER_G : 0.0;
    }
    return 0;
}

/* No "-0.00" in the packet. */
static double tidy(double v, double step) { return (v < step && v > -step) ? 0.0 : v; }

int g360_pose_xmp(const g360_pose *pose, char *out, size_t capacity, size_t *size)
{
    int n;
    if (size) *size = 0;
    if (pose == 0 || out == 0 || size == 0 || pose->samples == 0) return -1;
    n = snprintf(out, capacity,
        "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
        "  <rdf:Description rdf:about=\"\"\n"
        "    xmlns:GPano=\"http://ns.google.com/photos/1.0/panorama/\"\n"
        "    xmlns:g360=\"http://gear360-modding.invalid/ns/pose/1.0/\"\n"
        "    GPano:PosePitchDegrees=\"%.2f\"\n"
        "    GPano:PoseRollDegrees=\"%.2f\"\n"
        "    g360:PoseVersion=\"1\"\n"
        "    g360:Axes=\"x away from buttons, y away from front lens, z down out of the grip; up = accelerometer at rest, +1 g\"\n"
        "    g360:Calibration=\"%s\"\n"
        "    g360:Samples=\"%u\"\n"
        "    g360:UpX=\"%.5f\"\n"
        "    g360:UpY=\"%.5f\"\n"
        "    g360:UpZ=\"%.5f\"\n"
        "    g360:RawAccelX=\"%.1f\"\n"
        "    g360:RawAccelY=\"%.1f\"\n"
        "    g360:RawAccelZ=\"%.1f\"\n"
        "    g360:AccelStdG=\"%.4f\"\n"
        "    g360:GyroRmsDps=\"%.2f\"/>\n"
        " </rdf:RDF>\n"
        "</x:xmpmeta>\n"
        "<?xpacket end=\"w\"?>",
        tidy(pose->pitch_deg, 0.005), tidy(pose->roll_deg, 0.005), G360_POSE_CALIBRATION, (unsigned)pose->samples,
        tidy(pose->up[0], 5e-6), tidy(pose->up[1], 5e-6), tidy(pose->up[2], 5e-6),
        tidy(pose->raw_accel[0], 0.05), tidy(pose->raw_accel[1], 0.05), tidy(pose->raw_accel[2], 0.05),
        pose->accel_std_g, pose->gyro_rms_dps);
    if (n < 0 || (size_t)n >= capacity) return -1;
    *size = (size_t)n;
    return 0;
}
