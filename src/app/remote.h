/* The remote control: the web page (src/app/web/remote.html) talks to the
 * running app through a Unix socket.
 *
 * The page's CGI runs `g360-app --remote QUERY`, which connects to
 * G360_REMOTE_SOCKET, sends QUERY and one newline, and prints the app's
 * one-line JSON reply (g360_remote_call). In the app a thread accepts one
 * connection at a time (g360_remote_serve_one) and hands the request to a
 * handler, which passes it to the UI thread; the UI thread answers it with
 * the same code the buttons use.
 *
 * QUERY is a CGI query string (g360_remote_parse):
 *   cmd=state                     the app's state, settings and screen
 *   cmd=set&KEY=VALUE             one setting: mode=single|hdr,
 *                                 output=raw|jpeg|raw%2Bjpeg,
 *                                 save=flash|card,
 *                                 delay=0|2|5|10, sounds=on|off
 *                                 and, for single stills only (kept in
 *                                 memory, back to auto after a sweep or when
 *                                 the mode becomes HDR): shutter=auto|TV (one of
 *                                 G360_SHUTTER_TVS, 41-218), iso=100|200|400|800|
 *                                 1600 (with a set shutter only)
 *                                 (not Wi-Fi: the state shows it, only the
 *                                 camera's Menu changes it)
 *   cmd=capture[&sweep=1]         a Capture click (sweep=1: the hold)
 *   cmd=cancel                    cancels a self-timer countdown
 *   cmd=auto&every=N              auto photos every N s (1-86400); 0 stops
 *   cmd=preview&on=1|0            the live preview (preview.h): watch / stop
 *   cmd=stream                    (the CGI's own, not sent to the app) the
 *                                 preview's MJPEG, relayed from its port
 * No capture/SDK calls. */
#ifndef G360_APP_REMOTE_H
#define G360_APP_REMOTE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef G360_REMOTE_SOCKET
#define G360_REMOTE_SOCKET "/tmp/g360-app.sock"
#endif
#define G360_REMOTE_REQUEST_MAX 256u
#define G360_REMOTE_REPLY_MAX 2048u
#define G360_REMOTE_AUTO_MAX 86400u

enum g360_remote_command {
    G360_REMOTE_BAD, G360_REMOTE_STATE, G360_REMOTE_SET, G360_REMOTE_CAPTURE, G360_REMOTE_CANCEL, G360_REMOTE_AUTO,
    G360_REMOTE_PREVIEW
};
/* The settings in the camera's menu order (main.c's ITEM_*). The remote
   never sets G360_REMOTE_WIFI; it keeps the numbering. */
enum g360_remote_setting {
    G360_REMOTE_MODE, G360_REMOTE_OUTPUT, G360_REMOTE_SAVE, G360_REMOTE_WIFI,
    G360_REMOTE_DELAY, G360_REMOTE_SOUNDS,
    /* Remote only, never stored on the card or shown on the camera: the
       exposure of single stills. SHUTTER: 0 automatic, else the TV of one
       of G360_SHUTTER_TVS (the exposure is 2^(-TV/16) s). ISO: 100, 200, 400, 800 or
       1600, only with a set shutter (an automatic shutter means automatic
       ISO too). */
    G360_REMOTE_SHUTTER, G360_REMOTE_ISO
};

/* The HDR sweep, 7 frames at ISO 100 over the camera's full usable shutter
   range: the two ends, then familiar speeds 2 EV apart down from the
   fastest (TV 32 apart; 31 between 1/13 and 1/50, and the slowest step
   short, 1.1 EV): 1/6, 1/13, 1/50, 1/200, 1/800, 1/3200, 1/13000, the
   same TVs as those G360_SHUTTER_TVS entries. Measured (trials L1, F1,
   ENDS1, ENDS2, RANGE14): no still exposes longer than about 166 ms, so TV
   41 (1/5.9 s requested) is 1/6 s; TV 218 (1/12,634 s) is the shortest real
   exposure. The Exif has the real times (g360_exif_exposure_from_tv):
   1/6.0, 1/12.9, 1/49.4, 1/197.6, 1/792.7, 1/3209 and 1/13,486 s. */
