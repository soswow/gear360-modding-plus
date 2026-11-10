/*
 * g360ctl: the camera's buttons, LEDs and sound, for a process that runs
 * with the stock app stopped. See README.md for what each interface is and
 * how it was found.
 */
#ifndef G360CTL_H
#define G360CTL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Buttons: Linux evdev, "Drime5 GPIO Keyboard" ---------------------- */

#define G360CTL_BUTTON_DEVICE "/dev/input/event0"

/* Key codes from real presses (26 September and 5 November 2026).
   Menu and Power are decided by the driver: a short press arrives as one
   press/release pulse at release time; a hold arrives as a different code,
   pressed once the hold is recognised and released at let-go. Capture
   reports its real press and release, so the caller times holds and
   double presses itself. Two buttons together arrive as their own codes,
   independently (trials C2 and C2e). The hold threshold is about 1.5 s
   for Menu. */
#define G360CTL_CODE_POWER 73u
#define G360CTL_CODE_CAPTURE 126u
#define G360CTL_CODE_MENU 127u
#define G360CTL_CODE_MENU_HOLD 215u   /* guided trial C2 */
#define G360CTL_CODE_POWER_HOLD 116u  /* guided trial C2e; Linux KEY_POWER */

typedef enum g360ctl_key {
    G360CTL_KEY_OTHER = 0,
    G360CTL_KEY_POWER = 1,
    G360CTL_KEY_CAPTURE = 2,
    G360CTL_KEY_MENU = 3,
    G360CTL_KEY_MENU_HOLD = 4,
    G360CTL_KEY_POWER_HOLD = 5
} g360ctl_key;

typedef struct g360ctl_button_event {
    int32_t sec;              /* the kernel's timestamp */
    int32_t usec;
    uint32_t code;            /* Linux key code */
    int32_t value;            /* 1 press, 0 release, 2 auto-repeat */
    g360ctl_key key;
} g360ctl_button_event;

g360ctl_key g360ctl_key_from_code(uint32_t code);
const char *g360ctl_key_name(g360ctl_key key);

/* Opens the device read-only and non-blocking, without a grab: other
   readers keep receiving the same events. Returns a descriptor or -1. */
int g360ctl_buttons_open(const char *device);

/*
 * Waits up to timeout_ms (negative: forever) for the next key event.
 * Returns 1 with *event filled, 0 on timeout, -1 on error or end of file.
 * Non-key events are skipped. *dropped (if not NULL) is set to 1 when the
 * kernel reported SYN_DROPPED: events were lost before this one.
 */
int g360ctl_buttons_next(int fd, int timeout_ms, g360ctl_button_event *event, int *dropped);

void g360ctl_buttons_close(int fd);

/* ---- LEDs: kernel LED class, /sys/class/leds/led.N --------------------- */

#define G360CTL_LED_ROOT "/sys/class/leds"
#define G360CTL_MAX_LEDS 16

/*
 * What each LED is, as the user saw it in trial C1b (5 November 2026),
 * each lit alone at 255. "Front" is the lens on the screen side. The timer
 * trigger blinks led.0 (side orange). With the stock app stopped the three
 * green ones were on (brightness 1).
 */
#define G360CTL_LED_SIDE_ORANGE 0
#define G360CTL_LED_SIDE_GREEN 1
#define G360CTL_LED_SIDE_BLUE 2
#define G360CTL_LED_FRONT_GREEN 3
#define G360CTL_LED_BACK_GREEN 4
#define G360CTL_LED_FRONT_ORANGE 5
#define G360CTL_LED_BACK_ORANGE 6

/* The name of an LED index ("side-orange" ...), or NULL for others. */
const char *g360ctl_led_name(int index);
/* The index for a name, or -1. */
int g360ctl_led_index(const char *name);

typedef struct g360ctl_led_state {
    int present;
    uint32_t brightness;
    uint32_t max_brightness;
    char trigger[48];         /* the active trigger; "none" when none */
    int has_delays;           /* delay_on/delay_off were readable (timer trigger) */
    uint32_t delay_on;
    uint32_t delay_off;
} g360ctl_led_state;

/*
 * Reads led.0 .. led.(max-1) under root (NULL: /sys/class/leds) into
 * states[0..max-1]; absent LEDs have present = 0. Returns how many are
 * present, or -1 when max is out of range.
 */
int g360ctl_leds_read(const char *root, g360ctl_led_state *states, int max);

/* Sets one LED steadily: trigger "none", then brightness (at most its
   max_brightness). Returns 0, or -1 on a bad index/value or a failed write. */
int g360ctl_led_set(const char *root, int index, uint32_t brightness);

/* Blinks one LED with the kernel's timer trigger: trigger "timer", then
   delay_on/delay_off in milliseconds (attributes the kernel creates with
   that trigger), then brightness. Returns 0 or -1; a failure after the
   trigger write leaves the LED on the timer trigger, so callers restore
   saved states (g360ctl_leds_restore) when they are done. */
int g360ctl_led_blink(const char *root, int index, uint32_t brightness, uint32_t on_ms,
                      uint32_t off_ms);

/* Puts every present LED back as saved: its trigger, then its delays (timer)
   or brightness (none). Returns 0, or -1 if any write failed (it still
   tries every LED). */
int g360ctl_leds_restore(const char *root, const g360ctl_led_state *states, int count);

/* Writes and reads the saved states as text, one LED per line, so a restore
   can run from another process. Return 0 / the number of LEDs, or -1. */
int g360ctl_leds_save_file(const char *path, const g360ctl_led_state *states, int count);
int g360ctl_leds_load_file(const char *path, g360ctl_led_state *states, int max);

/* ---- Sound ------------------------------------------------------------- */

#define G360CTL_PAPLAY "/usr/bin/paplay"
#define G360CTL_APLAY "/usr/bin/aplay"
#define G360CTL_SOUND_DIR "/usr/apps/com.samsung.di-camera-app/res/sounds/camera"

/*
 * Plays a sound file by running player (for example G360CTL_PAPLAY) with the
 * file as its only argument, and waits for it. Returns the player's exit
 * status (0: played), -1 if it could not be started, or 128 + signal if it
 * was killed.
 */
int g360ctl_sound_play(const char *player, const char *file);

/*
 * The same player, not waited for. Start returns the player's pid (> 0) or
 * -1. Reap, called now and then, returns 1 once that player has ended (and
 * frees it), 0 while it still plays. Stop ends it now (SIGTERM) and waits.
 */
int g360ctl_sound_start(const char *player, const char *file);
int g360ctl_sound_reap(int pid);
void g360ctl_sound_stop(int pid);

#ifdef __cplusplus
}
#endif

#endif /* G360CTL_H */
