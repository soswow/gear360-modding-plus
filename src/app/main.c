/* Minimal physical camera app. Capture + saving run on one worker; UI stays
 * responsive and discards busy presses. All capture API calls stay on that
 * worker. Launch only through start.sh, after verified stock-app shutdown.
 * In single mode a Capture click takes one still and a one-second hold an
 * HDR sweep; in HDR mode a click takes the sweep. A sweep is up to 7
 * exposures at ISO 100, fastest first; each RAW frame is scanned in memory
 * and the sweep stops early when the slower frames would add nothing
 * (scan.h). Frames are saved to the built-in flash first (then moved to the card) or
 * straight to the card. A Menu press opens the settings (mode, output,
 * save, ...). OUTPUT selects RAW (DNG), JPEG, or both for stills and every
 * sweep frame. JPEG-only sweeps go straight to the card. After each
 * sweep the flash's own wear estimate is read; from 30-40% used on, the
 * screen warns instead of READY. The screen's top-right corner shows Wi-Fi,
 * battery and charging, read every 2 s (status.h); while a charger is in,
 * the battery icon's walls animate, redrawn every G360_CHARGE_FRAME_MS. The
 * side LED shows the charge: green when full on a cable, yellow while
 * charging, off otherwise. The web page (remote.h) asks the UI thread over
 * a socket; it changes settings and presses Capture with the buttons' own
 * code, and runs auto photos (a Capture press every N seconds). It can also
 * show a live preview (preview.h): the worker runs the camera's movie
 * preview while it is wanted and stops it before every still or sweep. */
#include "app_platform.h"
#include <stdatomic.h> /* the compiler's own header: no C library needed */
#include "storage.h"
#include "queue.h"
#include "wear.h"
#include "settings.h"
#include "status.h"
#include "alerts.h"
#include "wifi.h"
#include "remote.h"
#include "preview.h"
#include "scan.h"
#include "g360cap.h"
#include "g360_output.h"
#include "g360_dng.h"
#include "g360ctl.h"
#include "g360display.h"
#include "power.h"
#include "motion.h"
#include "g360_pose.h"
#ifdef G360_QUEUE_FRAME_BYTES_DEFAULT
/* A queued frame file is one full DNG: header, samples and calibration trailer. */
_Static_assert(G360_QUEUE_FRAME_BYTES == G360_DNG_FULL_FILE_BYTES, "queue frame size is not the DNG size");
#endif

#define SPACE_RESERVE UINT64_C(134217728)
#define FRAME_BYTES UINT64_C(33732608)
/* A JPEG must fit its full allowance; remaining-photo counts use a
   measured typical 6 MiB plus Exif and allocation slack. */
#define JPEG_BUDGET ((uint64_t)G360_MAX_IMAGE_BYTES + G360_EXIF_APP1_MAX_SIZE)
#define JPEG_TYPICAL (UINT64_C(6291456) + G360_EXIF_APP1_MAX_SIZE + 4096u)
/* The sweep: the camera's full shutter range at ISO 100, up to 7 frames,
   fastest first, 1/13000 s to 1/6 s, 2 EV apart from the fastest (remote.h,
   G360_SWEEP_TVS). Each RAW frame is scanned in memory (scan.h); the sweep
   stops early when the scan says the slower frames add nothing. */
#define SWEEP_FRAMES G360_SWEEP_COUNT
#define SWEEP_ISO 100u
static const int32_t sweep_tv[SWEEP_FRAMES] = G360_SWEEP_TVS;
#define SWEEP_HOLD_MS 1000u
/* A sweep's frames on the flash plus a 64 MiB margin; the card budget per
   frame adds cluster slack and its line in the exposure list. */
#define QUEUE_BUDGET ((uint64_t)SWEEP_FRAMES * G360_QUEUE_FRAME_BYTES + UINT64_C(67108864))
#define FRAME_CARD_BUDGET (G360_QUEUE_FRAME_BYTES + UINT64_C(32768))
#define SOUND(name) G360CTL_SOUND_DIR "/" name
/* How often the status corner is read, and where its files are; tests
   build these differently. */
/* How long each frame of the charging animation lasts (six frames). */
#ifndef G360_CHARGE_FRAME_MS
#define G360_CHARGE_FRAME_MS 300u
#endif
#ifndef G360_STATUS_EVERY_MS
#define G360_STATUS_EVERY_MS 2000u
#endif
#ifndef G360_STATUS_ROOT
#define G360_STATUS_ROOT ""
#endif
/* Where the motion history and its position are read (motion.h). */
#ifndef G360_MOTION_HISTORY
#define G360_MOTION_HISTORY "/dev/mem"
#endif
#ifndef G360_MOTION_HISTORY_OFFSET
#define G360_MOTION_HISTORY_OFFSET UINT64_C(0x9cd00000)
#endif
#ifndef G360_MOTION_POSITION
#define G360_MOTION_POSITION "/dev/accel"
#endif

enum app_state { STARTING, READY, CAPTURING, APP_ERROR, STOPPED, SWEEPING, SAVING };
/* Values the UI, worker and alert threads share, read with get() and written
   with set(). The signal handler writes `stopping`, which is safe only if
   these atomics need no lock. */
typedef _Atomic uint32_t shared_u32;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "the shared values must be lock-free atomics");
static shared_u32 state = STARTING, displayed_state = STARTING, stopping, saved_count, power_off;
static shared_u32 sweep_ready, sweep_frame, sweep_total = SWEEP_FRAMES, saving_done, saving_total, wear_warning;
/* The settings the worker uses, set by the UI thread (the settings screen). */
static shared_u32 setting_mode = G360_MODE_SINGLE, setting_save = G360_SAVE_FLASH, setting_delay, setting_sounds = 1,
                  setting_output = G360_OUTPUT_BOTH;
/* The remote's exposure for single stills, kept in memory only (nothing on
   the card, nothing on the camera; a restart means automatic): the shutter
   is the TV exposure_shutter (0 automatic, else one of G360_SHUTTER_TVS) and the ISO is exposure_iso (0
   automatic, which is what an automatic shutter always has; else 100 to
   1600). A sweep or the HDR mode puts both back to automatic. */
static shared_u32 exposure_shutter, exposure_iso;
/* Sweeps: whether they stop early (remote's stop=auto|full, in memory, auto
   after a restart); the next or current sweep's ND lenses (G360_SCAN_ND_*
   bits, 0 a plain sweep, set by its press); the scan file's revision (the
   page fetches it when this changes) and frame count. */
static shared_u32 sweep_stop = 1, sweep_nd, scan_rev, scan_frames;
/* The ISO a set shutter starts with: the one the September scripts verified. */
#define MANUAL_SHUTTER_ISO 100u
/* The card budget left, in KiB (rounded down), set by the worker. */
static shared_u32 budget_kib;
static int worker_failed;
/* The UI thread's own: the self-timer's countdown (when it ends, 0 when
   none runs; whether a sweep follows; the last second sounded; whether auto
   photos started it), and auto photos (every AUTO_EVERY seconds, 0 off; the
   next capture is due to start at auto_target; auto_taken presses so far;
   auto_warned: the target the 2 s warning sound was played for). */
static uint64_t countdown_until, auto_target, auto_warned;
static int countdown_sweep, countdown_last, countdown_auto;
static uint32_t auto_every, auto_taken;

typedef struct app_config {
    const char *root, *log, *device, *queue, *wear_dir, *status_root;
    uint64_t free_bytes, queue_free_bytes;
    int once; /* 1: one still, 2: one sweep (maintenance) */
    g360_motion_source motion;
} app_config;

typedef struct worker {
    app_config *config;
    g360_storage storage;
    g360_queue queue;
    uint64_t remaining;
    int capture_open, no_space, service_failed;
} worker;

static uint32_t get(shared_u32 *value) { return atomic_load(value); }
static void set(shared_u32 *value, uint32_t n) { atomic_store(value, n); }
/* Moves the app to state `to` only if it is still in state `from`. */
static void change_state(uint32_t from, uint32_t to) { (void)atomic_compare_exchange_strong(&state, &from, to); }
/* Plays a sound to the end, unless the Sounds setting is OFF (then nothing
   plays and it counts as played). */
static int app_sound_play(const char *file)
{
    if (!get(&setting_sounds)) return 0;
    return g360ctl_sound_play(G360CTL_PAPLAY, file);
}
/* The sweep's per-frame sound plays in the background: waiting for paplay
   held every frame up by 0.3-0.9 s (T24). Worker thread only. */
static int shot_sound_pid;
static void shot_sound_end(void)
{
    if (shot_sound_pid > 0 && !g360ctl_sound_reap(shot_sound_pid)) g360ctl_sound_stop(shot_sound_pid);
    shot_sound_pid = 0;
}
static int shot_sound_start(const char *file)
{
    if (!get(&setting_sounds)) return 0;
    shot_sound_end();
    shot_sound_pid = g360ctl_sound_start(G360CTL_PAPLAY, file);
    if (shot_sound_pid > 0) return 0;
    shot_sound_pid = 0;
    return -1;
}
static void stop(int number) { (void)number; set(&stopping, 1); }
static void pause_ms(unsigned ms)
{
    capture_timespec t;
    t.tv_sec = ms / 1000; t.tv_nsec = (long)(ms % 1000) * 1000000L;
    (void)nanosleep(&t, 0);
}
static uint64_t now_ms(void)
{
    capture_timespec t;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)(t.tv_nsec / 1000000L);
}
/* ---- Shot LEDs ---------------------------------------------------------- */

/* At the start of every shot (before its sound, so on the press) the two LEDs
   above the lenses go from green to orange for LED_FLASH_MS, then back as they
   were. A thread does the waiting, so the shot is not held up. LED trouble
   never fails a capture. */
#define LED_FLASH_MS 100u
static pthread_t flash_thread;
static int flash_running;

static void *flash_main(void *unused)
{
    g360ctl_led_state saved[G360CTL_MAX_LEDS];
    int count = g360ctl_leds_read(0, saved, G360CTL_MAX_LEDS), k;
    (void)unused;
    (void)g360ctl_led_set(0, G360CTL_LED_FRONT_GREEN, 0);
    (void)g360ctl_led_set(0, G360CTL_LED_BACK_GREEN, 0);
    (void)g360ctl_led_set(0, G360CTL_LED_FRONT_ORANGE, 1);
    (void)g360ctl_led_set(0, G360CTL_LED_BACK_ORANGE, 1);
    pause_ms(LED_FLASH_MS);
    /* The side LEDs belong to the charge light, which may change meanwhile:
       put back only the four this flash touched. */
    for (k = 0; k < 3 && k < count; ++k) saved[k].present = 0;
    if (count > 0) (void)g360ctl_leds_restore(0, saved, count);
    return 0;
}

static void shot_flash(void)
{
    if (flash_running) { (void)pthread_join(flash_thread, 0); flash_running = 0; }
    flash_running = pthread_create(&flash_thread, 0, flash_main, 0) == 0;
}

static void fail(const char *operation, const char *detail)
{
    printf("APP ERROR %s: %s\n", operation, detail); (void)fflush(stdout);
    worker_failed = 1; set(&state, APP_ERROR);
}
static void publish_budget(const worker *w)
{
    uint64_t kib = w->remaining >> 10;
    set(&budget_kib, kib > UINT32_MAX ? UINT32_MAX : (uint32_t)kib);
}
static void spend(worker *w, uint64_t bytes)
{
    w->remaining = w->remaining > bytes ? w->remaining - bytes : 0;
    if (w->capture_open) g360_set_free_bytes(w->remaining);
    publish_budget(w);
}

/* ---- Moving queued sweeps from the flash to the card ------------------- */