#define G360_SWEEP_COUNT 7u
#define G360_SWEEP_TVS {41, 59, 90, 122, 154, 186, 218}

/* The remote's shutter choices: the familiar third-stop speeds over the same
   range, each the TV nearest its name (round(16 log2 N), within 2%), named
   as the firmware names them. 1/6 and 1/13000 are the two ends above. */
#define G360_SHUTTER_COUNT 34u
#define G360_SHUTTER_TVS {41, 48, 53, 59, 63, 69, 74, 79, 85, 90, 95, 101, 106, 111, 117, 122, 127, 133, 138, \
                          143, 149, 154, 159, 165, 170, 175, 181, 186, 191, 197, 202, 207, 213, 218}
#define G360_SHUTTER_NAMES {"1/6", "1/8", "1/10", "1/13", "1/15", "1/20", "1/25", "1/30", "1/40", "1/50", "1/60", \
                            "1/80", "1/100", "1/125", "1/160", "1/200", "1/250", "1/320", "1/400", "1/500", "1/640", \
                            "1/800", "1/1000", "1/1250", "1/1600", "1/2000", "1/2500", "1/3200", "1/4000", "1/5000", \
                            "1/6400", "1/8000", "1/10000", "1/13000"}

typedef struct g360_remote_request {
    int command;     /* G360_REMOTE_* command */
    int setting;     /* SET: G360_REMOTE_MODE ... G360_REMOTE_ISO, never WIFI */
    uint32_t value;  /* SET: the value as settings.h stores it; AUTO: seconds (0 stops); PREVIEW: 1 on, 0 off */
    int sweep;       /* CAPTURE: 1 for a sweep (the one-second hold) */
} g360_remote_request;

/* Parses QUERY. Returns 0, or -1 (command G360_REMOTE_BAD) for anything
 * unknown, repeated, missing or out of range. */
int g360_remote_parse(const char *query, g360_remote_request *request);

/* The next auto photo's press, with the self-timer overlapping the
 * interval: the capture should start at TARGET, so the press comes LEAD
 * (the self-timer) earlier. Returns 1 when NOW is that time or later. */
int g360_remote_auto_due(uint64_t now, uint64_t target, uint64_t lead);
/* After an auto press at NOW: when the next capture should start, EVERY
 * after this one's (NOW + LEAD). A self-timer longer than the interval wins:
 * the next press is due before this capture even starts, so it comes as
 * soon as the app is ready again. */
uint64_t g360_remote_auto_next(uint64_t now, uint64_t lead, uint64_t every);

/* Answers one request: writes a NUL-terminated reply into REPLY. */
typedef void (*g360_remote_handler)(void *context, const char *request, char *reply, size_t size);

/* Listens on PATH (an old socket file there is removed first). Returns the
 * descriptor (close-on-exec), or -1. */
int g360_remote_listen(const char *path);
/* Accepts one connection, reads its request (one line, within 1 s), answers
 * it with HANDLER and closes it. Returns 0, or -1 if accept failed. */
int g360_remote_serve_one(int listener, g360_remote_handler handler, void *context);
/* The client: sends REQUEST to the app at PATH and reads its one-line reply
 * (without the newline) into REPLY, within TIMEOUT_MS. Returns 0, or -1 when
 * no app answers. */
int g360_remote_call(const char *path, const char *request, char *reply, size_t size, int timeout_ms);

/* The live preview's port (preview.h) and its relay for the CGI
 * (cmd=stream): connects to the app's preview on 127.0.0.1:PORT and copies
 * its MJPEG response, status line and all, to OUT until either side ends.
 * Returns 0, or -1 when nothing could be relayed. */
#ifndef G360_PREVIEW_PORT
#define G360_PREVIEW_PORT 8090
#endif
int g360_remote_stream(int port, int out);
#ifdef __cplusplus
}
#endif
#endif
