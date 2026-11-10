/*
 * Event-only, one-still diagnostic. This is not the capture backend and it
 * never reads, retains, copies, or releases any image data.
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
 * index. The pinned app maps CAP_SINGLE -> mm 1 -> command 0x1010001 for a
 * lock3a-complete still, and CAP_LOCK3A_CAPTURE (0x18) -> mm 0x15 -> command
 * 0x1010016 for a lock-3A-then-capture still.  A plain single still is 1;
 * 0x15 asks the RTOS to lock 3A as part of the capture and needs 3A
 * resources the bare diagnostic does not set up.  See
 * output/decompile/rea/pass-liveview-contract-20261030/next-edge/still-path/CAPTURE-TYPE-SELECTION.md.
 */
#ifdef STOCK_LIKE_STILL
/*
 * ACTIVE_PLAN step 4 (2 November 2026): reproduce the stock app's recorded
 * still request exactly - mm type 0x15 -> command 0x01010016, Cap=Max=1,
 * remaining count 2440 (0x988) - and skip every extra start-up command
 * (warm-boot init, wakeup, Lock3a, memory info, buffer request). The stock
 * still recorded in pass-plan-step2-ipcc-record-20261102 sent only the
 * 48-byte request.
 */
#define STILL_CAPTURE_TYPE 0x15u
#define STILL_REMAINING_COUNT 2440u
#else
#define STILL_CAPTURE_TYPE 1u
#define STILL_REMAINING_COUNT 1u
#endif
/*
 * The still-sequence notification topics, in the library's public encoded
 * spelling (handle id N <-> public topic 0x201000N for ids 1..7, see
 * next-edge/still-path/STILL-PATH.md). id 7 is STL_CAPTURE_READY, the still
 * completion; ids 1, 3, 4, 5, 6 are the sequence. The trial-04 defect was
 * subscribing only the image-set topic, so the id-7 completion could never
 * arrive. These topics are subscribed on the same client slot.
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
#define POLL_NS UINT64_C(100000000)

/*
 * The stock application's boot wakeup ioctl: UI_Mm_Camera_Set_Wake_Up ends
 * in the helper call FUN_00113ef8(0x44, 0, 0), i.e. mm_camera_ioctl with
 * selector 0x44. The library mapper maps 0x44 to 0x1070004, the RTOS
 * E_CAP_CMD_OP_MODECHANGE (CCapCmdIf::ChangeMode); a NULL payload sends the
 * mode word 0, which selects controller mode 3 (the preview/photo pipeline
 * mode). The bare client never issues it, so its still runs the boot-default
 * status-0x43 branch (drive mode 0); the application's still runs the
 * mode-3 branch (drive mode 4). See next-edge/still-path/MODE-CHANGE.md.
 */
#define WAKEUP_IOCTL_SELECTOR 0x44u
/*
 * The stock application's warm-boot init: UI_Mm_Camera_Set_Warm_Boot_Init
 * ends in FUN_00113ef8(0x43, 0, 0) - the ioctl selector 0x43. The mapper
 * maps 0x43 to 0x1070006, whose RTOS handler (FUN_80111a10) writes the
 * power state 1 (reversing the graceful stop's PowerSave) and runs the warm
 * re-init: the 0x100 init, the front-end, the pending-request clear, the
 * mode set. This is the Linux-reachable power-on: the graceful stock-app
 * stop plus this call is the clean handover to a bare client.
 */
#define WARM_BOOT_INIT_IOCTL_SELECTOR 0x43u
/*
 * The stock application's still-sequence 3A lock:
 * CAPPProgramMode::sendCmdEvent issues UI_Operate_Start_Lock3a(1, 3), i.e.
 * UI_Mm_Camera_Start_Lock3a(1, 3) -> the ioctl helper (1, payload, 0x10):
 * the selector 1 (E_CAP_CMD_3A_LOCK) with the 0x10-byte payload
 * [3, 0, 0, 0] (word 0 = the param 3, word 1 = the type-1 word 0, the
 * rest zero).  The still's checkPreCondition saves the still as a pending
 * Lock3a request while the lock flag is set, so the still pends instead of
 * firing the drive into the starved pipeline.  See
 * next-edge/still-path/APP-OPS-MAP.md.
 */