static int move_before(void *context, unsigned done, unsigned total, uint64_t bytes)
{
    worker *w = context;
    set(&saving_total, total);
    set(&saving_done, done);
    if (get(&stopping)) return 1;
    if (w->remaining < SPACE_RESERVE + bytes + 32768u) { w->no_space = 1; return 1; }
    /* The capture session is kept serviced between files. */
    if (w->capture_open && g360_service(0) < 0) { w->service_failed = 1; return 1; }
    return 0;
}
static void move_after(void *context, const char *stem, uint64_t bytes)
{
    worker *w = context;
    spend(w, bytes + 16384u);
    printf("APP SAVED %s (from flash)\n", stem); (void)fflush(stdout);
}
/* Moves every queued sweep, oldest first. 0 done, 1 stopped, -1 failed. */
static int move_queue(worker *w)
{
    g360_queue_hooks hooks;
    unsigned rounds;
    hooks.context = w; hooks.before = move_before; hooks.after = move_after;
    for (rounds = 0; rounds < 1000u; ++rounds) {
        char stamp[16];
        uint64_t written = 0;
        unsigned moved = 0, dropped = 0;
        int found = g360_queue_oldest(&w->queue, stamp), status;
        if (found == 0) return 0;
        if (found < 0) { fail("queue", "cannot list the flash queue"); return -1; }
        set(&saving_done, 0); set(&saving_total, 0);
        set(&state, SAVING);
        status = g360_queue_move(&w->queue, stamp, &w->storage, &hooks, &written, &moved, &dropped);
        printf("APP SWEEP MOVED %s: %u frame(s) to the card, %u incomplete dropped, status %s\n",
               stamp, moved, dropped, g360_queue_status_text(status));
        (void)fflush(stdout);
        if (status == G360_QUEUE_STOPPED) {
            if (w->no_space) { fail("card space", "insufficient reserved space"); return -1; }
            if (w->service_failed) { fail("capture service", "service failed while saving"); return -1; }
            return 1;
        }
        if (status != G360_QUEUE_OK) { fail("move to card", g360_queue_status_text(status)); return -1; }
    }
    fail("queue", "too many queued sweeps");
    return -1;
}

/* ---- The camera's pose around a shot ------------------------------------ */

/* The pose window of one capture: the history's write position when the
   capture starts, and, when it returns, the motion between the two (at least
   a quarter second) averaged into an XMP packet for the files. A shot never
   fails for lack of a pose: without one the files just have no XMP. */
typedef struct pose_window {
    g360_motion motion;
    uint32_t start;
    int open;
} pose_window;

static void pose_begin(const worker *w, pose_window *pose)
{
    memset(pose, 0, sizeof *pose);
    if (g360_motion_open(&pose->motion, &w->config->motion) != 0) return;
    pose->open = 1;
    if (g360_motion_position(&pose->motion, &pose->start) != 0) { g360_motion_close(&pose->motion); pose->open = 0; }
}

static double log_angle(double v) { return v > -0.05 && v < 0.05 ? 0.0 : v; } /* no "-0.0" */

/* Fills xmp and returns it, or returns NULL (no pose). Closes the window. */
static const char *pose_end(pose_window *pose, char xmp[G360_POSE_XMP_MAX])
{
    g360_pose_stats stats;
    g360_pose result;
    uint32_t stop, used = 0;
    size_t size = 0;
    const char *text = 0;
    if (!pose->open) { printf("APP POSE none: motion history unavailable\n"); (void)fflush(stdout); return 0; }
    g360_pose_stats_init(&stats);
    if (g360_motion_position(&pose->motion, &stop) == 0) used = g360_motion_window(&pose->motion, pose->start, stop, &stats);
    g360_motion_close(&pose->motion);
    pose->open = 0;
    if (used >= G360_MOTION_MIN_WINDOW / 2u && g360_pose_from_stats(&stats, &result) == 0 &&
        g360_pose_xmp(&result, xmp, G360_POSE_XMP_MAX, &size) == 0) {
        printf("APP POSE %u samples pitch %.1f roll %.1f up %.3f %.3f %.3f gyro %.1f dps\n", (unsigned)result.samples,
               log_angle(result.pitch_deg), log_angle(result.roll_deg), result.up[0], result.up[1], result.up[2], result.gyro_rms_dps);
        text = xmp;
    } else {
        printf("APP POSE none: %u usable samples\n", (unsigned)used);
    }
    (void)fflush(stdout);
    return text;
}

/* ---- One still ---------------------------------------------------------- */

/* The same output policy applies to stills and sweeps. */
static int output_raw(uint32_t output) { return output != G360_OUTPUT_JPEG; }
static int output_jpeg(uint32_t output) { return output != G360_OUTPUT_RAW; }
static const char *output_text(uint32_t output)
{
    return output == G360_OUTPUT_RAW ? "RAW" : output == G360_OUTPUT_JPEG ? "JPEG" : "RAW+JPEG";
}
static const char *output_wire(uint32_t output)
{
    return output == G360_OUTPUT_RAW ? "raw" : output == G360_OUTPUT_JPEG ? "jpeg" : "raw+jpeg";
}
static uint64_t single_need(uint32_t output)
{
    return (output_raw(output) ? FRAME_BYTES + G360_DNG_FULL_HEADER_BYTES + G360_DNG_TRAILER_BYTES : 0u) +
           (output_jpeg(output) ? JPEG_BUDGET : 0u);
}
static uint64_t single_typical(uint32_t output)
{
    return (output_raw(output) ? FRAME_BYTES + G360_DNG_FULL_HEADER_BYTES + G360_DNG_TRAILER_BYTES : 0u) +
           (output_jpeg(output) ? JPEG_TYPICAL : 0u);
}

/* The name of the remote's shutter TV, for the log. */
static const char *exposure_name(uint32_t tv)
{
    static const uint32_t tvs[G360_SHUTTER_COUNT] = G360_SHUTTER_TVS;
    static const char *const names[G360_SHUTTER_COUNT] = G360_SHUTTER_NAMES;
    unsigned k;
    for (k = 0; k < G360_SHUTTER_COUNT; ++k) if (tvs[k] == tv) return names[k];
    return "?";
}

/* Before a still: the remote's exposure, if it chose one. Returns 1 when
   the camera was changed (restore_exposure puts it back), 0 when nothing was
   chosen, -1 on failure (reported). *TV receives the shutter TV set (0 for
   none), for the still's real exposure time in Exif. */
static int apply_exposure(uint32_t *tv)
{
    uint32_t shutter = get(&exposure_shutter), iso = get(&exposure_iso);
    int status;
    *tv = 0;
    if (shutter == 0) return 0;
    *tv = shutter;
    status = g360_set_manual_exposure((int32_t)shutter, iso);
    printf("APP EXPOSURE TV %lu (%s s) ISO %lu\n", (unsigned long)shutter, exposure_name(shutter), (unsigned long)iso);
    (void)fflush(stdout);
    if (status != G360_OK) {
        fail("exposure", g360_status_text(status));
        (void)g360_set_auto_exposure();
        return -1;
    }
    return 1;
}

/* After the still (its shot freed): the automatic exposure again. */
static int restore_exposure(void)
{
    int status = g360_set_auto_exposure();
    if (status != G360_OK) fail("automatic settings", g360_status_text(status));
    return status == G360_OK ? 0 : -1;
}

/* One still: reserve and save exactly the selected outputs under one stem. */
static int single_shot(worker *w)
{
    char stem[64], jpg_path[512], dng_path[512], date[20];
    g360_capture_options capture;
    g360_shot shot;
    g360_exif_info exif;
    const g360_still *still;
    const g360_image *jpeg;
    uint64_t written;
    pose_window pose;
    char xmp[G360_POSE_XMP_MAX];
    const char *pose_xmp;
    uint32_t output = get(&setting_output);
    int status, raw = output_raw(output), jpg = output_jpeg(output), exposure;
    uint32_t tv;
    shot_flash();
    if (w->remaining < SPACE_RESERVE + single_need(output)) { fail("card space", "insufficient reserved space"); return -1; }
    status = g360_storage_reserve(&w->storage, stem, jpg ? jpg_path : 0, raw ? dng_path : 0, date);
    if (status != G360_STORAGE_OK) { fail("reserve filename", g360_storage_status_text(status)); return -1; }
    printf("APP CAPTURE %s (%s)\n", stem, output_text(output)); (void)fflush(stdout);
    /* A display failure must not leave an invisible capture running. */
    while (get(&displayed_state) != CAPTURING && !get(&stopping)) pause_ms(1);
    if (get(&stopping)) return 1;
    if (app_sound_play(SOUND("shutter.wav")) != 0) {
        fail("capture sound", "player failed"); return -1;
    }
    if (get(&stopping)) return 1;
    exposure = apply_exposure(&tv);
    if (exposure < 0) return -1;
    memset(&capture, 0, sizeof capture);
    capture.frame = (uint32_t)raw; capture.skip_jpeg_copy = (uint32_t)!jpg; capture.max_sets = 1;
    pose_begin(w, &pose);
    status = g360_capture_shot(&capture, &shot);
    pose_xmp = pose_end(&pose, xmp);
    if (status != G360_OK || shot.set_count != 1) {
        fail("capture", status == G360_OK ? "expected one image set" : g360_status_text(status));
        g360_shot_free(&shot); if (exposure) (void)restore_exposure(); return -1;
    }
    still = &shot.sets[0]; jpeg = &still->images[G360_IMAGE_MAIN];
    g360_exif_from_still(still, jpg ? jpeg : 0, &exif);
    /* A set shutter: the real exposure time, not the firmware's third-stop
       name (g360_exif.h). Automatic exposure keeps the firmware's value. */
    if (tv != 0) g360_exif_exposure_from_tv((int32_t)tv, &exif.exposure_numerator, &exif.exposure_denominator);
    memcpy(exif.datetime, date, sizeof exif.datetime);
    /* The DNG carries the same Exif and IQ MakerNote as the JPEG. */
    status = raw ? g360_save_dng_full_xmp(&shot.frame, &still->maker_note, &exif, 0, pose_xmp, dng_path) : G360_OK;
    if (status == G360_OK && jpg) status = g360_save_jpeg_exif_xmp(jpeg, &still->maker_note, &exif, pose_xmp, jpg_path);
    written = jpg ? jpeg->size + G360_EXIF_APP1_MAX_SIZE + 4096u : 0u;
    if (raw) written += shot.frame.image.size + G360_DNG_FULL_HEADER_BYTES + G360_DNG_TRAILER_BYTES;
    g360_shot_free(&shot);
    if (exposure && restore_exposure() != 0) return -1;
    if (status != G360_OK) { fail("save", g360_status_text(status)); return -1; }
    spend(w, written);
    if (raw && jpg) printf("APP SAVED %s.jpg %s.dng\n", stem, stem);
    else printf("APP SAVED %s.%s\n", stem, raw ? "dng" : "jpg");
    (void)fflush(stdout);
    /* Everything is on the card: the capture is done. */
    if (!get(&stopping) && app_sound_play(SOUND("preview_sound.wav")) != 0)
        printf("APP capture done sound failed\n");
    return 0;
}

/* ---- The live preview (preview.h) ---------------------------------------- */

/* On the worker, at READY: starts the camera's preview when the page wants
   it, stops it when not. A start that fails turns the request off. */
static char preview_error[48];
/* The remote's shutter and ISO as last sent to the camera for the preview
   (0, 0: automatic, the camera's state at READY otherwise). */
static uint32_t preview_shutter, preview_iso;
/* The preview shows the remote's exposure: sent before the preview starts
   and again whenever the remote changes it while the preview runs. */
