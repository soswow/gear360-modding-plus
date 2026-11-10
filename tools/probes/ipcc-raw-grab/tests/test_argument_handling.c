/*
 * Host check of the grabber's argument handling and its early exits.
 *
 * The program is linked as it is, with the syscall layer replaced by this
 * harness, so the real c_entry is exercised: the argument parser, the
 * pre-allocation error path, the bench-mode check and the exit paths.  The
 * harness is not a second parser; it only answers the calls the program
 * makes.
 *
 * No camera, no network, no device: /dev/d5_ipcc is a fake descriptor, every
 * ioctl fails, and /dev/mem cannot be opened.
 *
 * The regressions this covers:
 *   - the runner's own default line carries "--bench-address 0" and
 *     "--log-dump 0", and the program used to refuse both and then write its
 *     error through a null pointer, because the output buffer is mapped only
 *     after the arguments are read;
 *   - an argument error must be reported, must not map memory and must not
 *     touch the device;
 *   - the bench mode needs an address and must say so before it opens
 *     anything;
 *   - a run that never selected the RAW switch must not write to it;
 *   - a dry run prints the frame the live path would send and touches
 *     nothing: no mapping, no device, no log file, no output file, no
 *     setting;
 *   - a still request that cannot be built is refused before the device is
 *     asked for a lease.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int c_entry(unsigned long *initial_stack);
extern void ipcc_raw_grab_host_reset(void);

#define CAP 65536u

static char out_text[CAP];
static size_t out_length;
static char err_text[CAP];
static size_t err_length;
static char log_text[CAP];
static size_t log_length;

static int device_opens;
static int device_closes;
static int memory_opens;
static int mappings;
static int forks;
static int other_opens;   /* the progress log and any other file */
static int ioctls;

static void append(char *buffer, size_t *length, const char *data, size_t count)
{
    if (*length + count >= CAP) {
        count = CAP - *length - 1u;
    }
    memcpy(buffer + *length, data, count);
    *length += count;
    buffer[*length] = '\0';
}

static void reset_capture(void)
{
    out_text[0] = '\0';
    err_text[0] = '\0';
    log_text[0] = '\0';
    out_length = 0u;
    err_length = 0u;
    log_length = 0u;
    device_opens = 0;
    device_closes = 0;
    memory_opens = 0;
    mappings = 0;
    forks = 0;
    other_opens = 0;
    ioctls = 0;
}

/* The program's syscall layer, replaced by the harness. */
long ipcc_raw_grab_host_syscall(long nr, long a0, long a1, long a2, long a3,
                                long a4, long a5)
{
    (void)a3;
    (void)a4;
    (void)a5;
    switch (nr) {
    case 5: { /* open */
        const char *path = (const char *)(unsigned long)a0;
        if (strcmp(path, "/dev/d5_ipcc") == 0) {
            ++device_opens;
            return 9;
        }
        if (strcmp(path, "/dev/mem") == 0) {
            ++memory_opens;
            return -2;
        }
        ++other_opens; /* the progress log: a sink */
        return 10;
    }
    case 4: { /* write */
        long fd = a0;
        const char *data = (const char *)(unsigned long)a1;
        size_t count = (size_t)a2;
        if (fd == 1) {
            append(out_text, &out_length, data, count);
        } else if (fd == 2) {
            append(err_text, &err_length, data, count);
        } else {
            append(log_text, &log_length, data, count);
        }
        return (long)count;
    }
    case 6: /* close */
        ++device_closes;
        return 0;
    case 91: { /* munmap */
        free((void *)(unsigned long)a0);
        return 0;
    }
    case 192: { /* mmap2 */
        size_t length = (size_t)a1;
        void *at = calloc(1u, length != 0u ? length : 1u);
        ++mappings;
        if (at == NULL) {
            return -12;
        }
        return (long)(unsigned long)at;
    }
    case 118: /* fsync */
        return 0;
    case 162: /* nanosleep */
        return 0;
    case 263: { /* clock_gettime */
        struct { long seconds; long nanoseconds; } *clock;
        clock = (void *)(unsigned long)a1;
        if (clock != NULL) {
            clock->seconds = 1;
            clock->nanoseconds = 0;
        }
        return 0;
    }
    case 54: /* ioctl: the device refuses everything */
        ++ioctls;
        return -2;
    case 2: /* fork: the harness has no child to run */
        ++forks;
        return -2;
    case 114: /* wait4 */
        return 0;
    default:
        return -2;
    }
}

static int run_program(int argc, char **argv)
{
    unsigned long stack[64];
    int index;

    memset(stack, 0, sizeof(stack));
    stack[0] = (unsigned long)argc;
    for (index = 0; index < argc && index < 60; ++index) {
        stack[1 + index] = (unsigned long)argv[index];
    }
    reset_capture();
    ipcc_raw_grab_host_reset();
    return c_entry(stack);
}

