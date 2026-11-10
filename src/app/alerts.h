/* Which stock sound to play when the battery, the charger or the Wi-Fi
 * link changes. Pure logic: the app feeds it a reading about once a second
 * and plays what it returns.
 *
 * Battery: a warning sounds when the level gets worse, not on every reading.
 *   20% or lower   low_battery.wav, once
 *   10% or lower   13_N_NoBattery.wav, three times in a row
 * Both are silent while a charger is connected. Unplugging at a low level
 * warns again. A first reading that is already low warns at once.
 *
 * Charger: charger_connect.wav when a cable appears. The first reading is
 * only the starting point (a charger already in at start is silent).
 *
 * Link: BT_connected.wav / BT_disconnected.wav (the stock app's Bluetooth
 * tones, used here for Wi-Fi). The link count is 1 while the camera is on a
 * home network, and the number of stations on the camera's own access
 * point; a rise sounds "connected", a fall "disconnected". The access point
 * with nobody on it is not a link. Changing the Wi-Fi mode drops the old
 * mode's links ("disconnected") and counts the new mode's afresh.
 * The first reading starts from no link, so joining the home network before
 * or just after the app starts is announced. */
#ifndef G360_APP_ALERTS_H
#define G360_APP_ALERTS_H
#ifdef __cplusplus
extern "C" {
#endif

#define G360_ALERT_LOW_BATTERY_PERCENT 20
#define G360_ALERT_NO_BATTERY_PERCENT 10
#define G360_ALERT_NO_BATTERY_REPEATS 3
/* The most one step returns: charger, disconnected x2, connected, 3 warnings. */
#define G360_ALERT_MAX 8

enum g360_alert_sound {
    G360_ALERT_LOW_BATTERY = 1, /* low_battery.wav */
    G360_ALERT_NO_BATTERY,      /* 13_N_NoBattery.wav */
    G360_ALERT_CONNECTED,       /* BT_connected.wav */
    G360_ALERT_DISCONNECTED,    /* BT_disconnected.wav */
    G360_ALERT_CHARGER          /* charger_connect.wav */
};

typedef struct g360_alert_input {
    int battery; /* 0-100, or -1 when unreadable (the level is left as it was) */
    int cable;   /* 1 while a charger gives power */
    int wifi;    /* G360DISPLAY_WIFI_OFF, _HOME or _AP */
    int clients; /* stations on the access point, -1 if unknown (the count is left as it was) */
} g360_alert_input;

typedef struct g360_alerts {
    int started, level, cable, mode, link;
} g360_alerts;

void g360_alerts_init(g360_alerts *alerts);
/* Takes a reading, writes the sounds to play, in order, to sounds (at least
 * G360_ALERT_MAX entries) and returns how many. */
int g360_alerts_step(g360_alerts *alerts, const g360_alert_input *in, int sounds[G360_ALERT_MAX]);
/* The file name of a sound (inside G360CTL_SOUND_DIR), or 0. */
const char *g360_alert_file(int sound);

#ifdef __cplusplus
}
#endif
#endif