static void preview_exposure(void)
{
    uint32_t shutter = get(&exposure_shutter), iso = shutter ? get(&exposure_iso) : 0u;
    int status;
    if (shutter == preview_shutter && iso == preview_iso) return;
    status = shutter ? g360_set_manual_exposure((int32_t)shutter, iso) : g360_set_auto_exposure();
    printf("APP PREVIEW exposure %s%s ISO %lu: %s\n", shutter ? exposure_name(shutter) : "auto", shutter ? " s" : "",
           (unsigned long)iso, g360_status_text(status)); (void)fflush(stdout);
    if (status != G360_OK) { (void)g360_set_auto_exposure(); shutter = 0; iso = 0; }
    preview_shutter = shutter; preview_iso = iso;
}
/* Back to the automatic exposure the stills and sweeps start from. */
static void preview_exposure_off(void)
{
    if (preview_shutter == 0) return;
    int status = g360_set_auto_exposure();
    printf("APP PREVIEW exposure back to auto: %s\n", g360_status_text(status)); (void)fflush(stdout);
    preview_shutter = 0; preview_iso = 0;
}
static void preview_sync(int allowed)
{
    int status;
    if (allowed && g360_app_preview_wanted() && g360_preview_running()) preview_exposure();
    if (allowed && g360_app_preview_wanted() && !g360_preview_running()) {
        g360_preview_options options;
        preview_exposure();
        memset(&options, 0, sizeof options);
        options.width = G360_PREVIEW_WIDTH; options.height = G360_PREVIEW_HEIGHT;
        options.fps_x10 = G360_PREVIEW_FPS_X10; options.with_set = 1;
        options.callback = g360_app_preview_frame;
        status = g360_preview_start(&options);
        printf("APP PREVIEW camera start: %s\n", g360_status_text(status)); (void)fflush(stdout);
        if (status != G360_OK) {
            (void)snprintf(preview_error, sizeof preview_error, "%s", g360_status_text(status));
            g360_app_preview_want(0);
        } else {
            preview_error[0] = '\0';
        }
    } else if ((!allowed || !g360_app_preview_wanted()) && g360_preview_running()) {
        status = g360_preview_stop(3000u);
        printf("APP PREVIEW camera stop: %s\n", g360_status_text(status)); (void)fflush(stdout);
    }
    if (!g360_preview_running()) preview_exposure_off();
    g360_app_preview_running(g360_preview_running(), 0);
}

/* On the worker, before a still or sweep: the camera's preview must stop
   first (g360cap refuses a capture while it runs). */
static void preview_pause(void)
{
    if (g360_preview_running()) {
        int status = g360_preview_stop(3000u);
        printf("APP PREVIEW paused for the capture: %s\n", g360_status_text(status)); (void)fflush(stdout);
    }
    preview_exposure_off();
    g360_app_preview_running(0, g360_app_preview_wanted());
}

/* ---- The sweep's RAW scan (scan.h) -------------------------------------- */

/* The last plain sweep's frames, then the ND sweep aimed by it (if any):
   at most 2 x SWEEP_FRAMES. Worker thread only. */
static g360_scan_frame scans[2u * SWEEP_FRAMES];
static unsigned scan_plain, scan_nd; /* frames of each */
static uint8_t scan_target[G360_SCAN_LENSES * G360_SCAN_COLS * G360_SCAN_ROWS];
static uint8_t scan_measured[G360_SCAN_LENSES * G360_SCAN_COLS * G360_SCAN_ROWS];

#if !defined(__APPLE__)
int rename(const char *from, const char *to);
#endif

static uint64_t now_us(void)
{
    capture_timespec t;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)(t.tv_nsec / 1000L);
}

static int write_all(int file, const void *data, size_t size)
{
    const uint8_t *p = data;
    while (size > 0) {
        ptrdiff_t n = write(file, p, size);
        if (n <= 0) return -1;
        p += n; size -= (size_t)n;
    }
    return 0;
}

/* Writes every kept frame to G360_SCAN_FILE (a RAM file system, so the
   rename over the old file is safe there; never on the card) and bumps the
   revision the state shows. */
static void scan_publish(void)
{
    static const char temporary[] = G360_SCAN_FILE ".new";
    uint8_t head[G360_SCAN_FRAME_HEADER > G360_SCAN_FILE_HEADER ? G360_SCAN_FRAME_HEADER : G360_SCAN_FILE_HEADER];
    unsigned k, total = scan_plain + scan_nd;
    int file = open(temporary, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC, 0644), bad;
    if (file < 0) { printf("APP SCAN cannot write %s\n", temporary); (void)fflush(stdout); return; }
    bad = write_all(file, head, g360_scan_file_header(head, total));
    for (k = 0; k < total && !bad; ++k) {
        bad = write_all(file, head, g360_scan_frame_header(head, &scans[k], scan_nd > 0 && k < scan_plain));
        if (!bad) bad = write_all(file, scans[k].cells, sizeof scans[k].cells);
    }
    if (close(file) != 0) bad = 1;
    if (bad || rename(temporary, G360_SCAN_FILE) != 0) {
        printf("APP SCAN cannot write %s\n", G360_SCAN_FILE); (void)fflush(stdout);
        (void)unlink(temporary); return;
    }
    set(&scan_frames, total);
    set(&scan_rev, get(&scan_rev) + 1u);
}

/* ---- The sweep ---------------------------------------------------------- */

/* Reads the flash's wear estimate after a sweep; from G360_WEAR_WARNING on,
   the UI shows a warning instead of READY until the next Capture press. */
static void check_wear(const worker *w)
{
    g360_wear wear;
    int level;
    if (g360_wear_read(w->config->wear_dir, &wear) != 0) {
        printf("APP FLASH LIFE unreadable from %s\n", w->config->wear_dir); (void)fflush(stdout);
        return;
    }
    level = g360_wear_level(&wear);
    printf("APP FLASH LIFE A %d B %d pre-EOL %d%s\n", wear.life_a, wear.life_b, wear.pre_eol,
           level >= G360_WEAR_WARNING ? ": warning" : "");
    (void)fflush(stdout);
    if (level >= G360_WEAR_WARNING) set(&wear_warning, (uint32_t)level);
}

/* RAW budgets every frame; JPEG adds one full allowance plus typical
   remaining frames, backed by the card's 128 MiB reserve. */
static uint64_t sweep_need(unsigned frames, uint32_t output)
{
    return (output_raw(output) ? (uint64_t)frames * FRAME_CARD_BUDGET : 0u) +
           (output_jpeg(output) ? JPEG_BUDGET + (uint64_t)(frames - 1u) * JPEG_TYPICAL : 0u);
}
static uint64_t queue_need(unsigned frames, uint32_t output)
{
    return (uint64_t)frames * (G360_QUEUE_FRAME_BYTES + (output_jpeg(output) ? JPEG_BUDGET + 4096u : 0u)) + UINT64_C(67108864);
}

/* The ND lenses as the log and the DNG description name them. */
static const char *nd_text(uint32_t nd)
{
    return nd == (G360_SCAN_ND_LEFT | G360_SCAN_ND_RIGHT) ? "BOTH" : nd == G360_SCAN_ND_LEFT ? "LEFT"
           : nd == G360_SCAN_ND_RIGHT ? "RIGHT" : "NONE";
}

/* Every selected output is saved before the next exposure. RAW sweeps
   (including RAW+JPEG pairs) use the flash queue when selected and available;
   JPEG-only sweeps go straight to the card. Fastest frame first; with RAW
   each frame is scanned before it is saved, and the sweep stops early when
   the scan allows (scan.h) and sweep_stop is on. An ND sweep (sweep_nd)
   aims at the blocks the last plain sweep's fastest frame clipped. */
