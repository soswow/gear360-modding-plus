/* Direct OLED text output using the already validated full-frame draw ABI. */
#ifndef G360DISPLAY_H
#define G360DISPLAY_H

#define G360DISPLAY_WIDTH 72u
#define G360DISPLAY_HEIGHT 32u
#define G360DISPLAY_PIXELS 2304u
#define G360DISPLAY_MAX_TEXT 12u   /* characters per line */
#define G360DISPLAY_MAX_LINES 3u
#define G360DISPLAY_STATUS_LINES 2u /* text lines below the status corner */

/* The status corner's Wi-Fi icon. */
#define G360DISPLAY_WIFI_OFF 0  /* no network */
#define G360DISPLAY_WIFI_HOME 1 /* joined an existing network: the Wi-Fi fan */
#define G360DISPLAY_WIFI_AP 2   /* the camera's own access point: ((.)) */

/* The status corner's capture-mode icon, left of the Wi-Fi icon. */
#define G360DISPLAY_MODE_NONE 0   /* no icon */
#define G360DISPLAY_MODE_SINGLE 1 /* one camera: a Capture click takes one still */
#define G360DISPLAY_MODE_SWEEP 2  /* a camera with a second behind it: an HDR sweep */

/* The battery icon is 4 x 7 px: its 2 x 5 px interior fills from the bottom
 * in six steps (0-20, 21-40, 41-60, 61-80, 81-99, 100 %). While charging, its
 * walls rise from the floor in G360DISPLAY_CHARGE_FRAMES frames; the caller
 * picks the frame from the clock and redraws when it changes. */
#define G360DISPLAY_CHARGE_FRAMES 6
#define G360DISPLAY_WIFI_FRAMES 3

typedef struct g360display_status {
    int wifi;     /* G360DISPLAY_WIFI_* */
    int battery;  /* 0-100 percent; -1 unknown: no battery icon */
    int charging; /* nonzero: the battery's walls animate (see charge_frame) */
    int mode;     /* G360DISPLAY_MODE_* */
    int timer;    /* nonzero: a self-timer is set; a watch face at 3 o'clock, left of the mode icon */
    int charge_frame; /* 0 .. G360DISPLAY_CHARGE_FRAMES-1: the charging animation's frame, used while charging */
    int raw;      /* nonzero: RAW is on; a capital R, left of the timer icon */
    int sound;    /* nonzero: sounds are on; a pair of notes, leftmost */
    int wifi_searching; /* scanning or connecting, including while AP is up */
    int wifi_frame; /* 0..2: dot, dot and inner arc, full fan */
    int wifi_error; /* failure when there is no usable network */
    int ap_connected; /* at least one client on the access point */
} g360display_status;

#ifdef __cplusplus
extern "C" {
#endif

/* Center text in a cleared 72x32 bitmap, one byte (0/1) per pixel: up to
 * three lines separated by '\n', each centred, the block centred vertically
 * (a single line sits where it always did). Supports 0-9, A-Z, space,
 * slash, colon, hyphen, period, percent and plus, at most 12 per line. Empty text
 * makes a blank bitmap. Returns 0, or -1 for invalid arguments/text.
 * On failure pixels is unchanged. No allocation or device access. */
int g360display_render_center(const char *text, unsigned char pixels[G360DISPLAY_PIXELS]);

/* The same with the status corner: rows 0-6, right-aligned, the Wi-Fi icon
 * and the battery icon. Up to two lines of text are centred below it in rows 9-31
 * (one line at row 17, two at rows 12 and 22). A single line of digits only
 * (at most six) is drawn double size (10 x 14 px a digit, rows 13-26). Returns 0, or -1 for invalid
 * arguments, text (including a third line) or status (wifi not one of the
 * three, battery outside -1..100, or a frame outside its range). On failure pixels is unchanged. */
int g360display_render_status(const char *text, const g360display_status *status,
                              unsigned char pixels[G360DISPLAY_PIXELS]);

/* Render then issue one full-frame draw and update on /dev/ssd1309-ctrl.
 * No init, power or contrast operation. The caller owns display exclusivity
 * and should normally ensure the stock app has stopped; during handover, stock
 * may redraw over this output. Returns 0 or -1 on any error,
 * including an unexpected positive ioctl result or a failed close. */
int g360display_show(const char *text);
int g360display_show_status(const char *text, const g360display_status *status);
int g360display_clear(void);

#ifdef __cplusplus
}
#endif
#endif
