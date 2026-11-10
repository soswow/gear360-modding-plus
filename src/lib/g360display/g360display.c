#include "g360display.h"
#include "capture_libc.h"
#include <stdint.h>

#if defined(__APPLE__)
#include <sys/ioctl.h>
#define DISPLAY_RDWR O_RDWR
#else
#define DISPLAY_RDWR 2
int ioctl(int fd, unsigned long request, ...);
#endif

/* Tests substitute only the device calls; the actual render/ABI code is shared. */
#ifdef G360DISPLAY_TEST
int g360display_test_open(const char *path, int flags);
int g360display_test_ioctl(int fd, unsigned long request, void *argument);
int g360display_test_close(int fd);
#define display_open g360display_test_open
#define display_ioctl g360display_test_ioctl
#define display_close g360display_test_close
#else
#define display_open open
#define display_ioctl ioctl
#define display_close close
#endif

struct rectangle { int32_t top, bottom, left, right; };
struct drawing { struct rectangle rect; unsigned char pixels[G360DISPLAY_PIXELS]; };
typedef char drawing_size_check[sizeof(struct drawing) == 2320 ? 1 : -1];
typedef char drawing_offset_check[offsetof(struct drawing, pixels) == 16 ? 1 : -1];

/* Five columns, bit zero at top. Same glyphs as gear360-display.c, plus %. */
static const char characters[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /:-.%+";
static const unsigned char font[][5] = {
 {62,81,73,69,62},{0,66,127,64,0},{98,81,73,73,70},{34,65,73,73,54},
 {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
 {54,73,73,73,54},{6,73,73,41,30},
 {126,9,9,9,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,2,4,8,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {3,4,120,4,3},{97,81,73,69,67},
 {0,0,0,0,0},{96,16,8,4,3},{0,54,54,0,0},{8,8,8,8,8},{0,96,96,0,0},
 {35,19,8,100,98},{8,8,62,8,8}
};
typedef char font_count_check[(sizeof characters - 1) == sizeof font / sizeof font[0] ? 1 : -1];

/* The status corner's icons, 7 rows each, 'X' lit. */
static const char *const wifi_icons[3][7] = {
    {".XXXXXXX.", "X.......X", "..XXXXX..", ".X.....X.", "...X.X...", "....X....", "...X.X..."},
    {".XXXXXXX.", "X.......X", "..XXXXX..", ".X.....X.", "...XXX...", ".........", "....X...."},
    {"..X.....X..", ".X..X.X..X.", "X..X...X..X", "X..X.X.X..X", "X..X...X..X", ".X..X.X..X.",
     "..X.....X.."}
};
static const char *const wifi_search[3][7] = {
    {".........", ".........", ".........", ".........", ".........", ".........", "....X...."},
    {".........", ".........", "..XXXXX..", ".X.....X.", "...XXX...", ".........", "....X...."},
    {".XXXXXXX.", "X.......X", "..XXXXX..", ".X.....X.", "...XXX...", ".........", "....X...."}
};
static const char *const ap_client[7] = {
    "..X.....X..", ".X.......X.", "X...XXX...X", "X...XXX...X", "X...XXX...X", ".X.......X.", "..X.....X.."
};
/* The battery, 4 x 7 px, drawn by the owner as pixel art: a terminal on
   top, the walls and the floor, and a 2 x 5 px interior that fills from the
   bottom in six steps (0-20, 21-40, 41-60, 61-80, 81-99, 100 %). While a
   charger is in, the walls rise from the floor to the top in six frames, then
   start over; the terminal and floor stay lit. */
#define BATTERY_WIDTH 4u

/* The capture mode: one camera, or a camera with a second one behind it.
   Drawn by the owner as pixel art (output/mode-icons-20261106). */
static const char *const mode_icons[2][7] = {
    {"..XXX....", "XXXXXXXXX", "X.......X", "X..XXX..X", "X..X.X..X", "X..XXX..X", "XXXXXXXXX"},
    {"..XXX......", "XXXXXXXX.XX", "X......X..X", "X..XXX.X..X", "X..X.X.X..X", "X..XXX.X..X", "XXXXXXXX.XX"}
};

/* The self-timer: a watch face at 3 o'clock (hour hand right, minute hand up). */
static const char *const timer_icon[7] = {"..XXX..", ".X.X.X.", "X..X..X", "X..XXXX", "X.....X", ".X...X.", "..XXX.."};

/* Sounds on: a pair of beamed notes. */
static const char *const sound_icon[7] = {"..XXXXX", "..X...X", "..X...X", "..X...X", ".XX..XX", "XXX.XXX", ".X...X."};

typedef struct text_lines {
    unsigned char glyphs[G360DISPLAY_MAX_LINES][G360DISPLAY_MAX_TEXT];
    size_t lengths[G360DISPLAY_MAX_LINES];
    unsigned count;
} text_lines;

/* Validates text and saves glyph indices before anything is drawn, so
 * errors leave the destination intact, even when text lies within it. */
static int parse(const char *text, unsigned max_lines, text_lines *t)
{
    size_t length = 0;
    const char *p;
    t->count = 1;
    for (p = text; *p != '\0'; ++p) {
        const char *found;
        if (*p == '\n') {
            if (t->count == max_lines) return -1;
            t->lengths[t->count - 1] = length; length = 0; ++t->count;
            continue;
        }
        if (length == G360DISPLAY_MAX_TEXT) return -1;
        found = strchr(characters, *p);
        if (found == 0) return -1;
        t->glyphs[t->count - 1][length++] = (unsigned char)(found - characters);
    }
    t->lengths[t->count - 1] = length;
    return 0;
}

static void draw_glyph(unsigned char *pixels, unsigned glyph, unsigned left, unsigned top)
{
    unsigned x, y;
    for (x = 0; x < 5; ++x) for (y = 0; y < 7; ++y)
        if ((font[glyph][x] >> y) & 1u) pixels[(top + y) * G360DISPLAY_WIDTH + left + x] = 1;
}

/* Lines are 7 pixels high, 3 apart; each is centred, and the block is
   centred vertically in rows area_top .. area_top + area_height - 1. */
static void draw_lines(unsigned char *pixels, const text_lines *t, unsigned area_top, unsigned area_height)
{
    unsigned line, top = area_top + (area_height - (t->count * 7u + (t->count - 1u) * 3u)) / 2u;
    size_t col;
    for (line = 0; line < t->count; ++line, top += 10u) {
        unsigned left;
        if (t->lengths[line] == 0) continue;
        left = (G360DISPLAY_WIDTH - (unsigned)(t->lengths[line] * 6u - 1u)) / 2u;
        for (col = 0; col < t->lengths[line]; ++col)
            draw_glyph(pixels, t->glyphs[line][col], left + (unsigned)col * 6u, top);
    }
}

static void draw_icon(unsigned char *pixels, const char *const *rows, unsigned count, unsigned left, unsigned top)
{
    unsigned y, x;
    for (y = 0; y < count; ++y)
        for (x = 0; rows[y][x] != '\0'; ++x)
            if (rows[y][x] == 'X') pixels[(top + y) * G360DISPLAY_WIDTH + left + x] = 1;
}

/* Interior rows lit for a charge of 0-100 %: 0 to 5. */
static unsigned battery_fill(int percent)
{
    if (percent >= 100) return 5u;
    return percent <= 20 ? 0u : (unsigned)(percent - 1) / 20u;
}

/* Wall rows lit: all five when not charging, else the frame's 0 to 5. */
static void draw_battery(unsigned char *pixels, const g360display_status *s, unsigned left)
{
    char rows[7][BATTERY_WIDTH + 1u];
    const char *pointers[7];
    unsigned y, fill = battery_fill(s->battery), walls = s->charging ? (unsigned)s->charge_frame : 5u;
    for (y = 0; y < 7u; ++y) {
        const char *shape = y == 0 ? ".XX." : y == 6u ? "XXXX" : y > 5u - walls ? "X..X" : "....";
        memcpy(rows[y], shape, BATTERY_WIDTH + 1u);
        if (y >= 1u && y <= 5u && y > 5u - fill) rows[y][1] = rows[y][2] = 'X';
        pointers[y] = rows[y];
    }
    draw_icon(pixels, pointers, 7, left, 0);
}

/* Right-aligned in rows 0-6: battery icon, 3 px, Wi-Fi icon, 3 px, mode icon,
   3 px, self-timer icon (when set), 3 px, R (RAW on), 3 px, sound icon (on). */
static void draw_status(unsigned char *pixels, const g360display_status *s)
{
    unsigned right = G360DISPLAY_WIDTH;
    if (s->battery >= 0) {
        right -= BATTERY_WIDTH;
        draw_battery(pixels, s, right);
        right -= 3u;
    }
    {
        const char *const *icon = 0;
        if (s->wifi_searching) icon = wifi_search[s->wifi_frame];
        else if (s->wifi == G360DISPLAY_WIFI_HOME) icon = wifi_icons[G360DISPLAY_WIFI_HOME];
        else if (s->wifi == G360DISPLAY_WIFI_AP) icon = s->ap_connected ? ap_client : wifi_icons[G360DISPLAY_WIFI_AP];
        else if (s->wifi_error) icon = wifi_icons[G360DISPLAY_WIFI_OFF];
        right -= icon ? (unsigned)strlen(icon[0]) : 9u;
        if (icon) draw_icon(pixels, icon, 7, right, 0);
    }
    if (s->mode != G360DISPLAY_MODE_NONE) {
        const char *const *icon = mode_icons[s->mode - 1];
        right -= 3u + (unsigned)strlen(icon[0]);
        draw_icon(pixels, icon, 7, right, 0);
    }
    if (s->timer) {
        right -= 3u + (unsigned)strlen(timer_icon[0]);
        draw_icon(pixels, timer_icon, 7, right, 0);
    }
    if (s->raw) {
        right -= 3u + 5u;
        draw_glyph(pixels, (unsigned)(strchr(characters, 'R') - characters), right, 0);
    }
    if (s->sound) {
        right -= 3u + (unsigned)strlen(sound_icon[0]);
        draw_icon(pixels, sound_icon, 7, right, 0);
    }
}

/* A single line of 1 to 6 digits: drawn at double size, in place of the text. */
static int is_number(const text_lines *t)
{
    size_t k;
    if (t->count != 1 || t->lengths[0] == 0 || t->lengths[0] > 6u) return 0;
    for (k = 0; k < t->lengths[0]; ++k) if (t->glyphs[0][k] > 9u) return 0;
    return 1;
}

/* Digits twice as big (10 x 14 px, 2 px apart), centred in the rows below the status corner. */
static void draw_number(unsigned char *pixels, const text_lines *t, unsigned area_top, unsigned area_height)
{
    size_t col, n = t->lengths[0];
    unsigned top = area_top + (area_height - 14u) / 2u, left = (G360DISPLAY_WIDTH - (unsigned)(n * 12u - 2u)) / 2u;
    for (col = 0; col < n; ++col) {
        unsigned x, y, base = left + (unsigned)col * 12u;
        for (x = 0; x < 5; ++x) for (y = 0; y < 7; ++y)
            if ((font[t->glyphs[0][col]][x] >> y) & 1u) {
                unsigned dx, dy;
                for (dx = 0; dx < 2; ++dx) for (dy = 0; dy < 2; ++dy)
                    pixels[(top + y * 2u + dy) * G360DISPLAY_WIDTH + base + x * 2u + dx] = 1;
            }
    }
}

int g360display_render_center(const char *text, unsigned char pixels[G360DISPLAY_PIXELS])
{
    text_lines t;
    if (text == 0 || pixels == 0 || parse(text, G360DISPLAY_MAX_LINES, &t) != 0) return -1;
    memset(pixels, 0, G360DISPLAY_PIXELS);
    /* One line starts at row 12; two at 7 and 17; three at 2, 12 and 22. */
    draw_lines(pixels, &t, 0, G360DISPLAY_HEIGHT);
    return 0;
}

int g360display_render_status(const char *text, const g360display_status *status,
                              unsigned char pixels[G360DISPLAY_PIXELS])
{
    text_lines t;
    if (text == 0 || status == 0 || pixels == 0) return -1;
    if (status->wifi < G360DISPLAY_WIFI_OFF || status->wifi > G360DISPLAY_WIFI_AP) return -1;
    if (status->battery < -1 || status->battery > 100) return -1;
    if (status->charge_frame < 0 || status->charge_frame >= G360DISPLAY_CHARGE_FRAMES) return -1;
    if (status->wifi_frame < 0 || status->wifi_frame >= G360DISPLAY_WIFI_FRAMES) return -1;
    if (status->mode < G360DISPLAY_MODE_NONE || status->mode > G360DISPLAY_MODE_SWEEP) return -1;
    if (parse(text, G360DISPLAY_STATUS_LINES, &t) != 0) return -1;
    memset(pixels, 0, G360DISPLAY_PIXELS);
    if (is_number(&t)) draw_number(pixels, &t, 9, G360DISPLAY_HEIGHT - 9u);
    else draw_lines(pixels, &t, 9, G360DISPLAY_HEIGHT - 9u);
    draw_status(pixels, status);
    return 0;
}

static int show_pixels(struct drawing *draw)
{
    int fd, status;
    fd = display_open("/dev/ssd1309-ctrl", DISPLAY_RDWR);
    if (fd < 0) return -1;
    status = display_ioctl(fd, 0x49105302UL, draw);
    if (status == 0) status = display_ioctl(fd, 0x40105304UL, &draw->rect);
    if (display_close(fd) != 0) return -1;
    return status == 0 ? 0 : -1;
}

int g360display_show(const char *text)
{
    struct drawing draw = {{0,31,0,71}, {0}};
    if (g360display_render_center(text, draw.pixels) != 0) return -1;
    return show_pixels(&draw);
}

int g360display_show_status(const char *text, const g360display_status *status)
{
    struct drawing draw = {{0,31,0,71}, {0}};
    if (g360display_render_status(text, status, draw.pixels) != 0) return -1;
    return show_pixels(&draw);
}

int g360display_clear(void) { return g360display_show(""); }
