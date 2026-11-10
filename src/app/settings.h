/* The user's settings, kept on the card across launches and reboots.
 *
 * Two 20-byte slots, STATE/settings.0 and STATE/settings.1, written in place
 * like the photo counter (never renamed: renaming over a file on the card
 * leaves the old entry live). Each holds "G360SET1", a sequence number, the
 * four values (one byte each) and a checksum; byte 13 held the HDR shot
 * count (6 or 8) and is now always written as 6, the only sweep; the valid slot with the higher
 * sequence wins, and a save writes the other one. The Wi-Fi byte was 0 in
 * slots written before it existed, which reads as "not set". The self-timer
 * delay shares the Wi-Fi byte: Wi-Fi in the low two bits, the delay's index
 * (0 off, 1 2 s, 2 5 s, 3 10 s) in bits 4-5 of the high one. Bit 6 is set when
 * the sounds are OFF. Output uses bit 2 for RAW only and bit 7 for JPEG only;
 * both clear means RAW+JPEG. Older RAW ON slots migrate to RAW+JPEG and RAW
 * OFF slots to JPEG; both bits set is invalid. No capture/SDK calls. */
#ifndef G360_APP_SETTINGS_H
#define G360_APP_SETTINGS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { G360_MODE_SINGLE = 0, G360_MODE_HDR = 1 };
enum { G360_OUTPUT_RAW = 0, G360_OUTPUT_JPEG = 1, G360_OUTPUT_BOTH = 2 };
enum { G360_SAVE_FLASH = 0, G360_SAVE_CARD = 1 };
/* Wi-Fi: not chosen yet (the app treats it as HOME when the card has the
   network file, else AP), off, the camera's own access point, or the
   network in the card's network file. */
enum { G360_WIFI_UNSET = 0, G360_WIFI_OFF = 1, G360_WIFI_AP = 2, G360_WIFI_HOME = 3 };

typedef struct g360_settings {
    uint32_t mode;    /* G360_MODE_*: what a Capture press takes */
    uint32_t save;    /* G360_SAVE_*: sweep frames via the built-in flash, or straight to the card */
    uint32_t wifi;    /* G360_WIFI_* */
    uint32_t delay;   /* self-timer before a capture starts: 0 (off), 2, 5 or 10 seconds */
    uint32_t sounds;  /* 1 (ON, default): the app plays its sounds; 0 (OFF): it plays none */
    uint32_t output;  /* G360_OUTPUT_*: RAW (DNG), JPEG, or both, for stills and sweeps */
    uint32_t sequence;
    unsigned slot;
} g360_settings;

/* The defaults: single image, flash first, Wi-Fi not set, no delay, sounds on, RAW+JPEG. */
void g360_settings_default(g360_settings *settings);
/* Loads from STATE. Returns 1 if a slot was valid, 0 if none was (the
 * defaults are set, also for a missing directory), -1 on a read error
 * (the defaults are set). */
int g360_settings_load(const char *state_dir, g360_settings *settings);
/* Writes the older slot and fsyncs it (and the directory when the slot is
 * new). Returns 0 or -1; on failure the stored settings are unchanged. */
int g360_settings_save(const char *state_dir, g360_settings *settings);
#ifdef __cplusplus
}
#endif
#endif
