#include "wear.h"
#include "storage_platform.h"

#define EXT_CSD_HEX 1024u

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* DIR/<the one entry with a colon, e.g. mmc0:0001>/ext_csd */
static int ext_csd_path(const char *dir, char path[512])
{
    st_dir *d = opendir(dir);
    int found = 0, failed = 0;
    if (d == 0) return -1;
    for (;;) {
        st_dirent *item;
        int n;
        errno = 0;
        item = st_readdir(d);
        if (item == 0) { if (errno != 0) failed = 1; break; }
        if (strchr(item->d_name, ':') == 0 || item->d_name[0] == '.') continue;
        n = snprintf(path, 512, "%s/%s/ext_csd", dir, item->d_name);
        if (n < 0 || n >= 512) { failed = 1; break; }
        found = 1;
        break;
    }
    if (closedir(d) != 0) failed = 1;
    return found && !failed ? 0 : -1;
}

int g360_wear_read(const char *dir, g360_wear *wear)
{
    char path[512], text[EXT_CSD_HEX + 16];
    size_t used = 0;
    unsigned k;
    int fd, values[3];
    if (wear == 0) return -1;
    wear->pre_eol = wear->life_a = wear->life_b = -1;
    if (dir == 0 || ext_csd_path(dir, path) != 0) return -1;
    fd = open(path, ST_RDONLY | ST_CLOEXEC);
    if (fd < 0) return -1;
    while (used < sizeof text) {
        ptrdiff_t n = read(fd, text + used, sizeof text - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
    }
    (void)close(fd);
    if (used < EXT_CSD_HEX) return -1;
    for (k = 0; k < EXT_CSD_HEX; ++k)
        if (hex_digit(text[k]) < 0) return -1;
    for (k = 0; k < 3; ++k) {
        unsigned at = (267u + k) * 2u;
        values[k] = hex_digit(text[at]) * 16 + hex_digit(text[at + 1]);
    }
    if (values[0] > 3 || values[1] > 11 || values[2] > 11) return -1;
    wear->pre_eol = values[0];
    wear->life_a = values[1];
    wear->life_b = values[2];
    return 0;
}

int g360_wear_level(const g360_wear *wear)
{
    if (wear == 0 || wear->life_a < 0 || wear->life_b < 0) return 0;
    return wear->life_a > wear->life_b ? wear->life_a : wear->life_b;
}
