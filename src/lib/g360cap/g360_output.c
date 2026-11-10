#include "g360_output.h"
#include "g360_dng.h"
#include "capture_libc.h"

typedef struct output_part {
    const unsigned char *data;
    size_t size;
} output_part;

static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void g360_exif_from_still(const g360_still *still, const g360_image *jpeg,
                           g360_exif_info *info)
{
    const unsigned char *p;
    uint32_t value;
    if (info == 0) return;
    memset(info, 0, sizeof *info);
    info->exposure_program = G360_EXIF_UNKNOWN;
    info->white_balance = G360_EXIF_UNKNOWN;
    if (jpeg != 0) { info->width = jpeg->width; info->height = jpeg->height; }
    if (still == 0 || still->capture_exif_size != G360_CAPTURE_EXIF_BYTES) return;
    p = still->capture_exif;
    if (get32(p) != 0 && get32(p + 4) != 0) {
        info->exposure_numerator = get32(p);
        info->exposure_denominator = get32(p + 4);
    }
    value = get32(p + 0x70);
    if (value <= 65535u) info->iso = value;
    value = get32(p + 0x6c);
    if (value <= 8u) info->exposure_program = value;
    value = get32(p + 0x84);
    if (value <= 1u) info->white_balance = value;
}

/* The file goes out in pieces of at most this size, each synced before the
   next. Written whole, a 33.8 MB DNG sat in the page cache waiting for the
   SD card, and on 6 November that ran the camera out of memory while the app
   held the shot: the kernel killed the app mid-save. */
#define SAVE_SYNC_BYTES ((size_t)2u << 20)

static int save_parts(const char *path, const output_part *parts, size_t count)
{
    int fd;
    size_t k, unsynced = 0;
    if (path == 0 || path[0] == '\0') return G360_E_ARGUMENT;
    fd = open(path, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC, 0644);
    if (fd < 0) return G360_E_IO;
    for (k = 0; k < count; ++k) {
        size_t done = 0;
        while (done < parts[k].size) {
            size_t want = parts[k].size - done;
            ptrdiff_t n;
            if (want > SAVE_SYNC_BYTES - unsynced) want = SAVE_SYNC_BYTES - unsynced;
            n = write(fd, parts[k].data + done, want);
            if (n <= 0 || (size_t)n > want) {
                (void)close(fd);
                return G360_E_IO;
            }
            done += (size_t)n;
            unsynced += (size_t)n;
            if (unsynced == SAVE_SYNC_BYTES) {
                if (fsync(fd) != 0) {
                    (void)close(fd);
                    return G360_E_IO;
                }
                unsynced = 0;
            }
        }
    }
    if (fsync(fd) != 0) {
        (void)close(fd);
        return G360_E_IO;
    }
    return close(fd) == 0 ? G360_OK : G360_E_IO;
}

int g360_save_dng(const g360_frame *frame, const g360_image *maker_note, const char *path)
{
    unsigned char header[G360_DNG_HEADER_BYTES];
    size_t size = 0;
    output_part parts[2];
    int status = g360_dng_header(frame, maker_note, header, sizeof header, &size);
    if (status != G360_OK) return status;
    parts[0].data = header;
    parts[0].size = size;
    parts[1].data = frame->image.data;
    parts[1].size = frame->image.size;
    return save_parts(path, parts, 2);
}

int g360_save_dng_full(const g360_frame *frame, const g360_image *maker_note,
                       const g360_exif_info *info, const char *description, const char *path)
{
    return g360_save_dng_full_xmp(frame, maker_note, info, description, 0, path);
}

int g360_save_dng_full_xmp(const g360_frame *frame, const g360_image *maker_note,
                           const g360_exif_info *info, const char *description, const char *xmp,
                           const char *path)
{
    unsigned char *header = malloc(G360_DNG_FULL_HEADER_BYTES);
    size_t size = 0;
    output_part parts[2];
    int status;
    if (header == 0) return G360_E_IO;
    status = g360_dng_header_full_xmp(frame, maker_note, info, description, xmp, header,
                                      G360_DNG_FULL_HEADER_BYTES, &size);
    if (status == G360_OK) {
        parts[0].data = header;
        parts[0].size = size;
        parts[1].data = frame->image.data;
        parts[1].size = frame->image.size;
        status = save_parts(path, parts, 2);
    }
    free(header);
    return status;
}

int g360_save_jpeg_exif(const g360_image *jpeg, const g360_image *maker_note,
                        const g360_exif_info *info, const char *path)
{
    return g360_save_jpeg_exif_xmp(jpeg, maker_note, info, 0, path);
}

int g360_save_jpeg_exif_xmp(const g360_image *jpeg, const g360_image *maker_note,
                            const g360_exif_info *info, const char *xmp, const char *path)
{
    static const char xmp_id[] = "http://ns.adobe.com/xap/1.0/"; /* with its NUL */
    unsigned char app1[G360_EXIF_APP1_MAX_SIZE];
    unsigned char xmp_head[4 + sizeof xmp_id];
    size_t size = 0, xmp_size = 0, body;
    output_part parts[5];
    size_t count = 0;
    int status;
    if (jpeg == 0 || jpeg->data == 0 || jpeg->size < 4 ||
        jpeg->data[0] != 0xff || jpeg->data[1] != 0xd8 ||
        jpeg->data[jpeg->size - 2] != 0xff || jpeg->data[jpeg->size - 1] != 0xd9)
        return G360_E_IMAGE;
    if (xmp != 0) {
        xmp_size = strlen(xmp);
        if (xmp_size == 0 || xmp_size > G360_JPEG_XMP_MAX) return G360_E_ARGUMENT;
    }
    status = g360_exif_app1(maker_note, info, app1, sizeof app1, &size);
    if (status != G360_OK) return status;
    parts[count].data = jpeg->data; parts[count++].size = 2;
    parts[count].data = app1; parts[count++].size = size;
    if (xmp_size) {
        body = 2 + sizeof xmp_id + xmp_size;
        xmp_head[0] = 0xff; xmp_head[1] = 0xe1;
        xmp_head[2] = (unsigned char)(body >> 8); xmp_head[3] = (unsigned char)body;
        memcpy(xmp_head + 4, xmp_id, sizeof xmp_id);
        parts[count].data = xmp_head; parts[count++].size = sizeof xmp_head;
        parts[count].data = (const unsigned char *)xmp; parts[count++].size = xmp_size;
    }
    parts[count].data = jpeg->data + 2; parts[count++].size = jpeg->size - 2;
    return save_parts(path, parts, count);
}
