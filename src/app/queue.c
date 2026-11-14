#include "queue.h"
#include "storage_platform.h"

#define COPY_CHUNK ((size_t)1u << 20)
#define COPY_SYNC_CHUNKS 2u /* 2 MiB between syncs */
#define INFO_LIMIT 4096u
#define INFO_HEADER "g360-sweep 1\nframe,tv,iso,exposure_numerator,exposure_denominator,actual_iso\n"
#define CARD_HEADER "file,frame,tv,iso,exposure_time,actual_iso\n"

static int digit(char c) { return c >= '0' && c <= '9'; }

/* YYYYMMDD-HHMMSS: the shape only; the storage reservation checks the fields. */
static int stamp_shape(const char *p)
{
    unsigned k;
    for (k = 0; k < 15; ++k)
        if (k == 8 ? p[k] != '-' : !digit(p[k])) return 0;
    return 1;
}

static int stamp_valid(const char *stamp)
{
    return stamp != 0 && stamp_shape(stamp) && stamp[15] == '\0';
}

/* STAMP-K.dng/.jpg -> K (1..max), STAMP.csv -> 0; -1 otherwise. */
static int parse_name(const char *name, char stamp[16])
{
    size_t length = strlen(name);
    unsigned frame = 0, k;
    if (length < 19 || !stamp_shape(name)) return -1;
    if (length == 19 && !strcmp(name + 15, ".csv")) {
        memcpy(stamp, name, 15); stamp[15] = '\0';
        return 0;
    }
    if (name[15] != '-' || (length != 21 && length != 22) ||
        (strcmp(name + length - 4, ".dng") && strcmp(name + length - 4, ".jpg"))) return -1;
    for (k = 16; k < length - 4; ++k) {
        if (!digit(name[k])) return -1;
        frame = frame * 10u + (unsigned)(name[k] - '0');
    }
    if (name[16] == '0' || frame < 1 || frame > G360_QUEUE_MAX_FRAMES) return -1;
    memcpy(stamp, name, 15); stamp[15] = '\0';
    return (int)frame;
}

static int join(char *out, size_t size, const char *dir, const char *name)
{
    int n = snprintf(out, size, "%s/%s", dir, name);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}

static int sync_dir(const char *path)
{
    int fd = open(path, ST_RDONLY | ST_DIRECTORY | ST_NOFOLLOW | ST_CLOEXEC);
    int ok;
    if (fd < 0) return -1;
    ok = fsync(fd);
    if (close(fd) != 0) ok = -1;
    return ok;
}

static int write_all(int fd, const char *data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ptrdiff_t n = write(fd, data + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || (size_t)n > size - done) return -1;
        done += (size_t)n;
    }
    return 0;
}

int g360_queue_open(g360_queue *q, const char *dir)
{
    int fd;
    if (q == 0) return G360_QUEUE_ARGUMENT;
    memset(q, 0, sizeof *q);
    if (dir == 0 || dir[0] == '\0' || strlen(dir) >= sizeof q->dir) return G360_QUEUE_ARGUMENT;
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return G360_QUEUE_IO;
    fd = open(dir, ST_RDONLY | ST_DIRECTORY | ST_NOFOLLOW | ST_CLOEXEC);
    if (fd < 0) return G360_QUEUE_IO;
    if (close(fd) != 0) return G360_QUEUE_IO;
    memcpy(q->dir, dir, strlen(dir) + 1);
    q->opened = 1;
    return G360_QUEUE_OK;
}

static int create_output(const g360_queue *q, const char stamp[16], unsigned frame, const char *extension, char path[512])
{
    char name[32];
    int fd, n;
    if (path == 0) return G360_QUEUE_ARGUMENT;
    path[0] = '\0';
    if (q == 0 || !q->opened || !stamp_valid(stamp) || frame < 1 || frame > G360_QUEUE_MAX_FRAMES)
        return G360_QUEUE_ARGUMENT;
    n = snprintf(name, sizeof name, "%s-%u.%s", stamp, frame, extension);
    if (n < 0 || (size_t)n >= sizeof name || join(path, 512, q->dir, name)) return G360_QUEUE_ARGUMENT;
    fd = open(path, ST_WRONLY | ST_CREAT | ST_EXCL | ST_NOFOLLOW | ST_CLOEXEC, 0644);
    if (fd < 0) {
        int exists = errno == EEXIST;
        path[0] = '\0';
        return exists ? G360_QUEUE_EXISTS : G360_QUEUE_IO;
    }
    if (close(fd) != 0 || sync_dir(q->dir) != 0) return G360_QUEUE_IO;
    return G360_QUEUE_OK;
}

