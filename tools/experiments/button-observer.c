/* Passive ARM evdev observer: no ioctls, grabs, forwarding or actions.
 * --decode also reads synthetic 16-byte ARM input_event records on a host.
 * Linux ARM32 input_event: signed sec/usec, u16 type/code, signed value.
 */
#include "capture_libc.h"
#include <stdint.h>
#if defined(__APPLE__)
#include <errno.h>
#include <poll.h>
#define OBS_RDONLY O_RDONLY
#define OBS_NONBLOCK O_NONBLOCK
#else
struct pollfd { int fd; short events; short revents; };
int poll(struct pollfd *fds, unsigned long count, int timeout);
ptrdiff_t read(int fd, void *buffer, size_t count);
int *__errno_location(void);
int fflush(capture_file *stream);
#define errno (*__errno_location())
#define EINTR 4
#define EAGAIN 11
#define OBS_RDONLY 0
#define OBS_NONBLOCK 04000
#endif

static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int emit(const unsigned char *p)
{
    uint32_t type = (uint32_t)p[8] | (uint32_t)p[9] << 8;
    uint32_t code = (uint32_t)p[10] | (uint32_t)p[11] << 8;
    if (printf("event sec=%ld usec=%ld type=%lu code=%lu value=%ld syn_dropped=%u\n",
               (long)(int32_t)u32(p), (long)(int32_t)u32(p + 4),
               (unsigned long)type, (unsigned long)code, (long)(int32_t)u32(p + 12),
               (unsigned)(type == 0 && code == 3)) < 0) return -1;
    return fflush(stdout);
}

static int now_ms(uint64_t *ms)
{
    capture_timespec t;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &t) != 0) return -1;
    *ms = (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char event[16];
    size_t used = 0;
    int live, fd, result = 0;
    uint64_t start = 0, now = 0;
    unsigned long seconds = 0;
    if (argc < 3) return 2;
    live = strcmp(argv[1], "--observe") == 0;
    if (live) {
        char *end;
        if (argc != 4 || argv[3][0] < '0' || argv[3][0] > '9') return 2;
        errno = 0;
        seconds = strtoul(argv[3], &end, 10);
        if (errno || *end || seconds < 1 || seconds > 600) return 2;
        if (now_ms(&start) != 0) return 1;
    } else if (argc != 3 || strcmp(argv[1], "--decode") != 0) {
        return 2;
    }
    fd = open(argv[2], OBS_RDONLY | (live ? OBS_NONBLOCK : 0));
    if (fd < 0) return 1;
    for (;;) {
        ptrdiff_t n;
        if (live) {
            struct pollfd p = {fd, 1, 0}; /* POLLIN, Linux and Darwin */
            int ready;
            if (now_ms(&now) != 0) { result = 1; break; }
            if (now - start >= seconds * 1000u) break;
            ready = poll(&p, 1, 100);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0 || (p.revents & (8 | 16 | 32))) { result = 1; break; }
            if (!ready) continue;
        }
        n = read(fd, event + used, sizeof event - used);
        if (n < 0 && (errno == EINTR || (live && errno == EAGAIN))) continue;
        if (n < 0) { result = 1; break; }
        if (n == 0) { result = (live || used) ? 1 : 0; break; }
        used += (size_t)n;
        if (used == sizeof event) {
            if (emit(event) != 0) { result = 1; break; }
            used = 0;
        }
    }
    if (used) result = 1;
    (void)close(fd);
    return result;
}
