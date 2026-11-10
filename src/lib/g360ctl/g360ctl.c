/*
 * g360ctl implementation. Built against the camera's libc without headers
 * (capture_libc.h plus the few declarations below) and against the host's
 * own headers for tests.
 */
#include "capture_libc.h"
#include "g360ctl.h"

#include <stdint.h>

#if defined(__APPLE__)
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#define CTL_RDONLY O_RDONLY
#define CTL_NONBLOCK O_NONBLOCK
#define CTL_WRONLY O_WRONLY
#define CTL_CREAT O_CREAT
#define CTL_TRUNC O_TRUNC
#else
struct pollfd { int fd; short events; short revents; };
#define POLLIN 0x0001
int poll(struct pollfd *fds, unsigned long count, int timeout);
ptrdiff_t read(int fd, void *buffer, size_t count);
int fork(void);
int execv(const char *path, char *const argv[]);
int waitpid(int pid, int *status, int options);
int kill(int pid, int sig);
#define WNOHANG 1
#define SIGTERM 15
void _exit(int status) __attribute__((noreturn));
int *__errno_location(void);
#define errno (*__errno_location())
#define EINTR 4
#define EAGAIN 11
#define CTL_RDONLY 0
#define CTL_NONBLOCK 04000
#define CTL_WRONLY 01
#define CTL_CREAT 0100
#define CTL_TRUNC 01000
#endif

/* ---- Buttons ----------------------------------------------------------- */

/* The ARM32 input_event: int32 sec, int32 usec, u16 type, u16 code, s32 value. */
#define EVENT_SIZE 16u
#define EV_SYN 0u
#define EV_KEY 1u
#define SYN_DROPPED 3u

g360ctl_key g360ctl_key_from_code(uint32_t code)
{
    switch (code) {
    case G360CTL_CODE_POWER: return G360CTL_KEY_POWER;
    case G360CTL_CODE_CAPTURE: return G360CTL_KEY_CAPTURE;
    case G360CTL_CODE_MENU: return G360CTL_KEY_MENU;
    case G360CTL_CODE_MENU_HOLD: return G360CTL_KEY_MENU_HOLD;
    case G360CTL_CODE_POWER_HOLD: return G360CTL_KEY_POWER_HOLD;
    default: return G360CTL_KEY_OTHER;
    }
}

const char *g360ctl_key_name(g360ctl_key key)
{
    switch (key) {
    case G360CTL_KEY_POWER: return "POWER";
    case G360CTL_KEY_CAPTURE: return "CAPTURE";
    case G360CTL_KEY_MENU: return "MENU";
    case G360CTL_KEY_MENU_HOLD: return "MENU_HOLD";
    case G360CTL_KEY_POWER_HOLD: return "POWER_HOLD";
    default: return "OTHER";
    }
}

int g360ctl_buttons_open(const char *device)
{
    if (device == 0) return -1;
    return open(device, CTL_RDONLY | CTL_NONBLOCK);
}

void g360ctl_buttons_close(int fd)
{
    if (fd >= 0) (void)close(fd);
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t le16(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

/* Reads exactly one record; the kernel always returns whole records. */
static int read_record(int fd, int timeout_ms, unsigned char *record)
{
    size_t used = 0;
    while (used < EVENT_SIZE) {
        struct pollfd p;
        int ready;
        ptrdiff_t n;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        ready = poll(&p, 1, used == 0 ? timeout_ms : 1000);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ready == 0) return used == 0 ? 0 : -1;
        n = read(fd, record + used, EVENT_SIZE - used);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return -1;
        }
        if (n == 0) return -1; /* end of file (a recorded test file) */
        used += (size_t)n;
    }
    return 1;
}