static int sweep(worker *w)
{
    g360_queue_info info[SWEEP_FRAMES];
    char stamp[16], path[512], jpg_path[512], stem[64], description[80], date[20];
    unsigned k, done = 0, frames = SWEEP_FRAMES;
    uint32_t output = get(&setting_output), nd = get(&sweep_nd), stop_early = get(&sweep_stop);
    int status, failed = 0, restore, raw = output_raw(output), jpg = output_jpeg(output);
    int direct = !raw || get(&setting_save) == G360_SAVE_CARD;
    uint64_t started = 0, still_until = 0, moving;
    /* A sweep sets every exposure itself and ends on the automatic one. */
    set(&exposure_shutter, 0); set(&exposure_iso, 0);
    if (!direct && (!get(&sweep_ready) || w->config->queue_free_bytes < queue_need(frames, output))) {
        printf("APP SWEEP the flash queue is unavailable: saving straight to the card\n");
        direct = 1;
    }
    if (w->remaining < SPACE_RESERVE + sweep_need(frames, output)) {
        fail("card space", "insufficient reserved space for a sweep"); return -1;
    }
    status = g360_storage_timestamp(stamp);
    if (status != G360_STORAGE_OK) { fail("sweep clock", g360_storage_status_text(status)); return -1; }
    printf("APP SWEEP %s: up to %u frames fastest first, %s, %s, ND %s, %s\n", stamp, frames, output_text(output),
           direct ? "straight to the card" : "flash first", nd_text(nd), stop_early ? "early stop" : "every frame");
    if (nd) {
        /* The plain sweep's fastest frame aims the ND sweep; without one,
           every block of the covered lenses is a target. */
        const g360_scan_frame *reference = scan_plain > 0 && scans[0].index == 1 && scans[0].nd == 0 ? &scans[0] : 0;
        uint32_t targets = g360_scan_targets(reference, nd, scan_target);
        memset(scan_measured, 0, sizeof scan_measured);
        scan_nd = 0;
        printf("APP SWEEP ND targets: %lu blocks %s\n", (unsigned long)targets,
               reference ? "clipped in the last plain sweep's fastest frame" : "(no plain sweep scanned: all)");
    } else {
        scan_plain = 0; scan_nd = 0;
    }
    (void)fflush(stdout);
    shot_flash(); /* the first frame's flash, on the press */
    while (get(&displayed_state) != SWEEPING && !get(&stopping)) pause_ms(1);
    if (get(&stopping)) return 1;
    if (app_sound_play(SOUND("hdr_start.wav")) != 0) {
        fail("sweep sound", "player failed"); return -1;
    }
    for (k = 0; k < frames && !get(&stopping); ++k) {
        g360_capture_options capture;
        g360_shot shot;
        g360_exif_info exif;
        g360_queue_info *row = &info[done];
        pose_window pose;
        char xmp[G360_POSE_XMP_MAX];
        const char *pose_xmp;
        const g360_image *jpeg;
        uint64_t line_bytes = 0, frame_bytes;
        int queued = G360_QUEUE_OK, reserved = G360_STORAGE_OK, last;
        if (k > 0) shot_flash();
        set(&sweep_frame, k + 1u);
        status = g360_set_manual_exposure(sweep_tv[k], SWEEP_ISO);
        if (status != G360_OK) { fail("sweep exposure", g360_status_text(status)); failed = 1; break; }
        memset(&capture, 0, sizeof capture);
        capture.max_sets = 1;
        /* Copy only the outputs requested, from this one exposure. */
        capture.frame = (uint32_t)raw; capture.skip_jpeg_copy = (uint32_t)!jpg;
        if (k == 0) started = now_ms();
        if (shot_sound_start(SOUND("07_N_AF.wav")) != 0) printf("APP sweep shot sound failed\n");
        pose_begin(w, &pose);
        status = g360_capture_shot(&capture, &shot);
        pose_xmp = pose_end(&pose, xmp);
        still_until = now_ms();
        if (status != G360_OK || shot.set_count != 1) {
            fail("sweep capture", status == G360_OK ? "expected one image set" : g360_status_text(status));
            g360_shot_free(&shot); failed = 1; break;
        }
        jpeg = &shot.sets[0].images[G360_IMAGE_MAIN];
        g360_exif_from_still(&shot.sets[0], jpg ? jpeg : 0, &exif);
        /* The real exposure time, not the firmware's third-stop name. */
        g360_exif_exposure_from_tv(sweep_tv[k], &exif.exposure_numerator, &exif.exposure_denominator);
        last = k + 1u == frames;
        /* The RAW scan, while the frame is in memory: whether slower frames
           would add anything. */
        if (raw && scan_plain + scan_nd < 2u * SWEEP_FRAMES) {
            g360_scan_frame *f = &scans[scan_plain + scan_nd];
            const g360_image *image = &shot.frame.image;
            uint64_t scan_started = now_us();
            if (g360_scan_raw(image->data, image->size, image->width, image->height, image->stride_x, f) == 0) {
                int enough;
                f->scan_us = (uint32_t)(now_us() - scan_started);
                memcpy(f->stamp, stamp, sizeof f->stamp);
                f->index = k + 1u; f->nd = nd; f->tv = sweep_tv[k];
                f->exposure_numerator = exif.exposure_numerator; f->exposure_denominator = exif.exposure_denominator;
                f->still_clipped = 0;
                enough = nd ? g360_scan_nd_done(f, k + 1u, scan_target, scan_measured) : g360_scan_plain_done(f, k + 1u);
                if (enough && stop_early) last = 1;
                f->last = (uint32_t)last;
                if (nd) ++scan_nd; else ++scan_plain;
                printf("APP SCAN frame %u TV %ld: %lu us; left %lu quads %lu clipped %lu dark; right %lu quads %lu clipped "
                       "%lu dark%s%s%s\n", k + 1u, (long)sweep_tv[k], (unsigned long)f->scan_us,
                       (unsigned long)f->quads[0], (unsigned long)f->clipped[0], (unsigned long)f->dark[0],
                       (unsigned long)f->quads[1], (unsigned long)f->clipped[1], (unsigned long)f->dark[1],
                       nd && f->still_clipped ? "; targets still clipped: " : "",
                       nd && f->still_clipped ? "yes" : "", enough ? (stop_early ? "; enough, stopping" : "; enough") : "");
                (void)fflush(stdout);
                scan_publish();
            } else {
                printf("APP SCAN frame %u: not a full RAW frame, not scanned\n", k + 1u); (void)fflush(stdout);
            }
        }
        /* The last frame is in memory: the camera can move now, before the
           frame is written out. */
        if (last) {
            shot_sound_end();
            if (app_sound_play(SOUND("hdr_end.wav")) != 0) printf("APP sweep end sound failed\n");
        }
        /* All frames carry the sweep's start time and say where they belong. */
        (void)snprintf(exif.datetime, sizeof exif.datetime, "%.4s:%.2s:%.2s %.2s:%.2s:%.2s",
                       stamp, stamp + 4, stamp + 6, stamp + 9, stamp + 11, stamp + 13);
        if (nd) (void)snprintf(description, sizeof description, "HDR SWEEP %s FRAME %u ND %s", stamp, k + 1u, nd_text(nd));
        else (void)snprintf(description, sizeof description, "HDR SWEEP %s FRAME %u", stamp, k + 1u);
        row->frame = k + 1u; row->tv = sweep_tv[k]; row->iso = SWEEP_ISO;
        row->exposure_numerator = exif.exposure_numerator;
        row->exposure_denominator = exif.exposure_denominator;
        row->actual_iso = exif.iso;
        /* The exposure goes to the queue's list now, before the frame is
           written, so a crash during the write still leaves it for the move. */
        if (!direct) {
            int listed = g360_queue_append_info(&w->queue, stamp, row);
            if (listed != G360_QUEUE_OK) {
                fail("sweep exposures", g360_queue_status_text(listed));
                g360_shot_free(&shot); failed = 1; break;
            }
        }
        if (direct) reserved = g360_storage_reserve_at(&w->storage, stamp, stem,
                                                        jpg ? jpg_path : 0, raw ? path : 0, date);
        else {
            queued = g360_queue_create_frame(&w->queue, stamp, k + 1u, path);
            if (queued == G360_QUEUE_OK && jpg) queued = g360_queue_create_jpeg(&w->queue, stamp, k + 1u, jpg_path);
        }
        status = G360_OK;
        if (queued == G360_QUEUE_OK && reserved == G360_STORAGE_OK) {
            if (raw) status = g360_save_dng_full_xmp(&shot.frame, &shot.sets[0].maker_note, &exif, description, pose_xmp, path);
            if (status == G360_OK && jpg) status = g360_save_jpeg_exif_xmp(jpeg, &shot.sets[0].maker_note, &exif, pose_xmp, jpg_path);
        }
        frame_bytes = (raw ? G360_QUEUE_FRAME_BYTES + 16384u : 0u) +
                      (jpg ? jpeg->size + G360_EXIF_APP1_MAX_SIZE + 4096u : 0u);
        g360_shot_free(&shot);
        if (reserved != G360_STORAGE_OK) { fail("reserve filename", g360_storage_status_text(reserved)); failed = 1; break; }
        if (queued != G360_QUEUE_OK) { fail("sweep save", g360_queue_status_text(queued)); failed = 1; break; }
        if (status != G360_OK) { fail("sweep save", g360_status_text(status)); failed = 1; break; }
        if (direct) {
            unsigned kind;
            for (kind = 0; kind < 2; ++kind) {
                char file[72];
                uint64_t bytes;
                if (kind == 0 ? !raw : !jpg) continue;
                (void)snprintf(file, sizeof file, "%s.%s", stem, kind == 0 ? "dng" : "jpg");
                if (g360_sweep_list_append(&w->storage, stamp, file, k + 1u, row, 1, &bytes) != 0) {
                    fail("sweep list", "cannot write the exposure list"); failed = 1; break;
                }
                line_bytes += bytes;
                printf("APP SAVED %s\n", file);
            }
            if (failed) break;
            spend(w, frame_bytes + line_bytes);
        }
        ++done;
        printf("APP SWEEP FRAME %u TV %ld ISO %lu exposure %lu/%lu ISO %lu at %lu ms\n", k + 1u, (long)sweep_tv[k],
               (unsigned long)SWEEP_ISO, (unsigned long)exif.exposure_numerator,
               (unsigned long)exif.exposure_denominator, (unsigned long)exif.iso,
               (unsigned long)(now_ms() - started));
        (void)fflush(stdout);
        if (last) break;
    }
    shot_sound_end();
    restore = g360_set_auto_exposure();
    if (restore != G360_OK && !failed) { fail("automatic settings", g360_status_text(restore)); failed = 1; }
    if (failed) return -1;
    /* "Held still": from the first capture's start to the last capture's end. */
    printf("APP SWEEP CAPTURED %s: %u frame(s), held still %lu ms, last frame saved %lu ms\n", stamp, done,
           (unsigned long)(still_until - started), (unsigned long)(now_ms() - started));
    (void)fflush(stdout);
    if (get(&stopping)) return 1;
    status = 0;
    if (!direct) {
        moving = now_ms();
        status = move_queue(w);
        printf("APP SWEEP SAVED %s: moved to the card in %lu ms\n", stamp, (unsigned long)(now_ms() - moving));
        (void)fflush(stdout);
    }
    if (status == 0) check_wear(w);
    /* Everything done (captured, moved, wear read): the burst sound. */
    if (status == 0 && !get(&stopping) && app_sound_play(SOUND("shutter_continous.wav")) != 0)
        printf("APP sweep done sound failed\n");
    return status;
}

static void *capture_worker(void *argument)
{
    static worker w;
    g360_open_options options;
    int status;
    memset(&w, 0, sizeof w);
    w.config = argument;
    w.remaining = w.config->free_bytes;
    publish_budget(&w);
    status = g360_storage_open(&w.storage, w.config->root);
    if (status != G360_STORAGE_OK) {
        fail("storage open", g360_storage_status_text(status)); return 0;
    }
    /* At least one still with the selected outputs must fit. */
    if (w.remaining < SPACE_RESERVE + single_need(get(&setting_output))) {
        fail("card space", "insufficient reserved space"); goto end;
    }
    if (w.config->queue) {
        status = g360_queue_open(&w.queue, w.config->queue);
        if (status != G360_QUEUE_OK) {
            printf("APP FLASH QUEUE unavailable (%s: %s): sweeps go straight to the card\n", w.config->queue,
                   g360_queue_status_text(status));
        } else {
            /* Frames left by a stop, a power-off or a crash go first. */
            if (move_queue(&w) != 0) goto end;
            change_state(SAVING, STARTING);
            if (w.config->queue_free_bytes >= QUEUE_BUDGET) set(&sweep_ready, 1);
            else printf("APP FLASH QUEUE unavailable (flash space): sweeps go straight to the card\n");
        }
        (void)fflush(stdout);
    }
    memset(&options, 0, sizeof options);
    options.frame_pause = 1; options.free_bytes = w.remaining; options.log_path = w.config->log;
    status = g360_open(&options);
    if (status != G360_OK) { fail("capture open", g360_status_text(status)); goto end; }
    w.capture_open = 1;
    status = g360_set_auto_exposure();
    if (status == G360_OK) status = g360_set_image_quality(G360_QUALITY_SUPERFINE);
    if (status == G360_OK) status = g360_set_drive_mode(G360_DRIVE_SINGLE);
    if (status == G360_OK) status = g360_set_quickview(G360_QUICKVIEW_OFF);
    if (status == G360_OK) status = g360_set_user_attribute(G360_ATTR_WB, 0x50000u);
    if (status != G360_OK) { fail("automatic settings", g360_status_text(status)); goto end; }
    if (g360_service(1000) < 0) { fail("capture service", "startup service failed"); goto end; }
    set(&state, READY);
    while (!get(&stopping)) {
        uint32_t current = get(&state);
        int result;
        if (current != CAPTURING && current != SWEEPING) {
            preview_sync(current == READY && !w.config->once);
            if (g360_service(50) < 0) { fail("capture service", "idle service failed"); break; }
            continue;
        }
        preview_pause();
        result = current == SWEEPING ? sweep(&w) : single_shot(&w);
        if (result < 0) break;
        if (result > 0) break; /* stopping */
        (void)atomic_fetch_add(&saved_count, 1u);
        set(&state, READY);
    }
end:
    if (w.capture_open && g360_preview_running()) (void)g360_preview_stop(3000u);
    g360_app_preview_running(0, 0);
    if (w.capture_open) g360_close();
    g360_storage_close(&w.storage);
    if (!worker_failed) set(&state, STOPPED);
    return 0;
}

/* After a Power hold: the sound, then this process becomes a copy of
   itself in /tmp running --power-off. Returns only on failure. */
static int power_off_now(void)
{
    if (app_sound_play(SOUND("powerOFF.wav")) != 0)
        printf("APP POWER OFF sound failed\n");
    printf("APP POWER OFF from %s, log %s\n", G360_POWER_COPY, G360_POWER_LOG);
    (void)fflush(stdout);
    (void)fsync(1);
    (void)g360_power_relaunch();
    printf("APP ERROR power off: cannot run from %s\n", G360_POWER_COPY); (void)fflush(stdout);
    (void)g360display_show("ERROR");
    return 1;
}

/* The --power-off process: nothing of it is on the card. Returns only if
   the camera stayed on. */
static int power_off_process(void)
{
    g360_power_result result;
    int status;
    /* Once step 1 is done, only the last step may end this process. */
    (void)signal(SIGTERM, SIG_IGN); (void)signal(SIGINT, SIG_IGN);
    (void)g360_power_detach();
    printf("APP POWER OFF sequence\n"); (void)fflush(stdout);
    status = g360_power_off(10000, &result);
    printf("APP ERROR power off: status %d flag %d deviced %d state %d\n",
           status, result.flag, result.deviced, result.state);
    (void)fflush(stdout);
    (void)g360display_show("ERROR");
    return 1;
}

static int number(const char *s, uint64_t *out)
{
    uint64_t n = 0;
    if (!s || !*s) return -1;
    while (*s) {
        unsigned digit = (unsigned)(*s++ - '0');
        if (digit > 9 || n > (UINT64_MAX - digit) / 10u) return -1;
        n = n * 10u + digit;
    }
    *out = n; return 0;
}

/* ---- The settings screen (UI thread only) ------------------------------ */

enum ui_screen { UI_NORMAL, UI_LIST, UI_EDIT };
#define SETTING_COUNT 6u
enum { ITEM_MODE, ITEM_OUTPUT, ITEM_SAVE, ITEM_WIFI, ITEM_DELAY, ITEM_SOUNDS };
/* Whether the card has the network file, checked at start and when the
   settings open; HOME is offered only then. */
static int wifi_home_ok;

