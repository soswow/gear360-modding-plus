#include "alerts.h"
#include "g360display.h"

void g360_alerts_init(g360_alerts *alerts)
{
    alerts->started = 0; alerts->level = 0; alerts->cable = 0;
    alerts->mode = G360DISPLAY_WIFI_OFF; alerts->link = 0;
}

const char *g360_alert_file(int sound)
{
    switch (sound) {
    case G360_ALERT_LOW_BATTERY: return "low_battery.wav";
    case G360_ALERT_NO_BATTERY: return "13_N_NoBattery.wav";
    case G360_ALERT_CONNECTED: return "BT_connected.wav";
    case G360_ALERT_DISCONNECTED: return "BT_disconnected.wav";
    case G360_ALERT_CHARGER: return "charger_connect.wav";
    default: return 0;
    }
}

int g360_alerts_step(g360_alerts *a, const g360_alert_input *in, int sounds[G360_ALERT_MAX])
{
    int n = 0, level, link, previous, cable = in->cable != 0, mode = in->wifi;
    /* The charger. */
    if (a->started && cable && !a->cable) sounds[n++] = G360_ALERT_CHARGER;
    a->cable = cable;
    /* The Wi-Fi link. */
    if (mode != G360DISPLAY_WIFI_HOME && mode != G360DISPLAY_WIFI_AP) mode = G360DISPLAY_WIFI_OFF;
    if (mode == G360DISPLAY_WIFI_HOME) link = 1;
    else if (mode == G360DISPLAY_WIFI_AP) link = in->clients >= 0 ? in->clients : (a->mode == mode ? a->link : 0);
    else link = 0;
    previous = a->link;
    if (mode != a->mode) {
        if (previous > 0) sounds[n++] = G360_ALERT_DISCONNECTED;
        previous = 0;
    }
    if (link > previous) sounds[n++] = G360_ALERT_CONNECTED;
    else if (link < previous) sounds[n++] = G360_ALERT_DISCONNECTED;
    a->mode = mode; a->link = link;
    /* The battery. */
    level = a->level;
    if (cable) level = 0;
    else if (in->battery >= 0) {
        level = in->battery <= G360_ALERT_NO_BATTERY_PERCENT ? 2 : in->battery <= G360_ALERT_LOW_BATTERY_PERCENT ? 1 : 0;
    }
    if (level > a->level) {
        if (level == 2) {
            int k;
            for (k = 0; k < G360_ALERT_NO_BATTERY_REPEATS; ++k) sounds[n++] = G360_ALERT_NO_BATTERY;
        } else {
            sounds[n++] = G360_ALERT_LOW_BATTERY;
        }
    }
    a->level = level;
    a->started = 1;
    return n;
}
