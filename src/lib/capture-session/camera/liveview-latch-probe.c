/*
 * Event-only, liveview-only diagnostic with a destination-alive observation.
 *
 * This is a separate artifact from liveview-only-probe.c on purpose: that
 * probe's ARM binary is pinned by hash in the trial-01 review, so its source
 * must keep reproducing that exact binary. This probe adds one thing only,
 * the read of the transport's destination-alive byte, and it reads it in the
 * same process that issues the liveview request.
 *
 * It is not the capture backend. It never reads, retains, copies, or releases
 * any image data. It issues no still command of any kind and it never
 * dereferences a callback data pointer.
 */
#include "capture_libc.h"

#include <stdint.h>

#define SDK_LIBRARY "/usr/lib/libcapture-fw-prod.so"

/*
 * The stock liveview start edge. Selector 27 is the value the pinned stock
 * application passes to the library's ioctl entry point; the selector literal
 * appears exactly once in this file so that a source check can prove there is
 * one and only one liveview request site.
 */
#define LIVEVIEW_IOCTL_SELECTOR 0x1bu

/*
 * The notification id this diagnostic subscribes to. The handle callback id
 * 8 is the small id the app-facing callback family receives as word 0; the
 * library converts it to the public topic 0x02030001 and then to the
 * internal topic 0x01030001 by itself.
 */
#define NOTIFY_HANDLE_ID 8u

/* The library's id list is zero terminated. */
#define NOTIFY_ID_LIST_END 0u

#define OBSERVE_NS UINT64_C(20000000000)
#define POLL_NS UINT64_C(100000000)

#if defined(__APPLE__)
#define PROBE_O_APPEND O_APPEND
#else
#define PROBE_O_APPEND 0x0400
#endif

typedef void *(*adapter_instance_fn)(void);
typedef int (*adapter_initialize_fn)(void *instance);
typedef void *(*beauty_instance_fn)(void);
typedef int (*mm_camera_create_fn)(unsigned int *out_handle);
/*
 * The callback word is carried as uintptr_t so that the target's 32-bit
 * machine word is exactly the reviewed word while a 64-bit host test can
 * still round-trip a function pointer.
 *
 * The handle is passed BY VALUE to every one of these entry points. The
 * library's own handle validator (Ghidra 0x426c8) compares the incoming word
 * against the open-camera list by value, and the pinned stock application
 * dereferences its own global handle slot before passing the result. The
 * handle word is therefore an opaque handle token, not a pointer to a slot.
 */
typedef int (*mm_camera_set_notification_callback_fn)(uint32_t handle,
                                                       uintptr_t callback,
                                                       uint32_t userdata);
typedef int (*mm_camera_set_notification_id_fn)(uint32_t handle, uint32_t id,
                                                ...);
typedef void *(*mm_camera_ioctl_fn)(uint32_t handle, uint32_t selector,
                                    int *p3, int p4, int *p5, int *p6);

/*
 * The callback family the application registers receives three raw words: the
 * small handle id, a data pointer and a length. The data pointer is never
 * dereferenced, never retained and never released here.
 */
typedef void (*notify_callback_fn)(int id, void *data, unsigned int length);

/*
 * The bridge's destination-alive getter. In the pinned bridge it returns the
 * single byte m_bDestAlive that CSender::Send tests before it transmits.
 */
typedef unsigned char (*bridge_alive_fn)(void);

typedef char liveview_latch_function_pointer_size_matches[
    (sizeof(void *) == sizeof(adapter_instance_fn) &&
     sizeof(void *) == sizeof(adapter_initialize_fn) &&
     sizeof(void *) == sizeof(beauty_instance_fn) &&
     sizeof(void *) == sizeof(mm_camera_create_fn) &&
     sizeof(void *) == sizeof(mm_camera_set_notification_callback_fn) &&
     sizeof(void *) == sizeof(mm_camera_set_notification_id_fn) &&
     sizeof(void *) == sizeof(mm_camera_ioctl_fn) &&
     sizeof(void *) == sizeof(bridge_alive_fn)) ? 1 : -1];

/*
 * The handle is an opaque token returned by mm_camera_create. It is passed by
 * value to the notification entry points and to the ioctl entry point, so it is
 * kept as a single word here and never treated as a pointer to a handle record.
 */