int g360ctl_buttons_next(int fd, int timeout_ms, g360ctl_button_event *event, int *dropped)
{
    unsigned char record[EVENT_SIZE];
    if (event == 0) return -1;
    if (dropped != 0) *dropped = 0;
    for (;;) {
        uint32_t type;
        uint32_t code;
        int status = read_record(fd, timeout_ms, record);
        if (status <= 0) return status;
        type = le16(record + 8);
        code = le16(record + 10);
        if (type == EV_SYN && code == SYN_DROPPED) {
            if (dropped != 0) *dropped = 1;
            continue;
        }
        if (type != EV_KEY) continue;
        event->sec = (int32_t)le32(record);
        event->usec = (int32_t)le32(record + 4);
        event->code = code;
        event->value = (int32_t)le32(record + 12);
        event->key = g360ctl_key_from_code(code);
        return 1;
    }
}

/* ---- Small file helpers ------------------------------------------------ */

static int read_text(const char *path, char *buffer, size_t size)
{
    size_t used = 0;
    int fd = open(path, CTL_RDONLY);
    if (fd < 0) return -1;
    while (used + 1 < size) {
        ptrdiff_t n = read(fd, buffer + used, size - 1 - used);
        if (n < 0) {
            if (errno == EINTR) continue;
            (void)close(fd);
            return -1;
        }
        if (n == 0) break;
        used += (size_t)n;
    }
    (void)close(fd);
    buffer[used] = '\0';
    return 0;
}

static int write_text(const char *path, const char *text)
{
    size_t length = strlen(text);
    ptrdiff_t n;
    /* Like shell redirection: truncate (sysfs accepts it), never create, so
       a missing attribute fails instead of becoming a new file. */
    int fd = open(path, CTL_WRONLY | CTL_TRUNC);
    if (fd < 0) return -1;
    n = write(fd, text, length);
    if (close(fd) != 0) return -1;
    return n == (ptrdiff_t)length ? 0 : -1;
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end;
    unsigned long v;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text < '0' || *text > '9') return -1;
    v = strtoul(text, &end, 10);
    if (v > 0xffffffffUL) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    if (*end != '\0') return -1;
    *value = (uint32_t)v;
    return 0;
}

static int read_u32(const char *path, uint32_t *value)
{
    char text[64];
    if (read_text(path, text, sizeof text) != 0) return -1;
    return parse_u32(text, value);
}

static void led_path(char *out, size_t size, const char *root, int index, const char *name)
{
    (void)snprintf(out, size, "%s/led.%d/%s", root ? root : G360CTL_LED_ROOT, index, name);
}

/* The active trigger: the bracketed name in "a [b] c"; a plain single
   word (what a test file holds after a write) is taken as it is. */
static void active_trigger(const char *list, char *out, size_t size)
{
    const char *open_bracket = strchr(list, '[');
    const char *start = list;
    size_t n = 0;
    if (open_bracket != 0) start = open_bracket + 1;
    while (*start == ' ') ++start;
    while (start[n] != '\0' && start[n] != ']' && start[n] != ' ' && start[n] != '\n') ++n;
    if (n == 0 || n >= size) {
        (void)snprintf(out, size, "%s", "none");
        return;
    }
    memcpy(out, start, n);
    out[n] = '\0';
}

/* ---- LEDs -------------------------------------------------------------- */

static const char *const led_names[] = {
    "side-orange", "side-green", "side-blue", "front-green", "back-green",
    "front-orange", "back-orange",
};

const char *g360ctl_led_name(int index)
{
    if (index < 0 || index >= (int)(sizeof led_names / sizeof led_names[0])) return 0;
    return led_names[index];
}

int g360ctl_led_index(const char *name)
{
    int i;
    if (name == 0) return -1;
    for (i = 0; i < (int)(sizeof led_names / sizeof led_names[0]); ++i) {
        if (strcmp(name, led_names[i]) == 0) return i;
    }
    return -1;
}