typedef struct ui_settings {
    int screen;
    unsigned item;      /* the setting shown in the list */
    uint32_t pending;   /* the option shown while editing it */
    g360_settings stored;
    char state_dir[460];
} ui_settings;

/* The Wi-Fi choice in effect: not chosen yet is HOME with the network
   file and AP without it; HOME without the file is AP. */
static uint32_t wifi_effective(uint32_t wifi)
{
    if (wifi == G360_WIFI_UNSET) return wifi_home_ok ? G360_WIFI_HOME : G360_WIFI_AP;
    if (wifi == G360_WIFI_HOME && !wifi_home_ok) return G360_WIFI_AP;
    return wifi;
}
static uint32_t setting_value(const g360_settings *s, unsigned item)
{
    if (item == ITEM_WIFI) return wifi_effective(s->wifi);
    if (item == ITEM_DELAY) return s->delay;
    if (item == ITEM_SOUNDS) return s->sounds;
    if (item == ITEM_OUTPUT) return s->output;
    return item == ITEM_MODE ? s->mode : s->save;
}
static void set_value(g360_settings *s, unsigned item, uint32_t value)
{
    if (item == ITEM_MODE) s->mode = value;
    else if (item == ITEM_WIFI) s->wifi = value;
    else if (item == ITEM_DELAY) s->delay = value;
    else if (item == ITEM_SOUNDS) s->sounds = value;
    else if (item == ITEM_OUTPUT) s->output = value;
    else s->save = value;
}
static uint32_t next_value(unsigned item, uint32_t value)
{
    if (item == ITEM_OUTPUT) return (value + 1u) % 3u;
    if (item == ITEM_DELAY) return value == 0 ? 2 : value == 2 ? 5 : value == 5 ? 10 : 0;
    if (item == ITEM_WIFI) {
        if (value == G360_WIFI_OFF) return G360_WIFI_AP;
        if (value == G360_WIFI_AP && wifi_home_ok) return G360_WIFI_HOME;
        return G360_WIFI_OFF;
    }
    return value ? 0 : 1;
}
static const char *value_text(unsigned item, uint32_t value)
{
    if (item == ITEM_MODE) return value == G360_MODE_HDR ? "HDR" : "SINGLE";
    if (item == ITEM_DELAY) return value == 2 ? "2 S" : value == 5 ? "5 S" : value == 10 ? "10 S" : "OFF";
    if (item == ITEM_OUTPUT) return output_text(value);
    if (item == ITEM_SOUNDS) return value ? "ON" : "OFF";
    if (item == ITEM_WIFI) return value == G360_WIFI_OFF ? "OFF" : value == G360_WIFI_HOME ? "HOME" : "AP";
    return value == G360_SAVE_CARD ? "CARD" : "FLASH";
}
/* Starts wifi-join.sh apply off|ap|home in the background (wifi.h). */
static void wifi_apply(uint32_t wifi, const char *why)
{
    const char *mode = wifi == G360_WIFI_OFF ? "off" : wifi == G360_WIFI_HOME ? "home" : "ap";
    if (g360_wifi_apply(mode) == 0) printf("APP WIFI %s: apply %s started\n", why, mode);
    else printf("APP WIFI %s: apply %s NOT STARTED (%s)\n", why, mode, G360_WIFI_SCRIPT);
    (void)fflush(stdout);
}
static void apply_settings(const g360_settings *s)
{
    set(&setting_mode, s->mode); set(&setting_save, s->save);
    set(&setting_delay, s->delay); set(&setting_sounds, s->sounds); set(&setting_output, s->output);
}
static void log_settings(const char *what, const g360_settings *s)
{
    printf("APP SETTINGS %s: mode %s, output %s, save %s, Wi-Fi %s%s, delay %s, sounds %s\n", what,
           value_text(ITEM_MODE, s->mode), value_text(ITEM_OUTPUT, s->output), s->save == G360_SAVE_CARD ? "straight to the card" : "flash first",
           value_text(ITEM_WIFI, wifi_effective(s->wifi)), s->wifi == G360_WIFI_UNSET ? " (not chosen)" : "",
           value_text(ITEM_DELAY, s->delay), value_text(ITEM_SOUNDS, s->sounds));
    (void)fflush(stdout);
}

/* Selects VALUE for setting ITEM, from the settings screen or the remote:
   stored on the card and used from now on. Selecting a Wi-Fi option applies
   it even when it is already the stored one, so HOME tries the network
   again after a fall back to the access point. */
static void choose_setting(ui_settings *ui, unsigned item, uint32_t value)
{
    if (value != setting_value(&ui->stored, item) || (item == ITEM_WIFI && value != ui->stored.wifi)) {
        g360_settings next = ui->stored;
        set_value(&next, item, value);
        /* Kept for this session even if the card write fails. */
        if (g360_settings_save(ui->state_dir, &next) == 0) log_settings("saved", &next);
        else log_settings("changed, NOT SAVED to the card", &next);
        ui->stored = next;
        apply_settings(&next);
    }
    if (item == ITEM_WIFI) wifi_apply(value, "chosen");
    /* The remote's shutter and ISO are for single stills only. */
    if (item == ITEM_MODE && value == G360_MODE_HDR) { set(&exposure_shutter, 0); set(&exposure_iso, 0); }
}

/* One button event while the settings are open. Menu: next setting (list)
   or next option (editing); Capture (on release): open the setting, or
   select the option and store it; Power: leave the settings. Selecting a
   Wi-Fi option applies it even when it is already the stored one, so HOME
   tries the network again after a fall back to the access point. */
static void settings_event(ui_settings *ui, const g360ctl_button_event *e, int dropped, int *armed)
{
    if (e->key == G360CTL_KEY_POWER && e->value == 1) {
        ui->screen = UI_NORMAL; *armed = 0;
        printf("APP SETTINGS closed\n"); (void)fflush(stdout);
        return;
    }
    if (e->key == G360CTL_KEY_MENU && e->value == 1) {
        if (ui->screen == UI_LIST) ui->item = (ui->item + 1u) % SETTING_COUNT;
        else ui->pending = next_value(ui->item, ui->pending);
        return;
    }
    if (e->key != G360CTL_KEY_CAPTURE) return;
    if (e->value == 1) { *armed = !dropped; return; }
    if (e->value != 0 || !*armed) return;
    *armed = 0;
    if (ui->screen == UI_LIST) {
        ui->pending = setting_value(&ui->stored, ui->item);
        ui->screen = UI_EDIT;
        return;
    }
    choose_setting(ui, ui->item, ui->pending);
    ui->screen = UI_LIST;
}

/* The status corner's mode icon: what a Capture click takes. */
static int mode_icon(void)
{
    return get(&setting_mode) == G360_MODE_HDR ? G360DISPLAY_MODE_SWEEP : G360DISPLAY_MODE_SINGLE;
}
/* The status corner's self-timer icon: shown while a delay is set. */
static int timer_icon(void) { return get(&setting_delay) != 0; }
/* The R (output includes RAW) and the notes (sounds on) beside it. */
static int raw_icon(void) { return output_raw(get(&setting_output)); }
static int sound_icon(void) { return get(&setting_sounds) != 0; }
/* The charging animation's frame for the clock: 0 while not charging, so an
   idle screen never redraws for it. The main loop wakes every 50 ms at most
   and redraws when the status (this frame included) differs from what the
   screen shows. */
static int charge_frame(const g360display_status *s)
{
    return s->charging ? (int)((now_ms() / G360_CHARGE_FRAME_MS) % G360DISPLAY_CHARGE_FRAMES) : 0;
}

/* The side LED shows the charge: green when full on a cable, yellow while
   charging, off otherwise (blue is left alone). There is no yellow LED:
   "yellow" is the side orange one alone. To warm it toward yellow, mix some
   green in behind it with G360_YELLOW_GREEN (0-255; 0 is orange alone, the
   default; nobody has judged a mix by eye). Written only when the state changes; LED
   trouble never matters to the app. */
#ifndef G360_YELLOW_GREEN
#define G360_YELLOW_GREEN 0u
#endif
#ifndef G360_YELLOW_ORANGE
#define G360_YELLOW_ORANGE 255u
#endif
static int shown_light = -1;
static void show_light(int light)
{
    if (light == shown_light) return;
    shown_light = light;
    (void)g360ctl_led_set(0, G360CTL_LED_SIDE_ORANGE, light == G360_LIGHT_CHARGING ? G360_YELLOW_ORANGE : 0u);
    (void)g360ctl_led_set(0, G360CTL_LED_SIDE_GREEN,
                          light == G360_LIGHT_FULL ? 255u : light == G360_LIGHT_CHARGING ? G360_YELLOW_GREEN : 0u);
    printf("APP LED side %s\n", light == G360_LIGHT_FULL ? "green (full)" : light == G360_LIGHT_CHARGING ? "yellow (charging)" : "off");
    (void)fflush(stdout);
}

/* The status corner's state, logged when it changes. */
static void log_status(const g360display_status *s)
{
    static const char *const wifi[] = {"none", "home network", "access point"};
    if (s->battery < 0) printf("APP STATUS Wi-Fi %s%s, battery unreadable\n", wifi[s->wifi], s->wifi_searching ? " (searching)" : "");
    else printf("APP STATUS Wi-Fi %s%s, battery %d%%%s\n", wifi[s->wifi], s->wifi_searching ? " (searching)" : "", s->battery, s->charging ? ", charging" : "");
    (void)fflush(stdout);
}

/* ---- Battery, charger and Wi-Fi sounds ---------------------------------- */

/* The UI thread publishes the latest reading here (0 until the first); the
   alert thread turns changes into sounds (alerts.h). The thread owns the
   `wl` call, which waits, so the UI never does. */
static shared_u32 published_status;
/* Encoded as 0 unknown, 1 no clients, 2 clients. The alert thread owns wl. */
static shared_u32 published_ap_clients;
#define PUBLISHED_VALID (1u << 31)
_Static_assert(G360DISPLAY_WIFI_AP <= 3u, "the Wi-Fi state is packed into two bits");
static void publish_status(const g360display_status *s, int cable)
{
    set(&published_status, PUBLISHED_VALID | (cable ? 1u << 30 : 0u) | ((uint32_t)s->wifi & 3u) << 24 |
                           (uint32_t)(s->battery + 1));
}
#ifndef G360_ALERT_EVERY_MS
#define G360_ALERT_EVERY_MS 1000u
#endif
static void *alert_worker(void *argument)
{
    g360_alerts alerts;
    uint32_t seen = 0;
    (void)argument;
    g360_alerts_init(&alerts);
    while (!get(&stopping)) {
        uint32_t value = get(&published_status), waited;
        if (value & PUBLISHED_VALID) {
            g360_alert_input in;
            int sounds[G360_ALERT_MAX], count, k;
            in.battery = (int)(value & 0xffu) - 1;
            in.cable = (value >> 30) & 1u ? 1 : 0;
            in.wifi = (int)((value >> 24) & 3u);
            in.clients = in.wifi == G360DISPLAY_WIFI_AP ? g360_wifi_ap_clients(G360_WL_PATH) : -1;
            if (in.wifi != G360DISPLAY_WIFI_AP) set(&published_ap_clients, 0);
            else if (in.clients >= 0) set(&published_ap_clients, in.clients > 0 ? 2u : 1u);
            if (value != seen || in.wifi == G360DISPLAY_WIFI_AP) {
                seen = value;
                count = g360_alerts_step(&alerts, &in, sounds);
                for (k = 0; k < count && !get(&stopping); ++k) {
                    const char *file = g360_alert_file(sounds[k]);
                    char path[160];
                    int n = file ? snprintf(path, sizeof path, "%s/%s", G360CTL_SOUND_DIR, file) : -1;
                    if (n < 0 || (size_t)n >= sizeof path) continue;
                    /* Sounds OFF: the alert is still taken from the state machine (so it is not
                       replayed when sounds come back on) but nothing plays. */
                    if (!get(&setting_sounds)) continue;
                    printf("APP ALERT %s\n", file); (void)fflush(stdout);
                    if (app_sound_play(path) != 0) {
                        printf("APP ALERT sound failed: %s\n", file); (void)fflush(stdout);
                    }
                }
            }
        }
        for (waited = 0; waited < G360_ALERT_EVERY_MS && !get(&stopping); waited += 50u) pause_ms(50u);
    }
    return 0;
}

