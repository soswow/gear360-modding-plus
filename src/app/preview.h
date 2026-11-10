/* The live preview (T20): the camera's movie frames as MJPEG for the web
 * page.
 *
 * The capture worker owns the camera: it starts the camera's movie preview
 * (g360_preview_start) while the preview is wanted and the app is READY,
 * stops it before every still or sweep, and starts it again afterwards.
 * Frames reach g360_app_preview_frame on the capture library's thread; the
 * module copies one at a time, half size, only when its encoder asks.
 *
 * The encoder thread turns the latest copy into a 640x320 JPEG with the
 * camera's libturbojpeg (at most G360_PREVIEW_FPS a second) while someone
 * watches. The listener serves them as multipart MJPEG on TCP port
 * G360_PREVIEW_PORT (http://<camera>:8090/preview.mjpg) to at most
 * G360_PREVIEW_VIEWERS viewers at once; all share the same frames.
 *
 * The page asks for the preview (cmd=preview&on=1, remote.h); it stops on
 * cmd=preview&on=0 or G360_PREVIEW_IDLE_MS after the last viewer left (or
 * after asking, if nobody connects). */
#ifndef G360_APP_PREVIEW_H
#define G360_APP_PREVIEW_H
#include <stddef.h>
#include <stdint.h>
#include "g360cap.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef G360_PREVIEW_PORT
#define G360_PREVIEW_PORT 8090
#endif
#define G360_PREVIEW_VIEWERS 4u
#define G360_PREVIEW_FPS 8u
#ifndef G360_PREVIEW_IDLE_MS
#define G360_PREVIEW_IDLE_MS 20000u
#endif
#ifndef G360_TURBOJPEG_LIBRARY
#define G360_TURBOJPEG_LIBRARY "/usr/lib/libturbojpeg.so"
#endif
/* The camera frames asked for; the JPEGs are half that. */
#define G360_PREVIEW_WIDTH 1280u
#define G360_PREVIEW_HEIGHT 640u
#define G360_PREVIEW_FPS_X10 150u
#define G360_PREVIEW_QUALITY 70
#define G360_PREVIEW_JPEG_MAX ((size_t)262144u)

/* What the page sees (the state's "preview" field). */
enum g360_preview_state {
    G360_PREVIEW_OFF,      /* not asked for */
    G360_PREVIEW_STARTING, /* asked for, the camera's preview not running yet */
    G360_PREVIEW_ON,       /* frames coming */
    G360_PREVIEW_PAUSED    /* asked for, stopped for a capture */
};

/* Starts the listener and the encoder threads. Returns 0, or -1 when the
 * port can't be opened (the preview is then unavailable). */
int g360_app_preview_init(int port);
/* The page's request: 1 to watch, 0 to stop. */
void g360_app_preview_want(int on);
int g360_app_preview_wanted(void);
/* The capture worker reports whether the camera's preview runs (and why it
 * doesn't: 1 while a capture has it stopped). */
void g360_app_preview_running(int running, int capturing);
/* The capture library's frame callback (g360_preview_options.callback). */
void g360_app_preview_frame(const g360_preview_frame *frame, void *context);
int g360_app_preview_state(void);
uint32_t g360_app_preview_viewers(void);
uint32_t g360_app_preview_encoded(void);
/* Ends the threads' work (viewers are disconnected). */
void g360_app_preview_shutdown(void);

/* Exposed for tests: NV12 (Y rows, then Cb Cr rows) at half size to RGB.
 * Y holds every other row of the frame, each WIDTH bytes; C holds the
 * chroma rows, each WIDTH bytes (WIDTH/2 Cb Cr pairs). The output is
 * (WIDTH/2) x ROWS pixels, ROWS = the frame's height / 2. */
void g360_app_preview_half_rgb(const unsigned char *y, const unsigned char *c, uint32_t width,
                               uint32_t rows, unsigned char *rgb);
/* The part header written before each JPEG. */
int g360_app_preview_part_header(char *out, size_t size, size_t jpeg_bytes);
#ifdef __cplusplus
}
#endif
#endif
