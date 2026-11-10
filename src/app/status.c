#include "status.h"
#include "storage_platform.h"

#define PATH_BYTES 256u
#define CAPACITY "/sys/class/power_supply/battery/capacity"
#define BATTERY_STATUS "/sys/class/power_supply/battery/status"
#define OPERSTATE "/sys/class/net/wlan0/operstate"
#define ROUTES "/proc/net/route"
/* /proc/net/route writes addresses as little-endian hex: 192.168.43.0. */
#define AP_NETWORK "002BA8C0"
#define DEFAULT_ROUTE "00000000"

/* The last hostapd found, checked first next time. Only the UI thread
   reads the status. */
static unsigned long hostapd_pid;

/* Reads ROOT+PATH into text (NUL-terminated, at most size - 1 bytes).
   Returns the length, or -1. */
static int read_file(const char *root, const char *path, char *text, size_t size)
{
    char full[PATH_BYTES];
    size_t used = 0;
    int fd, n = snprintf(full, sizeof full, "%s%s", root, path);
    if (n < 0 || (size_t)n >= sizeof full) return -1;
    fd = open(full, ST_RDONLY | ST_CLOEXEC);
    if (fd < 0) return -1;
    while (used + 1u < size) {
        ptrdiff_t got = read(fd, text + used, size - 1u - used);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { (void)close(fd); return -1; }
        if (got == 0) break;
        used += (size_t)got;
    }
    (void)close(fd);
    text[used] = '\0';
    return (int)used;
}

/* The file's text is WORD, with or without a final newline. */
static int is_word(const char *text, const char *word)
{
    while (*word != '\0' && *text == *word) { ++text; ++word; }
    return *word == '\0' && (text[0] == '\0' || (text[0] == '\n' && text[1] == '\0'));
}

/* 0-100 with an optional newline; -1 otherwise. */
static int percent(const char *text)
{
    int value = 0, digits = 0;
    for (; *text >= '0' && *text <= '9'; ++text) {
        if (++digits > 3) return -1;
        value = value * 10 + (*text - '0');
    }
    if (digits == 0 || !(text[0] == '\0' || (text[0] == '\n' && text[1] == '\0')) || value > 100) return -1;
    return value;
}

int g360_status_cable(const char *root)
{
    static const char *const supplies[] = {"ac", "usb", "wireless"};
    char path[64], text[8];
    unsigned k;
    for (k = 0; k < sizeof supplies / sizeof supplies[0]; ++k) {
        (void)snprintf(path, sizeof path, "/sys/class/power_supply/%s/online", supplies[k]);
        if (read_file(root, path, text, sizeof text) >= 0 && is_word(text, "1")) return 1;
    }
    return 0;
}

int g360_status_home_ip(const char *root, char text[20])
{
    char input[32];
    const char *p;
    unsigned octets[4], i;
    if (!root || !text || read_file(root, "/tmp/g360-wifi/ip", input, sizeof input) < 0) return -1;
    p = input;
    for (i = 0; i < 4; ++i) {
        unsigned value = 0, digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) return -1;
            value = value * 10u + (unsigned)(*p++ - '0');
        }
        if (!digits || value > 255u) return -1;
        octets[i] = value;
        if (i < 3 && *p++ != '.') return -1;
    }
    if (*p != '\0' && !(*p == '\n' && p[1] == '\0')) return -1;
    (void)snprintf(text, 20, "%u.%u.\n%u.%u", octets[0], octets[1], octets[2], octets[3]);
    return 0;
}

int g360_status_light(const char *root)
{
    char text[32];
    if (root == 0 || read_file(root, BATTERY_STATUS, text, sizeof text) < 0) return G360_LIGHT_OFF;
    if (is_word(text, "Charging")) return G360_LIGHT_CHARGING;
    if (is_word(text, "Full") && g360_status_cable(root)) return G360_LIGHT_FULL;
    return G360_LIGHT_OFF;
}