#define LOCK3A_IOCTL_SELECTOR 1u
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
typedef void (*get_image_buffer_memory_info_fn)(int *info);
typedef void (*request_image_buffer_memory_info_fn)(int *req);
typedef int (*request_image_buffer_memory_fn)(int size);
typedef void (*mcb_send_fn)(unsigned short category, unsigned short attribute,
                            unsigned short length, unsigned char *payload);
typedef unsigned char (*mcb_alive_fn)(void);
typedef int (*device_get_property_fn)(int type, int property, int *value);

/*
 * Gyro history gate (ACTIVE_PLAN step 4e, 2 November 2026). libcapture's
 * gyro responder reads the motion history through
 * device_get_property(2, 0x20 size / 0x18 phys / 0x19 pos). libdevice-node
 * answers through libslp_devman_plugin.so, which it dlopens in its
 * constructor; that plugin needs sqrt (libm) and calls back into
 * libdevice-node. Unless both libraries are linked into this executable (as
 * in the stock app), the plugin fails to load, every property silently stays
 * 0, the responder returns the "no data" angles 1, 2, -1, and every still
 * from this client died right after. The build links libm and
 * libdevice-node; this gate checks the result before any capture command.
 */
/*
 * APEX gate (ACTIVE_PLAN step 4v, 3 November 2026). The capture library's
 * image-set handler (CCapImageSetCb::OnCallback) asks
 * Apex_Get_Max_Av_Value_For_Capture -> CApex::GetMaxApertureValue for the
 * lens's maximum aperture. That reads [info + 0x6f0] through the CApex
 * singleton's info pointer (this + 0xc), which only CApex::Initialize sets;
 * the stock app does it at start-up through Apex_Initialize_A9(). Without it
 * this client's MCB_Receiver faulted at 0x6f0 on every image set and the
 * kernel rebooted the camera ("CA9 fault occurs, reboot now!!!").
 *
 * Apex_Initialize_A9 would also write two process-virtual table pointers into
 * the shared RTOS preference area, so this gate instead gives the singleton a
 * private copy of the user-spec block (RTOS preference area + 0x40000,
 * physical 0x85440000, 0x2d20 bytes; APEX info at + 0x4d4). It first checks
 * that the block is mapped readable and carries the magic 0x330033 that the
 * library's own check (User_Spec_Get_Rtos_Pref_Crc) expects. Nothing is
 * written to shared memory. The APEX conversions read only info fields and
 * libapex's own static table.
 */
#define USER_SPEC_LIBRARY "/usr/lib/libuser-spec.so"
#define APEX_LIBRARY "/usr/lib/libapex.so"
#define SELF_MAPS_PATH "/proc/self/maps"
#define USER_SPEC_OFFSET 0x40000u
#define USER_SPEC_SIZE 0x2d20u
#define USER_SPEC_MAGIC 0x330033u
#define APEX_INFO_OFFSET 0x4d4u
#define APEX_INFO_TABLE_A 0x7cu
#define APEX_INFO_TABLE_B 0x39cu
#define APEX_INSTANCE_INFO_FIELD 0xcu
typedef uintptr_t (*pref_rtos_get_fd_fn)(void);
typedef void *(*capex_get_instance_fn)(void);
typedef void (*capex_initialize_fn)(void *instance, void *info);
typedef void (*apex_get_max_av_fn)(unsigned int *numerator, unsigned int *denominator);

#define GYRO_DEVICE_TYPE 2
#define GYRO_PROP_SIZE 0x20
#define GYRO_PROP_PHYS 0x18
#define GYRO_PROP_POS 0x19

/*
 * The original C parameter grouping is unknown. This diagnostic pins the
 * reviewed machine-word ABI: exactly seventeen 32-bit slots.
 */
typedef int (*capture_still_words17_fn)(
    uint32_t w1, uint32_t w2, uint32_t w3, uint32_t w4, uint32_t w5,
    uint32_t w6, uint32_t w7, uint32_t w8, uint32_t w9, uint32_t w10,
    uint32_t w11, uint32_t w12, uint32_t w13, uint32_t w14, uint32_t w15,
    uint32_t w16, uint32_t w17);

