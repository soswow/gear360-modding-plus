#include "scan.h"
#include "capture_libc.h"

#define CELLS (G360_SCAN_COLS * G360_SCAN_ROWS)

/* LEVELS[L] is the smallest net value v with 16 log2(1 + v) >= L - 0.5:
   the level of v is the largest L with LEVELS[L] <= v. */
static const uint32_t LEVELS[256] = {
    0u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u,
    2u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 4u, 4u, 4u, 4u, 4u, 5u, 5u, 5u, 5u, 6u, 6u, 6u, 6u, 7u, 7u, 7u, 8u,
    8u, 8u, 9u, 9u, 10u, 10u, 11u, 11u, 12u, 12u, 13u, 13u, 14u, 14u, 15u, 16u, 17u, 17u, 18u, 19u, 20u,
    21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 31u, 32u, 34u, 35u, 37u, 38u, 40u, 42u, 44u, 46u, 48u,
    50u, 52u, 54u, 57u, 59u, 62u, 65u, 68u, 71u, 74u, 77u, 81u, 84u, 88u, 92u, 96u, 100u, 105u, 109u,
    114u, 119u, 125u, 130u, 136u, 142u, 148u, 155u, 162u, 169u, 177u, 184u, 193u, 201u, 210u, 219u,
    229u, 239u, 250u, 261u, 273u, 285u, 297u, 311u, 324u, 339u, 354u, 369u, 386u, 403u, 421u, 439u,
    459u, 479u, 501u, 523u, 546u, 570u, 595u, 622u, 649u, 678u, 708u, 739u, 772u, 806u, 842u, 879u,
    918u, 959u, 1002u, 1046u, 1092u, 1141u, 1191u, 1244u, 1299u, 1357u, 1417u, 1479u, 1545u, 1613u,
    1685u, 1759u, 1837u, 1919u, 2004u, 2092u, 2185u, 2282u, 2383u, 2488u, 2599u, 2714u, 2834u, 2959u,
    3090u, 3227u, 3370u, 3519u, 3675u, 3838u, 4008u, 4185u, 4371u, 4564u, 4766u, 4977u, 5198u, 5428u,
    5668u, 5919u, 6181u, 6455u, 6741u, 7039u, 7351u, 7676u, 8016u, 8371u, 8742u, 9129u, 9533u, 9955u,
    10396u, 10856u, 11336u, 11838u, 12363u, 12910u, 13482u, 14078u, 14702u, 15353u, 16032u, 16742u,
    17484u, 18258u, 19066u, 19910u, 20792u, 21712u, 22673u, 23677u, 24726u, 25820u, 26964u, 28157u,
    29404u, 30706u, 32065u, 33485u, 34968u, 36516u, 38132u, 39821u, 41584u, 43425u, 45347u, 47355u,
    49452u, 51641u, 53928u, 56315u, 58809u, 61412u
};

static uint8_t level_of(uint32_t v)
{
    unsigned low = 0, high = 255;
    while (low < high) {
        unsigned mid = (low + high + 1u) / 2u;
        if (LEVELS[mid] <= v) low = mid; else high = mid - 1u;
    }
    return (uint8_t)low;
}

