#include "settings.h"
#include "storage_platform.h"

#define SLOT_BYTES 20u

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t checksum(const unsigned char *p)
{
    uint32_t hash = 2166136261u;
    unsigned k;
    for (k = 0; k < 16; ++k) { hash ^= p[k]; hash *= 16777619u; }
    return hash;
}
static int valid_values(uint32_t mode, uint32_t save, uint32_t wifi, uint32_t delay, uint32_t sounds,
                        uint32_t output)
{
    return sounds <= 1 && output <= G360_OUTPUT_BOTH && mode <= G360_MODE_HDR && save <= G360_SAVE_CARD && wifi <= G360_WIFI_HOME &&
           (delay == 0 || delay == 2 || delay == 5 || delay == 10);
}
/* The delay in seconds <-> its index in the Wi-Fi byte's high nibble. */
static unsigned delay_index(uint32_t delay) { return delay == 2 ? 1u : delay == 5 ? 2u : delay == 10 ? 3u : 0u; }
static uint32_t delay_seconds(unsigned index) { static const uint32_t s[4] = {0, 2, 5, 10}; return s[index & 3u]; }
static int slot_path(char out[512], const char *dir, unsigned slot)
{
    int n = snprintf(out, 512, "%s/settings.%u", dir, slot);
    return n > 0 && n < 512 ? 0 : -1;
}

void g360_settings_default(g360_settings *s)
{
    if (s == 0) return;
    s->mode = G360_MODE_SINGLE; s->save = G360_SAVE_FLASH; s->wifi = G360_WIFI_UNSET; s->delay = 0; s->sounds = 1; s->output = G360_OUTPUT_BOTH;
    s->sequence = 0; s->slot = 1; /* the first save writes slot 0 */
}

/* 1 valid, 0 missing or invalid, -1 read error. */
static int load_slot(const char *dir, unsigned slot, g360_settings *out)
{
    unsigned char record[SLOT_BYTES + 1];
    char path[512];
    size_t used = 0;
    int fd;
    if (slot_path(path, dir, slot) != 0) return -1;
    fd = open(path, ST_RDONLY | ST_NOFOLLOW | ST_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    while (used < sizeof record) {
        ptrdiff_t n = read(fd, record + used, sizeof record - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { (void)close(fd); return -1; }
        if (n == 0) break;
        used += (size_t)n;
    }
    (void)close(fd);
    if (used != SLOT_BYTES || memcmp(record, "G360SET1", 8) != 0 || get32(record + 16) != checksum(record) ||
        (record[15] & 0x8u) != 0 || (record[15] & 0x84u) == 0x84u ||
        (record[13] != 6 && record[13] != 8) ||
        !valid_values(record[12], record[14], record[15] & 3u, delay_seconds((record[15] >> 4) & 3u), 1u, 1u)) return 0;
    out->sequence = get32(record + 8);
    out->mode = record[12]; out->save = record[14]; out->wifi = record[15] & 3u; out->delay = delay_seconds((record[15] >> 4) & 3u);
    out->sounds = (record[15] & 0x40u) ? 0u : 1u;
    out->output = (record[15] & 0x80u) ? G360_OUTPUT_JPEG :
                  (record[15] & 0x04u) ? G360_OUTPUT_RAW : G360_OUTPUT_BOTH;
    out->slot = slot;
    return 1;
}

int g360_settings_load(const char *dir, g360_settings *s)
{
    g360_settings slots[2];
    int found[2], k;
    if (s == 0) return -1;
    g360_settings_default(s);
    if (dir == 0) return -1;
    for (k = 0; k < 2; ++k) {
        found[k] = load_slot(dir, (unsigned)k, &slots[k]);
        if (found[k] < 0) return -1;
    }
    if (!found[0] && !found[1]) return 0;
    *s = found[0] && (!found[1] || slots[0].sequence > slots[1].sequence) ? slots[0] : slots[1];
    return 1;
}

int g360_settings_save(const char *dir, g360_settings *s)
{
    unsigned char record[SLOT_BYTES];
    char path[512];
    unsigned slot;
    int fd, ok, created = 0;
    if (dir == 0 || s == 0 || !valid_values(s->mode, s->save, s->wifi, s->delay, s->sounds, s->output)) return -1;
    slot = s->slot ^ 1u;
    if (slot_path(path, dir, slot) != 0) return -1;
    memset(record, 0, sizeof record);
    memcpy(record, "G360SET1", 8);
    put32(record + 8, s->sequence + 1u);
    record[12] = (unsigned char)s->mode; record[13] = 6; /* the old HDR shot count */ record[14] = (unsigned char)s->save;
    record[15] = (unsigned char)(s->wifi | (delay_index(s->delay) << 4) | (s->sounds ? 0u : 0x40u) | (s->output == G360_OUTPUT_RAW ? 0x04u : s->output == G360_OUTPUT_JPEG ? 0x80u : 0u));
    put32(record + 16, checksum(record));
    fd = open(path, ST_RDWR | ST_NOFOLLOW | ST_CLOEXEC);
    if (fd < 0 && errno == ENOENT) {
        fd = open(path, ST_RDWR | ST_CREAT | ST_EXCL | ST_NOFOLLOW | ST_CLOEXEC, 0644);
        created = fd >= 0;
    }
    if (fd < 0) return -1;
    ok = lseek(fd, 0, 0) == 0;
    if (ok) {
        size_t done = 0;
        while (done < sizeof record) {
            ptrdiff_t n = write(fd, record + done, sizeof record - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { ok = 0; break; }
            done += (size_t)n;
        }
    }
    if (ok) ok = ftruncate(fd, SLOT_BYTES) == 0 && fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (ok && created) {
        int dir_fd = open(dir, ST_RDONLY | ST_DIRECTORY | ST_NOFOLLOW | ST_CLOEXEC);
        ok = dir_fd >= 0 && fsync(dir_fd) == 0;
        if (dir_fd >= 0 && close(dir_fd) != 0) ok = 0;
    }
    if (!ok) return -1;
    s->sequence += 1u;
    s->slot = slot;
    return 0;
}
