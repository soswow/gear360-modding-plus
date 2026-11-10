/*
 * Read-only check (plan step 4e): call device_get_property(2, 0x20/0x18/0x19)
 * the way libcapture's gyro responder does, from our own process, and print
 * the return codes and values. Optionally load libcapture first (as our
 * capture probe does) to see whether that changes the result.
 *
 *   devprop-check [--with-libcapture | --libm-then-libcapture | --plugin-error]
 *
 * No capture command, no device write. Hosted build (dynamic libc), see
 * build-devprop-check.sh.
 */
#include "capture_libc.h"

#include <stdint.h>

typedef int (*device_get_property_fn)(int type, int prop, int *value);

int main(int argc, char **argv)
{
    void *dev;
    device_get_property_fn get = 0;
    void *symbol;
    static const int props[3] = {0x20, 0x18, 0x19};
    static const char *names[3] = {"size", "phys", "pos"};
    int i;

    if (argc == 2 && strcmp(argv[1], "--libm-then-libcapture") == 0) {
        /* The fix under test: libm global first, then libcapture, which
           loads libdevice-node and its plugin (which needs sqrt). */
        void *libm = dlopen("/lib/libm.so.6", CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
        void *sdk;
        printf("libm dlopen %s\n", libm ? "ok" : "FAILED");
        sdk = dlopen("/usr/lib/libcapture-fw-prod.so", CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
        printf("libcapture dlopen %s\n", sdk ? "ok" : "FAILED");
    }
    if (argc == 2 && strcmp(argv[1], "--with-libcapture") == 0) {
        void *sdk = dlopen("/usr/lib/libcapture-fw-prod.so",
                           CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
        printf("libcapture dlopen %s\n", sdk ? "ok" : "FAILED");
    }
    if (argc == 2 && strcmp(argv[1], "--libm-plugin-error") == 0) {
        void *libm = dlopen("/lib/libm.so.6", CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
        void *plugin;
        const char *error;
        printf("libm dlopen %s\n", libm ? "ok" : "FAILED");
        plugin = dlopen("/usr/lib/libslp_devman_plugin.so", CAPTURE_RTLD_NOW);
        error = dlerror();
        printf("plugin dlopen after libm %s: %s\n", plugin ? "ok" : "FAILED", error ? error : "(no error)");
    }
    if (argc == 2 && strcmp(argv[1], "--plugin-error") == 0) {
        void *plugin = dlopen("/usr/lib/libslp_devman_plugin.so", CAPTURE_RTLD_NOW);
        const char *error = dlerror();
        printf("plugin dlopen %s: %s\n", plugin ? "ok" : "FAILED", error ? error : "(no error)");
        plugin = dlopen("/usr/lib/libslp_devman_plugin.so", 1 /* RTLD_LAZY */);
        error = dlerror();
        printf("plugin dlopen lazy %s: %s\n", plugin ? "ok" : "FAILED", error ? error : "(no error)");
    }
    dev = dlopen("/usr/lib/libdevice-node.so.0.1", CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    if (dev == 0) {
        printf("libdevice-node dlopen FAILED\n");
        return 1;
    }
    symbol = dlsym(dev, "device_get_property");
    if (symbol == 0) {
        printf("device_get_property not found\n");
        return 1;
    }
    memcpy(&get, &symbol, sizeof(get));
    for (i = 0; i < 3; ++i) {
        int value = 0x7eadbeef;
        int rc = get(2, props[i], &value);
        printf("device_get_property(2, 0x%x) %s: rc=%d value=%d (0x%x)\n",
               props[i], names[i], rc, value, (unsigned)value);
    }
    return 0;
}
