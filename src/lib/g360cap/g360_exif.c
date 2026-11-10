/* Bounded Exif APP1 / Samsung Type 2 MakerNote. No allocation or JPEG IO. */
#include "g360_exif.h"
#include "capture_libc.h"

typedef struct entry {
    uint16_t tag, type;
    uint32_t count;
    const unsigned char *data;
    size_t bytes, offset;
} entry;

static void put16(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v;
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static void add(entry *entries, size_t *count, uint16_t tag, uint16_t type,
                 uint32_t n, const void *data, size_t bytes)
{
    entry *e = &entries[(*count)++];
    e->tag = tag; e->type = type; e->count = n;
    e->data = data; e->bytes = bytes; e->offset = 0;
}

static void sort(entry *entries, size_t count)
{
    size_t i;
    for (i = 1; i < count; ++i) {
        entry e = entries[i];
        size_t j = i;
        while (j > 0 && entries[j - 1].tag > e.tag) {
            entries[j] = entries[j - 1]; --j;
        }
        entries[j] = e;
    }
}

static size_t directory_size(size_t count) { return 2 + 12 * count + 4; }

/* Allocate offsets for external values. Every size is bounded by the fixed
 * tag/record set; no caller-supplied lengths participate in this arithmetic. */
static size_t layout(entry *entries, size_t count, size_t cursor)
{
    size_t i;
    for (i = 0; i < count; ++i) if (entries[i].bytes > 4) {
        entries[i].offset = cursor;
        cursor += entries[i].bytes + (entries[i].bytes & 1u);
    }
    return cursor;
}

/* base is a TIFF header or the MakerNote's own start. The preflight layout
 * reserved every range, and the entire output was zeroed (inline padding,
 * external-value alignment and next-IFD pointers). NULL data reserves a blob
 * for a nested IFD that is emitted separately. */
static void emit(unsigned char *base, size_t at, const entry *entries, size_t count)
{
    size_t i;
    put16(base + at, (uint32_t)count);
    for (i = 0; i < count; ++i) {
        const entry *e = &entries[i];
        unsigned char *p = base + at + 2 + 12 * i;
        put16(p, e->tag); put16(p + 2, e->type); put32(p + 4, e->count);
        if (e->bytes <= 4) memcpy(p + 8, e->data, e->bytes);
        else {
            put32(p + 8, (uint32_t)e->offset);
            if (e->data != 0) memcpy(base + e->offset, e->data, e->bytes);
        }
    }
}

static int date_valid(const char date[20])
{
    static const char pattern[] = "0000:00:00 00:00:00";
    size_t i;
    if (date[0] == '\0') return 1;
    if (date[19] != '\0') return 0;
    for (i = 0; i < 19; ++i) {
        if (pattern[i] == '0') { if (date[i] < '0' || date[i] > '9') return 0; }
        else if (date[i] != pattern[i]) return 0;
    }
    return 1;
}

int g360_exif_app1(const g360_image *note, const g360_exif_info *info,
                   unsigned char *output, size_t capacity, size_t *size)
{
    static const unsigned char orientation[] = {0, 1};
    static const unsigned char resolution[] = {0, 0, 1, 94, 0, 0, 0, 1}; /* 350 dpi */
    static const unsigned char two[] = {0, 2};
    static const unsigned char fnumber[] = {0, 0, 0, 22, 0, 0, 0, 10};
    static const unsigned char focal[] = {0, 0, 0, 12, 0, 0, 0, 10};
    static const unsigned char focal35[] = {0, 6};
    static const unsigned char components[] = {1, 2, 3, 0};
    entry ifd0[10], exif[16], mn[5];
    size_t n0 = 0, ne = 0, nm = 0, exif_at, mn_bytes, mn_at = 0, total, i;
    unsigned char pointer[4], exposure[8], iso[2], program[2], wb[2], width[4], height[4];
    unsigned char *tiff;
    if (size == 0) return G360_E_ARGUMENT;
    *size = 0;
    if (note == 0 || info == 0 || output == 0 || info->width == 0 || info->height == 0 ||
        info->iso > 65535u || info->exposure_program > 65535u || info->white_balance > 65535u ||
        ((info->exposure_numerator == 0) != (info->exposure_denominator == 0)) ||
        !date_valid(info->datetime)) return G360_E_ARGUMENT;
    if (note->data == 0 || note->size < 0x5314 || !(note->flags & G360_IMAGE_COPIED))
        return G360_E_IMAGE;
    if ((note->data[0x14] ^ 0xc8u) != 'A' || (note->data[0x15] ^ 0xc8u) != 'W' ||
        (note->data[0x16] ^ 0xc8u) != 'B' || (note->data[0x17] ^ 0xc8u) != 0 ||
        memcmp(note->data + 0x32dc, "\xff\xaa\xff\xaa", 4) != 0 ||
        memcmp(note->data + 0x4a4c, "I!?C", 4) != 0) return G360_E_IMAGE;

    add(mn,&nm,0x0001,7,4,"0100",4);
    add(mn,&nm,0x0060,7,6000,note->data+0x32dc,6000);
    add(mn,&nm,0x00a0,7,13000,note->data+0x14,13000);
    add(mn,&nm,0x00a1,7,13000,note->data+0x14,13000);
    add(mn,&nm,0x00c0,7,2048,note->data+0x4a4c,2048);
    mn_bytes = layout(mn,nm,directory_size(nm));

    add(ifd0,&n0,0x010f,2,8,"SAMSUNG",8);
    add(ifd0,&n0,0x0110,2,8,"SM-R210",8);
    add(ifd0,&n0,0x0112,3,1,orientation,2);
    add(ifd0,&n0,0x011a,5,1,resolution,8);
    add(ifd0,&n0,0x011b,5,1,resolution,8);
    add(ifd0,&n0,0x0128,3,1,two,2);
    add(ifd0,&n0,0x0131,2,8,"g360cap",8);
    if (info->datetime[0]) add(ifd0,&n0,0x0132,2,20,info->datetime,20);
    add(ifd0,&n0,0x0213,3,1,two,2);
    add(ifd0,&n0,0x8769,4,1,pointer,4);
    if (info->exposure_numerator) {
        put32(exposure,info->exposure_numerator); put32(exposure+4,info->exposure_denominator);
        add(exif,&ne,0x829a,5,1,exposure,8);
    }
    add(exif,&ne,0x829d,5,1,fnumber,8);
    if (info->exposure_program != G360_EXIF_UNKNOWN) {
        put16(program,info->exposure_program); add(exif,&ne,0x8822,3,1,program,2);
    }
    if (info->iso) { put16(iso,info->iso); add(exif,&ne,0x8827,3,1,iso,2); }
    add(exif,&ne,0x9000,7,4,"0230",4);
    if (info->datetime[0]) {
        add(exif,&ne,0x9003,2,20,info->datetime,20);
        add(exif,&ne,0x9004,2,20,info->datetime,20);
    }
    add(exif,&ne,0x9101,7,4,components,4);
    add(exif,&ne,0x920a,5,1,focal,8);
    add(exif,&ne,0x927c,7,(uint32_t)mn_bytes,0,mn_bytes);
    add(exif,&ne,0xa000,7,4,"0100",4);
    add(exif,&ne,0xa001,3,1,orientation,2); /* sRGB = 1 */
    put32(width,info->width); put32(height,info->height);
    add(exif,&ne,0xa002,4,1,width,4); add(exif,&ne,0xa003,4,1,height,4);
    if (info->white_balance != G360_EXIF_UNKNOWN) {
        put16(wb,info->white_balance); add(exif,&ne,0xa403,3,1,wb,2);
    }
    add(exif,&ne,0xa405,3,1,focal35,2);
    sort(ifd0,n0); sort(exif,ne);
    exif_at = 8 + directory_size(n0);
    put32(pointer,(uint32_t)exif_at);
    total = layout(ifd0,n0,exif_at+directory_size(ne));
    total = layout(exif,ne,total);
    if (total + 10 > capacity || total + 8 > G360_EXIF_APP1_MAX_SEGMENT)
        return G360_E_ARGUMENT;
    for (i = 0; i < ne; ++i) if (exif[i].tag == 0x927c) mn_at = exif[i].offset;
    total += 10; /* APP1 marker/length and Exif signature precede TIFF. */
    memset(output,0,total);
    output[0]=0xff; output[1]=0xe1; put16(output+2,(uint32_t)(total-2));
    memcpy(output+4,"Exif\0\0",6);
    tiff=output+10; memcpy(tiff,"MM\0*",4); put32(tiff+4,8);
    emit(tiff,8,ifd0,n0); emit(tiff,exif_at,exif,ne); emit(tiff+mn_at,0,mn,nm);
    *size=total;
    return G360_OK;
}

/* 1e7 x 2^(-r/16) x 2^16 for r = 0..15: tenths of a microsecond, 16 fraction bits. */
static const uint64_t tenth_us_fraction[16] = {
    UINT64_C(655360000000), UINT64_C(627574886039), UINT64_C(600967769755), UINT64_C(575488707911),
    UINT64_C(551089874701), UINT64_C(527725471973), UINT64_C(505351643270), UINT64_C(483926391501),
    UINT64_C(463409500118), UINT64_C(443762457620), UINT64_C(424948385268), UINT64_C(406931967860),
    UINT64_C(389679387444), UINT64_C(373158259837), UINT64_C(357337573840), UINT64_C(342187633026)
};

void g360_exif_exposure_from_tv(int32_t tv, uint32_t *numerator, uint32_t *denominator)
{
    const uint32_t cap = 1660000u, floor = 788u, short_by = 50u;
    uint32_t t, a, b;
    if (numerator == 0 || denominator == 0) return;
    if (tv < 0) t = cap;
    else {
        uint32_t whole = (uint32_t)tv / 16u, shift = whole + 16u;
        uint64_t v = shift >= 63u ? 0u : (tenth_us_fraction[(uint32_t)tv % 16u] + (UINT64_C(1) << (shift - 1u))) >> shift;
        t = v > cap ? cap : (uint32_t)v;
    }
    if (t < floor) t = floor;
    t -= short_by;
    a = t; b = 10000000u;
    while (b != 0u) { uint32_t r = a % b; a = b; b = r; }
    *numerator = t / a;
    *denominator = 10000000u / a;
}
