/*
 * g360cap implementation. See g360cap.h for the contract and README.md for
 * the evidence behind each step.
 *
 * The start-up and still request are the sequence that survived on the
 * camera on 3 November 2026 (ACTIVE_PLAN step 4y, probe build
 * output/stock-like-apex-build): gyro gate, APEX gate, adapter Instance and
 * Initialize, Beauty singleton, mm_camera_create, one callback client
 * subscribed to the encoded-set topic and the six still notifications, the
 * bridge notify, then mm_camera_capture_still with the stock app's recorded
 * words (mm type 0x15, Cap = Max = 1, remaining count, card free bytes).
 *
 * Reading and returning the image set follows the stock photo library
 * (libmm-photo ImageGenerator, output/decompile/rea/pass-stock-release-20261104):
 * the callback copies only the small structures; the image bytes are copied
 * afterwards on the caller's thread; then every buffer is given back with
 * ReleaseCaptureBuffer in the stock order and types: maker note 5, first
 * (RAW) image 1, main 7, screen 8, thumbnail 9.
 */
#include "capture_libc.h"
#include "encoded_set.h"
#include "g360cap.h"
#include "g360cap_check.h"

#include <stdint.h>

#define SDK_LIBRARY "/usr/lib/libcapture-fw-prod.so"
#define BRIDGE_LIBRARY "/usr/lib/libmulticore-bridge.so"
#define DEVICE_LIBRARY "/usr/lib/libdevice-node.so.0.1"
#define USER_SPEC_LIBRARY "/usr/lib/libuser-spec.so"
#define APEX_LIBRARY "/usr/lib/libapex.so"
#define SELF_MAPS_PATH "/proc/self/maps"

#if defined(__APPLE__)
#define G360_O_APPEND O_APPEND
#else
#define G360_O_APPEND 0x0400
#endif

/* The public encoded-still image-set topic and the still notifications
   (public topic 0x201000N for notification id N). */
#define ENCODED_SET_TOPIC 0x04010005u
/* The still-expansion topic: the RTOS publishes it after writing the full
   RAW frame and waits for the gyro reply (pass-expansion-pause-20261104). */
#define EXPANSION_TOPIC 0x04010008u
#define EXPANSION_DESCRIPTOR_SIZE 0x3c8u
#define EXPANSION_MODE_OFFSET 0x3c0u
#define NOTIFY_TOPIC_BASE 0x02010000u
#define NOTIFY_ID_CAPTURE_START 1u
#define NOTIFY_ID_CAPTURING 3u
#define NOTIFY_ID_PROCESSING 4u
#define NOTIFY_ID_QVIEW_DONE 5u
#define NOTIFY_ID_NEXTSHOT_READY 6u
#define NOTIFY_ID_CAPTURE_READY 7u

/* The stock app's recorded still: mm type 0x15 (lock 3A, then capture). */
#define STILL_CAPTURE_TYPE G360_CAPTURE_LOCK3A
#define DEFAULT_REMAINING_SHOTS 2440u

/* The bridge's one-shot notify request (category 16). */
#define NOTIFY_CATEGORY 0x10u
#define BRIDGE_SETTLE_NS UINT64_C(5000000000)
#define POLL_NS 10000000L

/* ReleaseCaptureBuffer types (libmm-photo releaseCaptureResource). */
#define RELEASE_TYPE_RAW 1
#define RELEASE_TYPE_MAKER_NOTE 5
#define RELEASE_TYPE_MAIN 7
#define RELEASE_TYPE_SCREEN 8
#define RELEASE_TYPE_THUMB 9

/* The image-set structure handed to the callback (stCAPTURE_IMG_SET):
   word 0 points to the basic set; +0x2e0 holds the maker-note address. */
#define SET_BASIC_POINTER_OFFSET 0x0u
#define SET_EXIF_OFFSET 0x8u
#define SET_MAKER_NOTE_OFFSET 0x2e0u
#define SET_READ_LENGTH 0x2e4u

/* Gyro history properties (ACTIVE_PLAN step 4e). */
#define GYRO_DEVICE_TYPE 2
#define GYRO_PROP_SIZE 0x20
#define GYRO_PROP_PHYS 0x18
#define GYRO_PROP_POS 0x19

/* APEX gate (ACTIVE_PLAN step 4v): a private copy of the user-spec block. */
#define USER_SPEC_OFFSET 0x40000u
#define USER_SPEC_SIZE 0x2d20u
#define USER_SPEC_MAGIC 0x330033u
#define APEX_INFO_OFFSET 0x4d4u
#define APEX_INFO_TABLE_A 0x7cu
#define APEX_INFO_TABLE_B 0x39cu
#define APEX_INSTANCE_INFO_FIELD 0xcu

/*
 * Camera words are 32-bit virtual addresses. The host tests cannot map
 * memory below 4 GB on macOS, so their fake SDK supplies a bias; on the
 * camera it is zero.
 */
static uintptr_t address_bias;

typedef void *(*adapter_instance_fn)(void);
typedef int (*adapter_initialize_fn)(void *instance);
typedef void *(*beauty_instance_fn)(void);
typedef int (*mm_camera_create_fn)(unsigned int *out_handle);
typedef void (*notify_callback_fn)(int event, void *data, unsigned int size);
typedef void (*image_callback_fn)(int topic, void *rect_block, void *image_object,
                                  unsigned int size);
typedef void (*image_set_callback_fn)(int topic, void *image_set, void *data,
                                      unsigned int size);
typedef uint32_t (*reg_callback_client_fn)(char *name, notify_callback_fn notify,
                                           image_callback_fn image,
                                           image_set_callback_fn image_set);
typedef int (*reg_topic_fn)(uint32_t slot, uint32_t topic);
typedef int (*release_capture_buffer_fn)(int type, uint32_t address);
typedef int (*set_usr_attributes_fn)(unsigned int handle, int attribute, uint32_t value);
typedef int (*set_var_attributes_fn)(unsigned int handle, uint32_t id, int32_t value,
                                     uint32_t unused);
typedef void (*mcb_send_fn)(unsigned short category, unsigned short attribute,
                            unsigned short length, unsigned char *payload);
typedef unsigned char (*mcb_alive_fn)(void);
typedef int (*device_get_property_fn)(int type, int property, int *value);
typedef uintptr_t (*pref_rtos_get_fd_fn)(void);
typedef void *(*capex_get_instance_fn)(void);
typedef void (*capex_initialize_fn)(void *instance, void *info);
typedef void (*apex_get_max_av_fn)(unsigned int *numerator, unsigned int *denominator);
typedef uintptr_t (*test_bias_fn)(void);
/* libcapture's movie command (CameraInterface RecordMovie) and its data
   getter, as libmmfmodules cMmfCamera calls them. */
typedef int (*record_movie_fn)(uint32_t id, unsigned char *payload, uint32_t length);
typedef int (*get_capture_data_fn)(uint32_t id, void *data, int *size);
/* The reviewed machine-word ABI of mm_camera_capture_still: 17 words. */
typedef int (*capture_still_words17_fn)(
    uint32_t w1, uint32_t w2, uint32_t w3, uint32_t w4, uint32_t w5,
    uint32_t w6, uint32_t w7, uint32_t w8, uint32_t w9, uint32_t w10,
    uint32_t w11, uint32_t w12, uint32_t w13, uint32_t w14, uint32_t w15,
    uint32_t w16, uint32_t w17);

/*
 * Image sets handed over by the capture library's receiver thread (the only
 * producer) to the caller's thread (the only consumer). The callback copies
 * the small structures into a slot and advances set_head; the caller's
 * thread copies the images, gives the buffers back and advances set_tail.
 */
enum { SET_OK = 0, SET_UNREADABLE = 1, SET_BASIC_UNREADABLE = 2 };
#define SET_QUEUE_SIZE 16u
typedef struct queued_set {
    int result;
    uint32_t maker_note;
    uint64_t arrived_ns;
    unsigned char basic[ENCODED_SET_METADATA_SIZE];
    unsigned char exif[G360_CAPTURE_EXIF_BYTES];
} queued_set;

static struct {
    int open;
    int latched;
    int log_fd;
    unsigned int handle;
    uint64_t free_bytes;
    uint32_t remaining_shots;
    uint32_t sequence;
    capture_still_words17_fn capture_fn;
    release_capture_buffer_fn release_fn;
    set_usr_attributes_fn set_usr_fn;
    set_var_attributes_fn set_var_fn;
    unsigned char *user_spec_copy;
    int frame_pause;
    /* Kept for the preview: the library handle, the callback slot and the
       subscription calls. */
    void *sdk;
    uint32_t slot;
    reg_topic_fn subscribe_set_fn;
    reg_topic_fn subscribe_notify_fn;
} session = {0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* The movie-mode preview (g360_preview_*). */
#define MOVIE_START_ID 0x01020001u
#define MOVIE_STOP_ID 0x01020002u
#define MOVIE_NOTIFY_START 0x02030001u
#define MOVIE_NOTIFY_STOP 0x02030002u
#define SENSOR_RATE_DATA_ID 0x01080017u
#define PREVIEW_SET_BLOCK_OFFSET 0x90u
#define PREVIEW_RANGE_CACHE 16u
static volatile uint32_t movie_mask; /* bit 0: VIDEOSTART_DONE, bit 1: VIDEOSTOP_DONE */
static volatile uint32_t preview_counts[4];
static volatile uint32_t image_quiet_count;
static struct {
    volatile int running;
    int subscribed;
    int set_subscribed;
    record_movie_fn record_fn;
    g360_preview_callback callback;
    void *context;
    uint32_t sensor_rate_bits;
    /* Ranges already found readable in /proc/self/maps (receiver thread). */
    uintptr_t range_start[PREVIEW_RANGE_CACHE];
    size_t range_size[PREVIEW_RANGE_CACHE];
    uint32_t range_next;
} preview;

static volatile uint32_t notify_mask;
static volatile uint32_t callback_count;
static queued_set set_queue[SET_QUEUE_SIZE];
static volatile uint32_t set_head;
static volatile uint32_t set_tail;
static volatile uint32_t sets_overflowed;
static volatile int accepting_sets;
static char client_name[] = "G360Cap"; /* the library's limit is nine bytes */

/* Separate /proc/self/maps buffers: the callback runs on the capture
   library's receiver thread while the caller's thread may read its own. */
static char maps_main[262144];
static char maps_callback[262144];

/*
 * The full-frame copy made in the expansion callback. The caller's thread
 * arms it before the still request (buffer allocated, source located); the
 * callback moves it from ARMED to COPYING, copies and sets DONE; the
 * caller's thread takes the result afterwards. The state changes are
 * atomic, so a callback that never comes, or comes late, is handled.
 */
enum { FRAME_IDLE = 0, FRAME_ARMED = 1, FRAME_COPYING = 2, FRAME_DONE = 3 };
static struct {
    volatile int state;
    int stuck;               /* a copy never finished; no further frames */
    unsigned char *buffer;
    uintptr_t source;
    size_t size;
    uint32_t physical;
    uint32_t flags;
    uint32_t hold_us;
    uint32_t mode;
} frame_job;
#define FRAME_DEFAULT_PHYSICAL 0xa6000000u
#define FRAME_DEFAULT_WIDTH 5824u
#define FRAME_DEFAULT_HEIGHT 2896u
#define FRAME_DEFAULT_STRIDE 11648u
#define FRAME_DEFAULT_BITS 16u

static int monotonic_ns(uint64_t *value)
{
    capture_timespec now;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
        now.tv_nsec < 0 || now.tv_nsec >= 1000000000L) {
        return -1;
    }
    *value = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    return 0;
}

