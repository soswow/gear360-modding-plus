/*
 * g360-ctl: the camera's buttons, LEDs and sound from the command line.
 *
 *   g360-ctl [--root DIR] leds list
 *   g360-ctl [--root DIR] leds save FILE
 *   g360-ctl [--root DIR] leds restore FILE
 *   g360-ctl [--root DIR] leds set LED BRIGHTNESS
 *   g360-ctl [--root DIR] leds blink LED BRIGHTNESS ON_MS OFF_MS
 *   g360-ctl buttons DEVICE SECONDS
 *   g360-ctl sound PLAYER FILE
 *
 * LED is a number (0-15) or a name from trial C1b: side-orange, side-green,
 * side-blue, front-green, back-green, front-orange, back-orange ("front" is
 * the lens on the screen side). --root replaces /sys/class/leds (host tests). "leds set" turns the
 * trigger off first; "leds blink" uses the kernel's timer trigger; "leds
 * save"/"restore" keep every LED's state in a text file so a trial can put
 * them back even from another process. "buttons" reads DEVICE passively
 * (no grab) for SECONDS and prints one line per key event:
 *   button t=SEC.USEC code=N key=NAME value=1|0|2
 * and "dropped" when the kernel lost events. "sound" runs PLAYER FILE
 * (for example /usr/bin/paplay) and reports its exit status.
 *
 * Exit status: 0 done, 1 failed, 2 bad arguments.
 */
#include "capture_libc.h"
#include "g360ctl.h"

#include <stdint.h>

#if !defined(__APPLE__)
int fflush(capture_file *stream);
#endif

static int parse_number(const char *text, uint32_t limit, uint32_t *value)
{
    char *end;
    unsigned long v;
    if (text == 0 || text[0] < '0' || text[0] > '9') return -1;
    v = strtoul(text, &end, 10);
    if (*end != '\0' || v > limit) return -1;
    *value = (uint32_t)v;
    return 0;
}

static int monotonic_ms(uint64_t *ms)
{
    capture_timespec t;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &t) != 0) return -1;
    *ms = (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
    return 0;
}

/* An LED by number or by name (side-orange, side-green, ...). */
static int parse_led(const char *text, uint32_t *index)
{
    int named = g360ctl_led_index(text);
    if (named >= 0) {
        *index = (uint32_t)named;
        return 0;
    }
    return parse_number(text, G360CTL_MAX_LEDS - 1u, index);
}

static void print_led(int index, const g360ctl_led_state *s)
{
    const char *name = g360ctl_led_name(index);
    printf("led=%d brightness=%lu max=%lu trigger=%s", index, (unsigned long)s->brightness,
           (unsigned long)s->max_brightness, s->trigger);
    if (name != 0) printf(" name=%s", name);
    if (s->has_delays) {
        printf(" delay_on=%lu delay_off=%lu", (unsigned long)s->delay_on,
               (unsigned long)s->delay_off);
    }
    printf("\n");
}

static int leds(const char *root, int argc, char **argv)
{
    g360ctl_led_state states[G360CTL_MAX_LEDS];
    uint32_t index;
    uint32_t value;
    uint32_t on_ms;
    uint32_t off_ms;
    int count;
    int i;

    if (argc < 1) return 2;
    if (strcmp(argv[0], "list") == 0 && argc == 1) {
        count = g360ctl_leds_read(root, states, G360CTL_MAX_LEDS);
        if (count < 0) return 1;
        for (i = 0; i < G360CTL_MAX_LEDS; ++i) {
            if (states[i].present) print_led(i, &states[i]);
        }
        printf("leds present=%d\n", count);
        return 0;
    }
    if (strcmp(argv[0], "save") == 0 && argc == 2) {
        count = g360ctl_leds_read(root, states, G360CTL_MAX_LEDS);
        if (count <= 0) {
            fprintf(stderr, "g360-ctl: no LEDs found\n");
            return 1;
        }
        if (g360ctl_leds_save_file(argv[1], states, G360CTL_MAX_LEDS) != 0) {
            fprintf(stderr, "g360-ctl: cannot write %s\n", argv[1]);
            return 1;
        }
        printf("leds saved=%d file=%s\n", count, argv[1]);
        return 0;
    }
    if (strcmp(argv[0], "restore") == 0 && argc == 2) {
        count = g360ctl_leds_load_file(argv[1], states, G360CTL_MAX_LEDS);
        if (count <= 0) {
            fprintf(stderr, "g360-ctl: cannot read saved states from %s\n", argv[1]);
            return 1;
        }
        if (g360ctl_leds_restore(root, states, G360CTL_MAX_LEDS) != 0) {
            fprintf(stderr, "g360-ctl: some LEDs were not restored\n");
            return 1;
        }
        printf("leds restored=%d\n", count);
        return 0;
    }
    if (strcmp(argv[0], "set") == 0 && argc == 3) {
        if (parse_led(argv[1], &index) != 0 || parse_number(argv[2], 0xffffu, &value) != 0) {
            return 2;
        }
        if (g360ctl_led_set(root, (int)index, value) != 0) {
            fprintf(stderr, "g360-ctl: led %lu not set\n", (unsigned long)index);
            return 1;
        }
        printf("led=%lu set brightness=%lu\n", (unsigned long)index, (unsigned long)value);
        return 0;
    }
    if (strcmp(argv[0], "blink") == 0 && argc == 5) {
        if (parse_led(argv[1], &index) != 0 || parse_number(argv[2], 0xffffu, &value) != 0 ||
            parse_number(argv[3], 60000u, &on_ms) != 0 ||
            parse_number(argv[4], 60000u, &off_ms) != 0) {
            return 2;
        }
        if (g360ctl_led_blink(root, (int)index, value, on_ms, off_ms) != 0) {
            fprintf(stderr, "g360-ctl: led %lu not blinking\n", (unsigned long)index);
            return 1;
        }
        printf("led=%lu blink brightness=%lu on_ms=%lu off_ms=%lu\n", (unsigned long)index,
               (unsigned long)value, (unsigned long)on_ms, (unsigned long)off_ms);
        return 0;
    }
    return 2;
}