static int log_fd = -1;
static volatile int log_failure;
static volatile uint32_t callback_count;

/*
 * The resolved destination-alive byte address, and the getter, both taken from
 * the SDK handle. The capture library NEEDs libmulticore-bridge.so, so the
 * handle's dependency tree is the very instance whose byte CSender::Send
 * tests. Resolving through the SDK handle therefore cannot pick up a second,
 * unrelated copy of the bridge.
 */
static void *latch_variable_address;
static bridge_alive_fn latch_getter;

static int evidence_failed(void)
{
    return __sync_fetch_and_add(&log_failure, 0);
}

static int write_line(const char *line, size_t length)
{
    /* One bounded write keeps concurrent callback records from interleaving. */
    ptrdiff_t written = write(log_fd, line, length);
    if (written < 0 || (size_t)written != length) {
        __sync_lock_test_and_set(&log_failure, 1);
        return -1;
    }
    if (fsync(log_fd) != 0) {
        __sync_lock_test_and_set(&log_failure, 1);
        return -1;
    }
    return 0;
}

static void log_text(const char *text)
{
    char line[384];
    capture_timespec now;
    int length;

    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &now) == 0) {
        length = snprintf(line, sizeof(line),
                          "liveview-latch-probe: monotonic=%ld.%09ld %s\n",
                          now.tv_sec, now.tv_nsec, text);
    } else {
        length = snprintf(line, sizeof(line),
                          "liveview-latch-probe: monotonic=unavailable %s\n",
                          text);
    }
    if (length > 0) {
        size_t count = (size_t)length;
        if (count >= sizeof(line)) count = sizeof(line) - 1;
        (void)write_line(line, count);
    }
}

static void log_values(const char *event, int32_t first, uint32_t second,
                       int32_t third, uint32_t fourth)
{
    char message[256];
    (void)snprintf(message, sizeof(message),
                   "%s first=%ld second=%lu third=%ld fourth=%lu",
                   event, (long)first, (unsigned long)second,
                   (long)third, (unsigned long)fourth);
    log_text(message);
}

static void log_pointer(const char *event, const void *value)
{
    char message[160];
    (void)snprintf(message, sizeof(message), "%s pointer=%lu", event,
                   (unsigned long)(uintptr_t)value);
    log_text(message);
}

/*
 * The app-facing notification callback. Word 0 is the small handle id and
 * word 2 is the length. Word 1 is the library's data pointer: it is not
 * dereferenced, not retained and not released here.
 */
static void on_notify(int id, void *data, unsigned int length)
{
    (void)data; /* callback pointers are intentionally opaque and unused */
    __sync_fetch_and_add(&callback_count, 1u);
    log_values("callback.notify", (int32_t)id, length, 0, 0);
}

static int resolve_symbol(void *handle, const char *name, void *target,
                          size_t target_size)
{
    void *symbol = dlsym(handle, name);
    if (symbol == 0 || target_size != sizeof(symbol)) {
        char message[192];
        (void)snprintf(message, sizeof(message),
                       "required SDK symbol missing: %s; bootstrap not entered",
                       name);
        log_text(message);
        return -1;
    }
    memcpy(target, &symbol, target_size);
    return 0;
}

/*
 * Resolve the destination-alive byte. The variable symbol gives the exact
 * address of the byte; the getter is read as a second, independent view of
 * the same byte. Both come from the SDK handle, so both describe the bridge
 * instance the capture library itself uses.
 */
static int resolve_latch(void *sdk_handle)
{
    void *variable;
    void *getter;

    variable = dlsym(sdk_handle, "_ZN7CSender12m_bDestAliveE");
    getter = dlsym(sdk_handle, "_ZN16CMulticoreBridge18IsDestinationAliveEv");
    if (variable == 0 || getter == 0) {
        log_text("destination-alive symbols not resolvable through the SDK handle; observation unavailable");
        return -1;
    }
    latch_variable_address = variable;
    memcpy(&latch_getter, &getter, sizeof(latch_getter));
    log_pointer("destination-alive variable address", latch_variable_address);
    log_pointer("destination-alive getter address", (const void *)latch_getter);
    return 0;
}

