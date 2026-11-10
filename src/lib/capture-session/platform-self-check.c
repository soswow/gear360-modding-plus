/*
 * platform-self-check: the standalone check of the platform.
 *
 * This is the safe check to run on the camera.  It links nothing but the
 * camera's own C library, its clock library and the compiler's runtime,
 * together with our startup code, and it prints what the platform gave
 * the program: the arguments it was handed, the widths of the types it
 * is built with, and a reading of the monotonic clock.
 *
 * It opens no library, creates no client, realises nothing, and touches
 * no device, no settings, no file and no network.  It does not depend on
 * the capture session, on the capture library host, or on the camera's
 * own libraries: everything here is the platform and nothing else.
 *
 * The output is bounded: at most a fixed number of arguments are
 * printed, and each one is cut to a fixed length, so that a long
 * argument cannot make the report unreadable.
 *
 * Usage:
 *   platform-self-check [anything ...]
 * The arguments are only printed, never interpreted.
 */
#include "capture_libc.h"

/* How much of the command line is reported. */
#define PLATFORM_MAX_ARGUMENTS 8
#define PLATFORM_MAX_ARGUMENT_LENGTH 64

#if defined(__arm__) || defined(__thumb__)
#define PLATFORM_TARGET_NAME "arm (32-bit)"
#else
#define PLATFORM_TARGET_NAME "host"
#endif

static void print_argument(int index, const char *text)
{
    int shown = 0;

    printf("  argv[%d] = ", index);
    if (text == 0) {
        printf("(null)\n");
        return;
    }
    while (shown < PLATFORM_MAX_ARGUMENT_LENGTH && text[shown] != '\0') {
        printf("%c", text[shown]);
        ++shown;
    }
    if (text[shown] != '\0') {
        printf("... (%d characters shown)", PLATFORM_MAX_ARGUMENT_LENGTH);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    capture_timespec now;
    int limit;
    int shown;

    printf("platform-self-check\n");
    printf("  target: %s\n", PLATFORM_TARGET_NAME);
    printf("  this program opens no library and touches no device\n");

    printf("  pointer: %lu bytes, size_t: %lu bytes, long: %lu bytes\n",
           (unsigned long)sizeof(void *), (unsigned long)sizeof(size_t),
           (unsigned long)sizeof(long));

    limit = (argc < PLATFORM_MAX_ARGUMENTS) ? argc : PLATFORM_MAX_ARGUMENTS;
    printf("  argc = %d\n", argc);
    for (shown = 0; shown < limit; ++shown) {
        print_argument(shown, argv[shown]);
    }
    if (argc > PLATFORM_MAX_ARGUMENTS) {
        printf("  ... and %d more arguments\n",
               argc - PLATFORM_MAX_ARGUMENTS);
    }

    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &now) != 0) {
        printf("  the monotonic clock could not be read\n");
        return 1;
    }
    printf("  monotonic clock: %lu seconds and %lu nanoseconds\n",
           (unsigned long)now.tv_sec, (unsigned long)now.tv_nsec);

    printf("  platform-self-check done\n");
    return 0;
}
