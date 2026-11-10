/*
 * g360-preview: the T20 movie-preview trial (output/live-preview-T20/RESULT.md).
 *
 *   g360-preview --out DIR --log FILE --free-bytes N [--size 1280x640]
 *                [--fps10 150] [--seconds 10] [--set] [--still] [--quality 75]
 *                [--check-refusal]
 *
 * Opens the capture library like g360-shot, starts the camera's movie
 * preview (MV_START), counts the frames on every topic for --seconds,
 * copies one frame a second (timing the copy), stops it (MV_STOP), saves
 * the last copied frame as DIR/frame.yuv with DIR/frame.txt, times a
 * software JPEG encode of it with the camera's libturbojpeg (half and full
 * size, DIR/frame-half.jpg and DIR/frame-full.jpg), and with --still takes
 * one normal still afterwards (DIR/still-main.jpg).
 *
 * Exit status: 0 when the preview started, delivered frames, stopped with
 * its acknowledgement and (with --still) the still succeeded; 1 otherwise;
 * 2 for bad arguments.
 */
#include "capture_libc.h"
#include "g360cap.h"

#include <stdint.h>

#define TURBOJPEG_LIBRARY "/usr/lib/libturbojpeg.so"
#define COPY_BYTES ((size_t)16u * 1024u * 1024u)
#define TJPF_RGB 0
#define TJSAMP_420 2

typedef void *(*tj_init_fn)(void);
typedef int (*tj_compress2_fn)(void *handle, unsigned char *source, int width, int pitch,
                               int height, int pixel_format, unsigned char **jpeg,
                               unsigned long *jpeg_size, int subsampling, int quality,
                               int flags);
typedef void (*tj_free_fn)(unsigned char *buffer);
typedef int (*tj_destroy_fn)(void *handle);

/* Shared with the frame callback (the capture library's receiver thread). */
static struct {
    volatile uint32_t frames;
    volatile uint32_t with_planes;
    volatile uint32_t sides[4];
    volatile uint32_t want_copy;
    volatile uint32_t copies;
    uint32_t topic_seen[4];
    unsigned char first_block[4][G360_PREVIEW_BLOCK_BYTES];
    g360_preview_frame copied;     /* description of the last copy (pointers cleared) */
    unsigned char *copy;           /* Y then C */
    uint64_t copy_ns_total;
    uint64_t copy_ns_max;
    uint64_t first_ns;
    uint64_t last_ns;
    uint64_t first_stamp;
    uint64_t last_stamp;
} probe;