/*
 * Read the destination-alive byte. The variable address is authoritative: it
 * is the byte CSender::Send loads. The getter is called as well so that the
 * two views can be compared; a disagreement would mean the observation is not
 * describing the byte the send path tests.
 */
static int read_latch(unsigned char *variable_value, unsigned char *getter_value)
{
    volatile unsigned char *byte;

    if (latch_variable_address == 0 || latch_getter == 0) return -1;
    __sync_synchronize();
    byte = (volatile unsigned char *)latch_variable_address;
    *variable_value = *byte;
    *getter_value = latch_getter();
    __sync_synchronize();
    return 0;
}

static void log_latch(const char *event, unsigned char value)
{
    char message[160];
    (void)snprintf(message, sizeof(message), "%s value=%lu", event,
                   (unsigned long)value);
    log_text(message);
}

static int parse_ascii_u64(const char *text, uint64_t *result)
{
    uint64_t value = 0;
    size_t i;
    if (text == 0 || text[0] == '\0') return -1;
    for (i = 0; text[i] != '\0'; ++i) {
        uint32_t digit;
        if (text[i] < '0' || text[i] > '9') return -1;
        digit = (uint32_t)(text[i] - '0');
        if (value > (UINT64_MAX - digit) / 10u) return -1;
        value = value * 10u + digit;
    }
    *result = value;
    return 0;
}

static int valid_cli(int argc, char **argv, const char **log_path,
                     uint64_t *free_bytes)
{
    if (argc != 8 || strcmp(argv[1], "--diagnostic-liveview-latch-only") != 0 ||
        strcmp(argv[2], "--library") != 0 ||
        strcmp(argv[3], SDK_LIBRARY) != 0 ||
        strcmp(argv[4], "--log") != 0 || argv[5][0] == '\0' ||
        strcmp(argv[6], "--free-bytes") != 0 ||
        parse_ascii_u64(argv[7], free_bytes) != 0) {
        return -1;
    }
    *log_path = argv[5];
    return 0;
}

static int monotonic_ns(uint64_t *value)
{
    capture_timespec now;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &now) != 0 ||
        now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1000000000L) {
        return -1;
    }
    *value = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
             (uint64_t)now.tv_nsec;
    return 0;
}

/*
 * The bounded observation window: twenty seconds at a hundred millisecond
 * cadence. The window records arrivals only. It does not certify a fresh
 * peer response and it does not associate an arrival with any request. The
 * destination-alive byte is sampled once per tick so that a latch change
 * during the window is recorded even though it happened after the request.
 */
static int observe_for_twenty_seconds(void)
{
    uint64_t start;
    uint64_t now;
    capture_timespec pause;
    capture_timespec remaining;
    unsigned char variable_value = 0u;
    unsigned char getter_value = 0u;
    unsigned char last_value = 0u;
    int have_last = 0;

    if (monotonic_ns(&start) != 0) {
        log_text("callback observation incomplete: monotonic clock unavailable; outcome indeterminate; no retry");
        return -1;
    }
    for (;;) {
        if (monotonic_ns(&now) != 0 || now < start) {
            log_text("callback observation incomplete: monotonic clock unavailable or reversed; outcome indeterminate; no retry");
            return -1;
        }
        if (now - start >= OBSERVE_NS) break;
        if (read_latch(&variable_value, &getter_value) == 0) {
            if (!have_last || variable_value != last_value) {
                log_latch("destination-alive during observation", variable_value);
                last_value = variable_value;
                have_last = 1;
            }
        }
        pause.tv_sec = 0;
        pause.tv_nsec = (long)POLL_NS;
        if (nanosleep(&pause, &remaining) != 0) {
            log_text("callback observation incomplete: monotonic sleep failed; outcome indeterminate; no retry");
            return -1;
        }
    }
    return evidence_failed() ? -1 : 0;
}