int g360_queue_create_frame(const g360_queue *q, const char stamp[16], unsigned frame, char path[512])
{
    return create_output(q, stamp, frame, "dng", path);
}

int g360_queue_create_jpeg(const g360_queue *q, const char stamp[16], unsigned frame, char path[512])
{
    return create_output(q, stamp, frame, "jpg", path);
}

int g360_queue_append_info(const g360_queue *q, const char stamp[16], const g360_queue_info *info)
{
    char path[512], name[32], line[96];
    int fd, n, ok, created;
    long size;
    if (q == 0 || !q->opened || !stamp_valid(stamp) || info == 0 ||
        info->frame < 1 || info->frame > G360_QUEUE_MAX_FRAMES) return G360_QUEUE_ARGUMENT;
    n = snprintf(name, sizeof name, "%s.csv", stamp);
    if (n < 0 || (size_t)n >= sizeof name || join(path, sizeof path, q->dir, name)) return G360_QUEUE_ARGUMENT;
    n = snprintf(line, sizeof line, "%lu,%ld,%lu,%lu,%lu,%lu\n",
                 (unsigned long)info->frame, (long)info->tv, (unsigned long)info->iso,
                 (unsigned long)info->exposure_numerator, (unsigned long)info->exposure_denominator,
                 (unsigned long)info->actual_iso);
    if (n < 0 || (size_t)n >= sizeof line) return G360_QUEUE_ARGUMENT;
    fd = open(path, ST_WRONLY | ST_CREAT | ST_APPEND | ST_NOFOLLOW | ST_CLOEXEC, 0644);
    if (fd < 0) return G360_QUEUE_IO;
    size = (long)lseek(fd, 0, 2);
    created = size == 0;
    ok = size >= 0;
    if (ok && created) ok = write_all(fd, INFO_HEADER, strlen(INFO_HEADER)) == 0;
    if (ok) ok = write_all(fd, line, (size_t)n) == 0 && fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (ok && created) ok = sync_dir(q->dir) == 0;
    return ok ? G360_QUEUE_OK : G360_QUEUE_IO;
}

int g360_queue_oldest(const g360_queue *q, char stamp[16])
{
    st_dir *dir;
    int found = 0, failed = 0;
    if (q == 0 || !q->opened || stamp == 0) return -1;
    stamp[0] = '\0';
    dir = opendir(q->dir);
    if (dir == 0) return -1;
    for (;;) {
        st_dirent *item;
        char candidate[16];
        errno = 0;
        item = st_readdir(dir);
        if (item == 0) { if (errno != 0) failed = 1; break; }
        if (parse_name(item->d_name, candidate) < 0) continue;
        if (!found || strcmp(candidate, stamp) < 0) { memcpy(stamp, candidate, 16); found = 1; }
    }
    if (closedir(dir) != 0) failed = 1;
    if (failed) { stamp[0] = '\0'; return -1; }
    return found;
}

/* Strict "unsigned" or "-unsigned" field ending at ',' or '\n'. */
static const char *field(const char *p, int allow_sign, long *value)
{
    int negative = 0;
    unsigned long n = 0;
    if (allow_sign && *p == '-') { negative = 1; ++p; }
    if (!digit(*p)) return 0;
    while (digit(*p)) {
        n = n * 10u + (unsigned long)(*p - '0');
        if (n > 2147483647UL) return 0;
        ++p;
    }
    if (*p != ',' && *p != '\n') return 0;
    *value = negative ? -(long)n : (long)n;
    return p;
}