static uint64_t now_ns(void)
{
    capture_timespec t;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static void sleep_ms(uint32_t ms)
{
    capture_timespec t;
    capture_timespec left;
    t.tv_sec = (long)(ms / 1000u);
    t.tv_nsec = (long)(ms % 1000u) * 1000000L;
    (void)nanosleep(&t, &left);
}

static uint32_t topic_index(uint32_t topic)
{
    return topic == G360_PREVIEW_TOPIC_SET ? 0u : (topic & 3u);
}

static void on_frame(const g360_preview_frame *frame, void *context)
{
    uint32_t index = topic_index(frame->topic);
    uint64_t t = now_ns();
    (void)context;
    if (!probe.topic_seen[index]) {
        memcpy(probe.first_block[index], frame->block, G360_PREVIEW_BLOCK_BYTES);
        probe.topic_seen[index] = 1;
    }
    if (__sync_add_and_fetch(&probe.frames, 1u) == 1u) {
        probe.first_ns = t;
        probe.first_stamp = frame->timestamp;
    }
    probe.last_ns = t;
    probe.last_stamp = frame->timestamp;
    __sync_fetch_and_add(&probe.sides[frame->side & 3u], 1u);
    if (frame->y == 0 || frame->c == 0) return;
    __sync_fetch_and_add(&probe.with_planes, 1u);
    if (__sync_bool_compare_and_swap(&probe.want_copy, 1u, 0u) &&
        frame->y_bytes + frame->c_bytes <= COPY_BYTES) {
        uint64_t begin = now_ns();
        uint64_t spent;
        memcpy(probe.copy, frame->y, frame->y_bytes);
        memcpy(probe.copy + frame->y_bytes, frame->c, frame->c_bytes);
        spent = now_ns() - begin;
        probe.copy_ns_total += spent;
        if (spent > probe.copy_ns_max) probe.copy_ns_max = spent;
        probe.copied = *frame;
        probe.copied.y = 0;
        probe.copied.c = 0;
        probe.copied.block = 0;
        __sync_fetch_and_add(&probe.copies, 1u);
    }
}

static int write_file(const char *path, const unsigned char *data, size_t size)
{
    capture_file *f = fopen(path, "wb");
    size_t done;
    if (f == 0) return -1;
    done = fwrite(data, 1, size, f);
    if (fclose(f) != 0 || done != size) return -1;
    return 0;
}

static unsigned char clamp(int v)
{
    return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* NV12 (Y plane, then interleaved Cb Cr at half size) to RGB, BT.601
   full range. step 2: half size (one Y sample per chroma sample). */
static void nv12_to_rgb(const unsigned char *y, const unsigned char *c, uint32_t stride,
                        uint32_t width, uint32_t height, uint32_t step, unsigned char *rgb)
{
    uint32_t row;
    uint32_t col;
    uint32_t out_w = width / step;
    for (row = 0; row < height / step; ++row) {
        uint32_t sy = row * step;
        const unsigned char *yr = y + (size_t)sy * stride;
        const unsigned char *cr = c + (size_t)(sy / 2u) * stride;
        unsigned char *o = rgb + (size_t)row * out_w * 3u;
        for (col = 0; col < out_w; ++col) {
            uint32_t sx = col * step;
            int yy = yr[sx];
            int cb = cr[sx & ~1u] - 128;
            int cv = cr[(sx & ~1u) + 1u] - 128;
            o[0] = clamp(yy + ((91881 * cv) >> 16));
            o[1] = clamp(yy - ((22554 * cb + 46802 * cv) >> 16));
            o[2] = clamp(yy + ((116130 * cb) >> 16));
            o += 3;
        }
    }
}

static void encode_test(const char *out_dir, int quality)
{
    const g360_preview_frame *f = &probe.copied;
    void *lib = dlopen(TURBOJPEG_LIBRARY, CAPTURE_RTLD_NOW);
    void *sym;
    tj_init_fn init_fn;
    tj_compress2_fn compress_fn;
    tj_free_fn free_fn;
    tj_destroy_fn destroy_fn;
    void *handle;
    unsigned char *rgb;
    uint32_t step;
    char path[512];

    if (lib == 0) {
        printf("g360-preview: encode: %s not loaded\n", TURBOJPEG_LIBRARY);
        return;
    }
    sym = dlsym(lib, "tjInitCompress"); memcpy(&init_fn, &sym, sizeof sym);
    sym = dlsym(lib, "tjCompress2"); memcpy(&compress_fn, &sym, sizeof sym);
    sym = dlsym(lib, "tjFree"); memcpy(&free_fn, &sym, sizeof sym);
    sym = dlsym(lib, "tjDestroy"); memcpy(&destroy_fn, &sym, sizeof sym);
    if (init_fn == 0 || compress_fn == 0 || free_fn == 0 || destroy_fn == 0) {
        printf("g360-preview: encode: a turbojpeg symbol is missing\n");
        return;
    }
    handle = init_fn();
    if (handle == 0) {
        printf("g360-preview: encode: tjInitCompress failed\n");
        return;
    }
    rgb = malloc((size_t)f->width * f->height * 3u);
    if (rgb == 0) {
        (void)destroy_fn(handle);
        return;
    }
    for (step = 2; step >= 1; --step) {
        uint32_t w = f->width / step;
        uint32_t h = f->height / step;
        unsigned char *jpeg = 0;
        unsigned long size = 0;
        uint64_t t0 = now_ns();
        uint64_t t1;
        uint64_t t2;
        int rc;
        nv12_to_rgb(probe.copy, probe.copy + f->y_bytes, f->buffer_width, f->width, f->height,
                    step, rgb);
        t1 = now_ns();
        rc = compress_fn(handle, rgb, (int)w, (int)(w * 3u), (int)h, TJPF_RGB, &jpeg, &size,
                         TJSAMP_420, quality, 0);
        t2 = now_ns();
        printf("g360-preview: encode %lux%lu q%d: convert %lu ms, compress %lu ms, rc %d, %lu bytes\n",
               (unsigned long)w, (unsigned long)h, quality, (unsigned long)((t1 - t0) / 1000000u),
               (unsigned long)((t2 - t1) / 1000000u), rc, size);
        if (rc == 0 && jpeg != 0) {
            (void)snprintf(path, sizeof path, "%s/frame-%s.jpg", out_dir, step == 2 ? "half" : "full");
            if (write_file(path, jpeg, size) != 0) printf("g360-preview: could not write %s\n", path);
        }
        if (jpeg != 0) free_fn(jpeg);
    }
    free(rgb);
    (void)destroy_fn(handle);
}

static void print_block(uint32_t index)
{
    uint32_t k;
    printf("g360-preview: first block, topic index %lu:", (unsigned long)index);
    for (k = 0; k < G360_PREVIEW_BLOCK_BYTES; k += 4u) {
        uint32_t w;
        memcpy(&w, probe.first_block[index] + k, sizeof w);
        if (k % 32u == 0) printf("\ng360-preview:   +%02lx", (unsigned long)k);
        printf(" %08lx", (unsigned long)w);
    }
    printf("\n");
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end = 0;
    unsigned long v = strtoul(text, &end, 10);
    if (text[0] == '\0' || end == 0 || *end != '\0' || v > 0xffffffffUL) return -1;
    *value = (uint32_t)v;
    return 0;
}

int main(int argc, char **argv)
{
    const char *out_dir = 0;
    const char *log_path = 0;
    uint64_t free_bytes = 0;
    uint32_t width = 1280u;
    uint32_t height = 640u;
    uint32_t fps_x10 = 150u;
    uint32_t seconds = 10u;
    uint32_t quality = 75u;
    int with_set = 0;
    int still = 0;
    int check_refusal = 0;
    int ok = 1;
    int status;
    int i;
    uint32_t s;
    g360_open_options open_options;
    g360_preview_options options;
    char path[512];
    char text[1024];

    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : 0;
        if (strcmp(a, "--set") == 0) { with_set = 1; continue; }
        if (strcmp(a, "--still") == 0) { still = 1; continue; }
        if (strcmp(a, "--check-refusal") == 0) { check_refusal = 1; continue; }
        if (v == 0) goto usage;
        ++i;
        if (strcmp(a, "--out") == 0) out_dir = v;
        else if (strcmp(a, "--log") == 0) log_path = v;
        else if (strcmp(a, "--free-bytes") == 0) {
            char *end = 0;
            free_bytes = strtoull(v, &end, 10);
            if (end == 0 || *end != '\0' || free_bytes == 0) goto usage;
        } else if (strcmp(a, "--size") == 0) {
            const char *x = strchr(v, 'x');
            char w[16];
            if (x == 0 || (size_t)(x - v) >= sizeof w) goto usage;
            memcpy(w, v, (size_t)(x - v));
            w[x - v] = '\0';
            if (parse_u32(w, &width) != 0 || parse_u32(x + 1, &height) != 0) goto usage;
        } else if (strcmp(a, "--fps10") == 0) { if (parse_u32(v, &fps_x10) != 0) goto usage; }
        else if (strcmp(a, "--seconds") == 0) {
            if (parse_u32(v, &seconds) != 0 || seconds == 0 || seconds > 600u) goto usage;
        } else if (strcmp(a, "--quality") == 0) {
            if (parse_u32(v, &quality) != 0 || quality == 0 || quality > 100u) goto usage;
        } else goto usage;
    }
    if (out_dir == 0 || log_path == 0 || free_bytes == 0) goto usage;
    probe.copy = malloc(COPY_BYTES);
    if (probe.copy == 0) return 1;

    memset(&open_options, 0, sizeof open_options);
    open_options.log_path = log_path;
    open_options.free_bytes = free_bytes;
    status = g360_open(&open_options);
    printf("g360-preview: open: %s\n", g360_status_text(status));
    if (status != G360_OK) return 1;

    memset(&options, 0, sizeof options);
    options.width = width;
    options.height = height;
    options.fps_x10 = fps_x10;
    options.with_set = (uint32_t)with_set;
    options.callback = on_frame;
    g360_log("preview probe: starting");
    status = g360_preview_start(&options);
    printf("g360-preview: start %lux%lu at %lu/10 fps%s: %s (sensor rate bits 0x%08lx)\n",
           (unsigned long)width, (unsigned long)height, (unsigned long)fps_x10,
           with_set ? " with the set" : "", g360_status_text(status),
           (unsigned long)g360_preview_sensor_rate_bits());
    if (status != G360_OK) {
        g360_close();
        return 1;
    }
    for (s = 1; s <= seconds; ++s) {
        __sync_lock_test_and_set(&probe.want_copy, 1u);
        sleep_ms(1000u);
        (void)snprintf(text, sizeof text,
                       "preview probe: %lu s, frames %lu (planes %lu), topics set %lu thumb %lu "
                       "display %lu movie %lu, copies %lu",
                       (unsigned long)s, (unsigned long)probe.frames,
                       (unsigned long)probe.with_planes,
                       (unsigned long)g360_preview_count(G360_PREVIEW_TOPIC_SET),
                       (unsigned long)g360_preview_count(G360_PREVIEW_TOPIC_THUMB),
                       (unsigned long)g360_preview_count(G360_PREVIEW_TOPIC_DISPLAY),
                       (unsigned long)g360_preview_count(G360_PREVIEW_TOPIC_MOVIE),
                       (unsigned long)probe.copies);
        g360_log(text);
        printf("g360-preview: %s\n", text + 15);
        if (s == 1u && check_refusal) {
            /* Must be refused by the library before anything is sent. */
            g360_capture_options capture;
            g360_still result;
            memset(&capture, 0, sizeof capture);
            status = g360_capture(&capture, &result);
            printf("g360-preview: capture during the preview: %s\n", g360_status_text(status));
            if (status != G360_E_STATE) ok = 0;
            g360_still_free(&result);
        }
    }
    status = g360_preview_stop(3000u);
    printf("g360-preview: stop: %s\n", g360_status_text(status));
    if (status != G360_OK) ok = 0;
    __sync_lock_test_and_set(&probe.want_copy, 0u);
    sleep_ms(200u);

    printf("g360-preview: frames %lu, with readable planes %lu, sides single %lu left %lu right %lu\n",
           (unsigned long)probe.frames, (unsigned long)probe.with_planes,
           (unsigned long)probe.sides[1], (unsigned long)probe.sides[2],
           (unsigned long)probe.sides[3]);
    if (probe.frames > 1u && probe.last_ns > probe.first_ns) {
        printf("g360-preview: arrival rate %lu.%02lu frames/s; camera stamps %llu .. %llu\n",
               (unsigned long)((uint64_t)(probe.frames - 1u) * 1000000000u /
                               (probe.last_ns - probe.first_ns)),
               (unsigned long)(((uint64_t)(probe.frames - 1u) * 100000000000u /
                                (probe.last_ns - probe.first_ns)) % 100u),
               (unsigned long long)probe.first_stamp, (unsigned long long)probe.last_stamp);
    }
    for (s = 0; s < 4u; ++s) {
        if (probe.topic_seen[s]) print_block(s);
    }
    if (probe.frames == 0) ok = 0;
    if (probe.copies != 0) {
        const g360_preview_frame *f = &probe.copied;
        printf("g360-preview: copies %lu, mean %lu us, max %lu us (%lu + %lu bytes)\n",
               (unsigned long)probe.copies,
               (unsigned long)(probe.copy_ns_total / probe.copies / 1000u),
               (unsigned long)(probe.copy_ns_max / 1000u), (unsigned long)f->y_bytes,
               (unsigned long)f->c_bytes);
        (void)snprintf(path, sizeof path, "%s/frame.yuv", out_dir);
        if (write_file(path, probe.copy, f->y_bytes + f->c_bytes) != 0) {
            printf("g360-preview: could not write %s\n", path);
        }
        (void)snprintf(text, sizeof text,
                       "topic 0x%08lx\nbuffer %lux%lu\npicture %lux%lu\nyuv_kind %lu %lu\n"
                       "address_type %lu\ny 0x%08lx\nc 0x%08lx\nside %lu\ntimestamp %llu\n"
                       "y_bytes %lu\nc_bytes %lu\n",
                       (unsigned long)f->topic, (unsigned long)f->buffer_width,
                       (unsigned long)f->buffer_height, (unsigned long)f->width,
                       (unsigned long)f->height, (unsigned long)f->yuv_kind[0],
                       (unsigned long)f->yuv_kind[1], (unsigned long)f->address_type,
                       (unsigned long)f->y_address, (unsigned long)f->c_address,
                       (unsigned long)f->side, (unsigned long long)f->timestamp,
                       (unsigned long)f->y_bytes, (unsigned long)f->c_bytes);
        (void)snprintf(path, sizeof path, "%s/frame.txt", out_dir);
        (void)write_file(path, (const unsigned char *)text, strlen(text));
        printf("g360-preview: saved frame.yuv and frame.txt\n");
        if (f->width >= 2u && f->height >= 2u) encode_test(out_dir, (int)quality);
    } else {
        printf("g360-preview: no frame copied (no frame with readable planes)\n");
    }

    if (still) {
        g360_capture_options capture;
        g360_still result;
        memset(&capture, 0, sizeof capture);
        g360_log("preview probe: still after the preview");
        status = g360_capture(&capture, &result);
        printf("g360-preview: still: %s, notify mask 0x%lx\n", g360_status_text(status),
               (unsigned long)result.notify_mask);
        if (status != G360_OK) ok = 0;
        if (result.images[G360_IMAGE_MAIN].flags & G360_IMAGE_COPIED) {
            (void)snprintf(path, sizeof path, "%s/still-main.jpg", out_dir);
            status = g360_save_image(&result.images[G360_IMAGE_MAIN], path);
            printf("g360-preview: still main JPEG %lu bytes, flags 0x%lx, save %s\n",
                   (unsigned long)result.images[G360_IMAGE_MAIN].size,
                   (unsigned long)result.images[G360_IMAGE_MAIN].flags,
                   g360_status_text(status));
        } else {
            ok = 0;
        }
        g360_still_free(&result);
    }
    if (g360_service(3000u) < 0) ok = 0;
    g360_log(ok ? "preview probe: done, all steps passed" : "preview probe: done, a step failed");
    g360_close();
    printf("g360-preview: %s\n", ok ? "all steps passed" : "a step failed");
    return ok ? 0 : 1;

usage:
    fprintf(stderr, "usage: g360-preview --out DIR --log FILE --free-bytes N [--size WxH] "
                    "[--fps10 N] [--seconds N] [--set] [--still] [--quality N] "
                    "[--check-refusal]\n");
    return 2;
}
