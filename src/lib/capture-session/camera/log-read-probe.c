/*
 * log-read-probe: read the RTOS's shared log window through libuser-spec's
 * preference mapping.
 *
 * The RTOS's logutil_force_save appends records to a 0x400-entry ring and
 * LogBuffer_SendPrintEx pushes each 32-byte record to the shared address
 * 0x81c11c3c (see next-edge/still-path/PLUS38-CHECK-DECODED.md and the fact
 * session.rtos-log-ring-and-shared-send-20261102). libuser-spec's
 * pref_rtos_get_long reads a 32-bit word at the process's rtos_virt_addr
 * plus the unscaled caller byte offset (the physical 0x85400000 window, per
 * the fact session.rtos-preference-shared-byte-offset), so the word at the
 * offset 0x11c3c should be the first log record's word.
 *
 * This probe only reads shared memory. It never touches the capture
 * library, the bridge, or any camera state; it cannot issue a still.
 */
#include "capture_libc.h"
#include <stdint.h>

/* The dlfcn declarations, since the freestanding build has no system
 * headers.  The RTLD_NOW value is the Linux ABI's. */
#define RTLD_NOW 2
extern void *dlopen(const char *filename, int flag);
extern void *dlsym(void *handle, const char *symbol);

typedef long (*pref_rtos_get_long_fn)(unsigned int offset);

static capture_file *log_fp;
static int log_failures;

static void log_line(const char *text) {
    if (log_fp != 0) {
        fprintf(log_fp, "%s\n", text);
    }
}

static void log_values(const char *label, long a, long b, long c, long d) {
    if (log_fp != 0) {
        fprintf(log_fp, "log-read-probe: %s first=%ld second=%ld third=%ld fourth=%ld\n",
                label, a, b, c, d);
    }
}

static int hex_dump_words(pref_rtos_get_long_fn get_long, unsigned int offset,
                          unsigned int words) {
    char line[256];
    unsigned int i;
    for (i = 0; i < words; ++i) {
        long value = get_long(offset + i * 4);
        snprintf(line, sizeof(line), "log-read-probe: word @0x%04x = 0x%08lx",
                 offset + i * 4, (unsigned long)value);
        log_line(line);
        if (log_failures) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    void *handle = 0;
    pref_rtos_get_long_fn get_long = 0;
    const char *log_path = 0;
    int status = 0;
    unsigned int sanity_words = 16;

    if (argc != 3 || strcmp(argv[1], "--log") != 0) {
        fprintf(stderr, "usage: log-read-probe --log PATH\n");
        return 2;
    }
    log_path = argv[2];
    log_fp = fopen(log_path, "w");
    if (log_fp == 0) {
        return 1;
    }

    handle = dlopen("libuser-spec.so", RTLD_NOW);
    if (handle == 0) {
        log_line("log-read-probe: dlopen(libuser-spec.so) failed; no shared window access");
        fclose(log_fp);
        return 1;
    }
    get_long = (pref_rtos_get_long_fn)dlsym(handle, "pref_rtos_get_long");
    if (get_long == 0) {
        log_line("log-read-probe: dlsym(pref_rtos_get_long) failed");
        fclose(log_fp);
        return 1;
    }
    log_line("log-read-probe: libuser-spec resolved; reading the shared window");

    /* The known preference word (the boot type), as the sanity read. */
    log_values("boot-type pref @0x1268", get_long(0x1268), 0, 0, 0);

    /* The log records' first words, as 32-bit words. */
    status = hex_dump_words(get_long, 0x11c3c, sanity_words);

    log_values("log-read-probe done", status, 0, 0, 0);
    fclose(log_fp);
    return log_failures ? 1 : 0;
}
