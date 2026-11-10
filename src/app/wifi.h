/* The WIFI setting is carried out by src/wifi/wifi-join.sh
 * ("apply off|ap|home"), which the app starts in the background at launch
 * and whenever the setting is chosen. The script works without the screen;
 * the app's status corner shows what happens (status.h).
 *
 * The script runs in its own session, with standard input, output and error
 * on /dev/null (it writes its own log), every other descriptor closed (the
 * app's locks, buttons and camera devices) and its working directory at /.
 * The app does not wait for it. */
#ifndef G360_APP_WIFI_H
#define G360_APP_WIFI_H
#ifdef __cplusplus
extern "C" {
#endif
#ifndef G360_WIFI_SCRIPT
#define G360_WIFI_SCRIPT "/opt/storage/sdcard/mods/g360wifi/wifi-join.sh"
#endif
/* Line 1 the network name, line 2 the password; HOME is offered only when
   it is there. */
#ifndef G360_WIFI_FILE
#define G360_WIFI_FILE "/opt/storage/sdcard/wifi"
#endif

/* The tool that lists the stations on the access point (Broadcom `wl`):
   "wl assoclist" prints one "assoclist MAC" line per station. */
#ifndef G360_WL_PATH
#define G360_WL_PATH "/usr/bin/wl"
#endif

/* The number of stations on the camera's access point, from running
   "assoclist" with the tool WL (G360_WL_PATH), or -1 if it can't be run or failed. Waits
   for the tool, so call it off the UI thread. */
int g360_wifi_ap_clients(const char *wl);
/* 1 if the network file can be opened, else 0. */
int g360_wifi_file_present(void);
/* Starts "sh G360_WIFI_SCRIPT apply MODE" (MODE "off", "ap" or "home").
   Returns 0 once it is started, -1 if the script is missing or the start
   failed. */
int g360_wifi_apply(const char *mode);
#ifdef __cplusplus
}
#endif
#endif
