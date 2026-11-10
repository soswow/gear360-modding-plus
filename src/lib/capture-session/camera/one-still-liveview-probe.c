/*
 * Event-only, one-still diagnostic with the stock liveview-first ordering and
 * a destination-alive observation.
 *
 * This is a separate artifact from one-still-probe.c on purpose: that probe's
 * ARM binary is pinned by hash in the 29 October trial review
 * (53d0aae6f7aafe2a23b7db15348b6d001d03afad46ed03a4ba1efb0b2c7e00b1), so its
 * source must keep reproducing that exact binary.
 *
 * What this adds over one-still-probe.c, and why:
 *
 *  - The pinned stock application's CAPPCaptureMode::startStillCapture at
 *    0x82ed0 calls CAPPBaseMode::startLiveview BEFORE its still arm. This
 *    probe reproduces that ordering: one liveview start edge, then the still.
 *
 *  - The liveview start edge only succeeds once the transport's
 *    destination-alive byte is nonzero. One explicit empty category 0x10 send
 *    through the transport raises it. This probe performs that handshake
 *    before the liveview request, and reads the byte immediately before and
 *    after each request, so the admission state at each request is recorded
 *    rather than assumed.
 *
 * It is not the capture backend. It never reads, retains, copies, or releases
 * any image data. It issues no still command other than the single bounded
 * one below, and it never dereferences a callback data pointer.
 */
#include "capture_libc.h"
#include "capture_session_library.h"

#include <stdint.h>

#define SDK_LIBRARY "/usr/lib/libcapture-fw-prod.so"
#define BRIDGE_LIBRARY "/usr/lib/libmulticore-bridge.so"
#define ENCODED_SET_TOPIC CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL
/*
 * The third word of mm_camera_capture_still is the mm capture type, decoded
 * by the library (FUN_00042e18) into an internal command id, not a command
 * index. A plain single still is 1 (command 0x1010001); 0x15 is
 * LOCK3A_CAPTURE (command 0x1010016) and asks the RTOS to lock 3A as part
 * of the capture. See next-edge/still-path/CAPTURE-TYPE-SELECTION.md.
 */
#define STILL_CAPTURE_TYPE 1u
/*
 * The still-sequence notification topics, in the library's public encoded
 * spelling. The app-facing notification route converts handle id N to public
 * topic 0x201000N for ids 1..7 (publishConvert, see
 * next-edge/still-path/STILL-PATH.md), and RegNotifyCallbackId subscribes a
 * client slot to a public topic. id 7 is STL_CAPTURE_READY, the still
 * completion; ids 1, 3, 4, 5 and 6 are the sequence (start, capturing,
 * processing, qview done, nextshot ready). The trial-04 defect was
 * subscribing only the image-set topic, so the id-7 completion could never
 * arrive. These topics are now subscribed in the same order the id table
 * names them.
 */
#define NOTIFY_PUBLIC_STL_CAPTURE_START 0x2010001u
#define NOTIFY_PUBLIC_STL_CAPTURING 0x2010003u
#define NOTIFY_PUBLIC_STL_PROCESSING 0x2010004u
#define NOTIFY_PUBLIC_STL_QVIEW_DONE 0x2010005u
#define NOTIFY_PUBLIC_STL_NEXTSHOT_READY 0x2010006u
#define NOTIFY_PUBLIC_STL_CAPTURE_READY 0x2010007u
#define NOTIFY_CATEGORY 0x10u
#define NOTIFY_SETTLE_NS UINT64_C(5000000000)
#define OBSERVE_NS UINT64_C(20000000000)
#define ESTABLISH_NS UINT64_C(3000000000)
#define POLL_NS UINT64_C(100000000)

/*
 * The stock liveview start edge.  Selector 27 is the value the pinned stock
 * application passes to the library's ioctl entry point; the selector literal
 * appears exactly once in this file so that a source check can prove there is
 * one and only one liveview request site.
 */