/* Copies the next tab- or space-separated field (at most 15 characters). */
static const char *field(const char *p, char out[16])
{
    size_t n = 0;
    while (*p == ' ' || *p == '\t') ++p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
        if (n < 15u) out[n++] = *p;
        ++p;
    }
    out[n] = '\0';
    return p;
}

/* wlan0's routes: the access point's network, and a default route. */
static void routes(const char *root, int *ap, int *home)
{
    char text[4096];
    const char *line;
    *ap = *home = 0;
    if (read_file(root, ROUTES, text, sizeof text) < 0) return;
    for (line = strchr(text, '\n'); line != 0; line = strchr(line, '\n')) {
        char iface[16], destination[16];
        ++line; /* the first line is the header */
        line = field(field(line, iface), destination);
        if (strcmp(iface, "wlan0") != 0) continue;
        if (strcmp(destination, AP_NETWORK) == 0) *ap = 1;
        if (strcmp(destination, DEFAULT_ROUTE) == 0) *home = 1;
    }
}

static int is_hostapd(const char *root, unsigned long pid)
{
    char path[48], comm[32];
    (void)snprintf(path, sizeof path, "/proc/%lu/comm", pid);
    return read_file(root, path, comm, sizeof comm) >= 0 && is_word(comm, "hostapd");
}

static int hostapd_running(const char *root)
{
    char dir[PATH_BYTES];
    st_dir *d;
    int n, found = 0;
    if (hostapd_pid != 0 && is_hostapd(root, hostapd_pid)) return 1;
    hostapd_pid = 0;
    n = snprintf(dir, sizeof dir, "%s/proc", root);
    if (n < 0 || (size_t)n >= sizeof dir) return 0;
    d = opendir(dir);
    if (d == 0) return 0;
    for (;;) {
        st_dirent *item = st_readdir(d);
        const char *p;
        unsigned long pid = 0;
        if (item == 0) break;
        for (p = item->d_name; *p >= '0' && *p <= '9' && pid < 100000000ul; ++p) pid = pid * 10u + (unsigned long)(*p - '0');
        if (*p != '\0' || pid == 0) continue;
        if (is_hostapd(root, pid)) { hostapd_pid = pid; found = 1; break; }
    }
    (void)closedir(d);
    return found;
}

int g360_status_read(const char *root, g360display_status *status)
{
    char text[32];
    int ap, home, capacity = -1;
    if (status == 0) return -1;
    status->wifi = G360DISPLAY_WIFI_OFF;
    status->battery = -1;
    status->charging = 0;
    status->wifi_searching = 0;
    status->wifi_error = 0;
    if (root == 0) return -1;
    routes(root, &ap, &home);
    if (ap && hostapd_running(root)) status->wifi = G360DISPLAY_WIFI_AP;
    else if (home && read_file(root, OPERSTATE, text, sizeof text) >= 0 && is_word(text, "up"))
        status->wifi = G360DISPLAY_WIFI_HOME;
    if (read_file(root, "/tmp/g360-wifi/searching", text, sizeof text) >= 0 && is_word(text, "1"))
        status->wifi_searching = 1;
    if (read_file(root, "/tmp/g360-wifi/reconnecting", text, sizeof text) >= 0 && is_word(text, "1"))
        status->wifi_searching = 1;
    if (read_file(root, "/tmp/g360-wifi/error", text, sizeof text) >= 0 && is_word(text, "1"))
        status->wifi_error = 1;
    if (read_file(root, CAPACITY, text, sizeof text) >= 0) capacity = percent(text);
    if (capacity < 0) return -1;
    status->battery = capacity;
    if (read_file(root, BATTERY_STATUS, text, sizeof text) >= 0) {
        if (is_word(text, "Charging")) status->charging = 1;
        else if (is_word(text, "Full")) status->charging = g360_status_cable(root);
    }
    return 0;
}
