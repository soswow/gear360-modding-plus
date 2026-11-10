/* Battery and Wi-Fi state for the screen's status corner, read from the
 * kernel's own files: no daemon, no D-Bus. The app reads it every 2 s and
 * redraws only when something changed.
 *
 * Battery (the Samsung battery driver, /sys/class/power_supply):
 *   battery/capacity   0-100
 *   battery/status     Charging, Discharging, Not charging, Full
 *   ac|usb|wireless/online   1 while that cable gives power
 * The battery icon's walls animate while Charging, and while Full with a
 * cable in (g360display.h).
 *
 * Wi-Fi, the same test as src/wifi/wifi-join.sh:
 *   access point: a hostapd process, and wlan0 has the access point's
 *     network (192.168.43.0 in /proc/net/route);
 *   home network: wlan0's operstate is "up" and it has a default route;
 *   otherwise none. Temporary script markers indicate search/reconnection
 *   and errors separately, so the physical AP state remains available.
 *
 * ROOT is put before every path: "" on the camera, a test tree on the host. */
#ifndef G360_APP_STATUS_H
#define G360_APP_STATUS_H
#include "g360display.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Fills status; whatever can't be read is unknown (battery -1, no bolt,
 * no Wi-Fi). Returns 0 when the battery was read, -1 otherwise. */
int g360_status_read(const char *root, g360display_status *status);
/* The side LED's state (the charge light): Full with a cable in is
 * G360_LIGHT_FULL, Charging is G360_LIGHT_CHARGING, anything else (or
 * unreadable) is G360_LIGHT_OFF. It follows the kernel's status, like the
 * screen's charging animation. */
#define G360_LIGHT_OFF 0
#define G360_LIGHT_CHARGING 1
#define G360_LIGHT_FULL 2
int g360_status_light(const char *root);
/* 1 while a charger gives power (ac, usb or wireless online), else 0. */
int g360_status_cable(const char *root);
/* Read the Wi-Fi DHCP address as two display lines, split after octet 2.
 * Returns 0 only for a valid IPv4 address. */
int g360_status_home_ip(const char *root, char text[20]);
#ifdef __cplusplus
}
#endif
#endif