/* Count selected outputs, using the same admission checks as capture and
   typical sizes for the remaining photos after that first full allowance. */
static uint32_t left(int sweeps, unsigned frames, uint32_t output)
{
    uint64_t remaining = (uint64_t)get(&budget_kib) << 10, need, each, n;
    if (sweeps) {
        need = sweep_need(frames, output);
        each = (output_raw(output) ? (uint64_t)frames * FRAME_CARD_BUDGET : 0u) +
               (output_jpeg(output) ? (uint64_t)frames * JPEG_TYPICAL : 0u);
    } else {
        need = single_need(output);
        each = single_typical(output);
    }
    if (remaining < SPACE_RESERVE + need) return 0;
    n = 1u + (remaining - SPACE_RESERVE - need) / each;
    return n > 99999u ? 99999u : (uint32_t)n;
}

/* READY's place: how many shots or sweeps are left, as a bare number (the
   display draws it double size), or CARD FULL. The mode icon says which. */
static void left_text(char out[16])
{
    int hdr = get(&setting_mode) == G360_MODE_HDR;
    uint32_t n = left(hdr, SWEEP_FRAMES, get(&setting_output));
    if (n == 0) (void)snprintf(out, 16, "CARD FULL");
    else (void)snprintf(out, 16, "%lu", (unsigned long)n);
}

/* The screen text: up to two lines of 12 characters, below the status
   corner. */
static void label(uint32_t current, int hint, int countdown, int countdown_sweep, const ui_settings *ui, char text[48])
{
    static const char *const names[] = {"STARTING", "READY", "CAPTURING...", "ERROR", "STOPPED"};
    static const char *const list_names[] = {"MODE", "OUTPUT", "SAVE", "WIFI", "DELAY", "SOUNDS"};
    static const char *const titles[] = {"MODE", "OUTPUT", "SAVE TO", "WIFI", "DELAY", "SOUNDS"};
    if (current == READY && ui->screen == UI_LIST) {
        if (ui->item == ITEM_OUTPUT)
            (void)snprintf(text, 48, "OUTPUT %u/%u\n%s", ui->item + 1u, SETTING_COUNT, output_text(ui->stored.output));
        else (void)snprintf(text, 48, "SETTINGS %u/%u\n%s: %s", ui->item + 1u, SETTING_COUNT, list_names[ui->item],
                       value_text(ui->item, setting_value(&ui->stored, ui->item)));
    } else if (current == READY && ui->screen == UI_EDIT) {
        (void)snprintf(text, 48, "%s\n- %s -", titles[ui->item], value_text(ui->item, ui->pending));
    } else if (current == SWEEPING) {
        uint32_t frame = get(&sweep_frame);
        const char *kind = get(&sweep_nd) ? "ND SWEEP" : "SWEEP";
        if (frame == 0) (void)snprintf(text, 48, "%s", get(&sweep_nd) ? "ND SWEEP" : "HDR SWEEP");
        else (void)snprintf(text, 48, "%s %lu/%lu", kind, (unsigned long)frame, (unsigned long)get(&sweep_total));
    } else if (current == SAVING) {
        uint32_t total = get(&saving_total), done = get(&saving_done);
        if (total == 0) (void)snprintf(text, 48, "SAVING");
        else (void)snprintf(text, 48, "SAVING %lu/%lu", (unsigned long)(done < total ? done + 1u : total),
                            (unsigned long)total);
    } else if (current == READY && countdown > 0) {
        (void)snprintf(text, 48, "%s\n%d", countdown_sweep ? "SWEEP IN" : "CAPTURE IN", countdown);
    } else if (current == READY && hint) {
        (void)snprintf(text, 48, "HDR SWEEP");
    } else if (current == READY && auto_every) {
        /* Auto photos: the wait for the next press (the self-timer counts
           the rest), as seconds, M:SS or H:MM:SS. */
        uint64_t now = now_ms(), lead = (uint64_t)get(&setting_delay) * 1000u, wait = 1;
        if (auto_target > now + lead) wait = (auto_target - lead - now + 999u) / 1000u;
        if (wait < 60u) (void)snprintf(text, 48, "AUTO IN\n%lu", (unsigned long)wait);
        else if (wait < 3600u) (void)snprintf(text, 48, "AUTO IN\n%lu:%02lu", (unsigned long)(wait / 60u), (unsigned long)(wait % 60u));
        else (void)snprintf(text, 48, "AUTO IN\n%lu:%02lu:%02lu", (unsigned long)(wait / 3600u),
                            (unsigned long)(wait / 60u % 60u), (unsigned long)(wait % 60u));
    } else if (current == READY && get(&wear_warning)) {
        uint32_t level = get(&wear_warning);
        /* 10 is 90-100% used, 11 past the estimated life. */
        if (level >= 10) (void)snprintf(text, 48, "FLASH WORN");
        else (void)snprintf(text, 48, "FLASH WEAR %lu", (unsigned long)level);
    } else if (current == READY) {
        left_text(text);
    } else {
        (void)snprintf(text, 48, "%s", names[current <= STOPPED ? current : APP_ERROR]);
    }
}

/* The self-timer's sounds play in the background so a press can still
   cancel them. The one player running is ended (or reaped) before the next. */
static int countdown_pid;

static void countdown_sound(const char *file)
{
    if (!get(&setting_sounds)) return;
    if (countdown_pid > 0) g360ctl_sound_stop(countdown_pid);
    countdown_pid = g360ctl_sound_start(G360CTL_PAPLAY, file);
    if (countdown_pid < 0) { countdown_pid = 0; printf("APP DELAY sound %s failed\n", file); (void)fflush(stdout); }
}

/* One second boundary of the countdown, `left` whole seconds remaining:
   a tick for every second until the last two, which get the 2 s warning. */
static void countdown_tick(int left)
{
    if (left > 2) countdown_sound(SOUND("timer.wav"));
    else if (left == 2) countdown_sound(SOUND("timer_last2sec.wav"));
}

/* ---- Capture presses: the button, the remote and auto photos ----------- */

/* A Capture click (the release): the self-timer's countdown, or the
   capture now. FROM_AUTO: auto photos pressed it. */
static void press_capture(int sweep_now, int from_auto, uint32_t nd)
{
    uint32_t delay = get(&setting_delay);
    set(&sweep_nd, sweep_now ? nd : 0u);
    set(&wear_warning, 0); /* seen: back to READY */
    if (delay) {
        countdown_until = now_ms() + (uint64_t)delay * 1000u; countdown_sweep = sweep_now; countdown_last = 0;
        countdown_auto = from_auto;
        printf("APP DELAY %lu s before the %s\n", (unsigned long)delay, sweep_now ? "sweep" : "capture");
        (void)fflush(stdout);
    } else {
        set(&sweep_frame, 0); set(&sweep_total, SWEEP_FRAMES);
        change_state(READY, sweep_now ? SWEEPING : CAPTURING);
    }
}

static void cancel_countdown(const char *why)
{
    countdown_until = 0; countdown_auto = 0;
    if (countdown_pid > 0) { g360ctl_sound_stop(countdown_pid); countdown_pid = 0; }
    printf("APP DELAY cancelled%s\n", why); (void)fflush(stdout);
}

/* Whether a capture fits the card budget (a press at CARD FULL is an
   ERROR, which the remote and auto photos don't risk). */
static int capture_fits(int sweep_now)
{
    return left(sweep_now, SWEEP_FRAMES, get(&setting_output)) > 0;
}

static void auto_start(uint32_t every)
{
    uint64_t now = now_ms();
    /* Already running: the next capture is EVERY after the last one. */
    if (auto_every && auto_taken) auto_target = auto_target - (uint64_t)auto_every * 1000u + (uint64_t)every * 1000u;
    else { auto_target = now; auto_taken = 0; }
    auto_every = every;
    printf("APP AUTO every %lu s\n", (unsigned long)every); (void)fflush(stdout);
}

static void auto_stop(const char *why)
{
    if (!auto_every) return;
    auto_every = 0;
    printf("APP AUTO stopped (%s) after %lu presses\n", why, (unsigned long)auto_taken); (void)fflush(stdout);
    if (countdown_until && countdown_auto) cancel_countdown(" (auto photos stopped)");
    else if (!countdown_until && countdown_pid > 0) { g360ctl_sound_stop(countdown_pid); countdown_pid = 0; } /* the 2 s warning */
}

/* Auto photos: a Capture press when the next one is due, the self-timer
   counting into the interval (remote.h). Only at READY, with the settings
   closed, no countdown running and Capture not held. With no self-timer,
   the 2 s warning (timer_last2sec.wav) plays 2 s before the capture, for
   intervals over 2 s. */
static void auto_tick(const ui_settings *ui, int held)
{
    uint64_t now, lead;
    int sweep_now;
    if (!auto_every || get(&state) != READY || ui->screen != UI_NORMAL || countdown_until || held) return;
    now = now_ms(); lead = (uint64_t)get(&setting_delay) * 1000u;
    if (!lead && auto_every > 2u && auto_warned != auto_target && auto_target > now && auto_target - now <= 2000u) {
        auto_warned = auto_target;
        printf("APP AUTO 2 s warning\n"); (void)fflush(stdout);
        countdown_sound(SOUND("timer_last2sec.wav"));
    }
    if (!g360_remote_auto_due(now, auto_target, lead)) return;
    sweep_now = get(&setting_mode) == G360_MODE_HDR;
    if (!capture_fits(sweep_now)) { auto_stop("card full"); return; }
    ++auto_taken;
    printf("APP AUTO press %lu (every %lu s)\n", (unsigned long)auto_taken, (unsigned long)auto_every);
    (void)fflush(stdout);
    auto_target = g360_remote_auto_next(now, lead, (uint64_t)auto_every * 1000u);
    press_capture(sweep_now, 1, 0u);
}

/* ---- The remote --------------------------------------------------------- */

/* One request at a time passes from the remote thread to the UI thread:
   0 free, 1 asked, 2 being answered (UI thread), 3 answered. */
static shared_u32 remote_box;
static char remote_request[G360_REMOTE_REQUEST_MAX], remote_reply[G360_REMOTE_REPLY_MAX];
static int remote_listener = -1;
static int preview_listening; /* 1: the preview's port is open */

/* The screen's text as a JSON string's contents (only A-Z, 0-9, space and
   /:-. are drawn; a line break is \n). */
static void json_text(const char *text, char out[96])
{
    size_t k = 0;
    for (; *text != '\0' && k + 3 < 96u; ++text) {
        if (*text == '\n') { out[k++] = '\\'; out[k++] = 'n'; }
        else if (*text != '"' && *text != '\\' && (unsigned char)*text >= 0x20) out[k++] = *text;
    }
    out[k] = '\0';
}

