#ifndef G360_POWER_H
#define G360_POWER_H
/*
 * Turning the camera off, the way the stock app does.
 *
 * The stock app's last step, CAPPPowerOffMode::powerOffSystemShutdown,
 * makes three calls (REA/Ghidra, 5 November 2026; see VALIDATION.md):
 *
 *   1. device_set_property(4, 0x15, 1)  "Power_Shutdown"
 *      libdevice-node's power device calls the devman plugin's
 *      OEM_sys_set_power_shutdown(1), which writes 1 to
 *      /sys/devices/platform/d5-keys/shutdown.
 *   2. deviced_request_power_off()      "Request_Power_Off"
 *      A blocking D-Bus call to deviced's PowerOff. deviced runs its
 *      power-off notifiers: it stops uevent handling, syncs and
 *      unmounts the card, turns USB off and stops the battery timers.
 *      It accepts the call only once, and only after "booting done".
 *   3. device_set_property(4, 4, 0)     "Power_Suspend"
 *      OEM_sys_set_power_state(0), which writes 2 to the same
 *      d5-keys/shutdown file.
 *
 * Both libraries are opened only here, with dlopen, so the app does not
 * load deviced's D-Bus stack until it powers off.
 *
 * deviced unmounts the card in step 2. If the card is busy it runs
 * "/sbin/fuser -m -k -SIGTERM /opt/storage/sdcard", then -SIGKILL 5 s
 * later: every process with anything on the card (its executable, its
 * working directory, an open file) is killed. Our executable is on the
 * card, so the sequence runs in a copy of it in /tmp (RAM), started with
 * --power-off, with no file on the card open (g360_power_relaunch and
 * g360_power_detach).
 */
#include "app_platform.h"

#ifndef G360_POWER_NODE_LIBRARY
#define G360_POWER_NODE_LIBRARY "libdevice-node.so.0.1"
#endif
#ifndef G360_POWER_DEVICED_LIBRARY
#define G360_POWER_DEVICED_LIBRARY "libdeviced.so.0.1.0"
#endif

#ifndef G360_POWER_SELF
#define G360_POWER_SELF "/proc/self/exe"
#endif
#ifndef G360_POWER_COPY
#define G360_POWER_COPY "/tmp/g360-app-poweroff"
#endif
#ifndef G360_POWER_LOG
#define G360_POWER_LOG "/tmp/g360-app-poweroff.log"
#endif

#define G360_POWER_DEVICE 4          /* libdevice-node's "power" device */
#define G360_POWER_PROP_SHUTDOWN 0x15
#define G360_POWER_PROP_STATE 4

enum {
    G360_POWER_E_NODE = -1,      /* libdevice-node or device_set_property missing */
    G360_POWER_E_FLAG = -2,      /* step 1 failed; nothing else was done */
    G360_POWER_E_STATE = -3,     /* step 3 failed */
    G360_POWER_E_STILL_ON = -4   /* every step returned, the camera stayed on */
};

/* A step that did not run: an earlier step stopped the sequence, or its
   library could not be opened. */
#define G360_POWER_NOT_RUN (-1000)

typedef struct g360_power_result {
    int flag;     /* step 1: device_set_property's result */
    int deviced;  /* step 2: deviced's reply (the number of notifiers run),
                     -1 if deviced refused, or below -1 for a D-Bus error */
    int state;    /* step 3: device_set_property's result */
} g360_power_result;

/*
 * Syncs, then runs the three steps. A failed step 1 stops the sequence
 * before deviced is asked, so the camera stays usable. A failed or refused
 * step 2 does not stop it: the files are already synced. Returns only if
 * the camera is still on wait_ms after step 3, with a negative status and
 * *result filled in.
 */
int g360_power_off(unsigned wait_ms, g360_power_result *result);

/* Copies the running executable to G360_POWER_COPY and replaces this
   process with the copy, run as "--power-off". Returns -1 only on failure. */
int g360_power_relaunch(void);

/* For the --power-off process: standard output and error go to
   G360_POWER_LOG, standard input to /dev/null, every other descriptor is
   closed and the working directory becomes /. Returns 0, or -1 if the log
   could not be opened (the rest is still done). */
int g360_power_detach(void);

#endif