typedef char one_still_function_pointer_size_matches[
    (sizeof(void *) == sizeof(adapter_instance_fn) &&
     sizeof(void *) == sizeof(adapter_initialize_fn) &&
     sizeof(void *) == sizeof(beauty_instance_fn) &&
     sizeof(void *) == sizeof(mm_camera_create_fn) &&
     sizeof(void *) == sizeof(reg_callback_client_fn) &&
     sizeof(void *) == sizeof(reg_image_set_callback_id_fn) &&
     sizeof(void *) == sizeof(reg_notify_callback_id_fn) &&
     sizeof(void *) == sizeof(mm_camera_ioctl_fn) &&
     sizeof(void *) == sizeof(get_image_buffer_memory_info_fn) &&
     sizeof(void *) == sizeof(request_image_buffer_memory_info_fn) &&
     sizeof(void *) == sizeof(request_image_buffer_memory_fn) &&
     sizeof(void *) == sizeof(capture_still_words17_fn) &&
     sizeof(void *) == sizeof(mcb_send_fn) &&
     sizeof(void *) == sizeof(mcb_alive_fn)) ? 1 : -1];

static int log_fd = -1;
static volatile int log_failure;
static char client_name[] = "OneShot"; /* seven bytes; library limit is nine */
static volatile uint32_t callback_count;

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
                          "one-still-probe: monotonic=%ld.%09ld %s\n",
                          now.tv_sec, now.tv_nsec, text);
    } else {
        length = snprintf(line, sizeof(line),
                          "one-still-probe: monotonic=unavailable %s\n", text);
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
    log_values("callback.notify", (int32_t)event, size, 0, 0);
}

static void on_image(int topic, void *rect_block, void *image_object,
                     unsigned int size)
{
    (void)rect_block; /* never dereference or retain callback pointers */
    (void)image_object;
    __sync_fetch_and_add(&callback_count, 1u);
    log_values("callback.image", (int32_t)topic, size, 0, 0);
}

