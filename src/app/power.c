/* The stock app's power-off sequence; see power.h. */
#include "power.h"

typedef int (*set_property_fn)(int device, int property, int value);
typedef int (*request_power_off_fn)(void);

static void wait_ms(unsigned ms)
{
    capture_timespec t;
    t.tv_sec = ms / 1000; t.tv_nsec = (long)(ms % 1000) * 1000000L;
    (void)nanosleep(&t, 0);
}

static void *symbol(const char *library, const char *name)
{
    void *handle = dlopen(library, CAPTURE_RTLD_NOW);
    return handle ? dlsym(handle, name) : 0;
}

int g360_power_off(unsigned wait, g360_power_result *result)
{
    set_property_fn set_property;
    request_power_off_fn request_power_off;
    void *found;
    unsigned waited;
    result->flag = result->deviced = result->state = G360_POWER_NOT_RUN;
    sync();
    found = symbol(G360_POWER_NODE_LIBRARY, "device_set_property");
    if (!found) return G360_POWER_E_NODE;
    memcpy(&set_property, &found, sizeof set_property);
    result->flag = set_property(G360_POWER_DEVICE, G360_POWER_PROP_SHUTDOWN, 1);
    if (result->flag != 0) return G360_POWER_E_FLAG;
    found = symbol(G360_POWER_DEVICED_LIBRARY, "deviced_request_power_off");
    if (found) {
        memcpy(&request_power_off, &found, sizeof request_power_off);
        result->deviced = request_power_off();
    }
    sync();
    result->state = set_property(G360_POWER_DEVICE, G360_POWER_PROP_STATE, 0);
    if (result->state != 0) return G360_POWER_E_STATE;
    for (waited = 0; waited < wait; waited += 100) wait_ms(100);
    return G360_POWER_E_STILL_ON;
}