static uint32_t isqrt(uint64_t n)
{
    uint64_t r = 0, bit = UINT64_C(1) << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

/* Twice the circle's centre and radius: quad centres are (x + 0.5, y + 0.5)
   for a quad at sample (x, y), so twice that is odd and exact. */
#define C2X ((int64_t)(G360_SCAN_CENTER_X * 2.0))
#define C2Y ((int64_t)(G360_SCAN_CENTER_Y * 2.0))
#define R2 ((int64_t)(G360_SCAN_RADIUS * 2.0))

/* The sampled quads of row Y inside the circle: x from *FIRST to *LAST (both
   multiples of 4) in a half. Returns 0 when the row misses the circle. */
static int row_range(unsigned y, int64_t *first, int64_t *last)
{
    int64_t dy = 2 * (int64_t)y + 1 - C2Y, room = R2 * R2 - dy * dy, m, lo, hi;
    if (room < 0) return 0;
    m = isqrt((uint64_t)room);
    /* 2x + 1 within [C2X - m, C2X + m]. */
    lo = C2X - m - 1; lo = lo <= 0 ? 0 : (lo + 1) / 2;
    hi = (C2X + m - 1) / 2;
    if (hi > (int64_t)G360_SCAN_HALF - 2) hi = G360_SCAN_HALF - 2;
    lo = (lo + 3) / 4 * 4;
    *first = lo; *last = hi;
    return lo <= hi;
}

int g360_scan_raw(const void *data, size_t size, uint32_t width, uint32_t height, uint32_t stride,
                  g360_scan_frame *frame)
{
    static uint32_t sum[CELLS];
    static uint16_t peak[CELLS];
    const uint8_t *base = data;
    unsigned lens, y, k;
    memset(frame->quads, 0, sizeof frame->quads); memset(frame->clipped, 0, sizeof frame->clipped);
    memset(frame->dark, 0, sizeof frame->dark); memset(frame->cells, 0, sizeof frame->cells);
    if (data == 0 || width != G360_SCAN_WIDTH || height != G360_SCAN_HEIGHT || stride != G360_SCAN_STRIDE ||
        size < (size_t)G360_SCAN_STRIDE * G360_SCAN_HEIGHT)
        return -1;
    for (lens = 0; lens < G360_SCAN_LENSES; ++lens) {
        uint8_t *cells = frame->cells[lens];
        memset(sum, 0, sizeof sum); memset(peak, 0, sizeof peak);
        for (y = 0; y + 1u < G360_SCAN_HEIGHT; y += 4u) {
            int64_t first, last, x;
            const uint16_t *row0, *row1;
            unsigned cell_row = (y / G360_SCAN_BLOCK) * G360_SCAN_COLS;
            if (!row_range(y, &first, &last)) continue;
            row0 = (const uint16_t *)(base + (size_t)y * G360_SCAN_STRIDE) + lens * G360_SCAN_HALF;
            row1 = (const uint16_t *)(base + (size_t)(y + 1u) * G360_SCAN_STRIDE) + lens * G360_SCAN_HALF;
            for (x = first; x <= last; x += 4) {
                /* GRBG: G1 R / B G2. */
                uint32_t g1 = row0[x], r = row0[x + 1], b = row1[x], g2 = row1[x + 1];
                uint32_t hi = g1 > r ? g1 : r, g, c = cell_row + (unsigned)x / G360_SCAN_BLOCK;
                if (b > hi) hi = b;
                if (g2 > hi) hi = g2;
                g = (g1 + g2) / 2u;
                g = g > G360_SCAN_BLACK ? g - G360_SCAN_BLACK : 0u;
                hi = hi > G360_SCAN_BLACK ? hi - G360_SCAN_BLACK : 0u;
                ++cells[c * G360_SCAN_CELL_BYTES];
                if (hi >= G360_SCAN_WHITE - G360_SCAN_BLACK) { ++cells[c * G360_SCAN_CELL_BYTES + 1]; ++frame->clipped[lens]; }
                if (g < G360_SCAN_DARK) { ++cells[c * G360_SCAN_CELL_BYTES + 2]; ++frame->dark[lens]; }
                sum[c] += g;
                if (hi > peak[c]) peak[c] = (uint16_t)hi;
                ++frame->quads[lens];
            }
        }
        for (k = 0; k < CELLS; ++k) {
            uint8_t *cell = cells + k * G360_SCAN_CELL_BYTES;
            if (cell[0] == 0) continue;
            cell[3] = level_of((sum[k] + cell[0] / 2u) / cell[0]);
            cell[4] = level_of(peak[k]);
        }
    }
    return 0;
}

int g360_scan_plain_done(const g360_scan_frame *frame, uint32_t taken)
{
    uint64_t quads = (uint64_t)frame->quads[0] + frame->quads[1];
    uint64_t dark = (uint64_t)frame->dark[0] + frame->dark[1];
    if (taken < G360_SCAN_MIN_FRAMES || quads == 0) return 0;
    return dark * 1000u <= quads * G360_SCAN_STOP_PERMILLE;
}

uint32_t g360_scan_targets(const g360_scan_frame *reference, uint32_t nd,
                           uint8_t target[G360_SCAN_LENSES * CELLS])
{
    static g360_scan_frame circle;
    static int circle_ready;
    unsigned lens, k;
    uint32_t count = 0;
    memset(target, 0, G360_SCAN_LENSES * CELLS);
    if (reference == 0) {
        /* Which blocks the circle reaches, from the same geometry. */
        if (!circle_ready) {
            memset(&circle, 0, sizeof circle);
            for (lens = 0; lens < G360_SCAN_LENSES; ++lens) {
                unsigned y;
                for (y = 0; y + 1u < G360_SCAN_HEIGHT; y += 4u) {
                    int64_t first, last, x;
                    if (!row_range(y, &first, &last)) continue;
                    for (x = first; x <= last; x += 4)
                        circle.cells[lens][((y / G360_SCAN_BLOCK) * G360_SCAN_COLS + (unsigned)x / G360_SCAN_BLOCK) *
                                           G360_SCAN_CELL_BYTES] = 1;
                }
            }
            circle_ready = 1;
        }
    }
    for (lens = 0; lens < G360_SCAN_LENSES; ++lens) {
        if (!(nd & (1u << lens))) continue;
        for (k = 0; k < CELLS; ++k) {
            const uint8_t *cell = reference ? reference->cells[lens] + k * G360_SCAN_CELL_BYTES
                                            : circle.cells[lens] + k * G360_SCAN_CELL_BYTES;
            if (reference ? cell[1] != 0 : cell[0] != 0) { target[lens * CELLS + k] = 1; ++count; }
        }
    }
    return count;
}

int g360_scan_nd_done(g360_scan_frame *frame, uint32_t taken,
                      const uint8_t target[G360_SCAN_LENSES * CELLS], uint8_t measured[G360_SCAN_LENSES * CELLS])
{
    unsigned lens, k;
    uint32_t open = 0, clipped = 0;
    for (lens = 0; lens < G360_SCAN_LENSES; ++lens) {
        for (k = 0; k < CELLS; ++k) {
            const uint8_t *cell = frame->cells[lens] + k * G360_SCAN_CELL_BYTES;
            unsigned at = lens * CELLS + k;
            if (!target[at] || measured[at]) continue;
            if (cell[0] != 0 && cell[1] == 0 && cell[4] >= level_of(G360_SCAN_DARK)) { measured[at] = 1; continue; }
            if (cell[1] != 0) ++clipped;
            else ++open;
        }
    }
    frame->still_clipped = clipped;
    return taken >= 1u && open == 0;
}

static void put16(uint8_t *out, uint32_t v) { out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *out, uint32_t v) { put16(out, v & 0xffffu); put16(out + 2, v >> 16); }

size_t g360_scan_file_header(uint8_t out[G360_SCAN_FILE_HEADER], uint32_t frames)
{
    memset(out, 0, G360_SCAN_FILE_HEADER);
    memcpy(out, "G360SCN1", 8);
    put16(out + 8, 1u); put16(out + 10, frames); put16(out + 12, G360_SCAN_COLS); put16(out + 14, G360_SCAN_ROWS);
    put16(out + 16, G360_SCAN_BLOCK); put16(out + 18, G360_SCAN_CELL_BYTES); put16(out + 20, G360_SCAN_LENSES);
    put32(out + 24, G360_SCAN_BLACK); put32(out + 28, G360_SCAN_WHITE); put32(out + 32, G360_SCAN_DARK);
    return G360_SCAN_FILE_HEADER;
}

size_t g360_scan_frame_header(uint8_t out[G360_SCAN_FRAME_HEADER], const g360_scan_frame *f, int reference)
{
    unsigned k;
    memset(out, 0, G360_SCAN_FRAME_HEADER);
    memcpy(out, f->stamp, 16);
    put32(out + 16, f->index); put32(out + 20, f->nd); put32(out + 24, (uint32_t)f->tv);
    put32(out + 28, f->exposure_numerator); put32(out + 32, f->exposure_denominator); put32(out + 36, f->scan_us);
    for (k = 0; k < 2u; ++k) {
        put32(out + 40 + 4 * k, f->quads[k]); put32(out + 48 + 4 * k, f->clipped[k]); put32(out + 56 + 4 * k, f->dark[k]);
    }
    put32(out + 64, f->last); put32(out + 68, f->still_clipped); put32(out + 72, reference ? 1u : 0u);
    return G360_SCAN_FRAME_HEADER;
}