static void on_image_set(int topic, void *image_set_slot, void *data,
                         unsigned int size)
{
    (void)image_set_slot; /* never dereference or retain callback pointers */
    (void)data;
    __sync_fetch_and_add(&callback_count, 1u);
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
    if (argc != 8 || strcmp(argv[1], "--diagnostic-one-still") != 0 ||
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

static char maps_text[65536];

/* 1 when [start, start + length) lies inside one readable mapping of this
   process (from /proc/self/maps), else 0. Judged before any load, so a bad
   address aborts cleanly instead of faulting (which reboots the camera). */
static int range_readable(uintptr_t start, size_t length)
{
    capture_file *maps = fopen(SELF_MAPS_PATH, "r");
    size_t n;
    char *line;
    if (maps == 0) return 0;
    n = fread(maps_text, 1, sizeof maps_text - 1u, maps);
    fclose(maps);
    maps_text[n] = '\0';
    for (line = maps_text; *line != '\0';) {
        char *end;
        char *next = strchr(line, '\n');
        unsigned long lo = strtoul(line, &end, 16);
        if (*end == '-') {
            unsigned long hi = strtoul(end + 1, &end, 16);
            if (*end == ' ' && end[1] == 'r' && (uintptr_t)lo <= start &&
                start + length <= (uintptr_t)hi && start + length >= start) {
                return 1;
            }
        }
        if (next == 0) break;
        line = next + 1;
    }
    return 0;
}

static unsigned char *apex_user_spec_copy;

static int apex_gate(void)
{
    void *user_spec_handle = dlopen(USER_SPEC_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    void *apex_handle = dlopen(APEX_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    pref_rtos_get_fd_fn pref_base_fn = 0;
    apex_get_max_av_fn get_max_av_fn = 0;
    capex_get_instance_fn get_instance_fn = 0;
    capex_initialize_fn apex_initialize_fn = 0;
    uintptr_t base;
    uintptr_t user_spec;
    uint32_t magic;
    unsigned char *info;
    void *apex_instance;
    void *info_now = 0;
    uint32_t table;
    unsigned int max_av_num = 0;
    unsigned int max_av_den = 0;

    if (user_spec_handle == 0 || apex_handle == 0 ||
        resolve_symbol(user_spec_handle, "pref_rtos_get_fd", &pref_base_fn,
                       sizeof(pref_base_fn)) != 0 ||
        resolve_symbol(user_spec_handle, "_Z33Apex_Get_Max_Av_Value_For_CapturePjS_",
                       &get_max_av_fn, sizeof(get_max_av_fn)) != 0 ||
        resolve_symbol(apex_handle, "_ZN5CApex11GetInstanceEv", &get_instance_fn,
                       sizeof(get_instance_fn)) != 0 ||
        resolve_symbol(apex_handle, "_ZN5CApex10InitializeEP13tag_apex_info",
                       &apex_initialize_fn, sizeof(apex_initialize_fn)) != 0) {
        log_text("apex gate: libuser-spec/libapex symbol missing; abort before bootstrap");
        return -1;
    }
    base = pref_base_fn();
    log_values("apex gate: rtos pref base", (int32_t)(uint32_t)base, 0, 0, 0);
    if (base == 0) {
        log_text("apex gate: rtos pref area not mapped; abort before bootstrap");
        return -1;
    }
    user_spec = base + USER_SPEC_OFFSET;
    if (!range_readable(user_spec, USER_SPEC_SIZE)) {
        log_text("apex gate: user-spec block not mapped readable; abort before bootstrap");
        return -1;
    }
    memcpy(&magic, (const void *)user_spec, sizeof magic);
    log_values("apex gate: user-spec magic", (int32_t)magic, 0, 0, 0);
    if (magic != USER_SPEC_MAGIC) {
        log_text("apex gate: user-spec magic missing; refusing (library init would reset shared settings)");
        return -1;
    }
    apex_user_spec_copy = malloc(USER_SPEC_SIZE);
    if (apex_user_spec_copy == 0) {
        log_text("apex gate: no memory for the private user-spec copy; abort before bootstrap");
        return -1;
    }
    memcpy(apex_user_spec_copy, (const void *)user_spec, USER_SPEC_SIZE);
    info = apex_user_spec_copy + APEX_INFO_OFFSET;
    /* Initialize_Apex_Table_Ptr_For_Gui's two table pointers, in the copy
       only (32-bit addresses on the camera). */
    table = (uint32_t)(uintptr_t)(info + APEX_INFO_TABLE_A);
    memcpy(info + 4, &table, sizeof table);
    table = (uint32_t)(uintptr_t)(info + APEX_INFO_TABLE_B);
    memcpy(info + 0xc, &table, sizeof table);
    apex_instance = get_instance_fn();
    if (apex_instance == 0) {
        log_text("apex gate: CApex instance null; abort before bootstrap");
        return -1;
    }
    apex_initialize_fn(apex_instance, info);
    memcpy(&info_now, (const unsigned char *)apex_instance + APEX_INSTANCE_INFO_FIELD,
           sizeof info_now);
    log_values("apex gate: info set (private copy)", (int32_t)(uint32_t)(uintptr_t)info,
               (uint32_t)(uintptr_t)info_now, 0, 0);
    if (info_now != (void *)info) {
        log_text("apex gate: CApex info pointer not set; abort before bootstrap");
        return -1;
    }
    /* The exact call the image-set handler makes. */
    get_max_av_fn(&max_av_num, &max_av_den);
    log_values("apex gate: max aperture num/den", (int32_t)max_av_num, max_av_den, 0, 0);
    return evidence_failed() ? -1 : 0;
}

static int notify_gate(mcb_send_fn send_fn, mcb_alive_fn alive_fn)
{
    unsigned char before = alive_fn();
    unsigned char after;
    uint64_t start;
    uint64_t now;
    capture_timespec pause;
    capture_timespec remaining;

    log_values("notify.baseline", (int32_t)before, 0, 0, 0);
    if (before != 0) {
        log_text("notify skipped: bridge already alive; no reply association claimed");
        return evidence_failed() ? -1 : 0;
    }
    if (monotonic_ns(&start) != 0) {
        log_text("notify not sent: monotonic clock unavailable");
        return -1;
    }

    log_text("notify send category=16 attribute=0 length=0 payload=null");
    if (evidence_failed()) return -1;
    send_fn((unsigned short)NOTIFY_CATEGORY, 0, 0, 0);
    for (;;) {
        after = alive_fn();
        if (after != 0) {
            log_values("notify latch changed; association unknown",
                       (int32_t)before, after, 0, 0);
            return 0;
        }
        if (monotonic_ns(&now) != 0 || now < start) {
            log_text("notify observation failed: monotonic clock unavailable or reversed");
            return -1;
        }
        if (now - start >= NOTIFY_SETTLE_NS) {
            log_text("notify latch unchanged; no still requested");
            return -1;
        }
        pause.tv_sec = 0;
        pause.tv_nsec = (long)POLL_NS;
        if (nanosleep(&pause, &remaining) != 0) {
            log_text("notify observation failed: monotonic sleep failed; no still requested");
            return -1;
        }
    }
}

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
    get_image_buffer_memory_info_fn get_memory_info_fn = 0;
    request_image_buffer_memory_info_fn request_memory_info_fn = 0;
    request_image_buffer_memory_fn request_memory_fn = 0;
    mcb_send_fn send_fn = 0;
    mcb_alive_fn alive_fn = 0;
    void *instance;
    void *beauty;
    unsigned int handle = 0;
    uint32_t slot;
#ifndef SUPPRESS_THE_STILL
    uint32_t free_low;
    uint32_t free_high;
#endif
    int status;
    int observation_status;
    uint64_t clock_check;

    (void)sizeof(one_still_function_pointer_size_matches);
    if (valid_cli(argc, argv, &log_path, &free_bytes) != 0) {
        fprintf(stderr,
                "usage: capture-shot --diagnostic-one-still --library %s --log OWNEDPATH --free-bytes ASCIIU64\n",
                SDK_LIBRARY);
        return 2;
    }
    log_fd = open(log_path, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC | PROBE_O_APPEND,
                  0644);
    if (log_fd < 0) {
        fprintf(stderr, "one-still-probe: cannot open owned log path\n");
        return 1;
    }
    log_text("diagnostic-only start; no image data will be read or saved");
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

    /* Resolve every needed SDK and bridge entry point before any bootstrap. */
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
        resolve_symbol(sdk_handle, "GetImageBufferMemoryInfo", &get_memory_info_fn,
                       sizeof(get_memory_info_fn)) != 0 ||
        resolve_symbol(sdk_handle, "RequestImageBufferMemoryInfo", &request_memory_info_fn,
                       sizeof(request_memory_info_fn)) != 0 ||
        resolve_symbol(sdk_handle, "RequestImageBufferMemory", &request_memory_fn,
                       sizeof(request_memory_fn)) != 0 ||
        resolve_symbol(bridge_handle, "Mcb_send", &send_fn,
                       sizeof(send_fn)) != 0 ||
        resolve_symbol(bridge_handle,
                       "_ZN16CMulticoreBridge18IsDestinationAliveEv",
                       &alive_fn, sizeof(alive_fn)) != 0) {
        log_text("required SDK/bridge symbol missing; bootstrap not entered");
        return 1;
    }
    log_text("all required SDK and bridge symbols resolved");
    {
        void *device_handle = dlopen("/usr/lib/libdevice-node.so.0.1",
                                     CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
        device_get_property_fn property_fn = 0;
        int gyro_size = 0;
        int gyro_phys = 0;
        int gyro_pos = 0;
        if (device_handle == 0 ||
            resolve_symbol(device_handle, "device_get_property", &property_fn,
                           sizeof(property_fn)) != 0) {
            log_text("gyro gate: libdevice-node or device_get_property missing; abort before bootstrap");
            return 1;
        }
        (void)property_fn(GYRO_DEVICE_TYPE, GYRO_PROP_SIZE, &gyro_size);
        (void)property_fn(GYRO_DEVICE_TYPE, GYRO_PROP_PHYS, &gyro_phys);
        (void)property_fn(GYRO_DEVICE_TYPE, GYRO_PROP_POS, &gyro_pos);
        log_values("gyro gate: history size/phys/pos", gyro_size,
                   (uint32_t)gyro_phys, gyro_pos, 0);
        if (gyro_size <= 0 || gyro_phys == 0) {
            log_text("gyro gate: motion history unavailable (devman plugin not loaded); abort before bootstrap");
            return 1;
        }
    }
    if (apex_gate() != 0) return 1;

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

    if (notify_gate(send_fn, alive_fn) != 0) {
        log_text("notify gate not satisfied; still suppressed");
        return 1;
    }

#ifndef STOCK_LIKE_STILL
    /*
     * The stock application's boot sequence: the warm-boot init (the
     * selector 0x43, OP 0x1070006 - the power state 1 and the warm
     * re-init, the Linux-reachable power-on that reverses the graceful
     * stop's PowerSave), then the wakeup (the selector 0x44, the OP
     * MODECHANGE word 0 - controller mode 3). Both are fire-and-forget in
     * the app (only a nonzero return is logged); a nonzero return here is
     * recorded but does not suppress the still. Without them the still runs
     * the boot-default status-0x43 branch in whatever power state the stop
     * left.
     */
    {
        void *warm_result = ioctl_fn(handle, WARM_BOOT_INIT_IOCTL_SELECTOR, 0, 0, 0, 0);
        log_values("mm_camera_ioctl warm-boot-init returned", (int32_t)(uintptr_t)warm_result,
                   0, 0, 0);
        if (warm_result != 0) {
            log_text("warm-boot-init ioctl returned nonzero; recorded; the wakeup and still are still attempted once");
        } else {
            log_text("warm-boot-init ioctl returned zero; the power-on request was accepted at the transport");
        }
        void *wakeup_result = ioctl_fn(handle, WAKEUP_IOCTL_SELECTOR, 0, 0, 0, 0);
        log_values("mm_camera_ioctl wakeup returned", (int32_t)(uintptr_t)wakeup_result,
                   0, 0, 0);
        if (wakeup_result != 0) {
            log_text("wakeup ioctl returned nonzero; recorded; the still is still attempted once");
        } else {
            log_text("wakeup ioctl returned zero; the mode change request was accepted at the transport");
        }
    }

    /*
     * The stock application's still-sequence 3A lock: the selector 1 with
     * the 0x10-byte payload [3, 0, 0, 0] (the Lock3a type 1, param 3).  The
     * still's checkPreCondition saves the still as a pending Lock3a request
     * while the lock flag is set, so the still pends instead of firing the
     * drive into the starved pipeline.  A nonzero return is recorded and
     * the still is still attempted once, matching the app's fire-and-forget
     * behavior.
     */
    {
        static const int lock3a_payload[4] = {3, 0, 0, 0};
        void *lock_result = ioctl_fn(handle, LOCK3A_IOCTL_SELECTOR,
                                     (int *)lock3a_payload, 0x10, 0, 0);
        log_values("mm_camera_ioctl lock3a returned", (int32_t)(uintptr_t)lock_result,
                   0, 0, 0);
        if (lock_result != 0) {
            log_text("lock3a ioctl returned nonzero; recorded; the still is still attempted once");
        } else {
            log_text("lock3a ioctl returned zero; the lock request was accepted at the transport");
        }
    }

    /*
     * The memory-info reads: the library's exported GetImageBufferMemoryInfo
     * (the command 0x108002b) fills a seven-word info struct from the
     * memory manager (the +0x40 total, the +0x44 bank words, the +0x48 and
     * +0x4c), and RequestImageBufferMemoryInfo (the command 0x108002a, the
     * ten-megabyte request field) is the app's own buffer-request route.
     * The readings are logged verbatim; they do not gate the still.  This
     * is the diagnostic the trial-15-safe run uses to observe the SSIF
     * pool's state without issuing the still.
     */
    {
        int mem_info[7] = {0, 0, 0, 0, 0, 0, 0};
        int mem_req[7] = {0, 0, 0xa00000, 0, 0, 0, 0};
        get_memory_info_fn(mem_info);
        log_values("GetImageBufferMemoryInfo words", mem_info[0], mem_info[1],
                   mem_info[2], mem_info[3]);
        log_values("GetImageBufferMemoryInfo tail", mem_info[4], mem_info[5],
                   mem_info[6], 0);
        request_memory_info_fn(mem_req);
        log_values("RequestImageBufferMemoryInfo words", mem_req[0], mem_req[1],
                   mem_req[2], mem_req[3]);
        log_values("RequestImageBufferMemoryInfo tail", mem_req[4], mem_req[5],
                   mem_req[6], 0);
        /*
         * The ten-megabyte buffer request itself - the DLNA's own
         * allocation size, proven to succeed from the bare client in the
         * trial-15 safe run.  The return is logged; it does not gate the
         * still.
         */
        {
            int request_ret = request_memory_fn(0xa00000);
            log_values("RequestImageBufferMemory returned", request_ret, 0, 0, 0);
        }
    }

#else
    log_text("stock-like build: no warm-boot, wakeup, lock3a, memory-info or buffer request");
#endif
#ifdef STOP_LIVEVIEW_FIRST
    /*
     * ACTIVE_PLAN step 4j: every client still so far lost power in the
     * RTOS's automatic post-still liveview restart. Stop the liveview first
     * (selector 0x1c -> E_CAP_CMD_LV_STOP, as the stock app's ui wrapper
     * does) and wait 1.5 s, so the RTOS may have no liveview to restart.
     * The stock app does not do this before its own stills.
     */
    {
        void *stop_result = ioctl_fn(handle, 0x1cu, 0, 0, 0, 0);
        uint64_t stop_start = 0;
        uint64_t stop_now = 0;
        capture_timespec stop_pause;
        capture_timespec stop_remaining;
        log_values("mm_camera_ioctl liveview stop (0x1c) returned",
                   (int32_t)(uintptr_t)stop_result, 0, 0, 0);
        if (monotonic_ns(&stop_start) != 0) {
            log_text("liveview stop wait: clock unavailable; still suppressed");
            return 1;
        }
        for (;;) {
            if (monotonic_ns(&stop_now) != 0 || stop_now < stop_start) {
                log_text("liveview stop wait: clock failed; still suppressed");
                return 1;
            }
            if (stop_now - stop_start >= UINT64_C(1500000000)) break;
            stop_pause.tv_sec = 0;
            stop_pause.tv_nsec = (long)POLL_NS;
            if (nanosleep(&stop_pause, &stop_remaining) != 0) {
                log_text("liveview stop wait: sleep failed; still suppressed");
                return 1;
            }
        }
        log_text("liveview stop wait complete; still follows");
    }
#endif
    if (evidence_failed() || monotonic_ns(&clock_check) != 0) {
        log_text("pre-still monotonic clock/log check failed; still suppressed");
        return 1;
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
     * Bisect build only.  Everything above this point - the local bootstrap,
     * the adapter initialise, the create, the client registration, the public
     * encoded-set subscription and the one notify request - is performed
     * exactly as in the reviewed build. The still command is omitted; the
     * earlier notification request was sent as usual.
     */
    log_text("still command suppressed by build variant; no still command invoked; prior notify request was sent");
    status = 0;
#else
    status = capture_fn(handle, 0u, STILL_CAPTURE_TYPE,
                        1u, 1u, 0u, STILL_REMAINING_COUNT, 0u, free_low, free_high,
                        0u, 0u, 0u, 0u, 0u, 0u, 0u);
    log_values("mm_camera_capture_still returned", status, 0, 0, 0);
#endif
    if (status != 0) {
        log_text("still call returned nonzero; outcome indeterminate; no retry");
    }

    observation_status = observe_for_twenty_seconds();
    log_values("diagnostic end; completion/readiness unproven; callbacks observed",
               (int32_t)__sync_fetch_and_add(&callback_count, 0u),
               (uint32_t)status, 0, 0);
    /* No stop/destroy/unregister/release/dlclose: process exit ends this probe. */
    return status == 0 && observation_status == 0 && !evidence_failed() ? 0 : 1;
}
