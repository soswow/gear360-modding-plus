#include "app_platform.h"
#include "motion.h"
#include <stdint.h>
#if defined(__APPLE__)
#include <sys/ioctl.h>
#include <sys/mman.h>
#define APP_MAP(bytes, fd, offset) mmap(0, (bytes), PROT_READ, MAP_SHARED, (fd), (off_t)(offset))
#else
/* The camera's glibc 2.13 has no headers here. mmap's off_t is 32 bits and
   the history sits at 0x9cd00000, so the 64-bit mmap64 is the one to call. */
int ioctl(int file, unsigned long request, ...);
void *mmap64(void *address, size_t length, int protection, int flags, int file, long long offset);
int munmap(void *address, size_t length);
#define PROT_READ 1
#define MAP_SHARED 1
#define MAP_FAILED ((void *)-1)
#define APP_MAP(bytes, fd, offset) mmap64(0, (bytes), PROT_READ, MAP_SHARED, (fd), (long long)(offset))
#endif

/* _IOR('l', 7, 4): the write position of the history (the stock app's gyro
   responder uses the same call). */
#define ACCEL_POSITION_IOCTL 0x80046c07ul
#define OPEN_READ_ONLY 0
#define OPEN_READ_WRITE 2
#define SLOTS (G360_MOTION_HISTORY_BYTES / G360_MOTION_SAMPLE_BYTES)

int g360_motion_open(g360_motion *motion, const g360_motion_source *source)
{
    int fd;
    void *map;
    memset(motion, 0, sizeof *motion);
    if (source == 0 || source->history == 0 || source->position == 0) return -1;
    fd = open(source->history, OPEN_READ_ONLY);
    if (fd < 0) return -1;
    map = APP_MAP(G360_MOTION_HISTORY_BYTES, fd, source->history_offset);
    (void)close(fd);
    if (map == MAP_FAILED) return -1;
    motion->history = (const volatile uint32_t *)map;
    motion->map_bytes = G360_MOTION_HISTORY_BYTES;
    motion->position = source->position;
    motion->position_ioctl = source->position_ioctl;
    return 0;
}

void g360_motion_close(g360_motion *motion)
{
    if (motion->history) (void)munmap((void *)(uintptr_t)motion->history, motion->map_bytes);
    memset(motion, 0, sizeof *motion);
}

int g360_motion_position(const g360_motion *motion, uint32_t *position)
{
    int fd, ok;
    uint32_t value = 0;
    if (motion->history == 0 || position == 0) return -1;
    fd = open(motion->position, motion->position_ioctl ? OPEN_READ_WRITE : OPEN_READ_ONLY);
    if (fd < 0) return -1;
    if (motion->position_ioctl) ok = ioctl(fd, ACCEL_POSITION_IOCTL, &value) == 0;
    else ok = read(fd, &value, sizeof value) == (ptrdiff_t)sizeof value;
    (void)close(fd);
    if (!ok || value >= SLOTS) return -1;
    *position = value;
    return 0;
}

static int16_t high(uint32_t word) { return (int16_t)(uint16_t)(word >> 16); }
static int16_t low(uint32_t word) { return (int16_t)(uint16_t)(word & 0xffffu); }

uint32_t g360_motion_window(const g360_motion *motion, uint32_t from, uint32_t to, g360_pose_stats *stats)
{
    uint32_t first, last, count, k, added = 0, previous, rejected = 0;
    if (motion->history == 0 || from >= SLOTS || to >= SLOTS) return 0;
    first = (from + SLOTS - G360_MOTION_LAG) % SLOTS;
    last = (to + SLOTS - G360_MOTION_LAG) % SLOTS;
    count = (last + SLOTS - first) % SLOTS;
    if (count > SLOTS / 2u) count = 0; /* the position went backwards */
    if (count < G360_MOTION_MIN_WINDOW) count = G360_MOTION_MIN_WINDOW;
    if (count > G360_MOTION_MAX_WINDOW) count = G360_MOTION_MAX_WINDOW;
    first = (last + SLOTS - count) % SLOTS;
    /* The record before the window is the reference for the first delta. */
    previous = motion->history[((first + SLOTS - 1u) % SLOTS) * (G360_MOTION_SAMPLE_BYTES / 4u)];
    for (k = 0; k < count; ++k) {
        const volatile uint32_t *s = motion->history + ((first + k) % SLOTS) * (G360_MOTION_SAMPLE_BYTES / 4u);
        uint32_t stamp = s[0], delta = stamp - previous, w2 = s[2], w3 = s[3], w4 = s[4];
        int16_t accel[3], gyro[3];
        /* Records are about 5 ms apart; a stale one from the last lap is not. */
        if (delta < 2000u || delta > 20000u) {
            /* Three in a row: the reference was the bad one; start over from here. */
            if (++rejected >= 3u) { previous = stamp; rejected = 0; }
            continue;
        }
        previous = stamp;
        rejected = 0;
        accel[0] = high(w2); accel[1] = low(w2); accel[2] = low(w3);
        gyro[0] = low(w4); gyro[1] = high(w3); gyro[2] = high(w4);
        g360_pose_stats_add(stats, accel, gyro);
        ++added;
    }
    return added;
}
