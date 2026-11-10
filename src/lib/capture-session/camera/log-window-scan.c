/*
 * log-window-scan: dump the whole shared preference window and report what
 * is live in it.
 *
 * The log-read probe read the RTOS's log destination at offset 0x11c3c and
 * found zeros, while the boot-type preference at 0x1268 read a real value.
 * That leaves two readings: the log slot is empty, or the log slot is not at
 * that offset.  This probe answers that from the live device instead of
 * from the address arithmetic, by dumping every word of the mapped window.
 *
 * The window is read through libuser-spec's pref_rtos_get_long, the same
 * accessor the preferences themselves use, so the dump is exactly what a
 * Linux process sees of the shared region.  It writes two files:
 *
 *   --bin   the raw window bytes, 0x81000 of them
 *   --text  a summary: which chunks hold non-zero data, and every run of
 *           printable text with its offset
 *
 * The window is dumped twice, with a pause between, so that a later
 * comparison can tell which regions the RTOS is actively updating.
 *
 * This probe only reads shared memory.  It never touches the capture
 * library, the bridge, or any camera state.
 */
#include "capture_libc.h"
#include <stdint.h>

#define RTLD_NOW 2
extern void *dlopen(const char *filename, int flag);
extern void *dlsym(void *handle, const char *symbol);

typedef long (*pref_rtos_get_long_fn)(unsigned int offset);

/* The shared window's mapped length: physical 0x85400000 + 0x81000. */
#define WINDOW_BYTES 0x81000u

/* A chunk is this many bytes for the non-zero map. */
#define CHUNK_BYTES 256u

/* The shortest printable run that is worth reporting. */
#define MIN_TEXT_RUN 6u

static capture_file *text_fp;

static void text_line(const char *line) {
    if (text_fp != 0) {
        fprintf(text_fp, "%s\n", line);
    }
}

static int printable(int value) {
    return value >= 0x20 && value <= 0x7e;
}

/* Dump the window into a fresh file and return its size, or -1. */
static long dump_window(pref_rtos_get_long_fn get_long, const char *path) {
    capture_file *out = fopen(path, "wb");
    unsigned int offset;
    if (out == 0) {
        return -1;
    }
    for (offset = 0; offset + 4 <= WINDOW_BYTES; offset += 4) {
        long value = get_long(offset);
        unsigned char word[4];
        word[0] = (unsigned char)(value & 0xff);
        word[1] = (unsigned char)((value >> 8) & 0xff);
        word[2] = (unsigned char)((value >> 16) & 0xff);
        word[3] = (unsigned char)((value >> 24) & 0xff);
        if (fwrite(word, 1, 4, out) != 4) {
            fclose(out);
            return -1;
        }
    }
    fclose(out);
    return (long)WINDOW_BYTES;
}

/* Report the non-zero chunks and the printable runs of one dump. */
static void summarise(const char *path, const char *label) {
    capture_file *in = fopen(path, "rb");
    static unsigned char buffer[CHUNK_BYTES];
    unsigned int base = 0;
    if (in == 0) {
        char line[128];
        snprintf(line, sizeof(line), "%s: the dump could not be reopened", label);
        text_line(line);
        return;
    }
    while (fread(buffer, 1, CHUNK_BYTES, in) == CHUNK_BYTES) {
        unsigned int i;
        unsigned int non_zero = 0;
        for (i = 0; i < CHUNK_BYTES; ++i) {
            if (buffer[i] != 0) {
                ++non_zero;
            }
        }
        if (non_zero != 0) {
            char line[128];
            snprintf(line, sizeof(line), "%s: chunk @0x%05x non_zero=%u",
                     label, base, non_zero);
            text_line(line);
        }
        base += CHUNK_BYTES;
    }
    fclose(in);
}

