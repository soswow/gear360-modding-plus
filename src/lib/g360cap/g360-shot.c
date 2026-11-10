/*
 * g360-shot: take stills with g360cap and save their images.
 *
 *   g360-shot --out DIR --log PATH --free-bytes N [--count N] [--gap-ms N]
 *             [--hold-ms N] [--prefix NAME] [--set-timeout-ms N]
 *             [--ready-timeout-ms N] [--quality NAME] [--restore-quality NAME|none]
 *             [--drive NAME] [--restore-drive NAME|none] [--sets N]
 *             [--extra-set-wait-ms N] [--capture-type NAME]
 *             [--bracket-words W11,W12,W13,W14] [--exposure TV,ISO]
 *             [--restore-exposure auto|none] [--ltnr on|off] [--isonr off|low|mid|high]
 *             [--raw] [--raw-layout PHYS,WIDTH,HEIGHT,STRIDE,BITS (up to twice)]
 *             [--quickview NAME --restore-quickview NAME|none]
 *             [--frame] [--frame-layout PHYS,WIDTH,HEIGHT,STRIDE,BITS]
 *             [--jpeg save|discard|skip-copy] [--frame-output save|discard|dng]
 *
 * --jpeg discard copies/validates JPEGs but omits their files; skip-copy
 * also omits reading JPEG payloads (requires --frame). Both still return
 * buffers and wait for capture-ready. Neither disables RTOS encoding.
 * --frame-output discard validates then frees RAW without saving it.
 * --frame-output dng writes NAME-NNN.dng with per-shot colour metadata.
 * --exif on (default) inserts Exif and Samsung IQ records into saved JPEGs;
 * --exif off keeps the bare encoder bytes. Output is synchronous after capture.
 * metric,... rows report per-shot monotonic timing, status and frame flags.
 * --library-log off uses the C API's NULL log path: no synchronous g360cap
 * log file; completion metrics still go to stdout. Default: sync.
 *
 * --frame copies each still's full RAW frame (both lenses side by side,
 * 5824x2896, one 16-bit little-endian word per sample, stride 11648, from
 * physical 0xa6000000) while the camera waits for the gyro reply, and saves
 * it as NAME-NNN-frame.bin with a description NAME-NNN-frame.txt (layout,
 * how long the camera was held, checks). It opens the library with the
 * frame-pause start-up order. --frame-layout replaces the frame's layout.
 *
 * With the JPEGs, each still's maker-note buffer (the camera's AWB, AE and
 * colour records, up to 64 KiB) is saved as NAME-NNN-makernote.bin.
 *
 * --raw also copies each still's two per-lens RAW images (2896x2896, one
 * 16-bit little-endian word per sample, stride 5824, at 0xab134000 and
 * 0xad134000) and saves them as NAME-NNN-raw1.bin and -raw2.bin, each with
 * a text description (-raw1.txt, -raw2.txt). Each --raw-layout (with --raw)
 * replaces the defaults by one region; given once, one image is copied.
 * The per-lens images survive only while the camera does not restart live
 * view after the still, which --quickview holdzoom is meant to stop.
 *
 * --quickview sets the quick-view (review) time before the first still: off,
 * 1s, 3s, 5s, hold, holdzoom. It stays in the camera after exit, and its
 * previous value is not known here, so --restore-quickview is required with
 * it: the value to set back afterwards, or none.
 *
 * --exposure sets manual exposure before every still: shutter TV
 * (16 * log2(1/seconds): 96 = 1/64 s, 32 = 1/4 s, 0 = 1 s, -37 = 5 s) and
 * ISO (100, 200, ...). A list TV,ISO:TV,ISO:... takes one still per entry
 * (an exposure bracket made of separate stills; --count defaults to the
 * number of entries). Automatic exposure is set back afterwards unless
 * --restore-exposure none. --ltnr and --isonr set long-exposure and ISO noise
 * reduction; they are set back to on and mid (the camera's usual values).
 *
 * --bracket-words sets the request's words 11-14 (decimal or 0x hex), the
 * bracket parameters: for --capture-type wbb, word 13 0x1a0000..0x1a0005
 * (AB3, AB2, AB1, MG3, MG2, MG1) and word 14 1 (AB) or 2 (MG).
 *
 * --capture-type picks the still request's capture type: lock3a (default,
 * the stock request that locks 3A first), single, aeb, wbb, pwb, depthbrk.
 * The capture type also sets the camera's drive mode for that still.
 *
 * For every still it writes DIR/NAME-NNN-main.jpg, -screen.jpg, -thumb.jpg
 * and, for RAW qualities, -first.srw (only the images that were copied).
 * With --sets above 1 (brackets send one image set per frame) the names are
 * DIR/NAME-NNN-sK-main.jpg and so on, K counting the sets from 1.
 *
 * --quality sets the image quality before the first still (superfine, fine,
 * normal, raw, raw-superfine, raw-fine, raw-normal, sraw); --drive sets the
 * drive mode (single, contimid, contilow, burst, aebrk, wbbrk, pwbrk,
 * depthbrk). Both stay in the camera after exit, so each is set back
 * afterwards: to --restore-quality (default superfine) and --restore-drive
 * (default single), or not at all with "none".
 *
 * --sets N copies up to N image sets per still (default 1);
 * --extra-set-wait-ms keeps waiting that long after capture-ready for more
 * sets (default 0 for one set, 3000 otherwise).
 *
 * Stops at the first still that does not succeed; never retries a still.
 * After the last still it waits --hold-ms (default 5000) before exiting, so
 * the camera finishes its post-still work while the library is still up.
 *
 * Exit status: 0 all stills succeeded, 1 a still or the start-up failed,
 * 2 bad arguments (nothing loaded).
 */