int main(int argc, char **argv)
{
    const char *log_path = 0;
    uint64_t free_bytes = 0;
    void *sdk_handle;
    adapter_instance_fn instance_fn = 0;
    adapter_initialize_fn initialize_fn = 0;
    beauty_instance_fn beauty_fn = 0;
    mm_camera_create_fn create_fn = 0;
    mm_camera_set_notification_callback_fn set_callback_fn = 0;
    mm_camera_set_notification_id_fn set_id_fn = 0;
    mm_camera_ioctl_fn ioctl_fn = 0;
    void *instance;
    void *beauty;
    void *ioctl_result;
    unsigned int handle = 0u;
    int status;
    int ioctl_status;
    int observation_status;
    int latch_status;
    uint64_t clock_check;
    unsigned char latch_before = 0u;
    unsigned char latch_before_getter = 0u;
    unsigned char latch_after = 0u;
    unsigned char latch_after_getter = 0u;

    (void)sizeof(liveview_latch_function_pointer_size_matches);
    if (valid_cli(argc, argv, &log_path, &free_bytes) != 0) {
        fprintf(stderr,
                "usage: liveview-latch-probe --diagnostic-liveview-latch-only --library %s --log OWNEDPATH --free-bytes ASCIIU64\n",
                SDK_LIBRARY);
        return 2;
    }
    log_fd = open(log_path, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC | PROBE_O_APPEND,
                  0644);
    if (log_fd < 0) {
        fprintf(stderr, "liveview-latch-probe: cannot open owned log path\n");
        return 1;
    }
    log_text("diagnostic-only start; no image data will be read or saved");
    log_text("liveview diagnostic with destination-alive observation; no still command is issued and no image or RAW pointer is accessed");
    if (evidence_failed() || monotonic_ns(&clock_check) != 0) {
        log_text("initial monotonic clock/log preflight failed; SDK not loaded");
        return 1;
    }
    log_values("free-bytes accepted as ASCII u64; unused by the liveview diagnostic (low word, high word)",
               (int32_t)(uint32_t)free_bytes, (uint32_t)(free_bytes >> 32), 0,
               0);

    log_text("opening pinned SDK library");
    sdk_handle = dlopen(SDK_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    if (sdk_handle == 0) {
        log_text("SDK dlopen failed");
        return 1;
    }

    /* Resolve every needed SDK entry point before any bootstrap call. */
    if (resolve_symbol(sdk_handle, "_ZN11CMCBAdapter8InstanceEv",
                       &instance_fn, sizeof(instance_fn)) != 0 ||
        resolve_symbol(sdk_handle, "_ZN11CMCBAdapter10InitializeEv",
                       &initialize_fn, sizeof(initialize_fn)) != 0 ||
        resolve_symbol(sdk_handle,
                       "_ZN19CBeautyFaceIf_Linux11GetInstanceEv",
                       &beauty_fn, sizeof(beauty_fn)) != 0 ||
        resolve_symbol(sdk_handle, "mm_camera_create", &create_fn,
                       sizeof(create_fn)) != 0 ||
        resolve_symbol(sdk_handle, "mm_camera_set_notification_callback",
                       &set_callback_fn, sizeof(set_callback_fn)) != 0 ||
        resolve_symbol(sdk_handle, "mm_camera_set_notification_id",
                       &set_id_fn, sizeof(set_id_fn)) != 0 ||
        resolve_symbol(sdk_handle, "mm_camera_ioctl", &ioctl_fn,
                       sizeof(ioctl_fn)) != 0) {
        return 1;
    }
    log_text("all required SDK symbols resolved");

    /*
     * The latch observation is best effort: a failure to resolve it must not
     * change the liveview request itself. The probe still runs the request
     * and records that the observation was unavailable.
     */
    latch_status = resolve_latch(sdk_handle);
    if (latch_status == 0) {
        log_text("destination-alive observation armed");
    } else {
        log_text("destination-alive observation unavailable; the liveview request is unchanged");
    }

    log_text("adapter Instance entering");
    instance = instance_fn();
    if (instance == 0) {
        log_text("adapter Instance returned null; abort");
        return 1;
    }
    log_text("adapter Initialize(instance) entering");
    status = initialize_fn(instance);
    log_values("adapter Initialize(instance) returned", status, 0, 0, 0);
    if (status != 0) {
        log_text("adapter initialization failed; abort");
        return 1;
    }
    beauty = beauty_fn();
    if (beauty == 0) {
        log_text("Beauty singleton getter returned null; abort");
        return 1;
    }
    log_text("Beauty singleton getter returned nonnull");

    log_text("mm_camera_create entering");
    status = create_fn(&handle);
    log_values("mm_camera_create returned", status, handle, 0, 0);
    if (status != 0) {
        log_text("mm_camera_create failed; abort");
        return 1;
    }

    /*
     * Registration order is fixed by the library: the callback registration
     * performs the one-time client registration and stores the resulting
     * client slot, and the id subscription reads that slot. Registering the
     * callback first is therefore mandatory.
     */
    log_text("mm_camera_set_notification_callback intent: register the app-facing callback once, before any id subscription");
    status = set_callback_fn(handle, (uintptr_t)on_notify, 0u);
    log_values("mm_camera_set_notification_callback returned", status, 0, 0, 0);
    if (status != 0) {
        log_text("notification callback registration failed; abort before any id subscription");
        return 1;
    }

    log_text("mm_camera_set_notification_id intent: subscribe handle id 8 only, zero-terminated id list");
    status = set_id_fn(handle, NOTIFY_HANDLE_ID, NOTIFY_ID_LIST_END);
    log_values("mm_camera_set_notification_id returned", status,
               NOTIFY_HANDLE_ID, 0, 0);
    if (status != 0) {
        log_text("notification id subscription failed; abort before any liveview request");
        return 1;
    }
    log_text("notification id subscription accepted; callback delivery is still unproven");

    /*
     * The optional status read of the shared liveview preference boolean is
     * omitted on purpose. The extent semantics of that ioctl arm, meaning
     * which of the two output parameters receives the status byte and which
     * receives the written length, are not established for this call shape,
     * so the read is not attempted rather than guessed.
     */
    log_text("status read not attempted: extent semantics unqualified");

    if (evidence_failed() || monotonic_ns(&clock_check) != 0) {
        log_text("pre-liveview monotonic clock/log check failed; no liveview request issued");
        return 1;
    }

    /*
     * The destination-alive byte immediately before the request. This is the
     * value CSender::Send tests when it decides whether to transmit.
     */
    if (read_latch(&latch_before, &latch_before_getter) == 0) {
        log_latch("destination-alive before liveview request", latch_before);
        log_latch("destination-alive getter before liveview request",
                  latch_before_getter);
        if (latch_before != latch_before_getter) {
            log_text("destination-alive variable and getter disagree before the request; observation is not authoritative");
        }
    } else {
        log_text("destination-alive could not be read before the request");
    }

    {
        char message[256];
        (void)snprintf(message, sizeof(message),
                       "mm_camera_ioctl intent: handle=%lu selector=%lu p3=0 p4=0 p5=null p6=null; exactly one call; no retry; a zero return is not sensor readiness",
                       (unsigned long)handle,
                       (unsigned long)LIVEVIEW_IOCTL_SELECTOR);
        log_text(message);
    }
    ioctl_result = ioctl_fn(handle, LIVEVIEW_IOCTL_SELECTOR, 0, 0, 0, 0);
    log_pointer("mm_camera_ioctl returned", ioctl_result);
    ioctl_status = (ioctl_result != 0) ? 1 : 0;
    if (ioctl_status != 0) {
        log_text("liveview start edge returned nonzero; outcome indeterminate; no retry");
    } else {
        log_text("liveview start edge returned zero; transport request only; sensor readiness unproven");
    }

    /*
     * The same byte again immediately after the request. A latch that is
     * unchanged across the request means the transport did not admit the
     * frame on the strength of the latch.
     */
    if (read_latch(&latch_after, &latch_after_getter) == 0) {
        log_latch("destination-alive after liveview request", latch_after);
        log_latch("destination-alive getter after liveview request",
                  latch_after_getter);
    } else {
        log_text("destination-alive could not be read after the request");
    }

    observation_status = observe_for_twenty_seconds();
    log_values("diagnostic end; capture completion and readiness unproven; callbacks observed",
               (int32_t)__sync_fetch_and_add(&callback_count, 0u),
               (uint32_t)ioctl_status, 0, 0);
    log_text("a callback arrival is not request-associated and does not prove capture readiness");
    /* No stop/destroy/unregister/release/dlclose: process exit ends this probe. */
    return ioctl_status == 0 && observation_status == 0 && !evidence_failed() ? 0 : 1;
}