#define LIVEVIEW_IOCTL_SELECTOR 0x1bu
/*
 * The stock application's liveview stop edge: the ui_mm_camera wrapper ends
 * in the helper call (0x1c, 0, 0), the library mapper maps 0x1c to
 * 0x1030002 (E_CAP_CMD_LV_STOP).  The still's isRawWorkEnd precondition
 * requires the liveview's RAW work to have drained, so the app's flow is
 * liveview -> stop -> still: the stop drains the RAW and frees the SSIF
 * buffers the still's drive needs (see
 * next-edge/still-path/DEATH-MECHANISM-BUFFER-STARVATION.md).
 */
#define LIVEVIEW_STOP_IOCTL_SELECTOR 0x1cu
#define DRAIN_NS UINT64_C(1000000000)

#if defined(__APPLE__)
#define PROBE_O_APPEND O_APPEND
#else
#define PROBE_O_APPEND 0x0400
#endif

typedef void *(*adapter_instance_fn)(void);
typedef int (*adapter_initialize_fn)(void *instance);
typedef void *(*beauty_instance_fn)(void);
typedef int (*mm_camera_create_fn)(unsigned int *out_handle);
typedef void (*notify_callback_fn)(int event, void *data, unsigned int size);
typedef void (*image_callback_fn)(int topic, void *rect_block,
                                  void *image_object, unsigned int size);
typedef void (*image_set_callback_fn)(int topic, void *image_set_slot,
                                      void *data, unsigned int size);
typedef uint32_t (*reg_callback_client_fn)(char *name,
                                           notify_callback_fn notify,
                                           image_callback_fn image,
                                           image_set_callback_fn image_set);
typedef int (*reg_image_set_callback_id_fn)(uint32_t slot, uint32_t topic);
typedef int (*reg_notify_callback_id_fn)(uint32_t slot, uint32_t topic);
typedef void *(*mm_camera_ioctl_fn)(uint32_t handle, uint32_t selector,
                                    int *p3, int p4, int *p5, int *p6);
typedef void (*mcb_send_fn)(unsigned short category, unsigned short attribute,
                            unsigned short length, unsigned char *payload);
typedef unsigned char (*mcb_alive_fn)(void);

/*
 * The original C parameter grouping is unknown. This diagnostic pins the
 * reviewed machine-word ABI: exactly seventeen 32-bit slots.
 */
typedef int (*capture_still_words17_fn)(
    uint32_t w1, uint32_t w2, uint32_t w3, uint32_t w4, uint32_t w5,
    uint32_t w6, uint32_t w7, uint32_t w8, uint32_t w9, uint32_t w10,
    uint32_t w11, uint32_t w12, uint32_t w13, uint32_t w14, uint32_t w15,
    uint32_t w16, uint32_t w17);

typedef char one_still_liveview_function_pointer_size_matches[
    (sizeof(void *) == sizeof(adapter_instance_fn) &&
     sizeof(void *) == sizeof(adapter_initialize_fn) &&
     sizeof(void *) == sizeof(beauty_instance_fn) &&
     sizeof(void *) == sizeof(mm_camera_create_fn) &&
     sizeof(void *) == sizeof(reg_callback_client_fn) &&
     sizeof(void *) == sizeof(reg_image_set_callback_id_fn) &&
     sizeof(void *) == sizeof(reg_notify_callback_id_fn) &&
     sizeof(void *) == sizeof(capture_still_words17_fn) &&
     sizeof(void *) == sizeof(mm_camera_ioctl_fn) &&
     sizeof(void *) == sizeof(mcb_send_fn) &&
     sizeof(void *) == sizeof(mcb_alive_fn)) ? 1 : -1];

static int log_fd = -1;
static volatile int log_failure;
static char client_name[] = "OneShot"; /* seven bytes; library limit is nine */
static volatile uint32_t callback_count;
static volatile uint32_t notify_callback_count;
static volatile uint32_t image_callback_count;
static volatile uint32_t image_set_callback_count;

/* The resolved destination-alive byte address, the getter and the send entry.
 * They come from the SDK handle, so they describe the bridge instance the
 * capture library itself uses. */