/* The line the runner builds for a non-bench mode, with its defaults. */
static int run_runner_default_line(char **argv)
{
    static char *line[] = {
        "/tmp/gear360-grab-abc/grab",
        "--mode", "still",
        "--size", "0x00100000",
        "--bank", "2",
        "--command", "0x01010016",
        "--remaining", "2518",
        "--free", "7687503872",
        "--out", "/tmp/sensor.raw",
        "--tmo", "25000",
        "--bench-address", "0",
        "--bench-length", "0xfff200",
        "--log", "/opt/gear360-grab/gear360-grab-abc/progress.log",
        "--max-seconds", "90",
        "--per", "500",
        "--set", "2000",
        "--log-dump", "0"
    };
    memcpy(argv, line, sizeof(line));
    return (int)(sizeof(line) / sizeof(line[0]));
}

static int contains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

static int occurrences(const char *haystack, const char *needle)
{
    int count = 0;
    size_t step = strlen(needle);
    const char *at = haystack;

    while (step != 0u && (at = strstr(at, needle)) != NULL) {
        ++count;
        at += step;
    }
    return count;
}

/*
 * The frame the reviewed stock still request makes, as the dry run prints
 * it: the 6-byte header, the 48-byte body of the independent stock vector
 * (command 0x01010016, Cap=1, Max=1, remaining 2448, free 7477198848) and
 * the two trailer bytes.  56 bytes, 112 hex characters.
 */
static const char stock_frame_hex[] =
    "83000102300016000101010000000100"
    "00000000000000000000900900000000"
    "adbd0100000000000000000000000000"
    "0000000000000000";

/* Nothing that touches the target may happen in a dry run. */
static void assert_no_target_io(void)
{
    assert(mappings == 0);        /* no mapping of any kind */
    assert(device_opens == 0);    /* /dev/d5_ipcc */
    assert(memory_opens == 0);    /* /dev/mem */
    assert(other_opens == 0);     /* the progress log, or any other file */
    assert(ioctls == 0);          /* no device command */
    assert(forks == 0);           /* no setting helper */
    assert(log_text[0] == '\0');  /* nothing written to a file */
}