/* Reads STAMP.csv into info[frame-1]; missing or damaged lines stay unknown. */
static void read_info(const g360_queue *q, const char *stamp, g360_queue_info info[G360_QUEUE_MAX_FRAMES],
                      int known[G360_QUEUE_MAX_FRAMES])
{
    char path[512], name[32], text[INFO_LIMIT + 1];
    size_t used = 0, header = strlen(INFO_HEADER);
    const char *p;
    int fd;
    memset(known, 0, sizeof(int) * G360_QUEUE_MAX_FRAMES);
    if (snprintf(name, sizeof name, "%s.csv", stamp) <= 0 || join(path, sizeof path, q->dir, name)) return;
    fd = open(path, ST_RDONLY | ST_NOFOLLOW | ST_CLOEXEC);
    if (fd < 0) return;
    while (used < INFO_LIMIT) {
        ptrdiff_t n = read(fd, text + used, INFO_LIMIT - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
    }
    (void)close(fd);
    text[used] = '\0';
    if (used < header || memcmp(text, INFO_HEADER, header) != 0) return;
    p = text + header;
    while (*p) {
        long v[6];
        unsigned k;
        const char *line = p;
        for (k = 0; k < 6 && line; ++k) {
            line = field(line, k == 1, &v[k]);
            if (line && k < 5) line = *line == ',' ? line + 1 : 0;
        }
        if (line == 0 || *line != '\n') break; /* a damaged line ends the reading */
        p = line + 1;
        if (v[0] < 1 || v[0] > (long)G360_QUEUE_MAX_FRAMES) continue;
        k = (unsigned)v[0] - 1u;
        info[k].frame = (uint32_t)v[0]; info[k].tv = (int32_t)v[1]; info[k].iso = (uint32_t)v[2];
        info[k].exposure_numerator = (uint32_t)v[3]; info[k].exposure_denominator = (uint32_t)v[4];
        info[k].actual_iso = (uint32_t)v[5];
        known[k] = 1;
    }
}

static long file_size(int fd)
{
    long size = (long)lseek(fd, 0, 2);
    if (size < 0 || (long)lseek(fd, 0, 0) != 0) return -1;
    return size;
}

/* Copies from into the existing (reserved, empty) file to and fsyncs it. */
static int copy_file(const char *from, const char *to, unsigned char *buffer, uint64_t *bytes)
{
    int in, out, ok = 1;
    uint64_t total = 0;
    unsigned chunks = 0;
    in = open(from, ST_RDONLY | ST_NOFOLLOW | ST_CLOEXEC);
    if (in < 0) return -1;
    out = open(to, ST_WRONLY | ST_TRUNC | ST_NOFOLLOW | ST_CLOEXEC);
    if (out < 0) { (void)close(in); return -1; }
    for (;;) {
        ptrdiff_t n = read(in, buffer, COPY_CHUNK);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = 0; break; }
        if (n == 0) break;
        if (write_all(out, (const char *)buffer, (size_t)n) != 0) { ok = 0; break; }
        total += (uint64_t)n;
        /* Sync every COPY_SYNC_CHUNKS so the frame doesn't pile up in RAM
           waiting for the card (see SAVE_SYNC_BYTES in g360_output.c). */
        if (++chunks == COPY_SYNC_CHUNKS) {
            if (fsync(out) != 0) { ok = 0; break; }
            chunks = 0;
        }
    }
    if (ok && fsync(out) != 0) ok = 0;
    if (close(out) != 0) ok = 0;
    (void)close(in);
    *bytes = total;
    return ok ? 0 : -1;
}

int g360_sweep_list_append(const g360_storage *storage, const char stamp[16], const char *file,
                           unsigned frame, const g360_queue_info *info, int known, uint64_t *bytes)
{
    char path[512], name[40], line[160];
    int fd, n, ok, created;
    long size;
    if (bytes) *bytes = 0;
    if (storage == 0 || !stamp_valid(stamp) || file == 0 || info == 0) return -1;
    n = snprintf(name, sizeof name, "%s-sweep.csv", stamp);
    if (n < 0 || (size_t)n >= sizeof name || join(path, sizeof path, storage->photos, name)) return -1;
    if (!known)
        n = snprintf(line, sizeof line, "%s,%u,,,,\n", file, frame);
    else if (info->exposure_numerator && info->exposure_denominator)
        n = snprintf(line, sizeof line, "%s,%u,%ld,%lu,%lu/%lu,%lu\n", file, frame, (long)info->tv,
                     (unsigned long)info->iso, (unsigned long)info->exposure_numerator,
                     (unsigned long)info->exposure_denominator, (unsigned long)info->actual_iso);
    else
        n = snprintf(line, sizeof line, "%s,%u,%ld,%lu,,%lu\n", file, frame, (long)info->tv,
                     (unsigned long)info->iso, (unsigned long)info->actual_iso);
    if (n < 0 || (size_t)n >= sizeof line) return -1;
    fd = open(path, ST_WRONLY | ST_CREAT | ST_APPEND | ST_NOFOLLOW | ST_CLOEXEC, 0644);
    if (fd < 0) return -1;
    size = (long)lseek(fd, 0, 2);
    created = size == 0;
    ok = size >= 0;
    if (ok && created) ok = write_all(fd, CARD_HEADER, strlen(CARD_HEADER)) == 0;
    if (ok) ok = write_all(fd, line, (size_t)n) == 0 && fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (ok && created) ok = sync_dir(storage->photos) == 0;
    if (bytes) *bytes = (uint64_t)n + (created ? strlen(CARD_HEADER) : 0u);
    return ok ? 0 : -1;
}