/* The reply: what was refused (ERROR, "" if nothing), then the state. */
static void remote_state(char *out, size_t size, const char *error, const ui_settings *ui,
                         const g360display_status *s, int hint)
{
    static const char *const names[] = {"starting", "ready", "capturing", "error", "stopped", "sweeping", "saving"};
    static const char *const network[] = {"none", "home", "ap"};
    static const char *const previews[] = {"off", "starting", "on", "paused"};
    uint32_t current = get(&state), wifi = wifi_effective(ui->stored.wifi);
    uint64_t now = now_ms(), lead = (uint64_t)get(&setting_delay) * 1000u, next = 0;
    int countdown = countdown_until > now ? (int)((countdown_until - now + 999u) / 1000u) : 0;
    int hdr = get(&setting_mode) == G360_MODE_HDR;
    char text[48], screen[96];
    label(current, hint, countdown_until ? countdown : 0, countdown_sweep, ui, text);
    json_text(text, screen);
    if (auto_every && auto_target > now + lead) next = (auto_target - lead - now + 999u) / 1000u;
    (void)snprintf(out, size,
        "{\"app\":\"on\",\"ok\":%s,\"error\":\"%s\",\"state\":\"%s\",\"menu\":%s,\"screen\":\"%s\","
        "\"countdown\":%d,\"countdown_sweep\":%s,\"sweep\":[%lu,%lu],\"saving\":[%lu,%lu],"
        "\"settings\":{\"mode\":\"%s\",\"output\":\"%s\",\"save\":\"%s\",\"wifi\":\"%s\",\"delay\":%lu,\"sounds\":\"%s\"},"
        "\"exposure\":{\"shutter\":%lu,\"iso\":%lu},"
        "\"wifi_home\":%s,\"left\":%lu,\"left_unit\":\"%s\",\"battery\":%d,\"charging\":%s,\"network\":\"%s\","
        "\"auto\":{\"every\":%lu,\"taken\":%lu,\"next\":%lu},\"saved\":%lu,\"wear\":%lu,"
        "\"preview\":{\"state\":\"%s\",\"port\":%d,\"viewers\":%lu,\"frames\":%lu,\"error\":\"%s\"},"
        "\"sweep_stop\":\"%s\",\"sweep_nd\":%lu,\"scan\":{\"rev\":%lu,\"frames\":%lu}}",
        error[0] ? "false" : "true", error, names[current <= SAVING ? current : APP_ERROR],
        ui->screen != UI_NORMAL ? "true" : "false", screen,
        countdown_until ? countdown : 0, countdown_until && countdown_sweep ? "true" : "false",
        (unsigned long)get(&sweep_frame), (unsigned long)get(&sweep_total),
        (unsigned long)get(&saving_done), (unsigned long)get(&saving_total),
        hdr ? "hdr" : "single", output_wire(ui->stored.output),
        ui->stored.save == G360_SAVE_CARD ? "card" : "flash",
        wifi == G360_WIFI_OFF ? "off" : wifi == G360_WIFI_HOME ? "home" : "ap",
        (unsigned long)ui->stored.delay, ui->stored.sounds ? "on" : "off",
        (unsigned long)get(&exposure_shutter), (unsigned long)get(&exposure_iso),
        wifi_home_ok ? "true" : "false",
        (unsigned long)left(hdr, SWEEP_FRAMES, get(&setting_output)), hdr ? "sweeps" : "shots",
        s->battery, s->charging ? "true" : "false", network[s->wifi >= 0 && s->wifi <= 2 ? s->wifi : 0],
        (unsigned long)auto_every, (unsigned long)auto_taken, (unsigned long)next,
        (unsigned long)get(&saved_count), (unsigned long)get(&wear_warning),
        previews[g360_app_preview_state() & 3], G360_PREVIEW_PORT, (unsigned long)g360_app_preview_viewers(),
        (unsigned long)g360_app_preview_encoded(), preview_error,
        get(&sweep_stop) ? "auto" : "full", (unsigned long)get(&sweep_nd), (unsigned long)get(&scan_rev),
        (unsigned long)get(&scan_frames));
}

/* The UI thread answers a waiting request, with the same code the buttons
   use. HELD: Capture is held down. */
static void remote_answer(ui_settings *ui, const g360display_status *s, int held, int hint)
{
    uint32_t expected = 1;
    g360_remote_request r;
    const char *error = "";
    if (!atomic_compare_exchange_strong(&remote_box, &expected, 2u)) return;
    if (g360_remote_parse(remote_request, &r) != 0) {
        error = "bad request";
    } else if (r.command == G360_REMOTE_SET || r.command == G360_REMOTE_CAPTURE) {
        if (ui->screen != UI_NORMAL) error = "settings open on the camera";
        else if (countdown_until) error = "countdown running";
        else if (get(&state) != READY || held) error = "busy";
        else if (r.command == G360_REMOTE_SET) {
            if (r.setting == G360_REMOTE_STOP) {
                printf("APP REMOTE %s\n", remote_request); (void)fflush(stdout);
                set(&sweep_stop, r.value);
            } else if (r.setting == G360_REMOTE_SHUTTER || r.setting == G360_REMOTE_ISO) {
                if (r.value && get(&setting_mode) == G360_MODE_HDR) error = "single mode only";
                else if (r.setting == G360_REMOTE_ISO && !get(&exposure_shutter)) error = "set a shutter first";
                else {
                    printf("APP REMOTE %s\n", remote_request); (void)fflush(stdout);
                    if (r.setting == G360_REMOTE_ISO) set(&exposure_iso, r.value);
                    else {
                        /* Auto shutter is auto ISO; a set shutter keeps the ISO chosen before, else 100. */
                        set(&exposure_shutter, r.value);
                        if (!r.value) set(&exposure_iso, 0);
                        else if (!get(&exposure_iso)) set(&exposure_iso, MANUAL_SHUTTER_ISO);
                    }
                }
            } else {
                printf("APP REMOTE %s\n", remote_request); (void)fflush(stdout);
                choose_setting(ui, (unsigned)r.setting, r.value);
            }
        } else {
            int sweep_now = r.sweep || get(&setting_mode) == G360_MODE_HDR;
            if (!capture_fits(sweep_now)) {
                error = "card full";
            } else {
                printf("APP REMOTE %s\n", remote_request); (void)fflush(stdout);
                press_capture(sweep_now, 0, r.nd);
            }
        }
    } else if (r.command == G360_REMOTE_CANCEL) {
        if (countdown_until) cancel_countdown(" (remote)");
        else error = "no countdown";
    } else if (r.command == G360_REMOTE_PREVIEW) {
        uint32_t current = get(&state);
        if (r.value && (current == APP_ERROR || current == STOPPED || preview_listening != 1)) {
            error = preview_listening == 1 ? "busy" : "preview unavailable";
        } else {
            printf("APP REMOTE %s\n", remote_request); (void)fflush(stdout);
            if (r.value) preview_error[0] = '\0';
            g360_app_preview_want((int)r.value);
        }
    } else if (r.command == G360_REMOTE_AUTO) {
        /* The first press, if due, before the reply. */
        if (r.value) { auto_start(r.value); auto_tick(ui, held); }
        else auto_stop("remote");
    }
    remote_state(remote_reply, sizeof remote_reply, error, ui, s, hint);
    set(&remote_box, 3u);
}

/* The remote thread's side: hands the request over and waits for the
   answer. */
static void remote_handle(void *context, const char *request, char *reply, size_t size)
{
    uint32_t expected = 1;
    unsigned waited;
    (void)context;
    (void)snprintf(remote_request, sizeof remote_request, "%s", request);
    set(&remote_box, 1u);
    for (waited = 0; waited < 3000u && get(&remote_box) != 3u && !get(&stopping); waited += 5u) pause_ms(5u);
    /* Not taken (the app is stopping, or the UI thread is stuck). */
    if (atomic_compare_exchange_strong(&remote_box, &expected, 0u)) {
        (void)snprintf(reply, size, "{\"app\":\"on\",\"ok\":false,\"error\":\"no answer\"}");
        return;
    }
    while (get(&remote_box) == 2u) pause_ms(5u);
    (void)snprintf(reply, size, "%s", remote_reply);
    set(&remote_box, 0u);
}

static void *remote_main(void *unused)
{
    (void)unused;
    while (!get(&stopping)) {
        if (g360_remote_serve_one(remote_listener, remote_handle, 0) != 0) pause_ms(100u);
    }
    return 0;
}