static int buttons(const char *device, const char *seconds_text)
{
    uint32_t seconds;
    uint64_t start;
    uint64_t now;
    int fd;
    unsigned long events = 0;
    if (parse_number(seconds_text, 3600u, &seconds) != 0 || seconds == 0) return 2;
    fd = g360ctl_buttons_open(device);
    if (fd < 0) {
        fprintf(stderr, "g360-ctl: cannot open %s\n", device);
        return 1;
    }
    if (monotonic_ms(&start) != 0) {
        g360ctl_buttons_close(fd);
        return 1;
    }
    printf("buttons device=%s seconds=%lu\n", device, (unsigned long)seconds);
    for (;;) {
        g360ctl_button_event event;
        int dropped = 0;
        int status;
        uint64_t left;
        if (monotonic_ms(&now) != 0) break;
        if (now - start >= (uint64_t)seconds * 1000u) break;
        left = (uint64_t)seconds * 1000u - (now - start);
        status = g360ctl_buttons_next(fd, left > 1000u ? 1000 : (int)left, &event, &dropped);
        if (dropped) {
            printf("dropped\n");
            (void)fflush(stdout);
        }
        if (status < 0) {
            printf("buttons end (read error or end of input)\n");
            break;
        }
        if (status == 0) continue;
        ++events;
        printf("button t=%ld.%06ld code=%lu key=%s value=%ld\n", (long)event.sec,
               (long)event.usec, (unsigned long)event.code, g360ctl_key_name(event.key),
               (long)event.value);
        (void)fflush(stdout); /* the recording survives an interrupted run */
    }
    g360ctl_buttons_close(fd);
    printf("buttons events=%lu\n", events);
    return 0;
}

static int sound(const char *player, const char *file)
{
    uint64_t start = 0;
    uint64_t end = 0;
    int status;
    (void)monotonic_ms(&start);
    status = g360ctl_sound_play(player, file);
    (void)monotonic_ms(&end);
    printf("sound player=%s file=%s status=%d ms=%lu\n", player, file, status,
           (unsigned long)(end - start));
    return status == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *root = 0;
    int first = 1;
    if (argc >= 3 && strcmp(argv[1], "--root") == 0) {
        root = argv[2];
        first = 3;
    }
    if (argc - first >= 1 && strcmp(argv[first], "leds") == 0) {
        int status = leds(root, argc - first - 1, argv + first + 1);
        if (status == 2) goto usage;
        return status;
    }
    if (root == 0 && argc - first == 3 && strcmp(argv[first], "buttons") == 0) {
        int status = buttons(argv[first + 1], argv[first + 2]);
        if (status == 2) goto usage;
        return status;
    }
    if (root == 0 && argc - first == 3 && strcmp(argv[first], "sound") == 0) {
        return sound(argv[first + 1], argv[first + 2]);
    }
usage:
    fprintf(stderr,
            "usage: g360-ctl [--root DIR] leds list|save FILE|restore FILE|set LED VALUE|\n"
            "                                 blink LED VALUE ON_MS OFF_MS\n"
            "  LED: 0-15 or side-orange side-green side-blue front-green back-green\n"
            "       front-orange back-orange\n"
            "       g360-ctl buttons DEVICE SECONDS\n"
            "       g360-ctl sound PLAYER FILE\n");
    return 2;
}