static int sleep_poll(void)
{
    capture_timespec pause;
    capture_timespec remaining;
    pause.tv_sec = 0;
    pause.tv_nsec = POLL_NS;
    return nanosleep(&pause, &remaining) == 0 ? 0 : -1;
}

void g360_log(const char *text)
{
    char line[384];
    capture_timespec now;
    int length;
    size_t count;

    if (session.log_fd < 0 || text == 0) return;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &now) == 0) {
        length = snprintf(line, sizeof line, "g360cap: monotonic=%ld.%09ld %s\n",
                          (long)now.tv_sec, (long)now.tv_nsec, text);
    } else {
        length = snprintf(line, sizeof line, "g360cap: monotonic=unavailable %s\n", text);
    }
    if (length <= 0) return;
    count = (size_t)length;
    if (count >= sizeof line) {
        count = sizeof line - 1;
        line[count - 1] = '\n';
    }
    /* One write per line keeps callback lines from interleaving; the sync
       keeps the record if the camera goes down. */
    (void)write(session.log_fd, line, count);
    (void)fsync(session.log_fd);
}

static void log_values(const char *event, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    char message[256];
    (void)snprintf(message, sizeof message, "%s a=0x%lx b=0x%lx c=0x%lx d=0x%lx", event,
                   (unsigned long)a, (unsigned long)b, (unsigned long)c, (unsigned long)d);
    g360_log(message);
}

/* Reads /proc/self/maps into buffer; returns 0 when something was read. */
static int read_maps(char *buffer, size_t size)
{
    capture_file *maps = fopen(SELF_MAPS_PATH, "r");
    size_t total = 0;
    if (maps == 0) return -1;
    while (total < size - 1u) {
        size_t n = fread(buffer + total, 1, size - 1u - total, maps);
        if (n == 0) break;
        total += n;
    }
    fclose(maps);
    buffer[total] = '\0';
    return total > 0 ? 0 : -1;
}

static int readable(char *buffer, size_t size, uintptr_t start, size_t length)
{
    if (read_maps(buffer, size) != 0) return 0;
    return g360_maps_covers(buffer, start, length);
}

static uintptr_t camera_address(uint32_t word)
{
    return address_bias + (uintptr_t)word;
}

#define RAW_SAMPLE_STEP 65536u /* bytes between the spots re-read after a copy */
#define RAW_SAMPLE_BYTES 256u

/* Copies size bytes, then re-reads spots of the source (every
   RAW_SAMPLE_STEP bytes and the last RAW_SAMPLE_BYTES); returns 1 when any
   differ, meaning the source was being written while it was copied. */
static int copy_and_recheck(unsigned char *target, uintptr_t source, size_t size)
{
    size_t offset;
    memcpy(target, (const void *)source, size);
    for (offset = 0; offset < size; offset += RAW_SAMPLE_STEP) {
        size_t n = size - offset < RAW_SAMPLE_BYTES ? size - offset : RAW_SAMPLE_BYTES;
        if (memcmp(target + offset, (const void *)(source + offset), n) != 0) return 1;
    }
    if (size >= RAW_SAMPLE_BYTES &&
        memcmp(target + size - RAW_SAMPLE_BYTES, (const void *)(source + size - RAW_SAMPLE_BYTES),
               RAW_SAMPLE_BYTES) != 0) {
        return 1;
    }
    return 0;
}

/* 1 when every sample of every row (16-bit little-endian words) fits bits. */
static int samples_fit(const unsigned char *data, uint32_t width, uint32_t height, uint32_t stride,
                       uint32_t bits)
{
    uint32_t max_value = bits >= 16u ? 0xffffu : (1u << bits) - 1u;
    size_t y;
    if (bits >= 16u) return 1;
    for (y = 0; y < height; ++y) {
        const unsigned char *row = data + y * (size_t)stride;
        size_t x;
        for (x = 0; x < width; ++x) {
            uint32_t sample = (uint32_t)row[2 * x] | ((uint32_t)row[2 * x + 1] << 8);
            if (sample > max_value) return 0;
        }
    }
    return 1;
}

static void on_notify(int event, void *data, unsigned int size)
{
    uint32_t topic = (uint32_t)event;
    (void)data;
    __sync_fetch_and_add(&callback_count, 1u);
    if (topic > NOTIFY_TOPIC_BASE && topic < NOTIFY_TOPIC_BASE + 32u) {
        __sync_fetch_and_or(&notify_mask, 1u << (topic - NOTIFY_TOPIC_BASE));
    }
    if (topic == MOVIE_NOTIFY_START) __sync_fetch_and_or(&movie_mask, 1u);
    if (topic == MOVIE_NOTIFY_STOP) __sync_fetch_and_or(&movie_mask, 2u);
    log_values("callback.notify", topic, size, 0, 0);
}

/* 1 when [start, start + size) is mapped readable; remembers ranges it has
   seen, since the camera cycles a few frame buffers (receiver thread only). */
static int preview_readable(uintptr_t start, size_t size)
{
    uint32_t k;
    if (start == 0 || size == 0) return 0;
    for (k = 0; k < PREVIEW_RANGE_CACHE; ++k) {
        if (preview.range_size[k] != 0 && preview.range_start[k] == start &&
            preview.range_size[k] == size) {
            return 1;
        }
    }
    if (!readable(maps_callback, sizeof maps_callback, start, size)) return 0;
    k = preview.range_next++ % PREVIEW_RANGE_CACHE;
    preview.range_start[k] = start;
    preview.range_size[k] = size;
    return 1;
}

static uint32_t block_word(const unsigned char *block, size_t offset)
{
    uint32_t value;
    memcpy(&value, block + offset, sizeof value);
    return value;
}

/*
 * One movie frame's block (stCAPTURE_IMAGE), read the way the stock
 * consumer libmmfmodules cMmfCamera::VideoCallback reads it
 * (session.image-route-block-layout-from-stock-consumer-20261107).
 */
static void preview_frame(uint32_t topic, const unsigned char *block)
{
    g360_preview_frame frame;
    g360_preview_callback callback = preview.callback;
    size_t plane;

    memset(&frame, 0, sizeof frame);
    frame.topic = topic;
    frame.block = block;
    frame.buffer_width = block_word(block, 0x00);
    frame.buffer_height = block_word(block, 0x04);
    frame.width = block_word(block, 0x08);
    frame.height = block_word(block, 0x0c);
    frame.yuv_kind[0] = block_word(block, 0x38);
    frame.yuv_kind[1] = block_word(block, 0x3c);
    frame.address_type = block_word(block, 0x40);
    frame.y_address = block_word(block, 0x54);
    frame.side = block_word(block, 0x64);
    frame.timestamp = (uint64_t)block_word(block, 0x68) |
                      ((uint64_t)block_word(block, 0x6c) << 32);
    switch (frame.address_type) {
    case 0: frame.c_address = frame.y_address + frame.buffer_width * frame.buffer_height; break;
    case 5: case 7: frame.c_address = block_word(block, 0x58); break;
    case 6: frame.c_address = block_word(block, 0x5c); break;
    default: frame.c_address = 0; break;
    }
    plane = (size_t)frame.buffer_width * frame.buffer_height;
    if (frame.buffer_width != 0 && frame.buffer_height != 0 && frame.width != 0 &&
        frame.height != 0 && frame.width <= frame.buffer_width &&
        frame.height <= frame.buffer_height && plane <= G360_MAX_IMAGE_BYTES &&
        frame.y_address != 0 && frame.c_address != 0 &&
        preview_readable(camera_address(frame.y_address), plane) &&
        preview_readable(camera_address(frame.c_address), plane / 2u)) {
        frame.y = (const unsigned char *)camera_address(frame.y_address);
        frame.c = (const unsigned char *)camera_address(frame.c_address);
        frame.y_bytes = plane;
        frame.c_bytes = plane / 2u;
    }
    if (callback != 0) callback(&frame, preview.context);
}

/*
 * The still-expansion callback, on the capture library's receiver thread.
 * The camera waits for the gyro reply that the Beauty object sends after
 * this returns (with g360_open_options.frame_pause this client is called
 * first), so an armed frame copy happens here while the full RAW frame is
 * still intact.
 */