int main(int argc, char **argv)
{
    app_config config = {"/opt/storage/sdcard/G360", 0, G360CTL_BUTTON_DEVICE, 0, G360_WEAR_DIR, G360_STATUS_ROOT, 0, 0, 0, {0, 0, 0, 0}};
    pthread_t worker_thread;
    static ui_settings ui;
    int fd, i, eligible_press = 0, requested_once = 0, failed = 0, hint = 0, loaded, save = -1;
    int output = -1, sounds = -1, store = 0;
    uint32_t last_state = (uint32_t)-1;
    uint64_t pressed_at = 0, status_at;
    uint64_t ip_until = 0;
    int ip_shown = 0;
    char ip_text[20] = "";
    config.motion.history = G360_MOTION_HISTORY;
    config.motion.history_offset = G360_MOTION_HISTORY_OFFSET;
    config.motion.position = G360_MOTION_POSITION;
    config.motion.position_ioctl = 1;
    char shown[48] = "";
    g360display_status status = {0}, shown_status;
    if (argc == 2 && !strcmp(argv[1], "--power-off")) return power_off_process();
    /* The launcher holds our screen lock; stock may still redraw during handover. */
    if (argc == 2 && !strcmp(argv[1], "--loading")) return g360display_show("LOADING...") != 0;
    for (i = 1; i < argc; ++i) {
        const char *key = argv[i];
        if (!strcmp(key, "--once")) { config.once = 1; continue; }
        if (!strcmp(key, "--sweep-once")) { config.once = 2; continue; }
        if (!strcmp(key, "--store-settings")) { store = 1; continue; }
        if (++i >= argc) goto usage;
        if (!strcmp(key, "--root")) config.root = argv[i];
        else if (!strcmp(key, "--log")) config.log = argv[i];
        else if (!strcmp(key, "--event-device")) config.device = argv[i];
        else if (!strcmp(key, "--queue")) config.queue = argv[i];
        else if (!strcmp(key, "--wear-dir")) config.wear_dir = argv[i];
        else if (!strcmp(key, "--status-root")) config.status_root = argv[i];
        /* Tests: the motion history and its position from plain files. */
        else if (!strcmp(key, "--motion-history")) { config.motion.history = argv[i]; config.motion.history_offset = 0; }
        else if (!strcmp(key, "--motion-position")) { config.motion.position = argv[i]; config.motion.position_ioctl = 0; }
        else if (!strcmp(key, "--free-bytes")) { if (number(argv[i], &config.free_bytes)) goto usage; }
        else if (!strcmp(key, "--queue-free-bytes")) { if (number(argv[i], &config.queue_free_bytes)) goto usage; }
        /* Maintenance: this run only; the stored settings are not changed
           (unless --store-settings stores them). */
        else if (!strcmp(key, "--sweep-save")) {
            if (!strcmp(argv[i], "flash")) save = G360_SAVE_FLASH;
            else if (!strcmp(argv[i], "card")) save = G360_SAVE_CARD;
            else goto usage;
        } else if (!strcmp(key, "--output")) {
            if (!strcmp(argv[i], "raw")) output = G360_OUTPUT_RAW;
            else if (!strcmp(argv[i], "jpeg")) output = G360_OUTPUT_JPEG;
            else if (!strcmp(argv[i], "raw+jpeg")) output = G360_OUTPUT_BOTH;
            else goto usage;
        } else if (!strcmp(key, "--raw")) { /* compatibility with existing trial scripts */
            if (!strcmp(argv[i], "on")) output = G360_OUTPUT_BOTH;
            else if (!strcmp(argv[i], "off")) output = G360_OUTPUT_JPEG;
            else goto usage;
        } else if (!strcmp(key, "--sounds")) {
            int *value = &sounds;
            if (!strcmp(argv[i], "on")) *value = 1;
            else if (!strcmp(argv[i], "off")) *value = 0;
            else goto usage;
        }
        else goto usage;
    }
    if (!config.root[0] || !config.device[0] || (!config.free_bytes && !store)) goto usage;
    if (store && config.once) goto usage;
    if (config.queue && !config.queue[0]) goto usage;
    if (config.once == 2 && !config.queue && save != G360_SAVE_CARD) goto usage;
    i = snprintf(ui.state_dir, sizeof ui.state_dir, "%s/state", config.root);
    if (i < 0 || (size_t)i >= sizeof ui.state_dir) goto usage;
    loaded = g360_settings_load(ui.state_dir, &ui.stored);
    if (save >= 0) ui.stored.save = (uint32_t)save;
    if (output >= 0) ui.stored.output = (uint32_t)output;
    if (sounds >= 0) ui.stored.sounds = (uint32_t)sounds;
    apply_settings(&ui.stored);
    wifi_home_ok = g360_wifi_file_present();
    log_settings(loaded > 0 ? "loaded" : loaded == 0 ? "default" : "default (read error)", &ui.stored);
    /* Maintenance: store the settings with this command line's overrides,
       then exit. Refused while the app runs (it holds the storage lock and
       would later write its own copy over them). */
    if (store) {
        g360_storage lock;
        int status = loaded < 0 ? G360_STORAGE_IO : g360_storage_open(&lock, config.root);
        if (status != G360_STORAGE_OK) {
            printf("APP SETTINGS NOT STORED: %s\n", g360_storage_status_text(status)); (void)fflush(stdout);
            return 1;
        }
        status = g360_settings_save(ui.state_dir, &ui.stored);
        g360_storage_close(&lock);
        log_settings(status == 0 ? "stored" : "NOT STORED (write failed)", &ui.stored);
        return status == 0 ? 0 : 1;
    }
    (void)signal(SIGINT, stop); (void)signal(SIGTERM, stop); (void)signal(SIGHUP, SIG_IGN);
    /* The Wi-Fi setting, in the background. Not for maintenance runs, nor
       when wifi-join.sh itself restarted the app after join or revert. */
    if (!config.once) {
        const char *skip = getenv("G360_WIFI_SKIP_APPLY");
        if (skip != 0 && !strcmp(skip, "1")) {
            printf("APP WIFI start: not applied (the app was restarted by wifi-join.sh)\n"); (void)fflush(stdout);
        } else {
            wifi_apply(wifi_effective(ui.stored.wifi), "start");
        }
    }
    status.mode = mode_icon(); status.timer = timer_icon(); status.charge_frame = 0;
    status.raw = raw_icon(); status.sound = sound_icon();
    (void)g360_status_read(config.status_root, &status);
    status.charge_frame = charge_frame(&status);
    status.wifi_frame = status.wifi_searching ? (int)((now_ms() / 400u) % G360DISPLAY_WIFI_FRAMES) : 0;
    publish_status(&status, g360_status_cable(config.status_root));
    show_light(g360_status_light(config.status_root));
    status_at = now_ms();
    log_status(&status);
    if (g360display_clear() || g360display_show_status("STARTING", &status)) return 1;
    memcpy(shown, "STARTING", 9);
    shown_status = status;
    fd = g360ctl_buttons_open(config.device);
    if (fd < 0) { (void)g360display_show("ERROR"); return 1; }
    if (pthread_create(&worker_thread, 0, capture_worker, &config) != 0) {
        g360ctl_buttons_close(fd); (void)g360display_show("ERROR"); return 1;
    }
    /* The sounds for battery, charger and Wi-Fi changes. Not for maintenance
       runs. If the thread can't start the app works without them. */
    if (!config.once) {
        pthread_t alert_thread;
        if (pthread_create(&alert_thread, 0, alert_worker, 0) == 0) (void)pthread_detach(alert_thread);
        else printf("APP ALERT thread not started\n");
    }
    /* The remote (the web page). Not for maintenance runs. If it can't
       start the app works without it. */
    if (!config.once) {
        pthread_t remote_thread;
        remote_listener = g360_remote_listen(G360_REMOTE_SOCKET);
        if (remote_listener >= 0 && pthread_create(&remote_thread, 0, remote_main, 0) == 0) {
            (void)pthread_detach(remote_thread);
            printf("APP REMOTE listening on %s\n", G360_REMOTE_SOCKET);
        } else {
            printf("APP REMOTE not started (%s)\n", G360_REMOTE_SOCKET);
        }
        /* The live preview's own port (preview.h). */
        preview_listening = g360_app_preview_init(G360_PREVIEW_PORT) == 0 ? 1 : 0;
        if (preview_listening) printf("APP PREVIEW listening on port %d\n", G360_PREVIEW_PORT);
        else printf("APP PREVIEW not started (port %d)\n", G360_PREVIEW_PORT);
    }
    printf("APP UI STARTING\n"); (void)fflush(stdout);
    while (!get(&stopping)) {
        g360ctl_button_event event;
        uint32_t current = get(&state);
        int dropped = 0, received;
        char text[48];
        if (current != last_state) {
            if (current != READY) { eligible_press = 0; hint = 0; ui.screen = UI_NORMAL; countdown_until = 0; }
            last_state = current;
        }
        if (current == READY && ui.screen == UI_NORMAL && eligible_press && !hint &&
            get(&setting_mode) == G360_MODE_SINGLE && now_ms() - pressed_at >= SWEEP_HOLD_MS) hint = 1;
        status.mode = mode_icon(); status.timer = timer_icon();
        status.raw = raw_icon(); status.sound = sound_icon();
        /* The self-timer ran out: the capture starts now. */
        if (countdown_until && now_ms() >= countdown_until) {
            countdown_until = 0;
            countdown_auto = 0;
            printf("APP DELAY over: %s starts\n", countdown_sweep ? "sweep" : "capture"); (void)fflush(stdout);
            set(&sweep_frame, 0); set(&sweep_total, SWEEP_FRAMES);
            change_state(READY, countdown_sweep ? SWEEPING : CAPTURING);
            current = get(&state);
        }
        /* Reap a finished countdown sound; sound each second boundary. */
        if (countdown_pid > 0 && g360ctl_sound_reap(countdown_pid)) countdown_pid = 0;
        if (countdown_until) {
            int left = (int)((countdown_until - now_ms() + 999u) / 1000u);
            if (left != countdown_last) { countdown_last = left; countdown_tick(left); }
        }
        if (now_ms() - status_at >= G360_STATUS_EVERY_MS) {
            g360display_status was = status;
            (void)g360_status_read(config.status_root, &status);
            publish_status(&status, g360_status_cable(config.status_root));
            show_light(g360_status_light(config.status_root));
            status_at = now_ms();
            if (memcmp(&status, &was, sizeof status) != 0) log_status(&status);
        }
        status.charge_frame = charge_frame(&status);
        status.wifi_frame = status.wifi_searching ? (int)((now_ms() / 400u) % G360DISPLAY_WIFI_FRAMES) : 0;
        status.ap_connected = status.wifi == G360DISPLAY_WIFI_AP && get(&published_ap_clients) == 2u;
        /* Wait for both the route and DHCP address. Show once per connection;
           startup while already connected counts as a connection too. */
        if (status.wifi != G360DISPLAY_WIFI_HOME) { ip_shown = 0; ip_until = 0; }
        else if (!ip_shown && current == READY && ui.screen == UI_NORMAL && !countdown_until && !hint &&
                 g360_status_home_ip(config.status_root, ip_text) == 0) {
            ip_shown = 1;
            ip_until = now_ms() + 4000u;
        }
        if (current != READY || ui.screen != UI_NORMAL || countdown_until || hint) ip_until = 0;
        /* Auto photos' next press, then a request from the web page. */
        if (!config.once) {
            auto_tick(&ui, eligible_press);
            remote_answer(&ui, &status, eligible_press, hint);
            current = get(&state);
        }
        label(current, hint, countdown_until ? (int)((countdown_until - now_ms() + 999u) / 1000u) : 0, countdown_sweep, &ui, text);
        if (ip_until && now_ms() < ip_until) (void)snprintf(text, sizeof text, "%s", ip_text);
        if (strcmp(text, shown) != 0 || memcmp(&status, &shown_status, sizeof status) != 0) {
            if (g360display_show_status(text, &status) != 0) { failed = 1; set(&stopping, 1); break; }
            if (strcmp(text, shown) != 0) { printf("APP UI %s\n", text); (void)fflush(stdout); }
            memcpy(shown, text, sizeof shown);
            shown_status = status;
        }
        set(&displayed_state, current);
        if (current == APP_ERROR && config.once) { failed = 1; break; }
        if (current == STOPPED) break;
        if (config.once && current == READY && !requested_once) {
            requested_once = 1;
            set(&sweep_frame, 0); set(&sweep_total, SWEEP_FRAMES);
            change_state(READY, config.once == 2 ? SWEEPING : CAPTURING);
            continue;
        }
        if (config.once && get(&saved_count) && current == READY) break;
        received = g360ctl_buttons_next(fd, 50, &event, &dropped);
        if (dropped) { eligible_press = 0; hint = 0; }
        if (received < 0) { failed = 1; break; }
        if (received == 0) continue;
        /* Power held: the driver sends 116 once it recognises the hold. */
        if (event.key == G360CTL_KEY_POWER_HOLD && event.value == 1) { set(&power_off, 1); break; }
        /* During the countdown a Capture, Menu or Power press cancels it. */
        if (countdown_until) {
            if (event.value == 1 && (event.key == G360CTL_KEY_CAPTURE || event.key == G360CTL_KEY_MENU ||
                                     event.key == G360CTL_KEY_POWER)) {
                eligible_press = 0; hint = 0;
                /* Power also stops auto photos. */
                if (event.key == G360CTL_KEY_POWER) auto_stop("Power pressed");
                if (countdown_until) cancel_countdown("");
            }
            continue;
        }
        if (ui.screen != UI_NORMAL) { settings_event(&ui, &event, dropped, &eligible_press); continue; }
        /* A Menu press at READY opens the settings. (A hold's own code, 215,
           is ignored here; the press already opened them.) */
        if (event.key == G360CTL_KEY_MENU && event.value == 1) {
            if (get(&state) == READY && !eligible_press) {
                ui.screen = UI_LIST; ui.item = 0; hint = 0;
                wifi_home_ok = g360_wifi_file_present();
                printf("APP SETTINGS opened\n"); (void)fflush(stdout);
            }
            continue;
        }
        /* A short Power press stops auto photos. */
        if (event.key == G360CTL_KEY_POWER && event.value == 1) { auto_stop("Power pressed"); continue; }
        if (event.key != G360CTL_KEY_CAPTURE) continue;
        if (event.value == 1) {
            eligible_press = get(&state) == READY && !dropped;
            pressed_at = now_ms(); hint = 0;
            set(&wear_warning, 0); /* seen: back to READY */
        } else if (event.value == 0) {
            if (eligible_press) {
                /* HDR mode: every press is a sweep; single mode: a hold is. */
                int sweep_now = get(&setting_mode) == G360_MODE_HDR || now_ms() - pressed_at >= SWEEP_HOLD_MS;
                press_capture(sweep_now, 0, 0u);
            }
            eligible_press = 0; hint = 0;
        }
    }
    set(&stopping, 1);
    if (remote_listener >= 0) (void)unlink(G360_REMOTE_SOCKET);
    g360_app_preview_shutdown();
    show_light(G360_LIGHT_OFF); /* the light shows what the app knows; it is gone */
    if (get(&power_off)) {
        printf("APP POWER HOLD\n"); (void)fflush(stdout);
        (void)g360display_show("POWER OFF");
    }
    /* An in-flight shot (or sweep frame) is saved first; the worker then
       closes the capture session and storage. */
    if (pthread_join(worker_thread, 0) != 0) failed = 1;
    g360ctl_buttons_close(fd);
    if (get(&power_off)) return power_off_now();
    return failed || worker_failed ? 1 : 0;
usage:
    fprintf(stderr, "usage: g360-app --free-bytes N [--root DIR] [--log PATH] [--queue DIR --queue-free-bytes N]\n"
                    "                [--once | --sweep-once] [--sweep-save flash|card]\n"
                    "                [--output raw|jpeg|raw+jpeg] [--sounds on|off]\n"
                    "                [--event-device PATH] [--wear-dir DIR] [--status-root DIR]\n"
                    "       g360-app --store-settings [--root DIR] [--sweep-save flash|card]\n"
                    "                [--output raw|jpeg|raw+jpeg] [--sounds on|off]\n");
    return 2;
}