int main(void)
{
    char *argv[64];
    int argc;
    int result;

    /* 1. The runner's default line must be accepted.  Before the fix the
     *    program refused "--bench-address 0" and then wrote the refusal
     *    through a null pointer. */
    argc = run_runner_default_line(argv);
    result = run_program(argc, argv);
    printf("default line: result=%d\n", result);
    assert(!contains(out_text, "ERROR bad bench-address"));
    assert(!contains(out_text, "ERROR bad log-dump"));
    assert(!contains(err_text, "ERROR bad bench-address"));
    assert(!contains(err_text, "ERROR bad log-dump"));
    assert(device_opens == 1);      /* the run reached the device */
    assert(result == 0);            /* and ended in the ordinary way */

    /* 2. "--log-dump 0" is the runner's default and means "no dump". */
    {
        static char *line[] = { "/tmp/g/grab", "--mode", "still",
                                 "--log-dump", "0" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("log-dump zero: result=%d\n", result);
        assert(!contains(out_text, "ERROR bad log-dump"));
        assert(!contains(err_text, "ERROR bad log-dump"));
        assert(device_opens == 1);
    }

    /* 3. The bench mode needs an address, and says so before it maps
     *    anything or opens the device. */
    {
        static char *line[] = { "/tmp/g/grab", "--mode", "bench",
                                 "--bench-address", "0" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("bench without an address: result=%d\n", result);
        assert(result == 2);
        assert(device_opens == 0);
        assert(memory_opens == 0);
        assert(mappings == 0);
        assert(contains(err_text, "ERROR bench needs --bench-address"));
    }

    /* 4. The bench mode with an address gets past the check. */
    {
        static char *line[] = { "/tmp/g/grab", "--mode", "bench",
                                 "--bench-address", "0x1234" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("bench with an address: result=%d\n", result);
        assert(!contains(err_text, "ERROR bench needs --bench-address"));
        assert(device_opens == 1);
    }

    /* 5. A bad mode is reported, safely, with no memory and no device. */
    {
        static char *line[] = { "/tmp/g/grab", "--mode", "bogus" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("bad mode: result=%d\n", result);
        assert(result == 2);
        assert(device_opens == 0);
        assert(mappings == 0);
        assert(contains(err_text, "ERROR bad mode"));
    }

    /* 6. A bad number is reported the same way. */
    {
        static char *line[] = { "/tmp/g/grab", "--mode", "still",
                                 "--size", "0" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("bad size: result=%d\n", result);
        assert(result == 2);
        assert(device_opens == 0);
        assert(mappings == 0);
        assert(contains(err_text, "ERROR bad size"));
    }

    /* 7. A number that does not fit is refused, not truncated. */
    {
        static char *line[] = { "/tmp/g/grab", "--mode", "still",
                                 "--bench-address", "0x100000000" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("oversized bench address: result=%d\n", result);
        assert(result == 2);
        assert(device_opens == 0);
        assert(contains(err_text, "ERROR bad bench-address"));
    }

    /* 8. The still mode does not touch the capture system's RAW switch. */
    {
        static char *line[] = { "/tmp/g/grab", "--mode", "still" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("still mode: result=%d\n", result);
        assert(!contains(out_text, "SSIF_RAW"));
        assert(!contains(log_text, "SSIF_RAW"));
        assert(forks == 0);
    }

    /* 9. An option the record cannot be read with is refused before the
     *    record is followed, rather than reading an unset word as an
     *    address. */
    {
        static char *line[] = { "/tmp/g/grab", "--mode", "capture",
                                 "--object-bytes", "16" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("short object bytes: result=%d\n", result);
        assert(result == 2);
        assert(mappings == 0);
        assert(device_opens == 0);
        assert(contains(err_text, "ERROR capture needs --object-bytes"));
    }

    /* 10. A dry run prints the frame the live path would send, and it
     *     touches nothing: no mapping, no device, no log file, no output
     *     file, no setting. */
    {
        static char *line[] = { "/tmp/g/grab", "--dry-run", "--mode", "still",
                                 "--command", "0x01010016",
                                 "--remaining", "2448",
                                 "--free", "7477198848" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("dry run stock vector: result=%d\n", result);
        assert(result == 0);
        assert_no_target_io();
        assert(occurrences(err_text, stock_frame_hex) == 1);
        assert(contains(err_text, "DRYRUN frame bytes=56"));
        assert(out_length == 0u);
    }

    /* 11. The runner's own default line is accepted in a dry run: the zero
     *     log-dump and the zero bench address are values, not mistakes. */
    {
        static char *line[] = {
            "/tmp/gear360-grab-abc/grab",
            "--dry-run",
            "--mode", "still",
            "--size", "0x00100000",
            "--bank", "2",
            "--command", "0x01010016",
            "--remaining", "2518",
            "--free", "7687503872",
            "--out", "/tmp/sensor.raw",
            "--tmo", "25000",
            "--bench-address", "0",
            "--bench-length", "0xfff200",
            "--log", "/opt/gear360-grab/gear360-grab-abc/progress.log",
            "--max-seconds", "90",
            "--per", "500",
            "--set", "2000",
            "--log-dump", "0"
        };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("dry run default line: result=%d\n", result);
        assert(result == 0);
        assert_no_target_io();
        assert(!contains(err_text, "ERROR bad log-dump"));
        assert(!contains(err_text, "ERROR bad bench-address"));
    }

    /* 12. A command word outside the reviewed set is refused before
     *     anything is asked of the device. */
    {
        static char *line[] = { "/tmp/g/grab", "--dry-run", "--mode", "still",
                                 "--command", "0x12345678" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("dry run bad command: result=%d\n", result);
        assert(result == 2);
        assert_no_target_io();
        assert(contains(err_text, "ERROR bad still command"));
    }

    /* 13. A mode that sends no still cannot be dry run. */
    {
        static char *line[] = { "/tmp/g/grab", "--dry-run", "--mode", "acquire" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("dry run non still mode: result=%d\n", result);
        assert(result == 2);
        assert_no_target_io();
        assert(contains(err_text, "ERROR dry run needs a still mode"));
    }

    /* 14. The bench mode sends no still either, so it is refused the same
     *     way once its own address check has passed. */
    {
        static char *line[] = { "/tmp/g/grab", "--dry-run", "--mode", "bench",
                                 "--bench-address", "0x1234" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("dry run bench mode: result=%d\n", result);
        assert(result == 2);
        assert_no_target_io();
        assert(contains(err_text, "ERROR dry run needs a still mode"));
    }

    /* 15. A release on its own sends no still and is refused as well. */
    {
        static char *line[] = { "/tmp/g/grab", "--dry-run", "--mode", "still",
                                 "--release-address", "0x1234" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("dry run release address: result=%d\n", result);
        assert(result == 2);
        assert_no_target_io();
        assert(contains(err_text, "ERROR dry run does not apply to --release-address"));
    }

    /* 16. The grab mode is a still mode, and a dry run of it does not turn
     *     the RAW switch on: the setting helper is never forked. */
    {
        static char *line[] = { "/tmp/g/grab", "--dry-run", "--mode", "grab",
                                 "--command", "0x01010016",
                                 "--remaining", "2448",
                                 "--free", "7477198848" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("dry run grab mode: result=%d\n", result);
        assert(result == 0);
        assert_no_target_io();
        assert(occurrences(err_text, stock_frame_hex) == 1);
        assert(!contains(err_text, "SSIF_RAW"));
    }

    /* 17. A live run with an unreviewed command word is refused before it
     *     takes a lease: the request is built before anything is asked of
     *     the device. */
    {
        static char *line[] = { "/tmp/g/grab", "--mode", "compare",
                                 "--command", "0x12345678" };
        argc = (int)(sizeof(line) / sizeof(line[0]));
        memcpy(argv, line, sizeof(line));
        result = run_program(argc, argv);
        printf("live bad still command: result=%d\n", result);
        assert(result == 2);
        assert(device_opens == 0);
        assert(mappings == 0);
        assert(contains(err_text, "ERROR bad still command"));
    }

    printf("argument handling: PASS\n");
    return 0;
}