static void on_expansion(const void *descriptor, unsigned int size)
{
    uint32_t mode = 0;
    uint64_t begin = 0;
    uint64_t end = 0;
    uintptr_t source = 0;
    if (__sync_fetch_and_add(&frame_job.state, 0) != FRAME_ARMED) return;
    if (size != EXPANSION_DESCRIPTOR_SIZE ||
        !readable(maps_callback, sizeof maps_callback, (uintptr_t)descriptor, size)) {
        g360_log("frame: invalid expansion descriptor; ignored");
        return;
    }
    memcpy(&mode, (const unsigned char *)descriptor + EXPANSION_MODE_OFFSET, sizeof mode);
    /* Only orientation mode 3 is the measured still/gyro pause. Other
       expansion requests must not consume the next still's armed copy. */
    if (mode != 3u) {
        log_values("frame: unmeasured expansion mode; ignored", mode, size, 0, 0);
        return;
    }
    if (!__sync_bool_compare_and_swap(&frame_job.state, FRAME_ARMED, FRAME_COPYING)) {
        log_values("callback.expansion: no frame copy armed (mode, size)", mode, size, 0, 0);
        return;
    }
    if (monotonic_ns(&begin) != 0) begin = 0;
    frame_job.mode = mode;
    frame_job.flags |= G360_IMAGE_PRESENT;
    /* Re-resolve the physical mapping at copy time: a readable virtual
       range alone would also accept a remapped, unrelated allocation. */
    if (read_maps(maps_callback, sizeof maps_callback) != 0 ||
        !g360_maps_physical(maps_callback, frame_job.physical, frame_job.size, &source) ||
        source != frame_job.source) {
        frame_job.flags |= G360_IMAGE_UNMAPPED;
    } else {
        if (copy_and_recheck(frame_job.buffer, frame_job.source, frame_job.size)) {
            frame_job.flags |= G360_IMAGE_RAW_CHANGED;
        }
        frame_job.flags |= G360_IMAGE_COPIED;
    }
    if (monotonic_ns(&end) != 0 || begin == 0 || end < begin) end = begin;
    frame_job.hold_us = (uint32_t)((end - begin) / 1000u);
    log_values("frame: copied in the expansion callback (physical, flags, hold us, mode)",
               frame_job.physical, frame_job.flags, frame_job.hold_us, mode);
    __sync_synchronize();
    frame_job.state = FRAME_DONE;
}

static void on_image(int topic, void *rect_block, void *image_object, unsigned int size)
{
    uint32_t id = (uint32_t)topic;
    uint32_t count;
    (void)image_object;
    __sync_fetch_and_add(&callback_count, 1u);
    if (id >= G360_PREVIEW_TOPIC_THUMB && id <= G360_PREVIEW_TOPIC_MOVIE) {
        count = __sync_add_and_fetch(&preview_counts[id & 3u], 1u);
        /* Frames can come 30 times a second: log the first and every 300th. */
        if (count == 1u || count % 300u == 0u) {
            log_values("callback.image (topic, size, count, running)", id, size, count,
                       (uint32_t)preview.running);
        }
        if (preview.running && rect_block != 0) preview_frame(id, (const unsigned char *)rect_block);
        return;
    }
    count = __sync_add_and_fetch(&image_quiet_count, 1u);
    if (count == 1u || count % 300u == 0u) {
        log_values("callback.image (not subscribed; ignored; count)", id, size, count, 0);
    }
}

/*
 * Runs on the capture library's receiver thread. The set is the library's
 * stack copy, valid only during this call, so the small structures are
 * copied into the queue here; the image bytes stay in the camera's buffers,
 * which remain valid until they are given back.
 */
static void on_image_set(int topic, void *image_set, void *data, unsigned int size)
{
    uint32_t basic_word = 0;
    uint32_t maker_note = 0;
    uintptr_t set_address = (uintptr_t)image_set;
    uintptr_t basic;
    uint32_t head;
    queued_set *slot;

    __sync_fetch_and_add(&callback_count, 1u);
    if ((uint32_t)topic == G360_PREVIEW_TOPIC_SET) {
        /* A movie frame: its block sits at the set's first word + 0x90. */
        uint32_t count = __sync_add_and_fetch(&preview_counts[0], 1u);
        uint32_t first = 0;
        uintptr_t block = 0;
        if (image_set != 0) memcpy(&first, image_set, sizeof first);
        if (first != 0) {
            block = camera_address(first) + PREVIEW_SET_BLOCK_OFFSET;
            if (!preview_readable(block, G360_PREVIEW_BLOCK_BYTES)) block = 0;
        }
        if (count == 1u || count % 300u == 0u) {
            log_values("callback.image_set movie (size, first word, count, readable)", size, first,
                       count, block != 0);
        }
        if (preview.running && block != 0) {
            preview_frame(G360_PREVIEW_TOPIC_SET, (const unsigned char *)block);
        }
        return;
    }
    log_values("callback.image_set", (uint32_t)topic, size, (uint32_t)set_address, 0);
    if ((uint32_t)topic == EXPANSION_TOPIC) {
        on_expansion(data, size);
        return;
    }
    if ((uint32_t)topic != ENCODED_SET_TOPIC) return;
    if (!__sync_fetch_and_add(&accepting_sets, 0)) {
        g360_log("image set while the session is not open; not read");
        return;
    }
    head = set_head;
    if (head - __sync_fetch_and_add(&set_tail, 0u) >= SET_QUEUE_SIZE) {
        __sync_fetch_and_add(&sets_overflowed, 1u);
        g360_log("image set queue full; set not read and not given back");
        return;
    }
    slot = &set_queue[head % SET_QUEUE_SIZE];
    slot->maker_note = 0;
    if (monotonic_ns(&slot->arrived_ns) != 0) slot->arrived_ns = 0;
    if (image_set == 0 ||
        !readable(maps_callback, sizeof maps_callback, set_address, SET_READ_LENGTH)) {
        g360_log("image set structure not readable; nothing read");
        slot->result = SET_UNREADABLE;
    } else {
        memcpy(&basic_word, (const unsigned char *)image_set + SET_BASIC_POINTER_OFFSET,
               sizeof basic_word);
        memcpy(&maker_note, (const unsigned char *)image_set + SET_MAKER_NOTE_OFFSET,
               sizeof maker_note);
        basic = camera_address(basic_word);
        slot->maker_note = maker_note;
        memcpy(slot->exif, (const unsigned char *)image_set + SET_EXIF_OFFSET,
               sizeof slot->exif);
        if (basic_word == 0 ||
            !readable(maps_callback, sizeof maps_callback, basic, ENCODED_SET_METADATA_SIZE)) {
            log_values("basic set not readable; nothing read", basic_word, maker_note, 0, 0);
            slot->result = SET_BASIC_UNREADABLE;
        } else {
            memcpy(slot->basic, (const void *)basic, ENCODED_SET_METADATA_SIZE);
            slot->result = SET_OK;
            log_values("image set queued (basic word, maker note, slot)", basic_word, maker_note,
                       head, 0);
        }
    }
    __sync_synchronize();
    set_head = head + 1u;
}

static int resolve(void *handle, const char *name, void *target, size_t target_size)
{
    void *symbol = dlsym(handle, name);
    if (symbol == 0 || target_size != sizeof symbol) return -1;
    memcpy(target, &symbol, target_size);
    return 0;
}