static void *latch_variable_address;
static mcb_alive_fn latch_getter;
static mcb_send_fn bridge_send;

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
                          "one-still-liveview-probe: monotonic=%ld.%09ld %s\n",
                          now.tv_sec, now.tv_nsec, text);
    } else {
        length = snprintf(line, sizeof(line),
                          "one-still-liveview-probe: monotonic=unavailable %s\n",
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

/* Observer's bounded hold marker: preserve this exact standalone line. */
static void log_hold_marker(const char *marker)
{
    size_t length = strlen(marker);
    char line[64];
    if (length + 1 >= sizeof(line)) return;
    memcpy(line, marker, length);
    line[length++] = '\n';
    (void)write_line(line, length);
}

static void on_notify(int event, void *data, unsigned int size)
{
    (void)data; /* callback pointers are intentionally opaque and unused */
    __sync_fetch_and_add(&callback_count, 1u);
    __sync_fetch_and_add(&notify_callback_count, 1u);
    log_values("callback.notify", (int32_t)event, size, 0, 0);
}

static void on_image(int topic, void *rect_block, void *image_object,
                     unsigned int size)
{
    (void)rect_block; /* never dereference or retain callback pointers */
    (void)image_object;
    __sync_fetch_and_add(&callback_count, 1u);
    __sync_fetch_and_add(&image_callback_count, 1u);
    log_values("callback.image", (int32_t)topic, size, 0, 0);
}

static void on_image_set(int topic, void *image_set_slot, void *data,
                         unsigned int size)
{
    (void)image_set_slot; /* never dereference or retain callback pointers */
    (void)data;
    __sync_fetch_and_add(&callback_count, 1u);
    __sync_fetch_and_add(&image_set_callback_count, 1u);
    log_values("callback.image_set", (int32_t)topic, size, 0, 0);
}

static int resolve_symbol(void *handle, const char *name, void *target,
                          size_t target_size)
{
    void *symbol = dlsym(handle, name);
    if (symbol == 0 || target_size != sizeof(symbol)) return -1;
    memcpy(target, &symbol, target_size);
    return 0;
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
    if (argc != 8 || strcmp(argv[1], "--diagnostic-one-still-liveview") != 0 ||
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
 * Read the destination-alive byte.  The variable address is authoritative:
 * it is the byte CSender::Send loads, at the same GOT data slot the bridge
 * getter resolves.  The getter is called as well so that the two views can be
 * compared; a disagreement would mean the observation does not describe the
 * byte the send path tests.
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

/*
 * The transport's admission handshake: one explicit empty category 0x10 send,
 * then a bounded poll for the destination-alive byte to become nonzero.
 *
 * The rise is recorded as an observation and is not attributed.  If the byte
 * does not rise within the settle window the liveview and still requests are
 * both suppressed, because neither would be admitted.
 */
static int admission_handshake(void)
{
    unsigned char variable_value = 0u;
    unsigned char getter_value = 0u;
    uint64_t start;
    uint64_t now;
    unsigned long elapsed_ms;
    capture_timespec pause;
    capture_timespec remaining;

    if (read_latch(&variable_value, &getter_value) != 0) {
        log_text("destination-alive could not be read before the admission handshake");
        return -1;
    }
    log_latch("destination-alive before admission handshake", variable_value);
    if (variable_value != getter_value) {
        log_text("destination-alive variable and getter disagree before the handshake; observation is not authoritative");
    }
    if (variable_value != 0u) {
        log_text("destination-alive baseline is already nonzero; the handshake is not needed and is not sent");
        return 0;
    }
    if (monotonic_ns(&start) != 0) {
        log_text("admission handshake skipped: monotonic clock unavailable");
        return -1;
    }
    log_values("admission handshake intent: one explicit empty category 0x10 send; category 16 attribute 0 length 0",
               NOTIFY_CATEGORY, 0, 0, 0);
    bridge_send((unsigned short)NOTIFY_CATEGORY, 0, 0, 0);
    for (;;) {
        if (monotonic_ns(&now) != 0 || now < start) {
            log_text("admission handshake incomplete: monotonic clock unavailable or reversed");
            return -1;
        }
        elapsed_ms = (unsigned long)((now - start) / UINT64_C(1000000));
        if (read_latch(&variable_value, &getter_value) == 0) {
            if (variable_value != 0u) {
                char message[160];
                (void)snprintf(message, sizeof(message),
                               "destination-alive during admission handshake value=%lu at_ms=%lu",
                               (unsigned long)variable_value, elapsed_ms);
                log_text(message);
                log_latch("destination-alive after admission handshake", variable_value);
                log_text("the destination-alive byte is nonzero; the transport will admit a non-exempt category now");
                return 0;
            }
        }
        if (elapsed_ms >= 5000ul) {
            log_latch("destination-alive after admission handshake settle window", variable_value);
            log_text("the destination-alive byte is still zero after the admission handshake; the rise is not attributed and the requests are suppressed");
            return -1;
        }
        pause.tv_sec = 0;
        pause.tv_nsec = (long)POLL_NS;
        if (nanosleep(&pause, &remaining) != 0) {
            log_text("admission handshake incomplete: monotonic sleep failed");
            return -1;
        }
    }
}

/*
 * The bounded observation window: twenty seconds at a hundred millisecond
 * cadence.  The window records arrivals only.  It does not certify a fresh
 * peer response and it does not associate an arrival with any request.
 */
static int observe_for_twenty_seconds(void)
{
    uint64_t start;
    uint64_t now;
    capture_timespec pause;
    capture_timespec remaining;

    log_hold_marker("capture-shot: before probe hold");
    if (monotonic_ns(&start) != 0) {
        log_text("callback observation incomplete: monotonic clock unavailable; outcome indeterminate; no retry");
        log_hold_marker("capture-shot: after probe hold");
        return -1;
    }
    for (;;) {
        if (monotonic_ns(&now) != 0 || now < start) {
            log_text("callback observation incomplete: monotonic clock unavailable or reversed; outcome indeterminate; no retry");
            log_hold_marker("capture-shot: after probe hold");
            return -1;
        }
        if (now - start >= OBSERVE_NS) break;
        pause.tv_sec = 0;
        pause.tv_nsec = (long)POLL_NS;
        if (nanosleep(&pause, &remaining) != 0) {
            log_text("callback observation incomplete: monotonic sleep failed; outcome indeterminate; no retry");
            log_hold_marker("capture-shot: after probe hold");
            return -1;
        }
    }
    log_hold_marker("capture-shot: after probe hold");
    return evidence_failed() ? -1 : 0;
}

/*
 * The liveview-establishment wait, trial 12: three bounded seconds at a
 * hundred millisecond cadence between the liveview start edge and the
 * still.  The still's death is the SSIF buffer starvation
 * (DEATH-MECHANISM-BUFFER-STARVATION.md): the liveview's setup allocates
 * the capture buffers, and the still must fire after the allocation but
 * before the bare liveview's own ~5 s starvation (trial 08).  Trial 07 was
 * too early (0.4 ms, the buffers not yet allocated) and trial 08 too late
 * (10 s, the liveview itself starved).  Callbacks arriving during the
 * wait are recorded by the notify callback as usual.
 */
static int establish_wait(void)
{
    uint64_t start;
    uint64_t now;
    capture_timespec pause;
    capture_timespec remaining;

    log_text("liveview establishment wait: three bounded seconds before the still");
    if (monotonic_ns(&start) != 0) {
        log_text("establishment wait incomplete: monotonic clock unavailable; still suppressed");
        return -1;
    }
    for (;;) {
        if (monotonic_ns(&now) != 0 || now < start) {
            log_text("establishment wait incomplete: monotonic clock unavailable or reversed; still suppressed");
            return -1;
        }
        if (now - start >= ESTABLISH_NS) break;
        pause.tv_sec = 0;
        pause.tv_nsec = (long)POLL_NS;
        if (nanosleep(&pause, &remaining) != 0) {
            log_text("establishment wait incomplete: monotonic sleep failed; still suppressed");
            return -1;
        }
    }
    log_text("liveview establishment wait complete; still follows");
    return evidence_failed() ? -1 : 0;
}

/*
 * The drain wait: one bounded second after the liveview stop edge, letting
 * the liveview's RAW work drain before the still (the isRawWorkEnd
 * precondition and the SSIF buffer freedom).
 */
static int drain_wait(void)
{
    uint64_t start;
    uint64_t now;
    capture_timespec pause;
    capture_timespec remaining;

    log_text("drain wait: one bounded second after the liveview stop");
    if (monotonic_ns(&start) != 0) {
        log_text("drain wait incomplete: monotonic clock unavailable; still suppressed");
        return -1;
    }
    for (;;) {
        if (monotonic_ns(&now) != 0 || now < start) {
            log_text("drain wait incomplete: monotonic clock unavailable or reversed; still suppressed");
            return -1;
        }
        if (now - start >= DRAIN_NS) break;
        pause.tv_sec = 0;
        pause.tv_nsec = (long)POLL_NS;
        if (nanosleep(&pause, &remaining) != 0) {
            log_text("drain wait incomplete: monotonic sleep failed; still suppressed");
            return -1;
        }
    }
    log_text("drain wait complete; still follows");
    return evidence_failed() ? -1 : 0;
}

int main(int argc, char **argv)
{
    const char *log_path = 0;
    uint64_t free_bytes = 0;
    void *sdk_handle;
    void *bridge_handle;
    adapter_instance_fn instance_fn = 0;
    adapter_initialize_fn initialize_fn = 0;
    beauty_instance_fn beauty_fn = 0;
    mm_camera_create_fn create_fn = 0;
    reg_callback_client_fn register_fn = 0;
    reg_image_set_callback_id_fn subscribe_set_fn = 0;
    reg_notify_callback_id_fn subscribe_notify_fn = 0;
    capture_still_words17_fn capture_fn = 0;
    mm_camera_ioctl_fn ioctl_fn = 0;
    mcb_send_fn send_fn = 0;
    mcb_alive_fn alive_fn = 0;
    void *instance;
    void *beauty;
    unsigned int handle = 0u;
    uint32_t slot;
#ifndef SUPPRESS_THE_STILL
    uint32_t free_low;
    uint32_t free_high;
#endif
    int status;
    int liveview_status = 0;
    int observation_status;
    uint64_t clock_check;
    unsigned char latch_before_still = 0u;
    unsigned char latch_before_still_getter = 0u;
    unsigned char latch_after_still = 0u;
    unsigned char latch_after_still_getter = 0u;
    void *liveview_result;

    (void)sizeof(one_still_liveview_function_pointer_size_matches);
    if (valid_cli(argc, argv, &log_path, &free_bytes) != 0) {
        fprintf(stderr,
                "usage: one-still-liveview-probe --diagnostic-one-still-liveview --library %s --log OWNEDPATH --free-bytes ASCIIU64\n",
                SDK_LIBRARY);
        return 2;
    }
    log_fd = open(log_path, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC | PROBE_O_APPEND,
                  0644);
    if (log_fd < 0) {
        fprintf(stderr, "one-still-liveview-probe: cannot open owned log path\n");
        return 1;
    }
    log_text("diagnostic-only start; no image data will be read or saved");
    log_text("one-still diagnostic with the stock liveview-first ordering; no still command other than the single bounded one, and no image or RAW pointer is accessed");
    if (evidence_failed() || monotonic_ns(&clock_check) != 0) {
        log_text("initial monotonic clock/log preflight failed; SDK not loaded");
        return 1;
    }

    log_text("opening pinned SDK and bridge libraries");
    sdk_handle = dlopen(SDK_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    if (sdk_handle == 0) {
        log_text("SDK dlopen failed");
        return 1;
    }
    bridge_handle = dlopen(BRIDGE_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    if (bridge_handle == 0) {
        log_text("bridge dlopen failed; SDK handle retained until process exit");
        return 1;
    }

    /* Resolve every needed entry point before any bootstrap. */
    if (resolve_symbol(sdk_handle, "_ZN11CMCBAdapter8InstanceEv",
                       &instance_fn, sizeof(instance_fn)) != 0 ||
        resolve_symbol(sdk_handle, "_ZN11CMCBAdapter10InitializeEv",
                       &initialize_fn, sizeof(initialize_fn)) != 0 ||
        resolve_symbol(sdk_handle,
                       "_ZN19CBeautyFaceIf_Linux11GetInstanceEv",
                       &beauty_fn, sizeof(beauty_fn)) != 0 ||
        resolve_symbol(sdk_handle, "mm_camera_create", &create_fn,
                       sizeof(create_fn)) != 0 ||
        resolve_symbol(sdk_handle, "RegCallbackClient", &register_fn,
                       sizeof(register_fn)) != 0 ||
        resolve_symbol(sdk_handle, "RegImageSetCallbackId", &subscribe_set_fn,
                       sizeof(subscribe_set_fn)) != 0 ||
        resolve_symbol(sdk_handle, "RegNotifyCallbackId", &subscribe_notify_fn,
                       sizeof(subscribe_notify_fn)) != 0 ||
        resolve_symbol(sdk_handle, "mm_camera_capture_still", &capture_fn,
                       sizeof(capture_fn)) != 0 ||
        resolve_symbol(sdk_handle, "mm_camera_ioctl", &ioctl_fn,
                       sizeof(ioctl_fn)) != 0 ||
        resolve_symbol(bridge_handle, "Mcb_send", &send_fn, sizeof(send_fn)) != 0 ||
        resolve_symbol(bridge_handle,
                       "_ZN16CMulticoreBridge18IsDestinationAliveEv",
                       &alive_fn, sizeof(alive_fn)) != 0) {
        log_text("required SDK/bridge symbol missing; bootstrap not entered");
        return 1;
    }
    log_text("all required SDK and bridge symbols resolved");

    /*
     * The destination-alive byte and the bridge send entry are resolved
     * through the SDK handle, so they describe the bridge instance the capture
     * library itself uses rather than a second copy.
     */
    latch_variable_address = dlsym(sdk_handle, "_ZN7CSender12m_bDestAliveE");
    latch_getter = alive_fn;
    bridge_send = send_fn;
    if (latch_variable_address == 0) {
        log_text("destination-alive byte not resolvable through the SDK handle; the observation is unavailable");
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

    status = create_fn(&handle);
    log_values("mm_camera_create returned", status, handle, 0, 0);
    if (status != 0) {
        log_text("mm_camera_create failed; abort");
        return 1;
    }

    slot = register_fn(client_name, on_notify, on_image, on_image_set);
    log_values("RegCallbackClient returned", (int32_t)slot, 0, 0, 0);
    if (slot == UINT32_MAX) {
        log_text("client registration failed; still suppressed");
        return 1;
    }
    status = subscribe_set_fn(slot, ENCODED_SET_TOPIC);
    log_values("RegImageSetCallbackId returned", status, ENCODED_SET_TOPIC, 0, 0);
    if (status != 0) {
        log_text("encoded-set subscription failed; still suppressed");
        return 1;
    }
    /*
     * The still-sequence notification topics, on the same client slot. The
     * library keeps a per-client id bitmap and only invokes the client's
     * notify callback for subscribed ids; trial 04 subscribed none, so the
     * id-7 STL_CAPTURE_READY completion could never have arrived even had
     * the camera survived the hold. A nonzero return here suppresses the
     * still, since the completion observation is the point of the run.
     */
    {
        static const uint32_t still_notify_topics[] = {
            NOTIFY_PUBLIC_STL_CAPTURE_START,      /* id 1 */
            NOTIFY_PUBLIC_STL_CAPTURING,          /* id 3 */
            NOTIFY_PUBLIC_STL_PROCESSING,         /* id 4 */
            NOTIFY_PUBLIC_STL_QVIEW_DONE,         /* id 5 */
            NOTIFY_PUBLIC_STL_NEXTSHOT_READY,     /* id 6 */
            NOTIFY_PUBLIC_STL_CAPTURE_READY,      /* id 7 */
        };
        size_t topic_index;
        for (topic_index = 0;
             topic_index < sizeof(still_notify_topics) / sizeof(still_notify_topics[0]);
             ++topic_index) {
            status = subscribe_notify_fn(slot, still_notify_topics[topic_index]);
            log_values("RegNotifyCallbackId returned", status,
                       still_notify_topics[topic_index], 0, 0);
            if (status != 0) {
                log_text("notification subscription failed; still suppressed");
                return 1;
            }
        }
    }
    log_text("subscription API returned zero; native routing remains unverified");

    /*
     * The transport admission handshake, before any request.  Without it the
     * transport queues a non-exempt category instead of transmitting it.
     */
    if (admission_handshake() != 0) {
        log_text("admission handshake not satisfied; liveview and still requests suppressed");
        return 1;
    }

    if (evidence_failed() || monotonic_ns(&clock_check) != 0) {
        log_text("pre-liveview monotonic clock/log check failed; no request issued");
        return 1;
    }

    /*
     * The liveview start edge, in the stock application's order: the still
     * comes after it.
     */
    {
        unsigned char before = 0u;
        unsigned char before_getter = 0u;
        unsigned char after = 0u;
        unsigned char after_getter = 0u;
        char message[256];

        if (read_latch(&before, &before_getter) == 0) {
            log_latch("destination-alive before liveview request", before);
            log_latch("destination-alive getter before liveview request", before_getter);
            if (before != before_getter) {
                log_text("destination-alive variable and getter disagree before the liveview request; observation is not authoritative");
            }
        } else {
            log_text("destination-alive could not be read before the liveview request");
        }
        (void)snprintf(message, sizeof(message),
                       "mm_camera_ioctl intent: handle=%lu selector=%lu p3=0 p4=0 p5=null p6=null; exactly one call; no retry; a zero return is not sensor readiness",
                       (unsigned long)handle,
                       (unsigned long)LIVEVIEW_IOCTL_SELECTOR);
        log_text(message);
        liveview_result = ioctl_fn(handle, LIVEVIEW_IOCTL_SELECTOR, 0, 0, 0, 0);
        log_values("mm_camera_ioctl returned", (int32_t)(uintptr_t)liveview_result, 0, 0, 0);
        liveview_status = (liveview_result != 0) ? 1 : 0;
        if (liveview_status != 0) {
            log_text("liveview start edge returned nonzero; the still request is still attempted once; outcome indeterminate; no retry");
        } else {
            log_text("liveview start edge returned zero; transport request only; sensor readiness unproven");
        }
        if (read_latch(&after, &after_getter) == 0) {
            log_latch("destination-alive after liveview request", after);
            log_latch("destination-alive getter after liveview request", after_getter);
        } else {
            log_text("destination-alive could not be read after the liveview request");
        }
    }

    if (evidence_failed() || monotonic_ns(&clock_check) != 0) {
        log_text("pre-still monotonic clock/log check failed; still suppressed");
        return 1;
    }

    /*
     * The establishment wait comes after the liveview start edge and before
     * the still: three bounded seconds for the liveview to allocate its
     * buffers and establish, so the still's drive does not fire into the
     * liveview's startup (trial 07's death).  Any failure here suppresses
     * the still.
     */
    if (establish_wait() != 0) {
        log_text("establishment wait failed; still suppressed");
        return 1;
    }

    /*
     * The stock application's liveview stop edge, before the still: the
     * still's isRawWorkEnd precondition requires the liveview's RAW work to
     * have drained, and the drained SSIF buffers are what the still's drive
     * needs (the trial-09/11 starvation deaths).  The stop is followed by a
     * bounded one-second drain wait.  A nonzero stop return is recorded and
     * the still is still attempted once, matching the app's fire-and-forget
     * behavior.
     */
    {
        void *stop_result = ioctl_fn(handle, LIVEVIEW_STOP_IOCTL_SELECTOR, 0, 0, 0, 0);
        log_values("mm_camera_ioctl liveview stop returned", (int32_t)(uintptr_t)stop_result,
                   0, 0, 0);
        if (stop_result != 0) {
            log_text("liveview stop ioctl returned nonzero; recorded; the still is still attempted once");
        } else {
            log_text("liveview stop ioctl returned zero; the stop request was accepted at the transport");
        }
    }
    if (drain_wait() != 0) {
        log_text("drain wait failed; still suppressed");
        return 1;
    }

    /* The destination-alive byte immediately before the still request. */
    if (read_latch(&latch_before_still, &latch_before_still_getter) == 0) {
        log_latch("destination-alive before still request", latch_before_still);
        log_latch("destination-alive getter before still request",
                  latch_before_still_getter);
    } else {
        log_text("destination-alive could not be read before the still request");
    }

#ifndef SUPPRESS_THE_STILL
    free_low = (uint32_t)free_bytes;
    free_high = (uint32_t)(free_bytes >> 32);
    log_values("mm_camera_capture_still entering raw17", (int32_t)handle,
               free_low, (int32_t)free_high, STILL_CAPTURE_TYPE);
#endif
    if (log_failure || monotonic_ns(&clock_check) != 0) {
        log_text("immediate pre-still log/clock check failed; still suppressed");
        return 1;
    }
#ifdef SUPPRESS_THE_STILL
    /*
     * Bisect build only.  Everything above this point - the admission
     * handshake, the liveview start edge, the local bootstrap and the
     * subscriptions - is performed exactly as in the reviewed build.  The
     * still command itself is not sent, so this build cannot be the cause
     * of a still-command failure.
     */
    log_text("still command suppressed by build variant; no still command invoked");
    status = 0;
#else
    status = capture_fn(handle, 0u, STILL_CAPTURE_TYPE,
                        1u, 1u, 0u, 1u, 0u, free_low, free_high,
                        0u, 0u, 0u, 0u, 0u, 0u, 0u);
    log_values("mm_camera_capture_still returned", status, 0, 0, 0);
#endif
    if (status != 0) {
        log_text("still call returned nonzero; outcome indeterminate; no retry");
    } else {
        log_text("still call returned zero; transport acceptance only; capture completion and readiness unproven");
    }

    if (read_latch(&latch_after_still, &latch_after_still_getter) == 0) {
        log_latch("destination-alive after still request", latch_after_still);
        log_latch("destination-alive getter after still request",
                  latch_after_still_getter);
    } else {
        log_text("destination-alive could not be read after the still request");
    }

    observation_status = observe_for_twenty_seconds();
    log_values("diagnostic end; completion/readiness unproven; callbacks observed",
               (int32_t)__sync_fetch_and_add(&callback_count, 0u),
               (uint32_t)status, 0, 0);
    log_values("diagnostic end; callback families observed: notify, image, image_set",
               (int32_t)__sync_fetch_and_add(&notify_callback_count, 0u),
               (int32_t)__sync_fetch_and_add(&image_callback_count, 0u),
               (int32_t)__sync_fetch_and_add(&image_set_callback_count, 0u), 0);
    log_text("a callback arrival is not request-associated and does not prove capture readiness");
    /* No stop/destroy/unregister/release/dlclose: process exit ends this probe. */
    /* The exit status reports the still request's own return and the
     * observation outcome.  A nonzero liveview return is recorded in the log
     * and the still is still attempted once, but it is an indeterminate
     * outcome, so it is reported as a nonzero status too. */
    if (liveview_status != 0) {
        log_text("diagnostic end; the liveview start edge returned nonzero, so the still outcome is indeterminate");
        return 1;
    }
    return status == 0 && observation_status == 0 && !evidence_failed() ? 0 : 1;
}
