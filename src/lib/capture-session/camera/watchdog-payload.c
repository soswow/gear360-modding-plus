/*
 * The SDK-free watchdog qualification payload.
 *
 * It qualifies the camera watchdog: the staged helper must start, take the
 * runner's fixed arguments, write its marker and go away.  It never opens
 * the capture library and never calls into it, so nothing here can reach a
 * camera, a driver or a firmware service.  The only file it touches is the
 * generated progress path the runner hands it.
 */
#include "capture_libc.h"

#if !defined(__APPLE__)
/* The camera's C library is installed without its headers. */
int close(int file);
unsigned int sleep(unsigned int seconds);
#endif

#define MARKER "SDK-free watchdog qualification\n"
#define MARKER_LEN ((size_t)(sizeof(MARKER) - 1))
#define EXPECTED_ARGC 6
#define EXPECTED_LIBRARY "/usr/lib/libcapture-fw-prod.so"
#define LOG_PREFIX "/tmp/gear360-load-"
#define PREFIX_LEN ((size_t)(sizeof(LOG_PREFIX) - 1))
#define HEX_LEN ((size_t)12)
#define LOG_SUFFIX "/progress"

static int has_prefix(const char *path, const char *prefix)
{
    size_t i;

    for (i = 0; prefix[i] != '\0'; i++) {
        if (path[i] != prefix[i]) {
            return 0;
        }
    }
    return 1;
}

/* True only for the exact path the runner generates. */
static int log_path_ok(const char *path)
{
    size_t i;

    if (path == 0 || strlen(path) != PREFIX_LEN + HEX_LEN + (sizeof(LOG_SUFFIX) - 1)) {
        return 0;
    }
    if (!has_prefix(path, LOG_PREFIX)) {
        return 0;
    }
    for (i = 0; i < HEX_LEN; i++) {
        char c = path[PREFIX_LEN + i];

        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return 0;
        }
    }
    return strcmp(path + PREFIX_LEN + HEX_LEN, LOG_SUFFIX) == 0;
}

int main(int argc, char **argv)
{
    const char *path;
    long written;
    int fd;

    if (argc != EXPECTED_ARGC || strcmp(argv[1], "--load-only") != 0 ||
        strcmp(argv[2], "--library") != 0 ||
        strcmp(argv[3], EXPECTED_LIBRARY) != 0 ||
        strcmp(argv[4], "--log") != 0) {
        printf("watchdog-payload: refusing: not the runner's arguments\n");
        return 2;
    }
    path = argv[5];
    if (!log_path_ok(path)) {
        printf("watchdog-payload: refusing: not the generated log path\n");
        return 2;
    }
    fd = open(path, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC, 0644);
    if (fd < 0) {
        printf("watchdog-payload: refusing: the log path did not open\n");
        return 3;
    }
    written = (long)write(fd, MARKER, MARKER_LEN);
    fsync(fd);
    close(fd);
    if (written != (long)MARKER_LEN) {
        printf("watchdog-payload: refusing: the marker did not write\n");
        return 3;
    }
    if (write(1, MARKER, MARKER_LEN) != (ptrdiff_t)MARKER_LEN) {
        return 3;
    }
#if defined(WATCHDOG_TEST_HANG) && (WATCHDOG_TEST_HANG == 1)
    for (;;) {
        sleep(3600);
    }
#endif
    return 0;
}