static int gyro_gate(void)
{
    void *device = dlopen(DEVICE_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    device_get_property_fn property_fn = 0;
    int size = 0;
    int phys = 0;
    int pos = 0;
    if (device == 0 || resolve(device, "device_get_property", &property_fn,
                               sizeof property_fn) != 0) {
        g360_log("gyro gate: libdevice-node or device_get_property missing");
        return G360_E_LOAD;
    }
    (void)property_fn(GYRO_DEVICE_TYPE, GYRO_PROP_SIZE, &size);
    (void)property_fn(GYRO_DEVICE_TYPE, GYRO_PROP_PHYS, &phys);
    (void)property_fn(GYRO_DEVICE_TYPE, GYRO_PROP_POS, &pos);
    log_values("gyro gate: history size/phys/pos", (uint32_t)size, (uint32_t)phys,
               (uint32_t)pos, 0);
    if (size <= 0 || phys == 0) {
        g360_log("gyro gate: motion history unavailable (libdevice-node/libm not linked into the executable?)");
        return G360_E_GYRO;
    }
    return G360_OK;
}

/*
 * The capture library's image-set handler asks CApex for the lens's maximum
 * aperture, which needs CApex::Initialize. The stock app's
 * Apex_Initialize_A9() would also write two pointers into the shared RTOS
 * preference area, so this gives the singleton a private copy instead.
 */
static int apex_gate(void)
{
    void *user_spec = dlopen(USER_SPEC_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    void *apex = dlopen(APEX_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    pref_rtos_get_fd_fn pref_base_fn = 0;
    apex_get_max_av_fn get_max_av_fn = 0;
    capex_get_instance_fn get_instance_fn = 0;
    capex_initialize_fn initialize_fn = 0;
    uintptr_t base;
    uint32_t magic;
    uint32_t table;
    unsigned char *info;
    void *instance;
    void *info_now = 0;
    unsigned int num = 0;
    unsigned int den = 0;

    if (user_spec == 0 || apex == 0 ||
        resolve(user_spec, "pref_rtos_get_fd", &pref_base_fn, sizeof pref_base_fn) != 0 ||
        resolve(user_spec, "_Z33Apex_Get_Max_Av_Value_For_CapturePjS_", &get_max_av_fn,
                sizeof get_max_av_fn) != 0 ||
        resolve(apex, "_ZN5CApex11GetInstanceEv", &get_instance_fn,
                sizeof get_instance_fn) != 0 ||
        resolve(apex, "_ZN5CApex10InitializeEP13tag_apex_info", &initialize_fn,
                sizeof initialize_fn) != 0) {
        g360_log("apex gate: libuser-spec/libapex symbol missing");
        return G360_E_LOAD;
    }
    base = pref_base_fn();
    log_values("apex gate: rtos pref base", (uint32_t)base, 0, 0, 0);
    if (base == 0 ||
        !readable(maps_main, sizeof maps_main, base + USER_SPEC_OFFSET, USER_SPEC_SIZE)) {
        g360_log("apex gate: user-spec block not mapped readable");
        return G360_E_APEX;
    }
    memcpy(&magic, (const void *)(base + USER_SPEC_OFFSET), sizeof magic);
    log_values("apex gate: user-spec magic", magic, 0, 0, 0);
    if (magic != USER_SPEC_MAGIC) {
        g360_log("apex gate: user-spec magic missing; refusing (library init would reset shared settings)");
        return G360_E_APEX;
    }
    session.user_spec_copy = malloc(USER_SPEC_SIZE);
    if (session.user_spec_copy == 0) return G360_E_MEMORY;
    memcpy(session.user_spec_copy, (const void *)(base + USER_SPEC_OFFSET), USER_SPEC_SIZE);
    info = session.user_spec_copy + APEX_INFO_OFFSET;
    table = (uint32_t)(uintptr_t)(info + APEX_INFO_TABLE_A);
    memcpy(info + 4, &table, sizeof table);
    table = (uint32_t)(uintptr_t)(info + APEX_INFO_TABLE_B);
    memcpy(info + 0xc, &table, sizeof table);
    instance = get_instance_fn();
    if (instance == 0) {
        g360_log("apex gate: CApex instance null");
        return G360_E_APEX;
    }
    initialize_fn(instance, info);
    memcpy(&info_now, (const unsigned char *)instance + APEX_INSTANCE_INFO_FIELD,
           sizeof info_now);
    if (info_now != (void *)info) {
        g360_log("apex gate: CApex info pointer not set");
        return G360_E_APEX;
    }
    get_max_av_fn(&num, &den); /* the image-set handler's own call */
    log_values("apex gate: private copy set; max aperture num/den", num, den, 0, 0);
    return G360_OK;
}

static int bridge_gate(mcb_send_fn send_fn, mcb_alive_fn alive_fn)
{
    uint64_t start;
    uint64_t now;
    if (alive_fn() != 0) {
        g360_log("bridge already alive; no notify sent");
        return G360_OK;
    }
    if (monotonic_ns(&start) != 0) return G360_E_CLOCK;
    g360_log("bridge notify send (category 16)");
    send_fn((unsigned short)NOTIFY_CATEGORY, 0, 0, 0);
    for (;;) {
        if (alive_fn() != 0) {
            g360_log("bridge alive");
            return G360_OK;
        }
        if (monotonic_ns(&now) != 0 || now < start) return G360_E_CLOCK;
        if (now - start >= BRIDGE_SETTLE_NS) {
            g360_log("bridge did not come alive");
            return G360_E_BRIDGE;
        }
        if (sleep_poll() != 0) return G360_E_CLOCK;
    }
}

/*
 * Registers the library's callback client and its subscriptions: the
 * encoded-set topic, the still notifications and, with with_expansion, the
 * still-expansion topic. Returns G360_OK or G360_E_INIT.
 */
static int register_client(reg_callback_client_fn register_fn, reg_topic_fn subscribe_set_fn,
                           reg_topic_fn subscribe_notify_fn, int with_expansion)
{
    static const uint32_t notify_ids[] = {
        NOTIFY_ID_CAPTURE_START, NOTIFY_ID_CAPTURING, NOTIFY_ID_PROCESSING,
        NOTIFY_ID_QVIEW_DONE, NOTIFY_ID_NEXTSHOT_READY, NOTIFY_ID_CAPTURE_READY,
    };
    uint32_t slot = register_fn(client_name, on_notify, on_image, on_image_set);
    size_t i;
    log_values("open: RegCallbackClient slot", slot, 0, 0, 0);
    session.slot = slot;
    session.subscribe_set_fn = subscribe_set_fn;
    session.subscribe_notify_fn = subscribe_notify_fn;
    if (slot == UINT32_MAX || subscribe_set_fn(slot, ENCODED_SET_TOPIC) != 0) {
        g360_log("open: registration or image-set subscription failed");
        return G360_E_INIT;
    }
    if (with_expansion && subscribe_set_fn(slot, EXPANSION_TOPIC) != 0) {
        g360_log("open: expansion subscription failed");
        return G360_E_INIT;
    }
    for (i = 0; i < sizeof notify_ids / sizeof notify_ids[0]; ++i) {
        if (subscribe_notify_fn(slot, NOTIFY_TOPIC_BASE + notify_ids[i]) != 0) {
            g360_log("open: notification subscription failed");
            return G360_E_INIT;
        }
    }
    return G360_OK;
}

int g360_open(const g360_open_options *options)
{
    void *sdk;
    void *bridge;
    adapter_instance_fn instance_fn = 0;
    adapter_initialize_fn initialize_fn = 0;
    beauty_instance_fn beauty_fn = 0;
    mm_camera_create_fn create_fn = 0;
    reg_callback_client_fn register_fn = 0;
    reg_topic_fn subscribe_set_fn = 0;
    reg_topic_fn subscribe_notify_fn = 0;
    mcb_send_fn send_fn = 0;
    mcb_alive_fn alive_fn = 0;
    test_bias_fn bias_fn = 0;
    void *instance;
    uint64_t clock_check;
    int status;

    if (options == 0) return G360_E_ARGUMENT;
    if (session.open || session.latched) return G360_E_STATE;
    if (options->log_path != 0) {
        session.log_fd = open(options->log_path,
                              CAPTURE_O_WRONLY | CAPTURE_O_CREAT | G360_O_APPEND, 0644);
        if (session.log_fd < 0) return G360_E_IO;
    }
    if (monotonic_ns(&clock_check) != 0) return G360_E_CLOCK;
    session.free_bytes = options->free_bytes;
    session.remaining_shots = options->remaining_shots ? options->remaining_shots
                                                       : DEFAULT_REMAINING_SHOTS;
    session.frame_pause = options->frame_pause != 0;
    g360_log("open: loading the capture library");

    sdk = dlopen(SDK_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    bridge = sdk ? dlopen(BRIDGE_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL) : 0;
    if (sdk == 0 || bridge == 0 ||
        resolve(sdk, "_ZN11CMCBAdapter8InstanceEv", &instance_fn, sizeof instance_fn) != 0 ||
        resolve(sdk, "_ZN11CMCBAdapter10InitializeEv", &initialize_fn,
                sizeof initialize_fn) != 0 ||
        resolve(sdk, "_ZN19CBeautyFaceIf_Linux11GetInstanceEv", &beauty_fn,
                sizeof beauty_fn) != 0 ||
        resolve(sdk, "mm_camera_create", &create_fn, sizeof create_fn) != 0 ||
        resolve(sdk, "RegCallbackClient", &register_fn, sizeof register_fn) != 0 ||
        resolve(sdk, "RegImageSetCallbackId", &subscribe_set_fn,
                sizeof subscribe_set_fn) != 0 ||
        resolve(sdk, "RegNotifyCallbackId", &subscribe_notify_fn,
                sizeof subscribe_notify_fn) != 0 ||
        resolve(sdk, "mm_camera_capture_still", &session.capture_fn,
                sizeof session.capture_fn) != 0 ||
        resolve(sdk, "ReleaseCaptureBuffer", &session.release_fn,
                sizeof session.release_fn) != 0 ||
        resolve(sdk, "mm_camera_set_usr_attributes", &session.set_usr_fn,
                sizeof session.set_usr_fn) != 0 ||
        resolve(sdk, "mm_camera_set_var_attributes", &session.set_var_fn,
                sizeof session.set_var_fn) != 0 ||
        resolve(bridge, "Mcb_send", &send_fn, sizeof send_fn) != 0 ||
        resolve(bridge, "_ZN16CMulticoreBridge18IsDestinationAliveEv", &alive_fn,
                sizeof alive_fn) != 0) {
        g360_log("open: capture library, bridge or a required symbol missing; nothing initialized");
        return G360_E_LOAD;
    }
    session.sdk = sdk;
    if (resolve(sdk, "g360_test_address_bias", &bias_fn, sizeof bias_fn) == 0) {
        address_bias = bias_fn(); /* host tests only; the camera library has no such symbol */
    }

    status = gyro_gate();
    if (status != G360_OK) return status;
    status = apex_gate();
    if (status != G360_OK) return status;

    instance = instance_fn();
    if (instance == 0 || initialize_fn(instance) != 0) {
        g360_log("open: adapter instance/initialize failed");
        session.latched = 1;
        return G360_E_INIT;
    }
    if (session.frame_pause) {
        /* Before the Beauty object subscribes to the expansion topic, so the
           capture library calls this client first (it calls subscribers in
           subscription order). */
        g360_log("open: registering before the Beauty object (frame pause)");
        if (register_client(register_fn, subscribe_set_fn, subscribe_notify_fn, 1) != G360_OK) {
            session.latched = 1;
            return G360_E_INIT;
        }
    }
    if (beauty_fn() == 0) {
        g360_log("open: Beauty singleton null");
        session.latched = 1;
        return G360_E_INIT;
    }
    status = create_fn(&session.handle);
    log_values("open: mm_camera_create (status, handle)", (uint32_t)status, session.handle, 0, 0);
    if (status != 0) {
        session.latched = 1;
        return G360_E_INIT;
    }
    if (!session.frame_pause &&
        register_client(register_fn, subscribe_set_fn, subscribe_notify_fn, 0) != G360_OK) {
        session.latched = 1;
        return G360_E_INIT;
    }
    status = bridge_gate(send_fn, alive_fn);
    if (status != G360_OK) {
        session.latched = 1;
        return status;
    }
    session.open = 1;
    __sync_lock_test_and_set(&accepting_sets, 1);
    g360_log("open: ready");
    return G360_OK;
}

/*
 * Fills one image's description from its descriptor and, when do_copy is
 * set, copies its bytes from the camera's buffer into owned memory.
 */
static void copy_image(const encoded_set_image_descriptor *descriptor, int is_jpeg,
                       int do_copy, g360_image *image)
{
    uintptr_t source;
    size_t size = descriptor->buffer_size_word;

    image->width = descriptor->width;
    image->height = descriptor->height;
    image->stride_x = descriptor->stride_x;
    image->stride_y = descriptor->stride_y;
    image->camera_address = descriptor->first_virtual_word;
    if (descriptor->first_virtual_word == 0 || size == 0) return;
    image->flags |= G360_IMAGE_PRESENT;
    if (!do_copy) return;
    if (size > G360_MAX_IMAGE_BYTES) {
        image->flags |= G360_IMAGE_TOO_LARGE;
        return;
    }
    source = camera_address(descriptor->first_virtual_word);
    if (!readable(maps_main, sizeof maps_main, source, size)) {
        image->flags |= G360_IMAGE_UNMAPPED;
        return;
    }
    image->data = malloc(size);
    if (image->data == 0) {
        image->flags |= G360_IMAGE_NO_MEMORY;
        return;
    }
    memcpy(image->data, (const void *)source, size);
    image->size = size;
    image->flags |= G360_IMAGE_COPIED;
    if (is_jpeg && g360_jpeg_markers_ok(image->data, size)) image->flags |= G360_IMAGE_JPEG_OK;
}

/*
 * Copies the camera's maker-note buffer (the RTOS's image-processing
 * records) before it is given back: the largest of 64, 32 or 24 KiB that
 * the process can read from its address.
 */
static void copy_maker_note(uint32_t address, g360_image *image)
{
    static const size_t sizes[3] = {G360_MAKER_NOTE_BYTES, 0x8000u, 0x6000u};
    uintptr_t source;
    int i;

    image->camera_address = address;
    if (address == 0) return;
    image->flags |= G360_IMAGE_PRESENT;
    source = camera_address(address);
    for (i = 0; i < 3; ++i) {
        if (readable(maps_main, sizeof maps_main, source, sizes[i])) break;
    }
    if (i == 3) {
        image->flags |= G360_IMAGE_UNMAPPED;
        log_values("maker note: not mapped readable; not copied", address, 0, 0, 0);
        return;
    }
    image->data = malloc(sizes[i]);
    if (image->data == 0) {
        image->flags |= G360_IMAGE_NO_MEMORY;
        return;
    }
    memcpy(image->data, (const void *)source, sizes[i]);
    image->size = sizes[i];
    image->flags |= G360_IMAGE_COPIED;
    log_values("maker note: copied (address, bytes)", address, (uint32_t)sizes[i], 0, 0);
}

static uint32_t give_back(int type, uint32_t address)
{
    int result = session.release_fn(type, address);
    log_values("ReleaseCaptureBuffer (type, address, result)", (uint32_t)type, address,
               (uint32_t)result, 0);
    return result == 0 ? G360_IMAGE_RELEASED : G360_IMAGE_RELEASE_ERR;
}

static int capture_is_ready(void)
{
    return (__sync_fetch_and_add(&notify_mask, 0) & (1u << NOTIFY_ID_CAPTURE_READY)) != 0;
}

/*
 * The still's RAW images. The RTOS writes them to the same physical
 * addresses on every still and does not describe them in the image set, so
 * their virtual address comes from the set's own physical/virtual pair (the
 * capture library maps the whole capture memory in one mapping): virtual =
 * RAW physical + (image virtual - image physical). Defaults: the two
 * per-lens images (see g360_raw_layout in g360cap.h).
 */
static const uint32_t raw_default_physical[G360_MAX_RAW] = {0xab134000u, 0xad134000u};
#define RAW_DEFAULT_WIDTH 2896u
#define RAW_DEFAULT_HEIGHT 2896u
#define RAW_DEFAULT_STRIDE 5824u
#define RAW_DEFAULT_BITS 16u

static void raw_layout_defaults(const g360_raw_layout *in, uint32_t index, g360_raw_layout *out)
{
    out->physical = in && in->physical ? in->physical : raw_default_physical[index];
    out->width = in && in->width ? in->width : RAW_DEFAULT_WIDTH;
    out->height = in && in->height ? in->height : RAW_DEFAULT_HEIGHT;
    out->stride = in && in->stride ? in->stride : RAW_DEFAULT_STRIDE;
    out->bits = in && in->bits ? in->bits : RAW_DEFAULT_BITS;
}

/*
 * Copies one RAW image using the physical/virtual pair of one descriptor of
 * the same set. Sets image->flags (PRESENT, COPIED, UNMAPPED, TOO_LARGE,
 * NO_MEMORY, RAW_VALUES_OK, RAW_CHANGED).
 */
static void copy_raw(const encoded_set_image_descriptor *pair, const g360_raw_layout *layout,
                     g360_image *image)
{
    size_t size;
    uint32_t virtual_word;
    uintptr_t source;
    int values_ok;

    image->width = layout->width;
    image->height = layout->height;
    image->stride_x = layout->stride;
    image->stride_y = layout->bits;
    if (pair->first_physical_word == 0 || pair->first_virtual_word == 0 || layout->bits == 0 ||
        !g360_raw_extent(layout->width, layout->height, layout->stride, layout->bits,
                         G360_MAX_IMAGE_BYTES, &size)) {
        log_values("raw: no physical/virtual pair or bad layout; not copied",
                   pair->first_physical_word, pair->first_virtual_word, layout->stride, layout->bits);
        return;
    }
    virtual_word = layout->physical + (pair->first_virtual_word - pair->first_physical_word);
    image->camera_address = virtual_word;
    image->flags |= G360_IMAGE_PRESENT;
    source = camera_address(virtual_word);
    if (!readable(maps_main, sizeof maps_main, source, size)) {
        log_values("raw: range not mapped readable; nothing read", layout->physical, virtual_word,
                   (uint32_t)size, 0);
        image->flags |= G360_IMAGE_UNMAPPED;
        return;
    }
    image->data = malloc(size);
    if (image->data == 0) {
        image->flags |= G360_IMAGE_NO_MEMORY;
        return;
    }
    if (copy_and_recheck(image->data, source, size)) image->flags |= G360_IMAGE_RAW_CHANGED;
    image->size = size;
    image->flags |= G360_IMAGE_COPIED;
    values_ok = samples_fit(image->data, layout->width, layout->height, layout->stride,
                            layout->bits);
    if (values_ok) image->flags |= G360_IMAGE_RAW_VALUES_OK;
    log_values("raw: copied (physical, virtual, size, flags)", layout->physical, virtual_word,
               (uint32_t)size, image->flags);
}

/*
 * Copies (when do_copy is set) and gives back one queued image set, in the
 * stock order and types; also copies raw_count RAW images (layouts in raw)
 * first. Returns G360_E_SET when the set cannot be read or has an unknown
 * type (nothing but the maker note can be given back then), else the
 * outcome of the copies and give-backs.
 */
static int process_set(const queued_set *entry, int do_copy, g360_still *still,
                       const g360_raw_layout *raw, uint32_t raw_count, int skip_jpeg)
{
    uint32_t k;
    static const int jpeg_kinds[3] = {G360_IMAGE_MAIN, G360_IMAGE_SCREEN, G360_IMAGE_THUMB};
    static const int descriptor_index[3] = {
        ENCODED_SET_IMAGE_MAIN, ENCODED_SET_IMAGE_SCREEN, ENCODED_SET_IMAGE_THUMBNAIL};
    static const int release_types[3] = {
        RELEASE_TYPE_MAIN, RELEASE_TYPE_SCREEN, RELEASE_TYPE_THUMB};
    encoded_set_metadata meta;
    int result = G360_OK;
    int i;

    still->maker_note_address = entry->maker_note;
    if (entry->result != SET_OK) {
        /* Without the basic set the buffers' addresses are unknown; only
           the maker note can be given back. */
        if (entry->maker_note != 0) {
            still->maker_note_flags = give_back(RELEASE_TYPE_MAKER_NOTE, entry->maker_note);
        }
        return G360_E_SET;
    }
    if (encoded_set_decode(entry->basic, sizeof entry->basic, &meta) != ENCODED_SET_OK) {
        return G360_E_SET;
    }
    still->set_type = meta.set_type;
    if (do_copy) {
        memcpy(still->capture_exif, entry->exif, sizeof still->capture_exif);
        still->capture_exif_size = sizeof still->capture_exif;
    }
    if (meta.set_type < 1u || meta.set_type > 3u) {
        /* Not a layout this library knows: its addresses are not trusted,
           so nothing is read or given back. */
        log_values("capture: unknown set type; nothing read or given back", meta.set_type, 0, 0, 0);
        return G360_E_SET;
    }
    log_values("capture: set (type, main size, screen size, thumb size)", meta.set_type,
               meta.images[ENCODED_SET_IMAGE_MAIN].buffer_size_word,
               meta.images[ENCODED_SET_IMAGE_SCREEN].buffer_size_word,
               meta.images[ENCODED_SET_IMAGE_THUMBNAIL].buffer_size_word);

    /* The RAW images first: nothing holds them for us, while the JPEG
       buffers stay ours until they are given back. */
    if (do_copy) {
        for (k = 0; k < raw_count; ++k) {
            still->raw_physical[k] = raw[k].physical;
            copy_raw(&meta.images[ENCODED_SET_IMAGE_MAIN], &raw[k], &still->raw[k]);
        }
    }

    /* Copy first, on this thread, while the camera still holds the buffers. */
    for (i = 0; i < 3; ++i) {
        copy_image(&meta.images[descriptor_index[i]], 1, do_copy && !skip_jpeg,
                   &still->images[jpeg_kinds[i]]);
        if (do_copy && skip_jpeg &&
            (still->images[jpeg_kinds[i]].flags & G360_IMAGE_PRESENT)) {
            still->images[jpeg_kinds[i]].flags |= G360_IMAGE_SKIPPED;
        }
    }
    if (meta.set_type != 1u) {
        copy_image(&meta.images[ENCODED_SET_IMAGE_FIRST], 0, do_copy,
                   &still->images[G360_IMAGE_FIRST]);
    }
    for (i = 0; i < G360_IMAGE_COUNT; ++i) {
        const g360_image *image = &still->images[i];
        log_values("capture: image (kind, flags, size, width)", (uint32_t)i, image->flags,
                   (uint32_t)image->size, image->width);
    }

    if (do_copy) copy_maker_note(entry->maker_note, &still->maker_note);

    /* Then give every buffer back, in the stock order and types. */
    if (entry->maker_note != 0) {
        still->maker_note_flags = give_back(RELEASE_TYPE_MAKER_NOTE, entry->maker_note);
    }
    if (meta.set_type != 1u && still->images[G360_IMAGE_FIRST].camera_address != 0) {
        still->images[G360_IMAGE_FIRST].flags |=
            give_back(RELEASE_TYPE_RAW, still->images[G360_IMAGE_FIRST].camera_address);
    }
    for (i = 0; i < 3; ++i) {
        g360_image *image = &still->images[jpeg_kinds[i]];
        if (image->camera_address != 0) {
            image->flags |= give_back(release_types[i], image->camera_address);
        }
    }
    if (!do_copy) return G360_OK;

    /* The outcome: every listed image copied (JPEGs complete), and at
       least the main JPEG or the first image (RAW-only set types may list
       no main JPEG). */
    for (i = 0; i < G360_IMAGE_COUNT; ++i) {
        uint32_t flags = still->images[i].flags;
        if (!(flags & G360_IMAGE_PRESENT)) continue;
        if (flags & G360_IMAGE_SKIPPED) continue;
        if (!(flags & G360_IMAGE_COPIED)) result = G360_E_IMAGE;
        if (i != G360_IMAGE_FIRST && !(flags & G360_IMAGE_JPEG_OK)) result = G360_E_IMAGE;
    }
    if (!(still->images[G360_IMAGE_MAIN].flags &
          (G360_IMAGE_COPIED | G360_IMAGE_SKIPPED)) &&
        !(still->images[G360_IMAGE_FIRST].flags & G360_IMAGE_COPIED)) {
        result = G360_E_IMAGE;
    }
    for (i = 0; i < G360_IMAGE_COUNT; ++i) {
        if ((still->images[i].flags & G360_IMAGE_RELEASE_ERR) && result == G360_OK) {
            result = G360_E_RELEASE;
        }
    }
    if ((still->maker_note_flags & G360_IMAGE_RELEASE_ERR) && result == G360_OK) {
        result = G360_E_RELEASE;
    }
    for (k = 0; k < raw_count; ++k) {
        uint32_t flags = still->raw[k].flags;
        if (result == G360_OK &&
            ((flags & (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK)) !=
                 (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK) ||
             (flags & G360_IMAGE_RAW_CHANGED))) {
            result = G360_E_IMAGE;
        }
    }
    return result;
}

/* 1 when a queued set is waiting; it is then at *entry. */
static int next_queued_set(const queued_set **entry)
{
    uint32_t tail = set_tail;
    if (__sync_fetch_and_add(&set_head, 0u) == tail) return 0;
    *entry = &set_queue[tail % SET_QUEUE_SIZE];
    return 1;
}

static void pop_queued_set(void)
{
    __sync_synchronize();
    set_tail = set_tail + 1u;
}

int g360_service(uint32_t wait_ms);

/* Gives back image sets that arrived after their still returned. */
static void give_back_late_sets(void)
{
    (void)g360_service(0);
}

int g360_service(uint32_t wait_ms)
{
    uint64_t start;
    uint64_t now;
    int given = 0;
    if (!session.open) return -G360_E_STATE;
    if (monotonic_ns(&start) != 0) return -G360_E_CLOCK;
    for (;;) {
        const queued_set *entry;
        while (next_queued_set(&entry)) {
            g360_still scratch;
            memset(&scratch, 0, sizeof scratch);
            g360_log("late image set: giving its buffers back without copying");
            (void)process_set(entry, 0, &scratch, 0, 0, 0);
            pop_queued_set();
            ++given;
        }
        if (monotonic_ns(&now) != 0 || now < start) return -G360_E_CLOCK;
        if (now - start >= (uint64_t)wait_ms * UINT64_C(1000000)) return given;
        if (sleep_poll() != 0) return -G360_E_CLOCK;
    }
}

static int arm_frame(const g360_raw_layout *in, g360_frame *frame, int *armed);

static int capture_shot(const g360_capture_options *options, g360_shot *shot,
                        g360_frame *frame, int *armed)
{
    uint64_t set_limit = UINT64_C(15000) * UINT64_C(1000000);
    uint64_t ready_limit = UINT64_C(10000) * UINT64_C(1000000);
    uint64_t extra_wait = 0;
    uint64_t no_set_wait;
    uint32_t max_sets = 1;
    uint32_t capture_type = STILL_CAPTURE_TYPE;
    uint32_t words[4] = {0u, 0u, 0u, 0u};
    uint32_t raw_count = 0;
    g360_raw_layout raw_layout[G360_MAX_RAW];
    uint64_t start;
    uint64_t now = 0;
    uint64_t ready_ns = 0;
    uint64_t first_set_ns = 0;
    uint64_t last_set_ns = 0;
    uint32_t free_low;
    uint32_t free_high;
    uint32_t received = 0;
    int status;
    int result = G360_OK;
    uint32_t k;

    if (shot == 0) return G360_E_ARGUMENT;
    memset(shot, 0, sizeof *shot);
    if (preview.running) {
        g360_log("capture: refused, a preview is running (g360_preview_stop first)");
        return G360_E_STATE;
    }
    if (!session.open || session.latched) return G360_E_STATE;
    if (options != 0) {
        if (options->skip_jpeg_copy && !options->frame) return G360_E_ARGUMENT;
        if (options->set_timeout_ms != 0)
            set_limit = (uint64_t)options->set_timeout_ms * UINT64_C(1000000);
        if (options->ready_timeout_ms != 0)
            ready_limit = (uint64_t)options->ready_timeout_ms * UINT64_C(1000000);
        if (options->max_sets != 0)
            max_sets = options->max_sets < G360_MAX_SETS ? options->max_sets : G360_MAX_SETS;
        extra_wait = (uint64_t)options->extra_set_wait_ms * UINT64_C(1000000);
        if (options->capture_type != 0) capture_type = options->capture_type;
        memcpy(words, options->bracket_words, sizeof words);
        raw_count = options->raw;
    }
    /* The library's converter accepts mm types 1..0x1c except 0x17. */
    if (capture_type < 1u || capture_type > 0x1cu || capture_type == 0x17u) return G360_E_ARGUMENT;
    no_set_wait = extra_wait > UINT64_C(3000000000) ? extra_wait : UINT64_C(3000000000);
    if (raw_count > G360_MAX_RAW) return G360_E_ARGUMENT;
    for (k = 0; k < G360_MAX_RAW; ++k) {
        size_t raw_size;
        raw_layout_defaults(options ? &options->raw_layout[k] : 0, k, &raw_layout[k]);
        if (k < raw_count &&
            (!g360_raw_extent(raw_layout[k].width, raw_layout[k].height, raw_layout[k].stride,
                               raw_layout[k].bits, G360_MAX_IMAGE_BYTES, &raw_size) ||
             raw_size > UINT32_MAX - raw_layout[k].physical)) return G360_E_ARGUMENT;
    }

    give_back_late_sets();
    if (options != 0 && options->frame != 0) {
        status = arm_frame(&options->frame_layout, frame, armed);
        if (status != G360_OK) return status;
    }
    shot->sequence = ++session.sequence;
    __sync_lock_test_and_set(&notify_mask, 0u);
    if (monotonic_ns(&start) != 0) return G360_E_CLOCK;
    free_low = (uint32_t)session.free_bytes;
    free_high = (uint32_t)(session.free_bytes >> 32);
    log_values("capture: still request (sequence, type, remaining, max sets)", shot->sequence,
               capture_type, session.remaining_shots, max_sets);
    if (words[0] | words[1] | words[2] | words[3]) {
        log_values("capture: request words 11-14", words[0], words[1], words[2], words[3]);
    }
    status = session.capture_fn(session.handle, 0u, capture_type, 1u, 1u, 0u,
                                session.remaining_shots, 0u, free_low, free_high,
                                words[0], words[1], words[2], words[3], 0u, 0u, 0u);
    log_values("capture: mm_camera_capture_still returned", (uint32_t)status, 0, 0, 0);
    if (status != 0) {
        /* Indeterminate: the still may or may not be in flight. No retry. */
        session.latched = 1;
        return G360_E_SEND;
    }

    for (;;) {
        const queued_set *entry;
        while (next_queued_set(&entry)) {
            g360_still scratch;
            int copy = shot->set_count < max_sets;
            g360_still *target = copy ? &shot->sets[shot->set_count] : &scratch;
            memset(&scratch, 0, sizeof scratch);
            if (entry->arrived_ns != 0 && entry->arrived_ns < start) {
                /* A late set of the previous still, queued just before this
                   request: give it back, but it is not this still's. */
                g360_log("late image set: giving its buffers back without copying");
                (void)process_set(entry, 0, &scratch, 0, 0, 0);
                pop_queued_set();
                continue;
            }
            target->sequence = shot->sequence;
            if (entry->arrived_ns >= start) target->set_after_ns = entry->arrived_ns - start;
            status = process_set(entry, copy, target, raw_layout,
                                 (shot->set_count == 0 && copy) ? raw_count : 0u,
                                 options != 0 && options->skip_jpeg_copy != 0);
            pop_queued_set();
            ++received;
            if (copy) {
                ++shot->set_count;
            } else {
                ++shot->sets_not_copied;
            }
            if (monotonic_ns(&now) != 0) now = 0;
            if (first_set_ns == 0) first_set_ns = now;
            last_set_ns = now;
            if (status == G360_E_SET) {
                session.latched = 1;
                shot->notify_mask = __sync_fetch_and_add(&notify_mask, 0);
                return G360_E_SET;
            }
            if (status != G360_OK && result == G360_OK) result = status;
        }
        if (monotonic_ns(&now) != 0 || now < start) {
            session.latched = 1;
            return G360_E_CLOCK;
        }
        if (ready_ns == 0 && capture_is_ready()) ready_ns = now;
        if (ready_ns != 0 && received >= max_sets) break;
        if (ready_ns != 0 && received >= 1 &&
            now - (last_set_ns > ready_ns ? last_set_ns : ready_ns) >= extra_wait) {
            break;
        }
        if (ready_ns != 0 && received == 0 && now - ready_ns >= no_set_wait) {
            g360_log("capture: capture-ready without an image set; the camera dropped the still; session latched");
            shot->notify_mask = __sync_fetch_and_add(&notify_mask, 0);
            session.latched = 1;
            return G360_E_NO_SET;
        }
        if (received == 0 && now - start >= set_limit) {
            g360_log("capture: no image set; session latched");
            shot->notify_mask = __sync_fetch_and_add(&notify_mask, 0);
            session.latched = 1;
            return G360_E_TIMEOUT;
        }
        if (received >= 1 && ready_ns == 0 && now - first_set_ns >= ready_limit) {
            g360_log("capture: no capture-ready; session latched");
            shot->notify_mask = __sync_fetch_and_add(&notify_mask, 0);
            session.latched = 1;
            return G360_E_TIMEOUT;
        }
        if (sleep_poll() != 0) {
            session.latched = 1;
            return G360_E_CLOCK;
        }
    }
    shot->notify_mask = __sync_fetch_and_add(&notify_mask, 0);
    shot->ready_after_ns = ready_ns - start;
    for (k = 0; k < shot->set_count; ++k) {
        shot->sets[k].notify_mask = shot->notify_mask;
        shot->sets[k].ready_after_ns = shot->ready_after_ns;
    }
    log_values("capture: done (sequence, result, sets copied, sets not copied)", shot->sequence,
               (uint32_t)result, shot->set_count, shot->sets_not_copied);
    return result;
}

/*
 * Prepares the full-frame copy for the next still: locates the frame in
 * this process's /dev/mem mapping and allocates the buffer, so that the
 * expansion callback only copies. Returns G360_OK (also when the frame
 * cannot be located or allocated: frame->image.flags then say why, and the
 * still goes ahead) or G360_E_ARGUMENT / G360_E_STATE.
 */
static int arm_frame(const g360_raw_layout *in, g360_frame *frame, int *armed)
{
    g360_raw_layout layout;
    uintptr_t source = 0;
    size_t size;

    *armed = 0;
    memset(frame, 0, sizeof *frame);
    if (!session.frame_pause || frame_job.stuck) return G360_E_STATE;
    layout.physical = in && in->physical ? in->physical : FRAME_DEFAULT_PHYSICAL;
    layout.width = in && in->width ? in->width : FRAME_DEFAULT_WIDTH;
    layout.height = in && in->height ? in->height : FRAME_DEFAULT_HEIGHT;
    layout.stride = in && in->stride ? in->stride : FRAME_DEFAULT_STRIDE;
    layout.bits = in && in->bits ? in->bits : FRAME_DEFAULT_BITS;
    if (!g360_raw_extent(layout.width, layout.height, layout.stride, layout.bits,
                          G360_MAX_IMAGE_BYTES, &size) ||
        size > UINT32_MAX - layout.physical) return G360_E_ARGUMENT;
    frame->physical = layout.physical;
    frame->image.width = layout.width;
    frame->image.height = layout.height;
    frame->image.stride_x = layout.stride;
    frame->image.stride_y = layout.bits;
    if (read_maps(maps_main, sizeof maps_main) != 0 ||
        !g360_maps_physical(maps_main, (uintptr_t)layout.physical, size, &source)) {
        log_values("frame: physical range not in a /dev/mem mapping; not armed", layout.physical,
                   (uint32_t)size, 0, 0);
        frame->image.flags = G360_IMAGE_UNMAPPED;
        return G360_OK;
    }
    frame_job.buffer = malloc(size);
    if (frame_job.buffer == 0) {
        frame->image.flags = G360_IMAGE_NO_MEMORY;
        return G360_OK;
    }
    frame_job.source = source;
    frame_job.size = size;
    frame_job.physical = layout.physical;
    frame_job.flags = 0;
    frame_job.hold_us = 0;
    frame_job.mode = 0;
    frame->image.camera_address = (uint32_t)source;
    log_values("frame: armed (physical, virtual, size)", layout.physical, (uint32_t)source,
               (uint32_t)size, 0);
    __sync_synchronize();
    frame_job.state = FRAME_ARMED;
    *armed = 1;
    return G360_OK;
}

/* Takes the frame copy's result after the still (any outcome). */
static void collect_frame(g360_frame *frame)
{
    int waited;
    if (__sync_bool_compare_and_swap(&frame_job.state, FRAME_ARMED, FRAME_IDLE)) {
        g360_log("frame: no valid orientation expansion; not copied");
        free(frame_job.buffer);
        frame_job.buffer = 0;
        frame->image.flags |= G360_IMAGE_NO_PAUSE;
        return;
    }
    /* The callback took it: wait for it to finish (it normally has, since
       the image set follows the expansion on the same thread). */
    for (waited = 0; waited < 1000 &&
                     __sync_fetch_and_add(&frame_job.state, 0) != FRAME_DONE; ++waited) {
        (void)sleep_poll();
    }
    if (__sync_fetch_and_add(&frame_job.state, 0) != FRAME_DONE) {
        g360_log("frame: copy still running after 10 s; buffer abandoned, no further frames");
        frame_job.stuck = 1;
        frame->image.flags |= G360_IMAGE_NO_PAUSE;
        return;
    }
    __sync_synchronize();
    frame->image.data = frame_job.buffer;
    frame->image.flags = frame_job.flags;
    frame->hold_us = frame_job.hold_us;
    frame->mode = frame_job.mode;
    frame_job.buffer = 0;
    if (frame->image.flags & G360_IMAGE_COPIED) {
        frame->image.size = frame_job.size;
        if (samples_fit(frame->image.data, frame->image.width, frame->image.height,
                        frame->image.stride_x, frame->image.stride_y)) {
            frame->image.flags |= G360_IMAGE_RAW_VALUES_OK;
        }
    } else {
        free(frame->image.data);
        frame->image.data = 0;
    }
    __sync_lock_test_and_set(&frame_job.state, FRAME_IDLE);
}

int g360_capture_shot(const g360_capture_options *options, g360_shot *shot)
{
    g360_frame frame;
    int armed = 0;
    int status;
    uint32_t flags;

    if (shot == 0) return G360_E_ARGUMENT;
    memset(&frame, 0, sizeof frame);
    status = capture_shot(options, shot, &frame, &armed);
    if (options == 0 || options->frame == 0) return status;
    if (armed) collect_frame(&frame);
    shot->frame = frame;
    flags = frame.image.flags;
    log_values("frame: result (physical, flags, hold us, mode)", frame.physical, flags,
               frame.hold_us, frame.mode);
    if (status == G360_OK &&
        ((flags & (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK)) !=
             (G360_IMAGE_COPIED | G360_IMAGE_RAW_VALUES_OK) ||
         (flags & G360_IMAGE_RAW_CHANGED))) {
        status = G360_E_IMAGE;
    }
    return status;
}

int g360_capture(const g360_capture_options *options, g360_still *still)
{
    g360_capture_options one;
    g360_shot shot;
    int status;

    if (still == 0) return G360_E_ARGUMENT;
    memset(still, 0, sizeof *still);
    memset(&one, 0, sizeof one);
    if (options != 0) one = *options;
    one.max_sets = 1;
    status = g360_capture_shot(&one, &shot);
    *still = shot.sets[0];
    still->sequence = shot.sequence;
    still->notify_mask = shot.notify_mask;
    still->ready_after_ns = shot.ready_after_ns;
    still->frame = shot.frame;
    memset(&shot.sets[0], 0, sizeof shot.sets[0]);
    memset(&shot.frame, 0, sizeof shot.frame);
    g360_shot_free(&shot);
    return status;
}

void g360_shot_free(g360_shot *shot)
{
    uint32_t k;
    if (shot == 0) return;
    for (k = 0; k < G360_MAX_SETS; ++k) g360_still_free(&shot->sets[k]);
    free(shot->frame.image.data);
    memset(shot, 0, sizeof *shot);
}

static int set_user_attribute(int attribute, uint32_t value, const char *what)
{
    int status;
    if (!session.open) return G360_E_STATE;
    status = session.set_usr_fn(session.handle, attribute, value);
    log_values(what, (uint32_t)attribute, value, (uint32_t)status, 0);
    return status == 0 ? G360_OK : G360_E_SEND;
}

int g360_set_user_attribute(int attribute, uint32_t value)
{
    /* libcapture accepts attribute ids 0..90. */
    if (attribute < 0 || attribute > 90) return G360_E_ARGUMENT;
    return set_user_attribute(attribute, value,
                              "user attribute: mm_camera_set_usr_attributes (attribute, value, result)");
}

int g360_set_variable(uint32_t id, int32_t value)
{
    int status;
    /* libcapture accepts variable ids 0x100..0x12b. */
    if (id < 0x100u || id > 0x12bu) return G360_E_ARGUMENT;
    if (!session.open) return G360_E_STATE;
    status = session.set_var_fn(session.handle, id, value, 0u);
    log_values("variable: mm_camera_set_var_attributes (id, value, result)", id, (uint32_t)value,
               (uint32_t)status, 0);
    return status == 0 ? G360_OK : G360_E_SEND;
}

int g360_set_manual_exposure(int32_t tv, uint32_t iso)
{
    int status = g360_set_user_attribute(G360_ATTR_DIALMODE, 3u);
    if (status == G360_OK) status = g360_set_user_attribute(G360_ATTR_SHOOTINGMODE, 0x10003u);
    if (status == G360_OK) status = g360_set_user_attribute(G360_ATTR_ISO, iso);
    if (status == G360_OK) status = g360_set_variable(G360_VAR_TV, tv);
    return status;
}

int g360_set_auto_exposure(void)
{
    int status = g360_set_user_attribute(G360_ATTR_DIALMODE, 1u);
    if (status == G360_OK) status = g360_set_user_attribute(G360_ATTR_SHOOTINGMODE, 0x10000u);
    if (status == G360_OK) status = g360_set_user_attribute(G360_ATTR_ISO, 0u);
    return status;
}

int g360_set_drive_mode(uint32_t drive)
{
    if (drive < G360_DRIVE_SINGLE || drive > 0x80009u) return G360_E_ARGUMENT;
    return set_user_attribute(8, drive, "drive mode: mm_camera_set_usr_attributes (attribute, value, result)");
}

int g360_set_quickview(uint32_t value)
{
    if (value < G360_QUICKVIEW_OFF || value > G360_QUICKVIEW_HOLDZOOM) return G360_E_ARGUMENT;
    return set_user_attribute(G360_ATTR_QUICKVIEWTIME, value,
                              "quick view: mm_camera_set_usr_attributes (attribute, value, result)");
}

int g360_set_image_quality(uint32_t quality)
{
    if (quality < G360_QUALITY_RAW || quality > G360_QUALITY_SRAW) return G360_E_ARGUMENT;
    return set_user_attribute(3, quality, "image quality: mm_camera_set_usr_attributes (attribute, value, result)");
}

int g360_save_image(const g360_image *image, const char *path)
{
    int fd;
    size_t done = 0;
    if (image == 0 || path == 0 || image->data == 0 || image->size == 0) return G360_E_ARGUMENT;
    fd = open(path, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC, 0644);
    if (fd < 0) return G360_E_IO;
    while (done < image->size) {
        ptrdiff_t n = write(fd, image->data + done, image->size - done);
        if (n <= 0) {
            (void)close(fd);
            return G360_E_IO;
        }
        done += (size_t)n;
    }
    if (fsync(fd) != 0) {
        (void)close(fd);
        return G360_E_IO;
    }
    return close(fd) == 0 ? G360_OK : G360_E_IO;
}

void g360_still_free(g360_still *still)
{
    int i;
    if (still == 0) return;
    for (i = 0; i < G360_IMAGE_COUNT; ++i) free(still->images[i].data);
    for (i = 0; i < (int)G360_MAX_RAW; ++i) free(still->raw[i].data);
    free(still->frame.image.data);
    free(still->maker_note.data);
    memset(still, 0, sizeof *still);
}

/* The camera's movie sizes (RTOS StartMovie, fact
   session.rtos-startmovie-sizes-rates-and-stop-20261107) and frame rates
   with their float bits. */
static const uint32_t movie_sizes[][2] = {
    {4096u, 2048u}, {2880u, 1440u}, {2560u, 1280u}, {1920u, 960u},
    {1440u, 720u}, {1920u, 1080u}, {1280u, 720u}, {1280u, 640u},
};
static const uint32_t movie_rates[][2] = {
    {125u, 0x41480000u}, {150u, 0x41700000u}, {240u, 0x41c00000u}, {250u, 0x41c80000u},
    {300u, 0x41f00000u}, {500u, 0x42480000u}, {600u, 0x42700000u}, {1000u, 0x42c80000u},
    {1200u, 0x42f00000u},
};

/* Waits up to wait_ms for bit in movie_mask; 1 when it came. */
static int wait_movie_bit(uint32_t bit, uint32_t wait_ms)
{
    uint64_t start;
    uint64_t now;
    if (monotonic_ns(&start) != 0) return 0;
    for (;;) {
        if (__sync_fetch_and_add(&movie_mask, 0) & bit) return 1;
        if (monotonic_ns(&now) != 0 || now < start ||
            now - start >= (uint64_t)wait_ms * UINT64_C(1000000)) {
            return (__sync_fetch_and_add(&movie_mask, 0) & bit) != 0;
        }
        if (sleep_poll() != 0) return 0;
    }
}

/* Subscribes the movie notifications, the image topics and (with_set) the
   image set, once per process. */
static int preview_subscribe(reg_topic_fn subscribe_image_fn, int with_set)
{
    static const uint32_t images[3] = {
        G360_PREVIEW_TOPIC_THUMB, G360_PREVIEW_TOPIC_DISPLAY, G360_PREVIEW_TOPIC_MOVIE};
    uint32_t k;
    if (!preview.subscribed) {
        if (session.subscribe_notify_fn(session.slot, MOVIE_NOTIFY_START) != 0 ||
            session.subscribe_notify_fn(session.slot, MOVIE_NOTIFY_STOP) != 0) {
            g360_log("preview: movie notification subscription failed");
            return G360_E_INIT;
        }
        for (k = 0; k < 3u; ++k) {
            if (subscribe_image_fn(session.slot, images[k]) != 0) {
                log_values("preview: image subscription failed", images[k], 0, 0, 0);
                return G360_E_INIT;
            }
        }
        preview.subscribed = 1;
    }
    if (with_set && !preview.set_subscribed) {
        if (session.subscribe_set_fn(session.slot, G360_PREVIEW_TOPIC_SET) != 0) {
            g360_log("preview: image-set subscription failed");
            return G360_E_INIT;
        }
        preview.set_subscribed = 1;
    }
    return G360_OK;
}

int g360_preview_start(const g360_preview_options *options)
{
    uint32_t width = 1280u;
    uint32_t height = 640u;
    uint32_t fps_x10 = 150u;
    uint32_t wait_ms = 3000u;
    uint32_t rate_bits = 0;
    uint32_t payload[7];
    reg_topic_fn subscribe_image_fn = 0;
    get_capture_data_fn data_fn = 0;
    uint32_t sensor = 0;
    int size = 4;
    int found = 0;
    int status;
    size_t k;

    if (options == 0 || options->callback == 0) return G360_E_ARGUMENT;
    if (!session.open || session.latched || preview.running) return G360_E_STATE;
    if (options->width != 0 || options->height != 0) {
        width = options->width;
        height = options->height;
    }
    if (options->fps_x10 != 0) fps_x10 = options->fps_x10;
    if (options->wait_ms != 0) wait_ms = options->wait_ms;
    for (k = 0; k < sizeof movie_sizes / sizeof movie_sizes[0]; ++k) {
        if (movie_sizes[k][0] == width && movie_sizes[k][1] == height) found = 1;
    }
    for (k = 0; k < sizeof movie_rates / sizeof movie_rates[0]; ++k) {
        if (movie_rates[k][0] == fps_x10) rate_bits = movie_rates[k][1];
    }
    if (!found || rate_bits == 0) return G360_E_ARGUMENT;
    if (preview.record_fn == 0 &&
        (session.sdk == 0 ||
         resolve(session.sdk, "RecordMovie", &preview.record_fn, sizeof preview.record_fn) != 0)) {
        g360_log("preview: RecordMovie missing");
        return G360_E_LOAD;
    }
    if (session.sdk == 0 ||
        resolve(session.sdk, "RegImageCallbackId", &subscribe_image_fn,
                sizeof subscribe_image_fn) != 0) {
        g360_log("preview: RegImageCallbackId missing");
        return G360_E_LOAD;
    }
    status = preview_subscribe(subscribe_image_fn, options->with_set != 0);
    if (status != G360_OK) return status;

    preview.callback = options->callback;
    preview.context = options->context;
    __sync_fetch_and_and(&movie_mask, ~3u);
    __sync_synchronize();
    preview.running = 1;
    /* The stock layout (libmmfmodules cMmfCamera::CameraControl): rec size,
       rate, type 0 (a stream, not a file), rvf size (unused), rate. */
    payload[0] = width;
    payload[1] = height;
    payload[2] = rate_bits;
    payload[3] = 0;
    payload[4] = 0;
    payload[5] = 0;
    payload[6] = rate_bits;
    log_values("preview: MV_START (width, height, rate bits, wait ms)", width, height, rate_bits,
               wait_ms);
    status = preview.record_fn(MOVIE_START_ID, (unsigned char *)payload, (uint32_t)sizeof payload);
    log_values("preview: RecordMovie start returned", (uint32_t)status, 0, 0, 0);
    if (status != 0) {
        preview.running = 0;
        return G360_E_SEND;
    }
    if (!wait_movie_bit(1u, wait_ms)) {
        g360_log("preview: no VIDEOSTART_DONE; sending MV_STOP");
        (void)g360_preview_stop(wait_ms);
        return G360_E_TIMEOUT;
    }
    if (resolve(session.sdk, "GetCaptureData", &data_fn, sizeof data_fn) == 0 &&
        data_fn(SENSOR_RATE_DATA_ID, &sensor, &size) == 0) {
        preview.sensor_rate_bits = sensor;
    }
    log_values("preview: VIDEOSTART_DONE (sensor rate bits)", preview.sensor_rate_bits, 0, 0, 0);
    return G360_OK;
}

int g360_preview_stop(uint32_t wait_ms)
{
    int status;
    if (!preview.running) return G360_OK;
    if (wait_ms == 0) wait_ms = 3000u;
    __sync_fetch_and_and(&movie_mask, ~2u);
    g360_log("preview: MV_STOP");
    status = preview.record_fn(MOVIE_STOP_ID, 0, 0);
    log_values("preview: RecordMovie stop returned", (uint32_t)status, 0, 0, 0);
    if (!wait_movie_bit(2u, wait_ms)) {
        g360_log("preview: no VIDEOSTOP_DONE");
        preview.running = 0;
        return G360_E_TIMEOUT;
    }
    preview.running = 0;
    g360_log("preview: VIDEOSTOP_DONE");
    return G360_OK;
}

int g360_preview_running(void)
{
    return preview.running != 0;
}

uint32_t g360_preview_count(uint32_t topic)
{
    if (topic == G360_PREVIEW_TOPIC_SET) return __sync_fetch_and_add(&preview_counts[0], 0);
    if (topic >= G360_PREVIEW_TOPIC_THUMB && topic <= G360_PREVIEW_TOPIC_MOVIE) {
        return __sync_fetch_and_add(&preview_counts[topic & 3u], 0);
    }
    return 0;
}

uint32_t g360_preview_sensor_rate_bits(void)
{
    return preview.sensor_rate_bits;
}

void g360_set_free_bytes(uint64_t free_bytes)
{
    session.free_bytes = free_bytes;
}

void g360_close(void)
{
    if (preview.running) (void)g360_preview_stop(0);
    if (session.open && !session.latched) give_back_late_sets();
    __sync_lock_test_and_set(&accepting_sets, 0);
    g360_log("close: the capture library stays loaded until process exit");
    /* The APEX singleton keeps pointing into user_spec_copy, so it is kept. */
    session.open = 0;
    session.latched = 1;
    if (session.log_fd >= 0) {
        (void)close(session.log_fd);
        session.log_fd = -1;
    }
}

const char *g360_status_text(int status)
{
    switch (status) {
    case G360_OK: return "ok";
    case G360_E_ARGUMENT: return "bad argument";
    case G360_E_STATE: return "wrong session state";
    case G360_E_LOAD: return "camera library or symbol missing";
    case G360_E_GYRO: return "gyro history empty";
    case G360_E_APEX: return "APEX check refused";
    case G360_E_INIT: return "capture library initialization failed";
    case G360_E_BRIDGE: return "RTOS bridge not alive";
    case G360_E_CLOCK: return "clock failure";
    case G360_E_SEND: return "still request returned nonzero";
    case G360_E_TIMEOUT: return "timed out";
    case G360_E_SET: return "image set unreadable";
    case G360_E_MEMORY: return "out of memory";
    case G360_E_IO: return "file write failed";
    case G360_E_IMAGE: return "image missing or invalid";
    case G360_E_RELEASE: return "buffer give-back refused";
    case G360_E_NO_SET: return "capture-ready but no image set (still dropped by the camera)";
    default: return "unknown status";
    }
}