#include "capture_libc.h"
#include "g360cap.h"
#include "g360cap_check.h"
#include "g360_output.h"
#include "g360_dng.h"

#include <stdint.h>

static int parse_u64(const char *text, uint64_t *value)
{
    uint64_t result = 0;
    size_t i;
    if (text == 0 || text[0] == '\0') return -1;
    for (i = 0; text[i] != '\0'; ++i) {
        uint32_t digit;
        if (text[i] < '0' || text[i] > '9') return -1;
        digit = (uint32_t)(text[i] - '0');
        if (result > (UINT64_MAX - digit) / 10u) return -1;
        result = result * 10u + digit;
    }
    *value = result;
    return 0;
}

/* Decimal or 0x-prefixed hexadecimal, at most 32 bits. */
static int parse_u32_any(const char *text, size_t length, uint32_t *value)
{
    uint64_t result = 0;
    size_t i = 0;
    int hex = 0;
    if (length >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        hex = 1;
        i = 2;
    }
    if (i >= length) return -1;
    for (; i < length; ++i) {
        char c = text[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
        else if (hex && c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a') + 10u;
        else if (hex && c >= 'A' && c <= 'F') digit = (uint32_t)(c - 'A') + 10u;
        else return -1;
        result = result * (hex ? 16u : 10u) + digit;
        if (result > 0xffffffffu) return -1;
    }
    *value = (uint32_t)result;
    return 0;
}

/* A signed decimal number, at most 32 bits. */
static int parse_i32(const char *text, size_t length, int32_t *value)
{
    uint32_t magnitude = 0;
    int negative = 0;
    if (length > 0 && text[0] == '-') {
        negative = 1;
        ++text;
        --length;
    }
    if (length == 0 || (length >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))) return -1;
    if (parse_u32_any(text, length, &magnitude) != 0 || magnitude > 0x7fffffffu) return -1;
    *value = negative ? -(int32_t)magnitude : (int32_t)magnitude;
    return 0;
}

/* Four comma-separated numbers. */
static int parse_words(const char *text, uint32_t words[4])
{
    int k;
    for (k = 0; k < 4; ++k) {
        const char *end = text;
        while (*end != '\0' && *end != ',') ++end;
        if (parse_u32_any(text, (size_t)(end - text), &words[k]) != 0) return -1;
        if (k < 3) {
            if (*end != ',') return -1;
            text = end + 1;
        } else if (*end != '\0') {
            return -1;
        }
    }
    return 0;
}

static int valid_name(const char *text)
{
    size_t i;
    if (text == 0 || text[0] == '\0' || strlen(text) > 32) return 0;
    for (i = 0; text[i] != '\0'; ++i) {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int sleep_ms(uint64_t ms)
{
    capture_timespec pause;
    capture_timespec remaining;
    while (ms > 0) {
        uint64_t step = ms > 500u ? 500u : ms;
        pause.tv_sec = 0;
        pause.tv_nsec = (long)(step * 1000000u);
        if (nanosleep(&pause, &remaining) != 0) return -1;
        ms -= step;
    }
    return 0;
}

static int now_us(uint64_t *value)
{
    capture_timespec t;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &t) != 0) return -1;
    *value = (uint64_t)t.tv_sec * UINT64_C(1000000) + (uint64_t)t.tv_nsec / 1000u;
    return 0;
}

typedef struct named_value {
    const char *name;
    uint32_t value;
} named_value;

static const named_value qualities[] = {
    {"superfine", G360_QUALITY_SUPERFINE}, {"fine", G360_QUALITY_FINE},
    {"normal", G360_QUALITY_NORMAL}, {"raw", G360_QUALITY_RAW},
    {"raw-superfine", G360_QUALITY_RAW_SUPERFINE}, {"raw-fine", G360_QUALITY_RAW_FINE},
    {"raw-normal", G360_QUALITY_RAW_NORMAL}, {"sraw", G360_QUALITY_SRAW},
    {0, 0},
};

static const named_value capture_types[] = {
    {"lock3a", G360_CAPTURE_LOCK3A}, {"single", G360_CAPTURE_SINGLE},
    {"aeb", G360_CAPTURE_AEB}, {"wbb", G360_CAPTURE_WBB}, {"pwb", G360_CAPTURE_PWB},
    {"depthbrk", G360_CAPTURE_DEPTH_BRK},
    {0, 0},
};

static const named_value isonr_values[] = {
    {"off", 0x160000u}, {"low", 0x160001u}, {"mid", 0x160002u}, {"high", 0x160003u},
    {0, 0},
};

static const named_value quickviews[] = {
    {"off", G360_QUICKVIEW_OFF}, {"1s", G360_QUICKVIEW_1SEC}, {"3s", G360_QUICKVIEW_3SEC},
    {"5s", G360_QUICKVIEW_5SEC}, {"hold", G360_QUICKVIEW_HOLD},
    {"holdzoom", G360_QUICKVIEW_HOLDZOOM},
    {0, 0},
};

static const named_value drives[] = {
    {"single", G360_DRIVE_SINGLE}, {"contimid", G360_DRIVE_CONTIMID},
    {"contilow", G360_DRIVE_CONTILOW}, {"burst", G360_DRIVE_BURST},
    {"aebrk", G360_DRIVE_AEBRK}, {"wbbrk", G360_DRIVE_WBBRK},
    {"pwbrk", G360_DRIVE_PWBRK}, {"depthbrk", G360_DRIVE_DEPTHBRK},
    {0, 0},
};

/* 1 and the value for a known name, 0 otherwise. */
static int lookup(const named_value *table, const char *name, uint32_t *value)
{
    size_t i;
    for (i = 0; table[i].name != 0; ++i) {
        if (strcmp(name, table[i].name) == 0) {
            *value = table[i].value;
            return 1;
        }
    }
    return 0;
}

/* A setting changed for the run and set back afterwards. */
typedef struct setting_change {
    int set;           /* given on the command line */
    int restore;       /* -1 default, 0 none, 1 explicit */
    uint32_t value;
    uint32_t restore_value;
} setting_change;

static int parse_setting(const named_value *table, const char *value, setting_change *change)
{
    if (!lookup(table, value, &change->value)) return -1;
    change->set = 1;
    return 0;
}

static int parse_restore(const named_value *table, const char *value, setting_change *change)
{
    if (strcmp(value, "none") == 0) {
        change->restore = 0;
        return 0;
    }
    if (!lookup(table, value, &change->restore_value)) return -1;
    change->restore = 1;
    return 0;
}

static const char *const suffixes[G360_IMAGE_COUNT] = {
    "main.jpg", "screen.jpg", "thumb.jpg", "first.srw"};

/* Writes text to path (created or truncated, synced). */
static int save_text(const char *path, const char *text)
{
    g360_image image;
    memset(&image, 0, sizeof image);
    image.data = (unsigned char *)(uintptr_t)text;
    image.size = strlen(text);
    return g360_save_image(&image, path);
}

/* Saves a set's RAW images and their descriptions; returns the first failure. */
static int save_raw(const g360_still *still, uint32_t raw_count, int custom, const char *out_dir,
                    const char *prefix, uint64_t n, uint32_t set_number, int numbered,
                    uint64_t *free_bytes)
{
    int status = G360_OK;
    uint32_t k;
    for (k = 0; k < raw_count; ++k) {
        const g360_image *raw = &still->raw[k];
        char base[480];
        char path[512];
        char text[640];
        char line[256];
        int saved;
        if (!(raw->flags & G360_IMAGE_COPIED)) {
            printf("g360-shot:   set %lu raw%lu not copied (flags 0x%lx)\n",
                   (unsigned long)set_number, (unsigned long)(k + 1), (unsigned long)raw->flags);
            if (status == G360_OK) status = G360_E_IMAGE;
            continue;
        }
        if (numbered) {
            (void)snprintf(base, sizeof base, "%s/%s-%03lu-s%lu-raw%lu", out_dir, prefix,
                           (unsigned long)n, (unsigned long)set_number, (unsigned long)(k + 1));
        } else {
            (void)snprintf(base, sizeof base, "%s/%s-%03lu-raw%lu", out_dir, prefix,
                           (unsigned long)n, (unsigned long)(k + 1));
        }
        (void)snprintf(path, sizeof path, "%s.bin", base);
        saved = g360_save_image(raw, path);
        (void)snprintf(line, sizeof line,
                       "saved %s: %lu bytes %lux%lu stride %lu bits %lu flags 0x%lx: %s", path,
                       (unsigned long)raw->size, (unsigned long)raw->width,
                       (unsigned long)raw->height, (unsigned long)raw->stride_x,
                       (unsigned long)raw->stride_y, (unsigned long)raw->flags,
                       g360_status_text(saved));
        printf("g360-shot:   %s\n", line);
        g360_log(line);
        if (saved == G360_OK && *free_bytes > raw->size) *free_bytes -= raw->size;
        (void)snprintf(text, sizeof text,
                       "format: 16-bit little-endian words, one sample per word\n"
                       "width: %lu\nheight: %lu\nstride_bytes: %lu\nbits: %lu\n"
                       "physical: 0x%08lx\n"
                       "layout: %s %lu\n"
                       "values_ok: %s\nchanged_during_copy: %s\n",
                       (unsigned long)raw->width, (unsigned long)raw->height,
                       (unsigned long)raw->stride_x, (unsigned long)raw->stride_y,
                       (unsigned long)still->raw_physical[k],
                       custom ? "region" : "lens", (unsigned long)(k + 1),
                       (raw->flags & G360_IMAGE_RAW_VALUES_OK) ? "yes" : "no",
                       (raw->flags & G360_IMAGE_RAW_CHANGED) ? "yes" : "no");
        (void)snprintf(path, sizeof path, "%s.txt", base);
        if (saved == G360_OK) saved = save_text(path, text);
        if (saved != G360_OK && status == G360_OK) status = saved;
    }
    return status;
}

/* Saves a still's full RAW frame and its description; returns the save status. */
static int save_frame(const g360_frame *frame, const char *out_dir, const char *prefix,
                      uint64_t n, uint64_t *free_bytes)
{
    const g360_image *image = &frame->image;
    char base[480];
    char path[512];
    char text[640];
    char line[256];
    int saved;
    if (!(image->flags & G360_IMAGE_COPIED)) {
        printf("g360-shot:   frame not copied (flags 0x%lx)\n", (unsigned long)image->flags);
        return G360_E_IMAGE;
    }
    (void)snprintf(base, sizeof base, "%s/%s-%03lu-frame", out_dir, prefix, (unsigned long)n);
    (void)snprintf(path, sizeof path, "%s.bin", base);
    saved = g360_save_image(image, path);
    (void)snprintf(line, sizeof line,
                   "saved %s: %lu bytes %lux%lu stride %lu bits %lu flags 0x%lx held %lu us: %s",
                   path, (unsigned long)image->size, (unsigned long)image->width,
                   (unsigned long)image->height, (unsigned long)image->stride_x,
                   (unsigned long)image->stride_y, (unsigned long)image->flags,
                   (unsigned long)frame->hold_us, g360_status_text(saved));
    printf("g360-shot:   %s\n", line);
    g360_log(line);
    if (saved == G360_OK && *free_bytes > image->size) *free_bytes -= image->size;
    (void)snprintf(text, sizeof text,
                   "format: 16-bit little-endian words, one sample per word\n"
                   "width: %lu\nheight: %lu\nstride_bytes: %lu\nbits: %lu\n"
                   "physical: 0x%08lx\n"
                   "layout: frame (both lenses side by side, each %lu wide)\n"
                   "copied: during the gyro hand-off, camera held %lu us\n"
                   "expansion_mode: %lu\n"
                   "values_ok: %s\nchanged_during_copy: %s\n",
                   (unsigned long)image->width, (unsigned long)image->height,
                   (unsigned long)image->stride_x, (unsigned long)image->stride_y,
                   (unsigned long)frame->physical, (unsigned long)(image->width / 2u),
                   (unsigned long)frame->hold_us, (unsigned long)frame->mode,
                   (image->flags & G360_IMAGE_RAW_VALUES_OK) ? "yes" : "no",
                   (image->flags & G360_IMAGE_RAW_CHANGED) ? "yes" : "no");
    (void)snprintf(path, sizeof path, "%s.txt", base);
    if (saved == G360_OK) saved = save_text(path, text);
    return saved;
}

/* Saves the copied images of one set; returns the first save failure. */
static int save_set(const g360_still *still, const char *out_dir, const char *prefix,
                    uint64_t n, uint32_t set_number, int numbered, uint64_t *free_bytes,
                    int save_jpeg, int exif)
{
    int status = G360_OK;
    int k;
    for (k = 0; k < G360_IMAGE_COUNT; ++k) {
        const g360_image *image = &still->images[k];
        char path[512];
        char line[256];
        int saved;
        if (!save_jpeg && k != G360_IMAGE_FIRST) continue;
        if (!(image->flags & G360_IMAGE_COPIED)) {
            if (image->flags != 0) {
                printf("g360-shot:   set %lu %s not copied (flags 0x%lx)\n",
                       (unsigned long)set_number, suffixes[k], (unsigned long)image->flags);
            }
            continue;
        }
        if (numbered) {
            (void)snprintf(path, sizeof path, "%s/%s-%03lu-s%lu-%s", out_dir, prefix,
                           (unsigned long)n, (unsigned long)set_number, suffixes[k]);
        } else {
            (void)snprintf(path, sizeof path, "%s/%s-%03lu-%s", out_dir, prefix,
                           (unsigned long)n, suffixes[k]);
        }
        if (exif && k != G360_IMAGE_FIRST) {
            g360_exif_info info;
            g360_exif_from_still(still, image, &info);
            saved = g360_save_jpeg_exif(image, &still->maker_note, &info, path);
        } else {
            saved = g360_save_image(image, path);
        }
        (void)snprintf(line, sizeof line, "saved %s: %lu bytes %lux%lu flags 0x%lx: %s", path,
                       (unsigned long)image->size, (unsigned long)image->width,
                       (unsigned long)image->height, (unsigned long)image->flags,
                       g360_status_text(saved));
        printf("g360-shot:   %s\n", line);
        g360_log(line);
        if (saved != G360_OK) {
            if (status == G360_OK) status = saved;
        } else {
            /* Account conservatively for APP1 too, without building it a
               second time just to obtain its length. */
            size_t written = image->size;
            if (exif && k != G360_IMAGE_FIRST) written += G360_EXIF_APP1_MAX_SIZE;
            *free_bytes = *free_bytes > written ? *free_bytes - written : 0;
        }
    }
    /* The camera's maker-note records, saved with the JPEGs (the stock app
       builds the JPEG's MakerNote from them; see reverse-engineering/dng). */
    if (save_jpeg && (still->maker_note.flags & G360_IMAGE_COPIED)) {
        const g360_image *note = &still->maker_note;
        char path[512];
        char line[256];
        int saved;
        if (numbered) {
            (void)snprintf(path, sizeof path, "%s/%s-%03lu-s%lu-makernote.bin", out_dir, prefix,
                           (unsigned long)n, (unsigned long)set_number);
        } else {
            (void)snprintf(path, sizeof path, "%s/%s-%03lu-makernote.bin", out_dir, prefix,
                           (unsigned long)n);
        }
        saved = g360_save_image(note, path);
        (void)snprintf(line, sizeof line, "saved %s: %lu bytes from 0x%lx: %s", path,
                       (unsigned long)note->size, (unsigned long)note->camera_address,
                       g360_status_text(saved));
        printf("g360-shot:   %s\n", line);
        g360_log(line);
        if (saved != G360_OK) {
            if (status == G360_OK) status = saved;
        } else if (*free_bytes > note->size) {
            *free_bytes -= note->size;
        }
    } else if (still->maker_note.flags != 0 && !(still->maker_note.flags & G360_IMAGE_COPIED)) {
        printf("g360-shot:   set %lu maker note not copied (flags 0x%lx)\n",
               (unsigned long)set_number, (unsigned long)still->maker_note.flags);
    }
    return status;
}

int main(int argc, char **argv)
{
    const char *out_dir = 0;
    const char *log_path = 0;
    const char *prefix = "shot";
    uint64_t free_bytes = 0;
    uint64_t count = 1;
    uint64_t gap_ms = 2000;
    uint64_t hold_ms = 5000;
    uint64_t set_timeout_ms = 0;
    uint64_t ready_timeout_ms = 0;
    uint64_t sets = 1;
    uint64_t extra_set_wait_ms = 0;
    int have_extra_wait = 0;
    int have_free = 0;
    uint32_t capture_type = 0;
    uint32_t bracket_words[4] = {0u, 0u, 0u, 0u};
    int raw = 0;
    uint32_t raw_layout[G360_MAX_RAW][5];
    uint32_t raw_layouts = 0;
    uint32_t raw_count;
    int frame = 0;
    int have_frame_layout = 0;
    const char *jpeg_policy = "save";
    const char *frame_output = "save";
    int exif = 1;
    const char *library_log = "sync";
    uint64_t previous_start = 0;
    uint32_t frame_layout[5] = {0u, 0u, 0u, 0u, 0u};
    int manual = 0;
    int restore_exposure = 1;
    int have_count = 0;
    int32_t tvs[16];
    uint32_t isos[16];
    uint32_t exposures = 0;
    int ltnr = -1;           /* -1 unchanged, 0 off, 1 on */
    uint32_t isonr = 0;
    int set_isonr = 0;
    setting_change quality = {0, -1, 0, G360_QUALITY_SUPERFINE};
    setting_change drive = {0, -1, 0, G360_DRIVE_SINGLE};
    setting_change quickview = {0, -1, 0, 0};
    g360_open_options open_options;
    g360_capture_options capture_options;
    int status;
    int exit_status = 0;
    int i;
    uint64_t n;

    for (i = 1; i < argc; i += 2) {
        const char *key = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : 0;
        uint64_t number = 0;
        if (strcmp(key, "--raw") == 0) {
            raw = 1;
            --i; /* a flag without a value */
            continue;
        }
        if (strcmp(key, "--frame") == 0) {
            frame = 1;
            --i;
            continue;
        }
        if (value == 0) goto usage;
        if (strcmp(key, "--out") == 0) {
            out_dir = value;
        } else if (strcmp(key, "--log") == 0) {
            log_path = value;
        } else if (strcmp(key, "--quality") == 0) {
            if (parse_setting(qualities, value, &quality) != 0) goto usage;
        } else if (strcmp(key, "--jpeg") == 0) {
            if (strcmp(value, "save") && strcmp(value, "discard") && strcmp(value, "skip-copy")) goto usage;
            jpeg_policy = value;
        } else if (strcmp(key, "--frame-output") == 0) {
            if (strcmp(value, "save") && strcmp(value, "discard") && strcmp(value, "dng")) goto usage;
            frame_output = value;
        } else if (strcmp(key, "--exif") == 0) {
            if (strcmp(value, "on") && strcmp(value, "off")) goto usage;
            exif = strcmp(value, "on") == 0;
        } else if (strcmp(key, "--library-log") == 0) {
            if (strcmp(value, "sync") && strcmp(value, "off")) goto usage;
            library_log = value;
        } else if (strcmp(key, "--restore-quality") == 0) {
            if (parse_restore(qualities, value, &quality) != 0) goto usage;
        } else if (strcmp(key, "--drive") == 0) {
            if (parse_setting(drives, value, &drive) != 0) goto usage;
        } else if (strcmp(key, "--restore-drive") == 0) {
            if (parse_restore(drives, value, &drive) != 0) goto usage;
        } else if (strcmp(key, "--quickview") == 0) {
            if (parse_setting(quickviews, value, &quickview) != 0) goto usage;
        } else if (strcmp(key, "--restore-quickview") == 0) {
            if (parse_restore(quickviews, value, &quickview) != 0) goto usage;
        } else if (strcmp(key, "--exposure") == 0) {
            const char *item = value;
            exposures = 0;
            for (;;) {
                const char *end = strchr(item, ':');
                const char *comma = strchr(item, ',');
                size_t length = end ? (size_t)(end - item) : strlen(item);
                if (exposures >= 16 || comma == 0 || comma >= item + length ||
                    parse_i32(item, (size_t)(comma - item), &tvs[exposures]) != 0 ||
                    tvs[exposures] < -160 || tvs[exposures] > 240 ||
                    parse_u32_any(comma + 1, (size_t)(item + length - comma - 1), &isos[exposures]) != 0 ||
                    isos[exposures] < 50 || isos[exposures] > 25600) {
                    goto usage;
                }
                ++exposures;
                if (end == 0) break;
                item = end + 1;
            }
            manual = 1;
        } else if (strcmp(key, "--restore-exposure") == 0) {
            if (strcmp(value, "auto") == 0) restore_exposure = 1;
            else if (strcmp(value, "none") == 0) restore_exposure = 0;
            else goto usage;
        } else if (strcmp(key, "--ltnr") == 0) {
            if (strcmp(value, "on") == 0) ltnr = 1;
            else if (strcmp(value, "off") == 0) ltnr = 0;
            else goto usage;
        } else if (strcmp(key, "--isonr") == 0) {
            if (!lookup(isonr_values, value, &isonr)) goto usage;
            set_isonr = 1;
        } else if (strcmp(key, "--raw-layout") == 0) {
            const char *item = value;
            uint32_t *layout;
            int k;
            if (raw_layouts >= G360_MAX_RAW) goto usage;
            layout = raw_layout[raw_layouts++];
            for (k = 0; k < 5; ++k) {
                const char *end = strchr(item, ',');
                size_t length = end ? (size_t)(end - item) : strlen(item);
                if (parse_u32_any(item, length, &layout[k]) != 0 || layout[k] == 0) goto usage;
                if (k < 4) {
                    if (end == 0) goto usage;
                    item = end + 1;
                } else if (end != 0) {
                    goto usage;
                }
            }
            {
                size_t bytes;
                if (!g360_raw_extent(layout[1], layout[2], layout[3], layout[4],
                                      G360_MAX_IMAGE_BYTES, &bytes) ||
                    bytes > UINT32_MAX - layout[0]) goto usage;
            }
        } else if (strcmp(key, "--frame-layout") == 0) {
            const char *item = value;
            int k;
            for (k = 0; k < 5; ++k) {
                const char *end = strchr(item, ',');
                size_t length = end ? (size_t)(end - item) : strlen(item);
                if (parse_u32_any(item, length, &frame_layout[k]) != 0 || frame_layout[k] == 0) goto usage;
                if (k < 4) {
                    if (end == 0) goto usage;
                    item = end + 1;
                } else if (end != 0) {
                    goto usage;
                }
            }
            {
                size_t bytes;
                if (!g360_raw_extent(frame_layout[1], frame_layout[2], frame_layout[3],
                                      frame_layout[4], G360_MAX_IMAGE_BYTES, &bytes) ||
                    bytes > UINT32_MAX - frame_layout[0]) goto usage;
            }
            have_frame_layout = 1;
        } else if (strcmp(key, "--bracket-words") == 0) {
            if (parse_words(value, bracket_words) != 0) goto usage;
        } else if (strcmp(key, "--capture-type") == 0) {
            if (!lookup(capture_types, value, &capture_type)) goto usage;
        } else if (strcmp(key, "--prefix") == 0) {
            if (!valid_name(value)) goto usage;
            prefix = value;
        } else {
            if (parse_u64(value, &number) != 0) goto usage;
            if (strcmp(key, "--free-bytes") == 0) {
                free_bytes = number;
                have_free = 1;
            } else if (strcmp(key, "--count") == 0) {
                if (number < 1 || number > 100) goto usage;
                count = number;
                have_count = 1;
            } else if (strcmp(key, "--gap-ms") == 0) {
                if (number > 600000u) goto usage;
                gap_ms = number;
            } else if (strcmp(key, "--hold-ms") == 0) {
                if (number > 600000u) goto usage;
                hold_ms = number;
            } else if (strcmp(key, "--set-timeout-ms") == 0) {
                if (number < 1 || number > 600000u) goto usage;
                set_timeout_ms = number;
            } else if (strcmp(key, "--ready-timeout-ms") == 0) {
                if (number < 1 || number > 600000u) goto usage;
                ready_timeout_ms = number;
            } else if (strcmp(key, "--sets") == 0) {
                if (number < 1 || number > G360_MAX_SETS) goto usage;
                sets = number;
            } else if (strcmp(key, "--extra-set-wait-ms") == 0) {
                if (number > 60000u) goto usage;
                extra_set_wait_ms = number;
                have_extra_wait = 1;
            } else {
                goto usage;
            }
        }
    }
    if (out_dir == 0 || out_dir[0] == '\0' || log_path == 0 || log_path[0] == '\0' ||
        !have_free || free_bytes == 0 || (quality.restore == 1 && !quality.set) ||
        (drive.restore == 1 && !drive.set) || (raw_layouts != 0 && !raw) ||
        (have_frame_layout && !frame) ||
        (strcmp(jpeg_policy, "skip-copy") == 0 && !frame) ||
        (strcmp(frame_output, "save") != 0 && !frame) ||
        quickview.set != (quickview.restore != -1)) {
        goto usage;
    }
    /* The DNG profile describes this specific whole-frame raster. Reject a
       custom layout it cannot represent before loading the camera SDK. */
    if (strcmp(frame_output, "dng") == 0 &&
        (sets != 1 || (have_frame_layout &&
         (frame_layout[1] != 5824 || frame_layout[2] != 2896 ||
          frame_layout[3] != 11648 || frame_layout[4] != 16)))) goto usage;
    raw_count = raw ? (raw_layouts != 0 ? raw_layouts : G360_MAX_RAW) : 0u;
    if (quality.restore == -1) quality.restore = quality.set;
    if (manual && !have_count) count = exposures;
    /* A bracket capture type sets the camera's drive mode itself (step 5d
       left DRIVE_WBBRK behind), so set DRIVE_SINGLE back afterwards unless
       the caller said --restore-drive none. */
    if (!drive.set && drive.restore != 0 && capture_type != 0 &&
        capture_type != G360_CAPTURE_LOCK3A && capture_type != G360_CAPTURE_SINGLE) {
        drive.set = -1;      /* restore only; nothing set before the stills */
        drive.restore = 1;
    }
    if (drive.restore == -1) drive.restore = drive.set ? 1 : 0;
    if (!have_extra_wait) extra_set_wait_ms = sets > 1 ? 3000u : 0u;

    memset(&open_options, 0, sizeof open_options);
    open_options.log_path = strcmp(library_log, "off") == 0 ? 0 : log_path;
    open_options.free_bytes = free_bytes;
    open_options.frame_pause = (uint32_t)frame;
    status = g360_open(&open_options);
    printf("g360-shot: open: %s\n", g360_status_text(status));
    if (status != G360_OK) return 1;

    if (quality.set) {
        status = g360_set_image_quality(quality.value);
        printf("g360-shot: image quality 0x%lx: %s\n", (unsigned long)quality.value,
               g360_status_text(status));
        if (status != G360_OK) {
            g360_log("image quality not set; no still sent");
            g360_close();
            return 1;
        }
    }
    if (drive.set == 1) {
        status = g360_set_drive_mode(drive.value);
        printf("g360-shot: drive mode 0x%lx: %s\n", (unsigned long)drive.value,
               g360_status_text(status));
        if (status != G360_OK) {
            g360_log("drive mode not set; no still sent");
            exit_status = 1;
            count = 0;
        }
    }

    if (quickview.set && count > 0) {
        status = g360_set_quickview(quickview.value);
        printf("g360-shot: quick view 0x%lx: %s\n", (unsigned long)quickview.value,
               g360_status_text(status));
        if (status != G360_OK) {
            exit_status = 1;
            count = 0;
        }
    }
    if (ltnr >= 0 && count > 0) {
        status = g360_set_user_attribute(G360_ATTR_LTNR, (uint32_t)ltnr);
        printf("g360-shot: long-exposure noise reduction %s: %s\n", ltnr ? "on" : "off",
               g360_status_text(status));
        if (status != G360_OK) {
            exit_status = 1;
            count = 0;
        }
    }
    if (set_isonr && count > 0) {
        status = g360_set_user_attribute(G360_ATTR_ISONR, isonr);
        printf("g360-shot: ISO noise reduction 0x%lx: %s\n", (unsigned long)isonr,
               g360_status_text(status));
        if (status != G360_OK) {
            exit_status = 1;
            count = 0;
        }
    }

    memset(&capture_options, 0, sizeof capture_options);
    capture_options.set_timeout_ms = (uint32_t)set_timeout_ms;
    capture_options.ready_timeout_ms = (uint32_t)ready_timeout_ms;
    capture_options.max_sets = (uint32_t)sets;
    capture_options.extra_set_wait_ms = (uint32_t)extra_set_wait_ms;
    capture_options.capture_type = capture_type;
    capture_options.raw = raw_count;
    for (i = 0; i < (int)raw_layouts; ++i) {
        capture_options.raw_layout[i].physical = raw_layout[i][0];
        capture_options.raw_layout[i].width = raw_layout[i][1];
        capture_options.raw_layout[i].height = raw_layout[i][2];
        capture_options.raw_layout[i].stride = raw_layout[i][3];
        capture_options.raw_layout[i].bits = raw_layout[i][4];
    }
    memcpy(capture_options.bracket_words, bracket_words, sizeof bracket_words);
    capture_options.frame = (uint32_t)frame;
    capture_options.skip_jpeg_copy = (uint32_t)(strcmp(jpeg_policy, "skip-copy") == 0);
    capture_options.frame_layout.physical = frame_layout[0];
    capture_options.frame_layout.width = frame_layout[1];
    capture_options.frame_layout.height = frame_layout[2];
    capture_options.frame_layout.stride = frame_layout[3];
    capture_options.frame_layout.bits = frame_layout[4];
    for (n = 1; n <= count; ++n) {
        g360_shot shot;
        char line[640];
        uint64_t begin, capture_begin, captured, saved_at;
        uint32_t k;
        if (now_us(&begin) != 0) { exit_status = 1; break; }
        if (manual) {
            /* Reasserted before every still, as the September scripts did. */
            uint32_t e = (uint32_t)((n - 1u) % exposures);
            status = g360_set_manual_exposure(tvs[e], isos[e]);
            printf("g360-shot: manual exposure TV %ld ISO %lu: %s\n", (long)tvs[e],
                   (unsigned long)isos[e], g360_status_text(status));
            if (status != G360_OK) {
                exit_status = 1;
                break;
            }
        }
        if (now_us(&capture_begin) != 0) { exit_status = 1; break; }
        status = g360_capture_shot(&capture_options, &shot);
        if (now_us(&captured) != 0) {
            g360_shot_free(&shot);
            exit_status = 1;
            break;
        }
        (void)snprintf(line, sizeof line,
                       "still %lu: %s; %lu set(s) copied, %lu not copied; ready after %lu ms; notify mask 0x%lx",
                       (unsigned long)n, g360_status_text(status), (unsigned long)shot.set_count,
                       (unsigned long)shot.sets_not_copied,
                       (unsigned long)(shot.ready_after_ns / 1000000u),
                       (unsigned long)shot.notify_mask);
        printf("g360-shot: %s\n", line);
        g360_log(line);
        for (k = 0; k < shot.set_count; ++k) {
            const g360_still *still = &shot.sets[k];
            int saved;
            (void)snprintf(line, sizeof line, "  set %lu: type %lu, after %lu ms",
                           (unsigned long)(k + 1), (unsigned long)still->set_type,
                           (unsigned long)(still->set_after_ns / 1000000u));
            printf("g360-shot: %s\n", line);
            g360_log(line);
            saved = save_set(still, out_dir, prefix, n, k + 1, sets > 1, &free_bytes,
                             strcmp(jpeg_policy, "save") == 0, exif);
            if (saved != G360_OK && status == G360_OK) status = saved;
            if (raw_count != 0 && k == 0) {
                saved = save_raw(still, raw_count, raw_layouts != 0, out_dir, prefix, n, k + 1,
                                 sets > 1, &free_bytes);
                if (saved != G360_OK && status == G360_OK) status = saved;
            }
        }
        if (frame && strcmp(frame_output, "save") == 0) {
            int saved = save_frame(&shot.frame, out_dir, prefix, n, &free_bytes);
            if (saved != G360_OK && status == G360_OK) status = saved;
        }
        if (frame && strcmp(frame_output, "dng") == 0) {
            char path[512];
            int saved = G360_E_IMAGE;
            (void)snprintf(path, sizeof path, "%s/%s-%03lu.dng", out_dir, prefix, (unsigned long)n);
            if (shot.set_count == 1) {
                /* With the still's own exposure, ISO, program and WB (no date). */
                g360_exif_info info;
                g360_exif_from_still(&shot.sets[0], &shot.sets[0].images[G360_IMAGE_MAIN], &info);
                saved = g360_save_dng_full(&shot.frame, &shot.sets[0].maker_note, &info, 0, path);
            }
            printf("g360-shot: saved %s: %s\n", path, g360_status_text(saved));
            if (saved == G360_OK) {
                size_t written = shot.frame.image.size + G360_DNG_FULL_HEADER_BYTES;
                free_bytes = free_bytes > written ? free_bytes - written : 0;
            }
            if (saved != G360_OK && status == G360_OK) status = saved;
        }
        if (now_us(&saved_at) != 0) {
            status = G360_E_CLOCK;
        } else {
            /* capture_us includes frame collection/validation; save_us also
             * includes the ordinary per-shot logging above. cycle_us counts
             * the preceding inter-shot gap, frees and metric log sync. */
            (void)snprintf(line, sizeof line,
                "metric,sequence=%lu,status=%d,capture_us=%llu,save_us=%llu,total_us=%llu,"
                "cycle_us=%llu,set_us=%llu,ready_us=%llu,frame_hold_us=%lu,"
                "frame_bytes=%lu,frame_flags=%lu,jpeg_policy=%s,frame_output=%s,library_log=%s,exif=%s",
                (unsigned long)n, status,
                (unsigned long long)(captured - capture_begin),
                (unsigned long long)(saved_at - captured),
                (unsigned long long)(saved_at - begin),
                (unsigned long long)(previous_start ? begin - previous_start : 0),
                (unsigned long long)(shot.set_count ? shot.sets[0].set_after_ns / 1000u : 0),
                (unsigned long long)(shot.ready_after_ns / 1000u),
                (unsigned long)shot.frame.hold_us, (unsigned long)shot.frame.image.size,
                (unsigned long)shot.frame.image.flags, jpeg_policy, frame_output, library_log, exif ? "on" : "off");
            printf("g360-shot: %s\n", line);
            g360_log(line);
            previous_start = begin;
        }
        g360_set_free_bytes(free_bytes);
        g360_shot_free(&shot);
        if (status != G360_OK) {
            exit_status = 1;
            break;
        }
        if (n < count && sleep_ms(gap_ms) != 0) {
            exit_status = 1;
            break;
        }
    }
    if (quickview.set && quickview.restore == 1) {
        status = g360_set_quickview(quickview.restore_value);
        printf("g360-shot: quick view restored to 0x%lx: %s\n",
               (unsigned long)quickview.restore_value, g360_status_text(status));
        if (status != G360_OK) exit_status = 1;
    }
    if (manual && restore_exposure) {
        status = g360_set_auto_exposure();
        printf("g360-shot: automatic exposure restored: %s\n", g360_status_text(status));
        if (status != G360_OK) exit_status = 1;
    }
    if (ltnr == 0) {
        status = g360_set_user_attribute(G360_ATTR_LTNR, 1u);
        printf("g360-shot: long-exposure noise reduction restored to on: %s\n",
               g360_status_text(status));
        if (status != G360_OK) exit_status = 1;
    }
    if (set_isonr && isonr != 0x160002u) {
        status = g360_set_user_attribute(G360_ATTR_ISONR, 0x160002u);
        printf("g360-shot: ISO noise reduction restored to mid: %s\n", g360_status_text(status));
        if (status != G360_OK) exit_status = 1;
    }
    if (drive.set && drive.restore) {
        status = g360_set_drive_mode(drive.restore_value);
        printf("g360-shot: drive mode restored to 0x%lx: %s\n",
               (unsigned long)drive.restore_value, g360_status_text(status));
        if (status != G360_OK) exit_status = 1;
    }
    if (quality.set && quality.restore) {
        status = g360_set_image_quality(quality.restore_value);
        printf("g360-shot: image quality restored to 0x%lx: %s\n",
               (unsigned long)quality.restore_value, g360_status_text(status));
        if (status != G360_OK) exit_status = 1;
    }
    g360_log("holding before exit");
    if (g360_service((uint32_t)hold_ms) < 0) exit_status = 1;
    g360_log(exit_status == 0 ? "exit: all stills succeeded" : "exit: a still failed");
    g360_close();
    return exit_status;

usage:
    fprintf(stderr,
            "usage: g360-shot --out DIR --log PATH --free-bytes N [--count 1-100] [--gap-ms N]\n"
            "                 [--hold-ms N] [--prefix NAME] [--set-timeout-ms N] [--ready-timeout-ms N]\n"
            "                 [--quality NAME] [--restore-quality NAME|none]\n"
            "                 [--drive NAME] [--restore-drive NAME|none]\n"
            "                 [--sets 1-8] [--extra-set-wait-ms N] [--capture-type NAME]\n"
            "                 [--bracket-words W11,W12,W13,W14] [--exposure TV,ISO[:TV,ISO...]]\n"
            "                 [--restore-exposure auto|none] [--ltnr on|off] [--isonr off|low|mid|high]\n"
            "                 [--raw] [--raw-layout PHYS,WIDTH,HEIGHT,STRIDE,BITS (up to twice)]\n"
            "                 [--quickview NAME --restore-quickview NAME|none]\n"
            "                 [--frame] [--frame-layout PHYS,WIDTH,HEIGHT,STRIDE,BITS]\n"
            "                 [--jpeg save|discard|skip-copy] [--frame-output save|discard|dng]\n"
            "                 [--exif on|off] [--library-log sync|off] (off keeps metrics on stdout)\n"
            "  quick view names: off 1s 3s 5s hold holdzoom\n"
            "  quality names: superfine fine normal raw raw-superfine raw-fine raw-normal sraw\n"
            "  drive names: single contimid contilow burst aebrk wbbrk pwbrk depthbrk\n"
            "  capture types: lock3a (default) single aeb wbb pwb depthbrk\n");
    return 2;
}