/* Queue JPEGs are complete only with both file markers. This is an
 * interrupted-write check, not a decoder or a substitute for JPEG validation. */
static int read_marker(int fd, unsigned char marker[2])
{
    size_t used = 0;
    while (used < 2) {
        ptrdiff_t n = read(fd, marker + used, 2 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return -1;
        if (n == 0) return 0;
        used += (size_t)n;
    }
    return 1;
}

/* -1 IO error, 0 interrupted output, 1 complete output. */
static int complete_output(const char *path, int jpeg, uint64_t *bytes)
{
    int fd = open(path, ST_RDONLY | ST_NOFOLLOW | ST_CLOEXEC), valid;
    long size;
    unsigned char marker[2];
    if (fd < 0) return -1;
    size = file_size(fd);
    valid = size < 0 ? -1 : 0;
    if (size >= 0 && !jpeg)
        valid = (uint64_t)size == G360_QUEUE_FRAME_BYTES || (uint64_t)size == G360_QUEUE_PREV_FRAME_BYTES ||
                (uint64_t)size == G360_QUEUE_OLD_FRAME_BYTES;
    else if (size >= 4 && (uint64_t)size <= G360_QUEUE_JPEG_MAX_BYTES) {
        int read_status = read_marker(fd, marker);
        if (read_status < 0) valid = -1;
        else if (read_status && marker[0] == 0xffu && marker[1] == 0xd8u) {
            if ((long)lseek(fd, size - 2, 0) != size - 2) valid = -1;
            else {
                read_status = read_marker(fd, marker);
                valid = read_status < 0 ? -1 : read_status && marker[0] == 0xffu && marker[1] == 0xd9u;
            }
        }
    }
    if (close(fd) != 0) valid = -1;
    if (valid > 0) *bytes = (uint64_t)size;
    return valid;
}

int g360_queue_move(const g360_queue *q, const char stamp[16], g360_storage *storage,
                    const g360_queue_hooks *hooks, uint64_t *written, unsigned *moved, unsigned *dropped)
{
    static const char *const extension[2] = {"dng", "jpg"};
    g360_queue_info info[G360_QUEUE_MAX_FRAMES];
    int known[G360_QUEUE_MAX_FRAMES], present[G360_QUEUE_MAX_FRAMES];
    char path[512], name[32];
    unsigned char *buffer = 0;
    unsigned k, total = 0, done = 0;
    st_dir *dir;
    int status = G360_QUEUE_OK, failed = 0;
    if (moved) *moved = 0;
    if (dropped) *dropped = 0;
    if (q == 0 || !q->opened || !stamp_valid(stamp) || storage == 0 || written == 0) return G360_QUEUE_ARGUMENT;
    memset(info, 0, sizeof info);
    memset(present, 0, sizeof present);
    dir = opendir(q->dir);
    if (dir == 0) return G360_QUEUE_IO;
    for (;;) {
        st_dirent *item;
        char candidate[16];
        int frame;
        errno = 0;
        item = st_readdir(dir);
        if (item == 0) { if (errno != 0) failed = 1; break; }
        frame = parse_name(item->d_name, candidate);
        if (frame > 0 && !strcmp(candidate, stamp)) {
            if (!present[frame - 1]) ++total;
            present[frame - 1] |= !strcmp(item->d_name + strlen(item->d_name) - 4, ".jpg") ? 2 : 1;
        }
    }
    if (closedir(dir) != 0) failed = 1;
    if (failed) return G360_QUEUE_IO;
    read_info(q, stamp, info, known);
    if (total) {
        buffer = malloc(COPY_CHUNK);
        if (buffer == 0) return G360_QUEUE_IO;
    }
    for (k = 0; k < G360_QUEUE_MAX_FRAMES; ++k) {
        char stem[64], destination[2][512], source[2][512], file[72], date[20];
        uint64_t size[2] = {0, 0}, bytes = 0, line_bytes = 0, frame_bytes = 0, frame_lines = 0;
        unsigned j;
        int complete = 0, partial = 0, reserve;
        if (!present[k]) continue;
        for (j = 0; j < 2; ++j) {
            int valid;
            if (!(present[k] & (1 << j))) continue;
            if (snprintf(name, sizeof name, "%s-%u.%s", stamp, k + 1u, extension[j]) <= 0 ||
                join(source[j], sizeof source[j], q->dir, name)) { status = G360_QUEUE_ARGUMENT; break; }
            valid = complete_output(source[j], j == 1, &size[j]);
            if (valid < 0) { status = G360_QUEUE_IO; break; }
            if (valid) complete |= 1 << j;
            else partial = 1;
        }
        if (status != G360_QUEUE_OK) break;
        if (hooks && hooks->before && hooks->before(hooks->context, done, total, size[0] + size[1])) {
            status = G360_QUEUE_STOPPED; break;
        }
        if (complete) {
            reserve = g360_storage_reserve_at(storage, stamp, stem,
                        (complete & 2) ? destination[1] : 0,
                        (complete & 1) ? destination[0] : 0, date);
            if (reserve != G360_STORAGE_OK) { status = G360_QUEUE_STORAGE; break; }
            /* Keep both sources until every complete output and list row is
             * durable. A crash can duplicate a frame, never discard its only
             * complete copy. Pairs share a single reserved card stem. */
            for (j = 0; j < 2; ++j) {
                if (!(complete & (1 << j))) continue;
                if (copy_file(source[j], destination[j], buffer, &bytes) != 0 || bytes != size[j]) {
                    status = G360_QUEUE_IO; break;
                }
                frame_bytes += bytes;
            }
            if (status != G360_QUEUE_OK) break;
            for (j = 0; j < 2; ++j) {
                if (!(complete & (1 << j))) continue;
                if (snprintf(file, sizeof file, "%s.%s", stem, extension[j]) <= 0 ||
                    g360_sweep_list_append(storage, stamp, file, k + 1u, &info[k], known[k], &line_bytes) != 0) {
                    status = G360_QUEUE_IO; break;
                }
                frame_lines += line_bytes;
            }
            if (status != G360_QUEUE_OK) break;
        }
        for (j = 0; j < 2; ++j) {
            if ((present[k] & (1 << j)) && unlink(source[j]) != 0) { status = G360_QUEUE_IO; break; }
        }
        if (status != G360_QUEUE_OK) break;
        *written += frame_bytes + frame_lines;
        ++done;
        if (partial && dropped) ++*dropped;
        if (complete) {
            if (moved) ++*moved;
            if (hooks && hooks->after) hooks->after(hooks->context, stem, frame_bytes);
        }
    }
    free(buffer);
    if (status == G360_QUEUE_OK) {
        if (snprintf(name, sizeof name, "%s.csv", stamp) <= 0 || join(path, sizeof path, q->dir, name))
            return G360_QUEUE_ARGUMENT;
        if (unlink(path) != 0 && errno != ENOENT) return G360_QUEUE_IO;
    }
    if (sync_dir(q->dir) != 0 && status == G360_QUEUE_OK) status = G360_QUEUE_IO;
    return status;
}

const char *g360_queue_status_text(int status)
{
    switch (status) {
    case G360_QUEUE_OK: return "ok";
    case G360_QUEUE_ARGUMENT: return "invalid queue argument";
    case G360_QUEUE_IO: return "queue IO failure";
    case G360_QUEUE_EXISTS: return "queue file already exists";
    case G360_QUEUE_STORAGE: return "card name reservation failed";
    case G360_QUEUE_STOPPED: return "stopped";
    default: return "unknown queue status";
    }
}
