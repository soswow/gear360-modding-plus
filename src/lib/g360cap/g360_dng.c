/* DNG 1.4, uncompressed whole-frame CFA; see dng/make_dng.py. */
#include "g360_dng.h"
#include "capture_libc.h"

/* Python performs separate multiply/add operations. Preserve those roundings
 * on hosts with fused multiply-add as well as on the camera's Cortex-A9. */
#pragma STDC FP_CONTRACT OFF

#define DNG_WIDTH 5824u
#define DNG_HEIGHT 2896u
#define DNG_STRIDE 11648u
#define DNG_FRAME_BYTES ((size_t)DNG_STRIDE * DNG_HEIGHT)
#define RATIONAL_DENOMINATOR UINT64_C(1000000)

static void put16(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* AWB bytes are already XOR-masked when the RTOS supplies the IQ block. */
static double awb_i32(const unsigned char *p)
{
    unsigned char b[4];
    uint32_t v;
    unsigned k;
    for (k = 0; k < 4; ++k) b[k] = p[k] ^ 0xc8u;
    v = get32(b);
    return v <= INT32_MAX ? (double)v : (double)v - 4294967296.0;
}

/* Minimal unsigned 128-bit arithmetic, without target __int128 support.
 * Exact Fraction(double) rounding needs at most 94 bits below. */
typedef struct wide_uint { uint64_t hi, lo; } wide_uint;

static wide_uint wide_mul(uint64_t a, uint64_t b)
{
    uint64_t a0 = (uint32_t)a, a1 = a >> 32;
    uint64_t b0 = (uint32_t)b, b1 = b >> 32;
    uint64_t low = a0 * b0;
    uint64_t middle = a1 * b0 + (low >> 32);
    uint64_t carry = middle >> 32;
    wide_uint out;
    middle = a0 * b1 + (uint32_t)middle;
    out.hi = a1 * b1 + carry + (middle >> 32);
    out.lo = (middle << 32) | (uint32_t)low;
    return out;
}

static wide_uint wide_shift(uint64_t n, unsigned shift)
{
    wide_uint out;
    if (shift == 0) { out.hi = 0; out.lo = n; }
    else if (shift < 64) { out.hi = n >> (64 - shift); out.lo = n << shift; }
    else { out.hi = n << (shift - 64); out.lo = 0; }
    return out;
}

static int wide_compare(wide_uint a, wide_uint b)
{
    if (a.hi != b.hi) return a.hi < b.hi ? -1 : 1;
    return a.lo < b.lo ? -1 : a.lo > b.lo ? 1 : 0;
}

static wide_uint wide_difference(wide_uint a, wide_uint b)
{
    wide_uint out;
    if (wide_compare(a, b) < 0) { out = a; a = b; b = out; }
    out.lo = a.lo - b.lo;
    out.hi = a.hi - b.hi - (a.lo < b.lo);
    return out;
}

static wide_uint wide_scale(wide_uint a, uint64_t b)
{
    wide_uint out = wide_mul(a.lo, b);
    out.hi += a.hi * b;
    return out;
}

/* Divide 2^shift (shift <= 73) by a 53-bit mantissa. After the first
 * zero coefficient this is the only division needing more than 64 bits. */
static uint64_t divide_power(unsigned shift, uint64_t d, uint64_t *remainder)
{
    uint64_t q = 0, r = 1;
    unsigned k;
    for (k = 0; k < shift; ++k) {
        r <<= 1;
        q <<= 1;
        if (r >= d) { r -= d; ++q; }
    }
    *remainder = r;
    return q;
}

/* Fraction(x).limit_denominator(1000000), using x's exact IEEE binary
 * value. Continued-fraction convergents stay in 64 bits; the last distance
 * comparison is exact too, so halfway cases match Python's choice. */
static int rational(double x, int is_signed, unsigned char out[8])
{
    union { double value; uint64_t bits; } bits;
    uint64_t n, d, original_n, p0 = 0, q0 = 1, p1 = 1, q1 = 0;
    uint64_t numerator_limit, numerator, denominator;
    unsigned exponent, shift;
    int negative;
    bits.value = x;
    negative = (int)(bits.bits >> 63);
    exponent = (unsigned)((bits.bits >> 52) & 0x7ffu);
    if (exponent == 0x7ffu) return -1;
    if (negative) x = -x;
    numerator_limit = is_signed ? (negative ? UINT64_C(2147483648) : INT32_MAX) : UINT32_MAX;
    /* A double just above an integer limit can still round to that integer
     * at the denominator bound. Check the final numerator, below. */
    if (x >= (double)numerator_limit + 1.0) return -1;
    if (x == 0.0 || exponent == 0 || exponent < 1002u) {
        /* Every value below 2^-21 is closer to 0 than to 1/1000000. */
        put32(out, 0); put32(out + 4, 1); return 0;
    }
    n = (bits.bits & UINT64_C(0x000fffffffffffff)) | UINT64_C(0x0010000000000000);
    shift = 1075u - exponent; /* x <= UINT32_MAX, hence shift is 21..73. */
    while (shift > 0 && (n & 1u) == 0) { n >>= 1; --shift; }
    original_n = n;
    if (shift >= 64) {
        /* Initial coefficient is zero, then divide the wide denominator. */
        uint64_t remainder;
        uint64_t a = divide_power(shift, n, &remainder);
        p0 = 1; q0 = 0; p1 = 0; q1 = 1;
        if (a <= RATIONAL_DENOMINATOR) {
            p0 = 0; q0 = 1; p1 = 1; q1 = a;
            d = remainder;
            /* Next division is n/remainder, with ordinary 64-bit inputs. */
        } else {
            d = 0; /* break into the bounded final comparison below */
        }
    } else {
        d = UINT64_C(1) << shift;
    }
    while (d != 0) {
        uint64_t a = n / d, remainder = n % d;
        uint64_t p2, q2;
        if (q1 != 0 && a > (RATIONAL_DENOMINATOR - q0) / q1) break;
        p2 = p0 + a * p1;
        q2 = q0 + a * q1;
        p0 = p1; q0 = q1; p1 = p2; q1 = q2;
        n = d; d = remainder;
    }
    if (d == 0 && q1 != 0 && shift < 64) {
        numerator = p1; denominator = q1;
    } else if (d == 0 && q1 != 0 && p1 != 0) {
        /* The special wide division may also finish at an exact fraction. */
        numerator = p1; denominator = q1;
    } else {
        uint64_t k = (RATIONAL_DENOMINATOR - q0) / q1;
        uint64_t pb = p0 + k * p1, qb = q0 + k * q1;
        wide_uint error_b = wide_difference(wide_shift(pb, shift), wide_mul(original_n, qb));
        wide_uint error_1 = wide_difference(wide_shift(p1, shift), wide_mul(original_n, q1));
        if (wide_compare(wide_scale(error_1, qb), wide_scale(error_b, q1)) <= 0) {
            numerator = p1; denominator = q1;
        } else { numerator = pb; denominator = qb; }
    }
    if (numerator > numerator_limit || denominator == 0 ||
        (!is_signed && negative && numerator != 0)) return -1;
    put32(out, negative ? (uint32_t)(UINT64_C(0) - numerator) : (uint32_t)numerator);
    put32(out + 4, (uint32_t)denominator);
    return 0;
}

static void multiply(const double a[3][3], const double b[3][3], double out[3][3])
{
    unsigned r, c, k;
    for (r = 0; r < 3; ++r) for (c = 0; c < 3; ++c) {
        double v = 0.0;
        for (k = 0; k < 3; ++k) v += a[r][k] * b[k][c];
        out[r][c] = v;
    }
}

static int inverse(const double m[3][3], double out[3][3])
{
    double a=m[0][0], b=m[0][1], c=m[0][2], d=m[1][0], e=m[1][1];
    double f=m[1][2], g=m[2][0], h=m[2][1], i=m[2][2];
    double det = a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if (det == 0.0) return -1;
    out[0][0]=(e*i-f*h)/det; out[0][1]=(c*h-b*i)/det; out[0][2]=(b*f-c*e)/det;
    out[1][0]=(f*g-d*i)/det; out[1][1]=(a*i-c*g)/det; out[1][2]=(c*d-a*f)/det;
    out[2][0]=(d*h-e*g)/det; out[2][1]=(b*g-a*h)/det; out[2][2]=(a*e-b*d)/det;
    return 0;
}

static int colour_values(const unsigned char *awb, unsigned char color[72],
                         unsigned char forward[72], unsigned char neutral[24])
{
    static const double srgb[3][3] = {
        {0.4124564,0.3575761,0.1804375}, {0.2126729,0.7151522,0.0721750},
        {0.0193339,0.1191920,0.9503041}};
    static const double bradford[3][3] = {
        {1.0478112,0.0228866,-0.0501270}, {0.0295424,0.9904844,-0.0170491},
        {-0.0092345,0.0150436,0.7521316}};
    double ccm[3][3], inv_ccm[3][3], inv_srgb[3][3], diag[3][3] = {{0}};
    double temp[3][3], result[3][3];
    unsigned r, c;
    if ((awb[0]^0xc8u)!='A' || (awb[1]^0xc8u)!='W' ||
        (awb[2]^0xc8u)!='B' || (awb[3]^0xc8u)!=0 ||
        (awb[4]^0xc8u)!=5 || (awb[5]^0xc8u)!=1 ||
        (awb[0x2ec4]^0xc8u)!=1 || (awb[0x2ec5]^0xc8u)!=1) return -1;
    for (r = 0; r < 3; ++r) {
        double gain = awb_i32(awb + 0x2eb0 + r*4);
        double ga = awb_i32(awb + 0x2ec8 + 4), gb = awb_i32(awb + 0x2ed4 + 4);
        double wa = awb_i32(awb + 0x2ec8 + r*4), wb = awb_i32(awb + 0x2ed4 + r*4);
        if (gain <= 0.0 || ga <= 0.0 || gb <= 0.0 || wa <= 0.0 || wb <= 0.0) return -1;
        if (rational(1.0/(gain/65536.0), 0, neutral + r*8) != 0) return -1;
        diag[r][r] = (wa/ga + wb/gb)/2.0;
        for (c = 0; c < 3; ++c) ccm[r][c] = awb_i32(awb + 0x2e14 + (r*3+c)*4)/128.0;
    }
    if (inverse(ccm, inv_ccm) != 0 || inverse(srgb, inv_srgb) != 0) return -1;
    multiply(bradford, srgb, temp); multiply(temp, ccm, result);
    for (r = 0; r < 3; ++r) for (c = 0; c < 3; ++c)
        if (rational(result[r][c], 1, forward + (r*3+c)*8) != 0) return -1;
    multiply(diag, inv_ccm, temp); multiply(temp, inv_srgb, result);
    for (r = 0; r < 3; ++r) for (c = 0; c < 3; ++c)
        if (rational(result[r][c], 1, color + (r*3+c)*8) != 0) return -1;
    return 0;
}

typedef struct ifd_writer { unsigned char *header; size_t entry, blob; } ifd_writer;

static void tag(ifd_writer *w, uint32_t id, uint32_t type, uint32_t count,
                const unsigned char *value, size_t length)
{
    unsigned char *p = w->header + w->entry;
    put16(p, id); put16(p + 2, type); put32(p + 4, count);
    if (length <= 4) memcpy(p + 8, value, length);
    else {
        put32(p + 8, (uint32_t)w->blob);
        memcpy(w->header + w->blob, value, length);
        w->blob += length + (length & 1u);
    }
    w->entry += 12;
}

static void scalar(ifd_writer *w, uint32_t id, uint32_t type, uint32_t value)
{
    unsigned char bytes[4];
    put32(bytes, value);
    tag(w, id, type, 1, bytes, type == 3 ? 2 : 4);
}

int g360_dng_header(const g360_frame *frame, const g360_image *maker_note,
                    unsigned char *header, size_t capacity, size_t *size)
{
    unsigned char color[72], forward[72], neutral[24], baseline[8], bytes[4];
    const g360_image *image;
    ifd_writer writer;
    if (size == 0) return G360_E_ARGUMENT;
    *size = 0;
    if (frame == 0 || maker_note == 0 || header == 0 || capacity < G360_DNG_HEADER_BYTES)
        return G360_E_ARGUMENT;
    image = &frame->image;
    if (image->data == 0 || image->size != DNG_FRAME_BYTES || image->width != DNG_WIDTH ||
        image->height != DNG_HEIGHT || image->stride_x != DNG_STRIDE || image->stride_y != 16 ||
        (image->flags & (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK)) !=
            (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK) ||
        (image->flags & (G360_IMAGE_RAW_CHANGED | G360_IMAGE_NO_PAUSE)) != 0 ||
        maker_note->data == 0 || maker_note->size < 0x5314u ||
        !(maker_note->flags & G360_IMAGE_COPIED)) return G360_E_IMAGE;
    if (colour_values(maker_note->data + 0x14, color, forward, neutral) != 0)
        return G360_E_IMAGE;
    if (rational(0.34, 1, baseline) != 0) return G360_E_IMAGE;
    memset(header, 0, G360_DNG_HEADER_BYTES);
    memcpy(header, "II*\0", 4); put32(header + 4, 8); put16(header + 8, 28);
    writer.header = header; writer.entry = 10; writer.blob = 350;
    scalar(&writer,254,4,0); scalar(&writer,256,4,DNG_WIDTH); scalar(&writer,257,4,DNG_HEIGHT);
    scalar(&writer,258,3,16); scalar(&writer,259,3,1); scalar(&writer,262,3,32803);
    tag(&writer,271,2,8,(const unsigned char *)"SAMSUNG",8);
    tag(&writer,272,2,8,(const unsigned char *)"SM-R210",8);
    scalar(&writer,273,4,(uint32_t)G360_DNG_HEADER_BYTES);
    scalar(&writer,277,3,1); scalar(&writer,278,4,DNG_HEIGHT); scalar(&writer,279,4,(uint32_t)DNG_FRAME_BYTES);
    scalar(&writer,284,3,1);
    tag(&writer,305,2,20,(const unsigned char *)"gear360 make_dng.py",20);
    put16(bytes,2); put16(bytes+2,2); tag(&writer,33421,3,2,bytes,4);
    bytes[0]=1; bytes[1]=0; bytes[2]=2; bytes[3]=1; tag(&writer,33422,1,4,bytes,4);
    bytes[0]=1; bytes[1]=4; bytes[2]=0; bytes[3]=0; tag(&writer,50706,1,4,bytes,4);
    bytes[1]=3; tag(&writer,50707,1,4,bytes,4);
    tag(&writer,50708,2,25,(const unsigned char *)"Samsung Gear 360 SM-R210",25);
    bytes[0]=0; bytes[1]=1; bytes[2]=2; tag(&writer,50710,1,3,bytes,3);
    scalar(&writer,50711,3,1); scalar(&writer,50714,4,4106); scalar(&writer,50717,4,65472);
    tag(&writer,50721,10,9,color,sizeof color); tag(&writer,50728,5,3,neutral,sizeof neutral);
    tag(&writer,50730,10,1,baseline,sizeof baseline); scalar(&writer,50778,3,21);
    tag(&writer,50964,10,9,forward,sizeof forward);
    *size = G360_DNG_HEADER_BYTES;
    return G360_OK;
}

/* ---- The full header (make_dng.build_full) ----------------------------- */

typedef struct dng_entry {
    uint32_t tag, type, count;
    const unsigned char *data; /* NULL: reserved, filled by the caller */
    size_t bytes;
} dng_entry;

static void add_entry(dng_entry *e, size_t *n, uint32_t tag, uint32_t type, uint32_t count,
                      const unsigned char *data, size_t bytes)
{
    e[*n].tag = tag; e[*n].type = type; e[*n].count = count; e[*n].data = data; e[*n].bytes = bytes;
    ++*n;
}

static void sort_entries(dng_entry *e, size_t n)
{
    size_t i, j;
    for (i = 1; i < n; ++i) {
        dng_entry t = e[i];
        for (j = i; j > 0 && e[j - 1].tag > t.tag; --j) e[j] = e[j - 1];
        e[j] = t;
    }
}

/* Bytes an IFD takes with its values longer than 4 bytes (each padded to an
   even length), as make_dng._ifd lays it out. */
static size_t ifd_bytes(const dng_entry *e, size_t n)
{
    size_t i, total = 2 + 12 * n + 4;
    for (i = 0; i < n; ++i) if (e[i].bytes > 4) total += e[i].bytes + (e[i].bytes & 1u);
    return total;
}

/* Writes a sorted IFD at base + at; value offsets are relative to base. The
   reserved value's offset goes to *reserved. Returns the end offset. */
static size_t write_ifd(unsigned char *base, size_t at, const dng_entry *e, size_t n, size_t *reserved)
{
    size_t i, blob = at + 2 + 12 * n + 4;
    put16(base + at, (uint32_t)n);
    for (i = 0; i < n; ++i) {
        unsigned char *p = base + at + 2 + 12 * i;
        put16(p, e[i].tag); put16(p + 2, e[i].type); put32(p + 4, e[i].count);
        if (e[i].bytes <= 4) {
            memset(p + 8, 0, 4);
            memcpy(p + 8, e[i].data, e[i].bytes);
            continue;
        }
        put32(p + 8, (uint32_t)blob);
        if (e[i].data) memcpy(base + blob, e[i].data, e[i].bytes);
        else if (reserved) *reserved = blob;
        if (e[i].bytes & 1u) base[blob + e[i].bytes] = 0;
        blob += e[i].bytes + (e[i].bytes & 1u);
    }
    put32(base + at + 2 + 12 * n, 0);
    return blob;
}

static int datetime_ok(const char *text)
{
    static const char pattern[] = "0000:00:00 00:00:00";
    size_t i;
    for (i = 0; i < 19; ++i) {
        if (pattern[i] == '0') { if (text[i] < '0' || text[i] > '9') return 0; }
        else if (text[i] != pattern[i]) return 0;
    }
    return text[19] == '\0';
}

#define MAKERNOTE_BYTES ((size_t)(2 + 12 * 5 + 4 + 6000 + 13000 + 13000 + 2048))

int g360_dng_header_full(const g360_frame *frame, const g360_image *maker_note,
                         const g360_exif_info *info, const char *description,
                         unsigned char *header, size_t capacity, size_t *size)
{
    return g360_dng_header_full_xmp(frame, maker_note, info, description, 0, header, capacity, size);
}

int g360_dng_header_full_xmp(const g360_frame *frame, const g360_image *maker_note,
                             const g360_exif_info *info, const char *description, const char *xmp,
                             unsigned char *header, size_t capacity, size_t *size)
{
    static const unsigned char cfa_dims[4] = {2, 0, 2, 0}, cfa[4] = {1, 0, 2, 1};
    static const unsigned char version[4] = {1, 4, 0, 0}, backward[4] = {1, 3, 0, 0};
    static const unsigned char plane[3] = {0, 1, 2}, exif_version[4] = {'0', '2', '3', '0'};
    static const unsigned char makernote_version[4] = {'0', '1', '0', '0'};
    static const unsigned char fnumber[8] = {22, 0, 0, 0, 10, 0, 0, 0};
    static const unsigned char focal[8] = {12, 0, 0, 0, 10, 0, 0, 0};
    static const unsigned char lens[32] = {12, 0, 0, 0, 10, 0, 0, 0, 12, 0, 0, 0, 10, 0, 0, 0,
                                           22, 0, 0, 0, 10, 0, 0, 0, 22, 0, 0, 0, 10, 0, 0, 0};
    unsigned char color[72], forward[72], neutral[24], baseline[8];
    unsigned char zero[4], width[4], height[4], strip[4], count_bytes[4], black[4], white[4], exif_at_bytes[4];
    unsigned char sixteen[2], one[2], cfa_photometric[2], d65[2], focal35[2];
    unsigned char exposure[8], program[2], iso[2], wb[2];
    dng_entry ifd0[41], exif[16], note[5];
    size_t n0 = 0, ne = 0, nn = 0, exif_at, end, mn_at = 0, described = 0, xmp_size = 0;
    const g360_image *image;
    if (size == 0) return G360_E_ARGUMENT;
    *size = 0;
    if (frame == 0 || maker_note == 0 || info == 0 || header == 0 || capacity < G360_DNG_FULL_HEADER_BYTES)
        return G360_E_ARGUMENT;
    if (info->datetime[0] != '\0' && !datetime_ok(info->datetime)) return G360_E_ARGUMENT;
    if (description != 0) {
        for (described = 0; description[described] != '\0'; ++described)
            if (described >= 63 || description[described] < 32 || description[described] > 126)
                return G360_E_ARGUMENT;
    }
    if (xmp != 0) {
        xmp_size = strlen(xmp);
        if (xmp_size == 0 || xmp_size > G360_DNG_XMP_MAX) return G360_E_ARGUMENT;
    }
    image = &frame->image;
    if (image->data == 0 || image->size != DNG_FRAME_BYTES || image->width != DNG_WIDTH ||
        image->height != DNG_HEIGHT || image->stride_x != DNG_STRIDE || image->stride_y != 16 ||
        (image->flags & (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK)) !=
            (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK) ||
        (image->flags & (G360_IMAGE_RAW_CHANGED | G360_IMAGE_NO_PAUSE)) != 0 ||
        maker_note->data == 0 || maker_note->size < 0x5314u ||
        !(maker_note->flags & G360_IMAGE_COPIED)) return G360_E_IMAGE;
    if (colour_values(maker_note->data + 0x14, color, forward, neutral) != 0) return G360_E_IMAGE;
    if (rational(0.34, 1, baseline) != 0) return G360_E_IMAGE;
    put32(zero, 0); put32(width, DNG_WIDTH); put32(height, DNG_HEIGHT);
    put32(strip, (uint32_t)G360_DNG_FULL_HEADER_BYTES); put32(count_bytes, (uint32_t)DNG_FRAME_BYTES);
    put32(black, 4106); put32(white, 65472);
    put16(sixteen, 16); put16(one, 1); put16(cfa_photometric, 32803); put16(d65, 21); put16(focal35, 6);

    add_entry(ifd0, &n0, 254, 4, 1, zero, 4);
    add_entry(ifd0, &n0, 256, 4, 1, width, 4);
    add_entry(ifd0, &n0, 257, 4, 1, height, 4);
    add_entry(ifd0, &n0, 258, 3, 1, sixteen, 2);
    add_entry(ifd0, &n0, 259, 3, 1, one, 2);
    add_entry(ifd0, &n0, 262, 3, 1, cfa_photometric, 2);
    if (described) add_entry(ifd0, &n0, 270, 2, (uint32_t)described + 1u,
                             (const unsigned char *)description, described + 1u);
    add_entry(ifd0, &n0, 271, 2, 8, (const unsigned char *)"SAMSUNG", 8);
    add_entry(ifd0, &n0, 272, 2, 8, (const unsigned char *)"SM-R210", 8);
    add_entry(ifd0, &n0, 273, 4, 1, strip, 4);
    add_entry(ifd0, &n0, 274, 3, 1, one, 2);
    add_entry(ifd0, &n0, 277, 3, 1, one, 2);
    add_entry(ifd0, &n0, 278, 4, 1, height, 4);
    add_entry(ifd0, &n0, 279, 4, 1, count_bytes, 4);
    add_entry(ifd0, &n0, 284, 3, 1, one, 2);
    add_entry(ifd0, &n0, 305, 2, 20, (const unsigned char *)"gear360 make_dng.py", 20);
    if (info->datetime[0]) add_entry(ifd0, &n0, 306, 2, 20, (const unsigned char *)info->datetime, 20);
    if (xmp_size) add_entry(ifd0, &n0, 700, 1, (uint32_t)xmp_size, (const unsigned char *)xmp, xmp_size);
    add_entry(ifd0, &n0, 33421, 3, 2, cfa_dims, 4);
    add_entry(ifd0, &n0, 33422, 1, 4, cfa, 4);
    add_entry(ifd0, &n0, 34665, 4, 1, exif_at_bytes, 4);
    add_entry(ifd0, &n0, 50706, 1, 4, version, 4);
    add_entry(ifd0, &n0, 50707, 1, 4, backward, 4);
    add_entry(ifd0, &n0, 50708, 2, 25, (const unsigned char *)"Samsung Gear 360 SM-R210", 25);
    add_entry(ifd0, &n0, 50710, 1, 3, plane, 3);
    add_entry(ifd0, &n0, 50711, 3, 1, one, 2);
    add_entry(ifd0, &n0, 50714, 4, 1, black, 4);
    add_entry(ifd0, &n0, 50717, 4, 1, white, 4);
    add_entry(ifd0, &n0, 50721, 10, 9, color, sizeof color);
    add_entry(ifd0, &n0, 50728, 5, 3, neutral, sizeof neutral);
    add_entry(ifd0, &n0, 50730, 10, 1, baseline, sizeof baseline);
    add_entry(ifd0, &n0, 50736, 5, 4, lens, sizeof lens);
    add_entry(ifd0, &n0, 50778, 3, 1, d65, 2);
    add_entry(ifd0, &n0, 50964, 10, 9, forward, sizeof forward);

    if (info->exposure_numerator && info->exposure_denominator) {
        put32(exposure, info->exposure_numerator); put32(exposure + 4, info->exposure_denominator);
        add_entry(exif, &ne, 33434, 5, 1, exposure, 8);
    }
    add_entry(exif, &ne, 33437, 5, 1, fnumber, 8);
    if (info->exposure_program != G360_EXIF_UNKNOWN && info->exposure_program <= 0xffffu) {
        put16(program, info->exposure_program); add_entry(exif, &ne, 34850, 3, 1, program, 2);
    }
    if (info->iso && info->iso <= 0xffffu) { put16(iso, info->iso); add_entry(exif, &ne, 34855, 3, 1, iso, 2); }
    add_entry(exif, &ne, 36864, 7, 4, exif_version, 4);
    if (info->datetime[0]) {
        add_entry(exif, &ne, 36867, 2, 20, (const unsigned char *)info->datetime, 20);
        add_entry(exif, &ne, 36868, 2, 20, (const unsigned char *)info->datetime, 20);
    }
    add_entry(exif, &ne, 37386, 5, 1, focal, 8);
    add_entry(exif, &ne, 37500, 7, (uint32_t)MAKERNOTE_BYTES, 0, MAKERNOTE_BYTES);
    if (info->white_balance != G360_EXIF_UNKNOWN && info->white_balance <= 0xffffu) {
        put16(wb, info->white_balance); add_entry(exif, &ne, 41987, 3, 1, wb, 2);
    }
    add_entry(exif, &ne, 41989, 3, 1, focal35, 2);

    add_entry(note, &nn, 0x0001, 7, 4, makernote_version, 4);
    add_entry(note, &nn, 0x0060, 7, 6000, maker_note->data + 0x32dc, 6000);
    add_entry(note, &nn, 0x00a0, 7, 13000, maker_note->data + 0x14, 13000);
    add_entry(note, &nn, 0x00a1, 7, 13000, maker_note->data + 0x14, 13000);
    add_entry(note, &nn, 0x00c0, 7, 2048, maker_note->data + 0x4a4c, 2048);

    sort_entries(ifd0, n0); sort_entries(exif, ne);
    exif_at = 8 + ifd_bytes(ifd0, n0);
    if (exif_at + ifd_bytes(exif, ne) > G360_DNG_FULL_HEADER_BYTES) return G360_E_ARGUMENT;
    put32(exif_at_bytes, (uint32_t)exif_at);
    memset(header, 0, G360_DNG_FULL_HEADER_BYTES);
    memcpy(header, "II*\0", 4); put32(header + 4, 8);
    end = write_ifd(header, 8, ifd0, n0, 0);
    end = write_ifd(header, end, exif, ne, &mn_at);
    if (mn_at == 0 || write_ifd(header + mn_at, 0, note, nn, 0) != MAKERNOTE_BYTES) return G360_E_ARGUMENT;
    (void)end;
    *size = G360_DNG_FULL_HEADER_BYTES;
    return G360_OK;
}