/* Every run of printable text, with its offset. */
static void report_text(const char *path, const char *label) {
    capture_file *in = fopen(path, "rb");
    static unsigned char buffer[CHUNK_BYTES];
    unsigned int base = 0;
    if (in == 0) {
        return;
    }
    while (fread(buffer, 1, CHUNK_BYTES, in) == CHUNK_BYTES) {
        unsigned int i = 0;
        while (i < CHUNK_BYTES) {
            unsigned int start;
            unsigned int length;
            char line[256];
            if (!printable(buffer[i])) {
                ++i;
                continue;
            }
            start = i;
            while (i < CHUNK_BYTES && printable(buffer[i])) {
                ++i;
            }
            length = i - start;
            if (length < MIN_TEXT_RUN) {
                continue;
            }
            {
                unsigned int copy = length < 180 ? length : 180;
                unsigned int k;
                for (k = 0; k < copy; ++k) {
                    line[k] = (char)buffer[start + k];
                }
                line[copy] = '\0';
                snprintf(line + copy, sizeof(line) - copy,
                         " (len %u)", length);
                {
                    char full[320];
                    snprintf(full, sizeof(full), "%s: text @0x%05x: %s",
                             label, base + start, line);
                    text_line(full);
                }
            }
        }
        base += CHUNK_BYTES;
    }
    fclose(in);
}

int main(int argc, char **argv) {
    void *handle = 0;
    pref_rtos_get_long_fn get_long = 0;
    const char *text_path = 0;
    const char *bin_path = 0;
    const char *bin2_path = 0;
    long size;
    int i;

    for (i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--text") == 0) {
            text_path = argv[i + 1];
        } else if (strcmp(argv[i], "--bin") == 0) {
            bin_path = argv[i + 1];
        } else if (strcmp(argv[i], "--bin2") == 0) {
            bin2_path = argv[i + 1];
        } else {
            fprintf(stderr, "usage: log-window-scan --text PATH --bin PATH "
                            "[--bin2 PATH]\n");
            return 2;
        }
    }
    if (text_path == 0 || bin_path == 0) {
        fprintf(stderr, "usage: log-window-scan --text PATH --bin PATH "
                        "[--bin2 PATH]\n");
        return 2;
    }

    text_fp = fopen(text_path, "w");
    if (text_fp == 0) {
        return 1;
    }

    handle = dlopen("libuser-spec.so", RTLD_NOW);
    if (handle == 0) {
        text_line("log-window-scan: dlopen(libuser-spec.so) failed");
        fclose(text_fp);
        return 1;
    }
    get_long = (pref_rtos_get_long_fn)dlsym(handle, "pref_rtos_get_long");
    if (get_long == 0) {
        text_line("log-window-scan: dlsym(pref_rtos_get_long) failed");
        fclose(text_fp);
        return 1;
    }
    text_line("log-window-scan: libuser-spec resolved");

    {
        char line[160];
        snprintf(line, sizeof(line),
                 "log-window-scan: boot-type pref @0x1268 = 0x%08lx",
                 (unsigned long)get_long(0x1268));
        text_line(line);
    }

    size = dump_window(get_long, bin_path);
    {
        char line[160];
        snprintf(line, sizeof(line), "log-window-scan: first dump bytes = %ld",
                 size);
        text_line(line);
    }
    if (size < 0) {
        fclose(text_fp);
        return 1;
    }
    summarise(bin_path, "first");
    report_text(bin_path, "first");

    if (bin2_path != 0) {
        /* A short pause, so that a later comparison can see the RTOS's own
         * updates rather than only the state at one instant. */
        {
            capture_timespec wait;
            wait.tv_sec = 0;
            wait.tv_nsec = 500000000L;
            nanosleep(&wait, 0);
        }
        size = dump_window(get_long, bin2_path);
        {
            char line[160];
            snprintf(line, sizeof(line), "log-window-scan: second dump bytes = %ld",
                     size);
            text_line(line);
        }
        if (size >= 0) {
            summarise(bin2_path, "second");
            report_text(bin2_path, "second");
        }
    }

    text_line("log-window-scan: done");
    fclose(text_fp);
    return 0;
}