int g360ctl_leds_read(const char *root, g360ctl_led_state *states, int max)
{
    int i;
    int count = 0;
    if (states == 0 || max <= 0 || max > G360CTL_MAX_LEDS) return -1;
    for (i = 0; i < max; ++i) {
        char path[256];
        char list[1024];
        g360ctl_led_state *s = &states[i];
        memset(s, 0, sizeof *s);
        led_path(path, sizeof path, root, i, "brightness");
        if (read_u32(path, &s->brightness) != 0) continue;
        led_path(path, sizeof path, root, i, "max_brightness");
        if (read_u32(path, &s->max_brightness) != 0) continue;
        led_path(path, sizeof path, root, i, "trigger");
        if (read_text(path, list, sizeof list) != 0) continue;
        active_trigger(list, s->trigger, sizeof s->trigger);
        led_path(path, sizeof path, root, i, "delay_on");
        if (read_u32(path, &s->delay_on) == 0) {
            led_path(path, sizeof path, root, i, "delay_off");
            s->has_delays = read_u32(path, &s->delay_off) == 0;
        }
        if (!s->has_delays) s->delay_on = s->delay_off = 0;
        s->present = 1;
        ++count;
    }
    return count;
}

static int led_write(const char *root, int index, const char *name, const char *text)
{
    char path[256];
    led_path(path, sizeof path, root, index, name);
    return write_text(path, text);
}

static int led_write_u32(const char *root, int index, const char *name, uint32_t value)
{
    char text[16];
    (void)snprintf(text, sizeof text, "%lu", (unsigned long)value);
    return led_write(root, index, name, text);
}

static int led_max(const char *root, int index, uint32_t *max)
{
    char path[256];
    if (index < 0 || index >= G360CTL_MAX_LEDS) return -1;
    led_path(path, sizeof path, root, index, "max_brightness");
    return read_u32(path, max);
}

int g360ctl_led_set(const char *root, int index, uint32_t brightness)
{
    uint32_t max;
    if (led_max(root, index, &max) != 0 || brightness > max) return -1;
    if (led_write(root, index, "trigger", "none") != 0) return -1;
    return led_write_u32(root, index, "brightness", brightness);
}

int g360ctl_led_blink(const char *root, int index, uint32_t brightness, uint32_t on_ms,
                      uint32_t off_ms)
{
    uint32_t max;
    if (led_max(root, index, &max) != 0 || brightness == 0 || brightness > max) return -1;
    if (led_write(root, index, "trigger", "timer") != 0) return -1;
    if (led_write_u32(root, index, "delay_on", on_ms) != 0) return -1;
    if (led_write_u32(root, index, "delay_off", off_ms) != 0) return -1;
    return led_write_u32(root, index, "brightness", brightness);
}

int g360ctl_leds_restore(const char *root, const g360ctl_led_state *states, int count)
{
    int i;
    int result = 0;
    if (states == 0 || count < 0 || count > G360CTL_MAX_LEDS) return -1;
    for (i = 0; i < count; ++i) {
        const g360ctl_led_state *s = &states[i];
        if (!s->present) continue;
        if (led_write(root, i, "trigger", s->trigger) != 0) {
            result = -1;
            continue;
        }
        if (strcmp(s->trigger, "timer") == 0 && s->has_delays) {
            if (led_write_u32(root, i, "delay_on", s->delay_on) != 0 ||
                led_write_u32(root, i, "delay_off", s->delay_off) != 0) {
                result = -1;
            }
        }
        if (strcmp(s->trigger, "none") == 0 || strcmp(s->trigger, "timer") == 0) {
            if (led_write_u32(root, i, "brightness", s->brightness) != 0) result = -1;
        }
    }
    return result;
}

int g360ctl_leds_save_file(const char *path, const g360ctl_led_state *states, int count)
{
    char text[G360CTL_MAX_LEDS * 160];
    size_t used = 0;
    int i;
    int fd;
    ptrdiff_t n;
    if (path == 0 || states == 0 || count < 0 || count > G360CTL_MAX_LEDS) return -1;
    for (i = 0; i < count; ++i) {
        const g360ctl_led_state *s = &states[i];
        int w;
        if (!s->present) continue;
        w = snprintf(text + used, sizeof text - used,
                     "led=%d brightness=%lu max=%lu trigger=%s delays=%d delay_on=%lu delay_off=%lu\n",
                     i, (unsigned long)s->brightness, (unsigned long)s->max_brightness, s->trigger,
                     s->has_delays, (unsigned long)s->delay_on, (unsigned long)s->delay_off);
        if (w < 0 || (size_t)w >= sizeof text - used) return -1;
        used += (size_t)w;
    }
    fd = open(path, CTL_WRONLY | CTL_CREAT | CTL_TRUNC, 0644);
    if (fd < 0) return -1;
    n = write(fd, text, used);
    if (fsync(fd) != 0) n = -1;
    if (close(fd) != 0) return -1;
    return n == (ptrdiff_t)used ? 0 : -1;
}

static int starts_with(const char *text, const char *prefix, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        if (text[i] != prefix[i]) return 0;
    }
    return 1;
}

/* Value of "key=" in one line; copies up to the next space or end of line. */
static int field(const char *line, const char *key, char *out, size_t size)
{
    size_t key_length = strlen(key);
    const char *p = line;
    while (*p != '\0' && *p != '\n') {
        if ((p == line || p[-1] == ' ') && starts_with(p, key, key_length) &&
            p[key_length] == '=') {
            size_t n = 0;
            p += key_length + 1;
            while (p[n] != '\0' && p[n] != ' ' && p[n] != '\n') ++n;
            if (n == 0 || n >= size) return -1;
            memcpy(out, p, n);
            out[n] = '\0';
            return 0;
        }
        ++p;
    }
    return -1;
}

static int field_u32(const char *line, const char *key, uint32_t *value)
{
    char text[24];
    if (field(line, key, text, sizeof text) != 0) return -1;
    return parse_u32(text, value);
}

int g360ctl_leds_load_file(const char *path, g360ctl_led_state *states, int max)
{
    char text[G360CTL_MAX_LEDS * 160 + 1];
    const char *line;
    int count = 0;
    if (path == 0 || states == 0 || max <= 0 || max > G360CTL_MAX_LEDS) return -1;
    if (read_text(path, text, sizeof text) != 0) return -1;
    memset(states, 0, sizeof *states * (size_t)max);
    line = text;
    while (*line != '\0') {
        uint32_t index;
        uint32_t delays;
        g360ctl_led_state s;
        memset(&s, 0, sizeof s);
        if (field_u32(line, "led", &index) != 0 || index >= (uint32_t)max ||
            field_u32(line, "brightness", &s.brightness) != 0 ||
            field_u32(line, "max", &s.max_brightness) != 0 ||
            field(line, "trigger", s.trigger, sizeof s.trigger) != 0 ||
            field_u32(line, "delays", &delays) != 0 || delays > 1u ||
            field_u32(line, "delay_on", &s.delay_on) != 0 ||
            field_u32(line, "delay_off", &s.delay_off) != 0 ||
            s.brightness > s.max_brightness || states[index].present) {
            return -1;
        }
        s.has_delays = (int)delays;
        s.present = 1;
        states[index] = s;
        ++count;
        while (*line != '\0' && *line != '\n') ++line;
        if (*line == '\n') ++line;
    }
    return count;
}

/* ---- Sound ------------------------------------------------------------- */

int g360ctl_sound_play(const char *player, const char *file)
{
    char *argv[3];
    int pid;
    int status = 0;
    if (player == 0 || file == 0) return -1;
    argv[0] = (char *)player;
    argv[1] = (char *)file;
    argv[2] = 0;
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        (void)execv(player, argv);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    /* Linux and Darwin encode wait statuses the same way. */
    if ((status & 0x7f) == 0) {
        int code = (status >> 8) & 0xff;
        return code == 127 ? -1 : code;
    }
    return 128 + (status & 0x7f);
}

int g360ctl_sound_start(const char *player, const char *file)
{
    char *argv[3];
    int pid;
    if (player == 0 || file == 0) return -1;
    argv[0] = (char *)player;
    argv[1] = (char *)file;
    argv[2] = 0;
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        (void)execv(player, argv);
        _exit(127);
    }
    return pid;
}

int g360ctl_sound_reap(int pid)
{
    int status;
    int r;
    if (pid <= 0) return 1;
    do { r = waitpid(pid, &status, WNOHANG); } while (r < 0 && errno == EINTR);
    return r != 0; /* ended, or no such child */
}

void g360ctl_sound_stop(int pid)
{
    int status;
    if (pid <= 0) return;
    (void)kill(pid, SIGTERM);
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }
}
