/*
 * A stub of the camera's capture library, for the host tests.
 *
 * It exports the same C names the real library exports, and behaves the
 * way the real library was observed to behave: creating a client realizes
 * it, the settings are recorded, and a still request delivers the still's
 * own set to the registered callback.
 *
 * The still the stub delivers is the real shape: the image-set route, with
 * an encoded-set constituent whose first image is the raw candidate and
 * whose main image is a JPEG stream.  Before it, the stub delivers an
 * image-route frame - the liveview - so that the tests can check that a
 * preview never answers a still.
 */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>



#include "capture_session_library.h"

/* The image the stub delivers: the block the image route really delivers,
 * and the bytes it points at. */
static uint8_t sample_bytes[64];
static capture_library_rect_block sample_block;

/* The still the stub delivers: the raw candidate and the encoded still. */
static uint8_t raw_bytes[64];
static uint8_t jpeg_bytes[4] = {0xffu, 0xd8u, 0xffu, 0xd9u};

/*
 * How many sets the stub has built.
 *
 * Every set the stub builds carries a **different** pattern in its first
 * image, so that a test can tell which delivery a copy came from: the first
 * delivery's bytes are i+1, the second's are i+1+0x20, and so on.  A test
 * that says "the first result is the one that was kept" needs the two
 * deliveries to differ, or the claim is not tested.
 */
static unsigned int still_build_count;
static uint8_t constituent[CAPTURE_LIBRARY_SET_CONSTITUENT_SIZE];
static capture_library_image_set still_set;

/*
 * The image object the image route hands over.
 *
 * The real library hands over its own C++ image object and exports the
 * accessors that describe such an object.  This stub hands over a small
 * structure and exports the same accessor names, so that the backend's
 * reading of the image - its geometry, and the address and the size of the
 * buffer that holds the samples - can be tested with no camera.
 *
 * The stub is built twice: once with these accessors, and once without them,
 * so that the backend's behaviour when the library does not export them is
 * tested too.
 */
typedef struct stub_buffer {
    uint32_t address[4];
    uint32_t size;
} stub_buffer;

typedef struct stub_image {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t raw_pixel_bit;
    uint32_t format;
    stub_buffer *buffer;
} stub_image;

static stub_image the_stub_image;
static stub_buffer the_stub_buffer;

#ifndef STUB_NO_IMAGE_ACCESSORS

/*
 * The exported name, in the two spellings the two platforms need.
 *
 * The camera's library is a Linux one: it exports the mangled name with one
 * leading underscore, and dlsym finds it exactly.  The host test runs on
 * macOS, whose dynamic loader adds an underscore of its own to the name it
 * is given, so the raw name it finds is the one with two.  Both spellings
 * are exported here, and both point at the same body, so the backend can ask
 * for the name the camera's library exports and the host test can still find
 * it.
 */
#define STUB_EXPORTED(name, body)                                                \
    uint32_t stub_linux_##name(void *p) __asm__("_" #name);                      \
    uint32_t stub_host_##name(void *p) __asm__("__" #name);                       \
    uint32_t stub_linux_##name(void *p)                                          \
    {                                                                            \
        return body(p);                                                          \
    }                                                                            \
    uint32_t stub_host_##name(void *p)                                           \
    {                                                                            \
        return body(p);                                                          \
    }

static uint32_t stub_body_get_width(void *image)
{
    return ((const stub_image *)image)->width;
}

static uint32_t stub_body_get_height(void *image)
{
    return ((const stub_image *)image)->height;
}

static uint32_t stub_body_get_raw_stride(void *image)
{
    return ((const stub_image *)image)->stride;
}

static uint32_t stub_body_get_raw_pixel_bit(void *image)
{
    return ((const stub_image *)image)->raw_pixel_bit;
}

static uint32_t stub_body_get_image_format(void *image)
{
    return ((const stub_image *)image)->format;
}

static void *stub_body_get_buffer(void *image)
{
    return ((stub_image *)image)->buffer;
}

static void stub_body_get_virt_address(void *buffer, void *address)
{
    /* The mapped address is sixteen bytes: four 32-bit words. */
    memcpy(address, ((const stub_buffer *)buffer)->address, 16u);
}

static uint32_t stub_body_get_size(void *buffer)
{
    return ((const stub_buffer *)buffer)->size;
}

STUB_EXPORTED(ZN9CCapImage8GetWidthEv, stub_body_get_width)
STUB_EXPORTED(ZN9CCapImage9GetHeightEv, stub_body_get_height)
STUB_EXPORTED(ZN9CCapImage12GetRawStrideEv, stub_body_get_raw_stride)
STUB_EXPORTED(ZN9CCapImage14GetRawPixelBitEv, stub_body_get_raw_pixel_bit)
STUB_EXPORTED(ZN9CCapImage14GetImageFormatEv, stub_body_get_image_format)

/* The two that return a pointer or take one are written out, because their
 * signatures are not a plain word. */
void *stub_linux_get_buffer(void *image) __asm__("_ZN9CCapImage9GetBufferEv");
void *stub_host_get_buffer(void *image) __asm__("__ZN9CCapImage9GetBufferEv");
void *stub_linux_get_buffer(void *image) { return stub_body_get_buffer(image); }
void *stub_host_get_buffer(void *image) { return stub_body_get_buffer(image); }

void stub_linux_get_virt_address(void *buffer, void *address)
    __asm__("_ZN10CCapBuffer14getVirtAddressEPNS_9uCAP_ADDRE");
void stub_host_get_virt_address(void *buffer, void *address)
    __asm__("__ZN10CCapBuffer14getVirtAddressEPNS_9uCAP_ADDRE");
void stub_linux_get_virt_address(void *buffer, void *address)
{
    stub_body_get_virt_address(buffer, address);
}
void stub_host_get_virt_address(void *buffer, void *address)
{
    stub_body_get_virt_address(buffer, address);
}

uint32_t stub_linux_get_size(void *buffer) __asm__("_ZN10CCapBuffer7getSizeEv");
uint32_t stub_host_get_size(void *buffer) __asm__("__ZN10CCapBuffer7getSizeEv");
uint32_t stub_linux_get_size(void *buffer) { return stub_body_get_size(buffer); }
uint32_t stub_host_get_size(void *buffer) { return stub_body_get_size(buffer); }

#endif /* STUB_NO_IMAGE_ACCESSORS */

static capture_library_image_callback image_callback;
static capture_library_image_set_callback image_set_callback;
static capture_library_notify_callback notify_callback;

static int last_usr_id = -1;
static uint32_t last_usr_value;
static int last_var_id = -1;
static uint32_t last_var_value;
/*
 * The variable writes the facade refused.
 *
 * The facade accepts the camera's own variable ids, 0x100 to 0x12b, and
 * refuses anything else.  The stub refuses the same ids, and it counts the
 * refusals, so that a test can say that an id outside the range was offered
 * and not taken.
 */
static int var_refused_count;
/*
 * The user-attribute write that carried the image quality: the id the
 * library was given and the value it was given with.
 */
static int quality_write_count;
static int last_quality_api_id = -1;
static uint32_t last_quality_api_value;
/*
 * The name the client registered under, and its length.
 */
static char last_client_name[16];
static int last_client_name_length = -1;
static uint32_t last_command;
static uint32_t last_cap;
static uint32_t last_max;
static uint32_t last_remaining;
static uint64_t last_free;
static int still_count;
static int client_count;
static int create_count;
static int destroy_count;
static int pref_power_on;
static capture_library_notify_callback notification_callback;
static int notification_id_count;
static int image_topics_registered;
static uint32_t last_image_topic;
static int image_set_topics_registered;
static uint32_t last_image_set_topic;
static int last_release_type = -1;
static uint32_t last_release_address;
static int release_count;
/* The set type the stub writes into the still's set. */
static uint32_t still_set_type = CAPTURE_LIBRARY_SET_TYPE_RAW_THREE;
/* Whether the stub delivers the still's set at all. */
static int deliver_still_set = 1;
/*
 * Whether the stub delivers a set on an *unrelated* topic as well.
 *
 * The camera publishes several sets for one still - the YCC set, the MPO
 * set, the UD-YCC set, the expansion set - and only the encoded still's
 * payload is an image set whose first word is the basic set.  The others
 * carry another payload shape, so the stub hands over a deliberately
 * invalid pointer for them: a backend that reads such a payload is doing
 * a wild read, and the pointer makes that fault instead of returning a
 * plausible answer.
 */
static int deliver_unrelated_set = 0;
/* The capture mode the host asked for. */
static int last_capture_mode = -1;
/* The size the first image's descriptor states; zero means it states none. */
static uint32_t first_image_size = 64u;
/* Whether the main image is a JPEG stream. */
static int main_image_is_jpeg = 1;
/* The bytes the main image points at when it is not a JPEG. */
static uint8_t not_jpeg_bytes[4] = {0x00u, 0x01u, 0x02u, 0x03u};
/*
 * Whether the stub refuses to create a client, or to register one.
 *
 * The camera's library can refuse either, and the adapter's open has to
 * report which step refused - so a test has to be able to make each step
 * refuse.
 */
static int fail_create;
static int fail_register;
/*
 * Whether the still's send reports a native send error.
 *
 * The native send status can be -1 while the command is still queued, and a
 * later wake can transmit the queued work.  The stub reproduces that: the
 * call fails, and the still's set is delivered afterwards, by the test, as
 * if the queued work went out later.
 */
static int still_send_fails;
/*
 * The calls the library was asked for.
 *
 * A test that says "no further call reaches the library" needs to be able to
 * count them.
 */
static int setting_write_count;
static int capture_mode_write_count;
/*
 * The status the setting facades report.
 *
 * Zero is the local Send return, not proof of peer receipt or effect.  -2 is a handle the library does not
 * know and -3 is a value or an id its own converter refuses; both are
 * decided before the send, so neither of them sent anything.  Transport errors are collapsed into -1 by the pinned facade. Unknown
 * nonzero statuses are conservatively unresolved.
 */
static int usr_status_to_report;
static int var_status_to_report;
/* The calls the setting facades were asked for, sent or not. */
static int setting_call_count;
/*
 * Whether the release delivers the still's set again from inside itself.
 *
 * The library may deliver a callback from inside a call into it, and the
 * release of the still's first image is such a call.  A test has to be able
 * to produce that: the delivery is made once, from inside the release, on the
 * thread that made the release.
 */
static int release_reenters_image_set;
/* Whether the re-entrant delivery has already been made. */
static int release_reentered;
/*
 * Whether the release reports a failure.
 *
 * The library's release call is the library's own call, and a nonzero status
 * from it says nothing about what the camera did with the plane.  A test has
 * to be able to produce that status, so that the adapter's own handling of
 * an unresolved release can be tested.
 */
static int release_fails;
/*
 * Whether the release waits for the test to let it go, and whether a release
 * is waiting right now.
 *
 * The release is a call into the library, and the library may deliver a
 * callback from inside it.  A test has to be able to hold that call in
 * flight, so that it can see what the adapter does while the release runs.
 *
 * The gate is a mutex and a condition: the test's thread and the thread
 * that is inside the release hand over to each other through it, and a
 * shared word would be a race rather than a hand-off.
 */
static pthread_mutex_t release_gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t release_gate_changed = PTHREAD_COND_INITIALIZER;
static int release_gate_closed;
static int release_waiting;

/*
 * Whether the still's call waits for the test to let it go, and whether it
 * is waiting right now.
 *
 * The still's call is a call into the library, and the library may deliver
 * a callback from inside it.  A test has to be able to hold that call in
 * flight, so that it can close the host while the call is running.
 */
static pthread_mutex_t capture_gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t capture_gate_changed = PTHREAD_COND_INITIALIZER;
static int capture_gate_closed;
static int capture_waiting;

/*
 * The same for the adapter's other mutating calls: the two setting
 * facades and the capture-mode setter.
 *
 * A test has to be able to hold one of those calls in flight too, so that
 * it can close the host while the call is running and see what the adapter
 * does with the result it gets afterwards.
 */
static pthread_mutex_t mutating_gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mutating_gate_changed = PTHREAD_COND_INITIALIZER;
static int mutating_gate_closed;
static int mutating_waiting;

/*
 * Whether a native call is on the stack right now, and whether a release
 * was made while one was.
 *
 * The library delivers its callbacks from inside its own calls, and the
 * adapter must keep its own calls from overlapping: a release made from
 * inside a mutating call would be a second call into the library at the
 * same time.  A test has to be able to see that, so the stub counts the
 * releases that were made while a call of each kind was running.
 */
static pthread_mutex_t call_stack_lock = PTHREAD_MUTEX_INITIALIZER;
static int capture_call_running;
static int setting_call_running;
static int release_inside_capture;
static int release_inside_setting;

/*
 * Whether a setting delivers the still's set from inside itself.
 *
 * The library may deliver a callback from inside any call into it, and a
 * setting is a call into it.  A test has to be able to produce that: the
 * delivery is made from inside the setting, on the thread that made it.
 */
static int setting_delivers_still;

static void build_still_set(void);

/* Whether the still's call is on the stack right now. */
static void set_capture_call_running(int running)
{
    pthread_mutex_lock(&call_stack_lock);
    capture_call_running = running ? 1 : 0;
    pthread_mutex_unlock(&call_stack_lock);
}

/* Whether a setting's call is on the stack right now. */
static void set_setting_call_running(int running)
{
    pthread_mutex_lock(&call_stack_lock);
    setting_call_running = running ? 1 : 0;
    pthread_mutex_unlock(&call_stack_lock);
}

/*
 * Note a release against the calls that are on the stack.
 *
 * A release made while a mutating call is running is a second call into the
 * library at the same time, and the stub counts it so that a test can say
 * that it did not happen.
 */
static void note_a_release(void)
{
    pthread_mutex_lock(&call_stack_lock);
    if (capture_call_running) {
        release_inside_capture += 1;
    }
    if (setting_call_running) {
        release_inside_setting += 1;
    }
    pthread_mutex_unlock(&call_stack_lock);
}

/*
 * Deliver the still's set now, from inside a call into the library.
 */
static void deliver_the_still_set(void)
{
    if (image_set_callback != 0) {
        build_still_set();
        image_set_callback((int)CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL,
                           &still_set, 0, 0u);
    }
}

/* Whether the still's call is being held at its gate. */
static int capture_gate_is_closed(void)
{
    int closed;

    pthread_mutex_lock(&capture_gate_lock);
    closed = capture_gate_closed;
    pthread_mutex_unlock(&capture_gate_lock);
    return closed;
}

/*
 * Hold the still's call until the test lets it go.
 *
 * The wait is bounded: a test that fails before it opens the gate must not
 * turn into a run that never ends.
 */
static void wait_at_the_capture_gate(void)
{
    struct timespec deadline;

    if (!capture_gate_is_closed()) {
        return;
    }
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 10;
    pthread_mutex_lock(&capture_gate_lock);
    capture_waiting = 1;
    pthread_cond_broadcast(&capture_gate_changed);
    while (capture_gate_closed) {
        if (pthread_cond_timedwait(&capture_gate_changed, &capture_gate_lock,
                                   &deadline) != 0) {
            break;
        }
    }
    capture_waiting = 0;
    pthread_mutex_unlock(&capture_gate_lock);
}

/* Whether a mutating call is being held at its gate. */
static int mutating_gate_is_closed(void)
{
    int closed;

    pthread_mutex_lock(&mutating_gate_lock);
    closed = mutating_gate_closed;
    pthread_mutex_unlock(&mutating_gate_lock);
    return closed;
}

/*
 * Hold a mutating call until the test lets it go.
 *
 * The wait is bounded, as the still's gate is.
 */
static void wait_at_the_mutating_gate(void)
{
    struct timespec deadline;

    if (!mutating_gate_is_closed()) {
        return;
    }
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 10;
    pthread_mutex_lock(&mutating_gate_lock);
    mutating_waiting = 1;
    pthread_cond_broadcast(&mutating_gate_changed);
    while (mutating_gate_closed) {
        if (pthread_cond_timedwait(&mutating_gate_changed,
                                   &mutating_gate_lock, &deadline) != 0) {
            break;
        }
    }
    mutating_waiting = 0;
    pthread_mutex_unlock(&mutating_gate_lock);
}

/* Whether the release is being held. */
static int release_gate_is_closed(void)
{
    int closed;

    pthread_mutex_lock(&release_gate_lock);
    closed = release_gate_closed;
    pthread_mutex_unlock(&release_gate_lock);
    return closed;
}

/*
 * The capture library's own capture-mode setter, under the name it exports.
 * The stub records the mode and refuses one above four, exactly as the
 * library does.
 */
static int stub_body_set_capture_mode_user_data(void *self, unsigned char mode)
{
    (void)self;
    if (mode > 4u) {
        return -1;
    }
    capture_mode_write_count += 1;
    last_capture_mode = (int)mode;
    wait_at_the_mutating_gate();
    return 0;
}

int stub_linux_set_capture_mode_user_data(void *self, unsigned char mode)
    __asm__("_ZN17CAttributeHandler22setCaptureModeUserDataEh");
int stub_host_set_capture_mode_user_data(void *self, unsigned char mode)
    __asm__("__ZN17CAttributeHandler22setCaptureModeUserDataEh");
int stub_linux_set_capture_mode_user_data(void *self, unsigned char mode)
{
    return stub_body_set_capture_mode_user_data(self, mode);
}
int stub_host_set_capture_mode_user_data(void *self, unsigned char mode)
{
    return stub_body_set_capture_mode_user_data(self, mode);
}

int stub_last_capture_mode(void) { return last_capture_mode; }

int mm_camera_create(unsigned int *out_handle)
{
    create_count += 1;
    if (fail_create) {
        return -3;
    }
    if (out_handle == 0) {
        return -3;
    }
    *out_handle = 0x1234u;
    return 0;
}

int mm_camera_destroy(unsigned int handle)
{
    (void)handle;
    destroy_count += 1;
    return 0;
}

int mm_camera_realize(unsigned int handle)
{
    (void)handle;
    return 0;
}

int mm_camera_unrealize(unsigned int handle)
{
    (void)handle;
    return 0;
}

int mm_camera_start(unsigned int handle)
{
    (void)handle;
    return 0;
}

int mm_camera_stop(unsigned int handle)
{
    (void)handle;
    return 0;
}

int mm_camera_set_usr_attributes(unsigned int handle, int id, uint32_t value)
{
    int status = CAPTURE_LIBRARY_STATUS_OK;

    (void)handle;
    setting_call_count += 1;
    set_setting_call_running(1);
    if (id < 0 || id > 90) {
        status = CAPTURE_LIBRARY_STATUS_REFUSED;
    } else if (id == CAPTURE_LIBRARY_SETTING_QUALITY) {
        /*
         * The library's quality takes the tagged values 0x30000 to 0x30007
         * and refuses anything else, and it keeps the value it is given.
         * The stub refuses the same values, so a backend that writes the
         * quality in another form is caught here.
         */
        if (value < CAPTURE_LIBRARY_QUALITY_API_FIRST ||
            value > CAPTURE_LIBRARY_QUALITY_API_LAST) {
            status = CAPTURE_LIBRARY_STATUS_REFUSED;
        } else {
            quality_write_count += 1;
            last_quality_api_id = id;
            last_quality_api_value = value;
        }
    }
    if (status == CAPTURE_LIBRARY_STATUS_OK &&
        usr_status_to_report != CAPTURE_LIBRARY_STATUS_OK) {
        /*
         * The transport failed.  The stub does not take the value, and it
         * does not say that nothing was sent: the status is all the caller
         * gets.
         */
        status = usr_status_to_report;
    }
    if (status == CAPTURE_LIBRARY_STATUS_OK) {
        last_usr_id = id;
        last_usr_value = value;
        setting_write_count += 1;
        if (setting_delivers_still) {
            deliver_the_still_set();
        }
    }
    wait_at_the_mutating_gate();
    set_setting_call_running(0);
    return status;
}

int mm_camera_set_var_attributes(unsigned int handle, int id, uint32_t value)
{
    int status = CAPTURE_LIBRARY_STATUS_OK;

    (void)handle;
    setting_call_count += 1;
    set_setting_call_running(1);
    /*
     * The library's variable facade accepts the camera's own variable ids,
     * 0x100 to 0x12b, and refuses anything outside that range before the
     * write leaves the process.  The stub refuses the same ids, so a
     * backend that writes an index where the facade wants an id is caught
     * here rather than on the camera.
     */
    last_var_id = id;
    last_var_value = value;
    if (id < 0x100 || id > 0x12b) {
        var_refused_count += 1;
        status = CAPTURE_LIBRARY_STATUS_REFUSED;
    }
    if (status == CAPTURE_LIBRARY_STATUS_OK &&
        var_status_to_report != CAPTURE_LIBRARY_STATUS_OK) {
        status = var_status_to_report;
    }
    if (status == CAPTURE_LIBRARY_STATUS_OK) {
        setting_write_count += 1;
        if (setting_delivers_still) {
            deliver_the_still_set();
        }
    }
    wait_at_the_mutating_gate();
    set_setting_call_running(0);
    return status;
}

/* Write a word into the constituent, little endian. */
static void put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xffu);
    destination[1] = (uint8_t)((value >> 8) & 0xffu);
    destination[2] = (uint8_t)((value >> 16) & 0xffu);
    destination[3] = (uint8_t)((value >> 24) & 0xffu);
}

/*
 * Build the still's set: the encoded-set constituent with a first image -
 * the raw candidate - and a main image - the encoded still.
 */
static void build_still_set(void)
{
    size_t i;

    still_build_count += 1u;
    memset(constituent, 0, sizeof(constituent));
    put_u32(constituent + 0x000u, still_set_type);

    /* The first image: the raw candidate.  Its block is the first one, at
     * the constituent's +0x008, and it states its own geometry. */
    put_u32(constituent + 0x008u, 8u);  /* the stride x */
    put_u32(constituent + 0x00cu, 2u);  /* the stride y */
    put_u32(constituent + 0x010u, 4u);  /* the width */
    put_u32(constituent + 0x014u, 2u);  /* the height */
    constituent[0x048u] = 7u;                      /* the address type */
    put_u32(constituent + 0x04cu, 0x00a6000000u); /* the physical word */
    put_u32(constituent + 0x05cu, (uint32_t)(uintptr_t)raw_bytes);
    put_u32(constituent + 0x07cu, first_image_size);

    /* The main image: the encoded still.  Its block is the second one, at
     * the constituent's +0x090. */
    put_u32(constituent + 0x090u, 12u);
    put_u32(constituent + 0x094u, 6u);
    put_u32(constituent + 0x098u, 8u);
    put_u32(constituent + 0x09cu, 4u);
    constituent[0x0d0u] = 7u;
    put_u32(constituent + 0x0d4u, 0x00a6001000u);
    if (main_image_is_jpeg) {
        put_u32(constituent + 0x0e4u, (uint32_t)(uintptr_t)jpeg_bytes);
        put_u32(constituent + 0x104u, (uint32_t)sizeof(jpeg_bytes));
    } else {
        put_u32(constituent + 0x0e4u, (uint32_t)(uintptr_t)not_jpeg_bytes);
        put_u32(constituent + 0x104u, (uint32_t)sizeof(not_jpeg_bytes));
    }

    for (i = 0; i < sizeof(raw_bytes); ++i) {
        raw_bytes[i] = (uint8_t)(i + 1u + (still_build_count - 1u) * 0x20u);
    }

    memset(&still_set, 0, sizeof(still_set));
    still_set.basic_set = constituent;
}

int mm_camera_capture_still(unsigned int handle, void *param2, uint32_t command,
                            uint32_t cap, uint32_t max, uint32_t p6, uint32_t p7,
                            uint32_t p8, uint32_t p9, uint32_t p10, uint32_t p11,
                            uint32_t p12, int p13, int p14, uint32_t p15,
                            uint32_t p16, uint32_t p17)
{
    size_t i;

    (void)handle;
    (void)param2;
    (void)p6;
    (void)p8;
    (void)p11;
    (void)p12;
    (void)p13;
    (void)p14;
    (void)p15;
    (void)p16;
    (void)p17;

    if (command == 0 || command > 0x1cu) {
        return -3;
    }
    last_command = command;
    last_cap = cap;
    last_max = max;
    last_remaining = p7;
    last_free = (uint64_t)p9 | ((uint64_t)p10 << 32);
    still_count += 1;
    set_capture_call_running(1);

    if (still_send_fails) {
        /*
         * The native send status is a failure, and the command is still
         * queued: the still's set is delivered later, by the test, as if a
         * later wake had transmitted the queued work.
         */
        set_capture_call_running(0);
        return -1;
    }

    /* The liveview, on the image route.  It is delivered *after* the
     * submission, and it must not answer the still.  Its bytes are a
     * different pattern from the still's first image, so that a test can
     * tell the two apart. */
    for (i = 0; i < sizeof(sample_bytes); ++i) {
        sample_bytes[i] = (uint8_t)(0x80u + i);
    }
    memset(&sample_block, 0, sizeof(sample_block));
    sample_block.addr_type = 7u;         /* virtual only */
    sample_block.source_addr = 0x00a6000000u;
    sample_block.mapped_addr = (uint32_t)(uintptr_t)sample_bytes;
    sample_block.mapped_second = sample_block.mapped_addr +
                                 (uint32_t)sizeof(sample_bytes);

    /* What the image object says about the same image. */
    the_stub_image.width = 4u;
    the_stub_image.height = 2u;
    the_stub_image.stride = 8u;
    the_stub_image.raw_pixel_bit = 12u;
    the_stub_image.format = 1u;          /* one is raw */
    the_stub_image.buffer = &the_stub_buffer;
    the_stub_buffer.address[0] = sample_block.mapped_addr;
    the_stub_buffer.address[1] = 0u;
    the_stub_buffer.address[2] = 0u;
    the_stub_buffer.address[3] = 0u;
    the_stub_buffer.size = (uint32_t)sizeof(sample_bytes);

    if (image_callback != 0) {
        image_callback(0, &sample_block, &the_stub_image, 0u);
    }

    /*
     * Another topic's set, with another payload shape.  The expansion topic
     * is one the backend subscribes to, and its payload is not an image set
     * at all, so the pointer handed over here is invalid on purpose: a
     * backend that reads it before it has decided the topic is doing a wild
     * read, and this makes that fault rather than go unnoticed.
     */
    if (deliver_unrelated_set && image_set_callback != 0) {
        image_set_callback((int)CAPTURE_LIBRARY_TOPIC_SET_EXPANSION_STILL,
                           (void *)(uintptr_t)1u, 0, 0u);
    }

    /* The still, on the image-set route. */
    if (deliver_still_set && image_set_callback != 0) {
        build_still_set();
        image_set_callback((int)CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL,
                           &still_set, 0, 0u);
    }
    /*
     * The call is held here, with the still's set already delivered, so that
     * a test can close the host while the call is still running.
     */
    wait_at_the_capture_gate();
    set_capture_call_running(0);
    return 0;
}

int mm_camera_set_notification_callback(unsigned int handle, void *callback,
                                        unsigned int id)
{
    (void)handle;
    (void)id;
    notification_callback = (capture_library_notify_callback)callback;
    return 0;
}

int RegImageCallbackId(uint32_t slot, uint32_t id)
{
    (void)slot;
    image_topics_registered += 1;
    last_image_topic = id;
    return 0;
}

int RegImageSetCallbackId(uint32_t slot, uint32_t id)
{
    (void)slot;
    image_set_topics_registered += 1;
    last_image_set_topic = id;
    return 0;
}

unsigned long long stub_raw_address(void)
{ return (unsigned long long)(uintptr_t)raw_bytes; }
uint32_t stub_raw_count(void) { return (uint32_t)sizeof(raw_bytes); }
unsigned long long stub_jpeg_address(void)
{ return (unsigned long long)(uintptr_t)jpeg_bytes; }
uint32_t stub_jpeg_count(void) { return (uint32_t)sizeof(jpeg_bytes); }
unsigned long long stub_sample_address(void)
{ return (unsigned long long)(uintptr_t)sample_bytes; }
uint32_t stub_sample_count(void) { return (uint32_t)sizeof(sample_bytes); }
int stub_image_topics_registered(void) { return image_topics_registered; }
uint32_t stub_last_image_topic(void) { return last_image_topic; }
int stub_image_set_topics_registered(void) { return image_set_topics_registered; }
uint32_t stub_last_image_set_topic(void) { return last_image_set_topic; }

/*
 * Put the stub back to its start-up state.
 *
 * The backend deliberately does not unload the library, so the stub stays
 * loaded for the life of the test program and its state carries over from
 * one test to the next.  Every test therefore resets it.
 */
void stub_reset(void)
{
    still_set_type = CAPTURE_LIBRARY_SET_TYPE_RAW_THREE;
    deliver_still_set = 1;
    deliver_unrelated_set = 0;
    first_image_size = 64u;
    main_image_is_jpeg = 1;
    last_capture_mode = -1;
    last_usr_id = -1;
    last_var_id = -1;
    last_command = 0u;
    still_count = 0;
    client_count = 0;
    create_count = 0;
    destroy_count = 0;
    image_topics_registered = 0;
    image_set_topics_registered = 0;
    release_count = 0;
    last_release_type = -1;
    last_release_address = 0u;
    fail_create = 0;
    fail_register = 0;
    release_reenters_image_set = 0;
    release_reentered = 0;
    release_fails = 0;
    pthread_mutex_lock(&release_gate_lock);
    release_gate_closed = 0;
    release_waiting = 0;
    pthread_mutex_unlock(&release_gate_lock);
    pthread_mutex_lock(&capture_gate_lock);
    capture_gate_closed = 0;
    capture_waiting = 0;
    pthread_mutex_unlock(&capture_gate_lock);
    pthread_mutex_lock(&mutating_gate_lock);
    mutating_gate_closed = 0;
    mutating_waiting = 0;
    pthread_mutex_unlock(&mutating_gate_lock);
    pthread_mutex_lock(&call_stack_lock);
    capture_call_running = 0;
    setting_call_running = 0;
    release_inside_capture = 0;
    release_inside_setting = 0;
    pthread_mutex_unlock(&call_stack_lock);
    setting_delivers_still = 0;
    still_build_count = 0u;
    var_refused_count = 0;
    quality_write_count = 0;
    last_quality_api_id = -1;
    last_quality_api_value = 0u;
    last_client_name[0] = '\0';
    last_client_name_length = -1;
    still_send_fails = 0;
    setting_write_count = 0;
    capture_mode_write_count = 0;
    usr_status_to_report = CAPTURE_LIBRARY_STATUS_OK;
    var_status_to_report = CAPTURE_LIBRARY_STATUS_OK;
    setting_call_count = 0;
}

void stub_set_still_set_type(uint32_t type) { still_set_type = type; }
void stub_set_first_image_size(uint32_t size) { first_image_size = size; }
void stub_set_main_image_is_jpeg(int is_jpeg) { main_image_is_jpeg = is_jpeg; }

/* Deliver one liveview frame now, so that a test can deliver frames from
 * another thread while the host is being closed. */
void stub_deliver_preview(void)
{
    if (image_callback != 0) {
        image_callback(0, &sample_block, &the_stub_image, 0u);
    }
}

/*
 * Deliver the still's set now.
 *
 * The camera's completion can arrive after the wait that was waiting for it
 * has given up, and a test has to be able to produce that: the delivery is
 * made here, on the test's own thread, and not by the stub's own trigger.
 */
void stub_deliver_still_set_now(void)
{
    deliver_the_still_set();
}
void stub_set_deliver_still_set(int deliver) { deliver_still_set = deliver; }
void stub_set_fail_create(int fail) { fail_create = fail; }
void stub_set_fail_register(int fail) { fail_register = fail; }
void stub_set_still_send_fails(int fail) { still_send_fails = fail; }
void stub_set_usr_status(int status) { usr_status_to_report = status; }
void stub_set_var_status(int status) { var_status_to_report = status; }
void stub_set_release_reenters_image_set(int enable)
{
    release_reenters_image_set = enable;
    release_reentered = 0;
}
/* Make the release report a failure. */
void stub_set_release_fails(int fail) { release_fails = fail; }
/* Hold the release in flight, or let it go. */
void stub_set_release_gate(int closed)
{
    pthread_mutex_lock(&release_gate_lock);
    release_gate_closed = closed ? 1 : 0;
    pthread_cond_broadcast(&release_gate_changed);
    pthread_mutex_unlock(&release_gate_lock);
}
/* Whether a release is waiting for the gate to be opened right now. */
int stub_release_waiting(void)
{
    int waiting;

    pthread_mutex_lock(&release_gate_lock);
    waiting = release_waiting;
    pthread_mutex_unlock(&release_gate_lock);
    return waiting;
}
/* Hold the still's call in flight, or let it go. */
void stub_set_capture_gate(int closed)
{
    pthread_mutex_lock(&capture_gate_lock);
    capture_gate_closed = closed ? 1 : 0;
    pthread_cond_broadcast(&capture_gate_changed);
    pthread_mutex_unlock(&capture_gate_lock);
}
/* Whether the still's call is waiting for the gate to be opened right now. */
int stub_capture_waiting(void)
{
    int waiting;

    pthread_mutex_lock(&capture_gate_lock);
    waiting = capture_waiting;
    pthread_mutex_unlock(&capture_gate_lock);
    return waiting;
}
/*
 * Wait until the still's call is at its gate.
 *
 * The wait is a bounded timed wait on the gate's own condition variable,
 * and the call notifies the waiters when it enters the gate: the test's
 * schedule is predicate-driven and not a poll.  Returns 1 when the call is
 * at the gate, and 0 when the wait gave up.
 */
int stub_wait_for_capture_gate(void)
{
    struct timespec deadline;
    int reached;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 10;
    pthread_mutex_lock(&capture_gate_lock);
    while (!capture_waiting) {
        if (pthread_cond_timedwait(&capture_gate_changed, &capture_gate_lock,
                                   &deadline) != 0) {
            break;
        }
    }
    reached = capture_waiting ? 1 : 0;
    pthread_mutex_unlock(&capture_gate_lock);
    return reached;
}
/* Hold a mutating call in flight, or let it go. */
void stub_set_mutating_gate(int closed)
{
    pthread_mutex_lock(&mutating_gate_lock);
    mutating_gate_closed = closed ? 1 : 0;
    pthread_cond_broadcast(&mutating_gate_changed);
    pthread_mutex_unlock(&mutating_gate_lock);
}
/* Whether a mutating call is waiting for the gate to be opened right now. */
int stub_mutating_waiting(void)
{
    int waiting;

    pthread_mutex_lock(&mutating_gate_lock);
    waiting = mutating_waiting;
    pthread_mutex_unlock(&mutating_gate_lock);
    return waiting;
}
/*
 * Wait until a mutating call is at its gate.
 *
 * Bounded, predicate-driven, and notified by the call itself - see
 * stub_wait_for_capture_gate.
 */
int stub_wait_for_mutating_gate(void)
{
    struct timespec deadline;
    int reached;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 10;
    pthread_mutex_lock(&mutating_gate_lock);
    while (!mutating_waiting) {
        if (pthread_cond_timedwait(&mutating_gate_changed,
                                   &mutating_gate_lock, &deadline) != 0) {
            break;
        }
    }
    reached = mutating_waiting ? 1 : 0;
    pthread_mutex_unlock(&mutating_gate_lock);
    return reached;
}
/* Make a setting deliver the still's set from inside itself. */
void stub_set_setting_delivers_still(int deliver)
{
    setting_delivers_still = deliver ? 1 : 0;
}
/* How many releases were made while the still's call was on the stack. */
int stub_release_inside_capture_count(void)
{
    int count;

    pthread_mutex_lock(&call_stack_lock);
    count = release_inside_capture;
    pthread_mutex_unlock(&call_stack_lock);
    return count;
}
/* How many releases were made while a setting's call was on the stack. */
int stub_release_inside_setting_count(void)
{
    int count;

    pthread_mutex_lock(&call_stack_lock);
    count = release_inside_setting;
    pthread_mutex_unlock(&call_stack_lock);
    return count;
}
/* The number of sets the stub has built: the deliveries it has made. */
unsigned int stub_still_build_count(void) { return still_build_count; }
void stub_set_deliver_unrelated_set(int deliver)
{
    deliver_unrelated_set = deliver;
}

int mm_camera_set_notification_id(unsigned int handle, int first_id, ...)
{
    (void)handle;
    notification_id_count = 0;
    (void)first_id;
    return 0;
}

int stub_notification_id_count(void) { return notification_id_count; }
int stub_notification_registered(void) { return notification_callback != 0; }

int pref_rtos_get_long(unsigned int key)
{
    if (key == 0x1268u) {
        return (int)pref_power_on;
    }
    return 0;
}

void stub_set_power_on_preference(int value)
{
    pref_power_on = value;
}

int ReleaseCaptureBuffer(int type, uint32_t address)
{
    last_release_type = type;
    last_release_address = address;
    release_count += 1;
    note_a_release();
    if (release_gate_is_closed()) {
        /*
         * The call is held in flight until the test opens the gate, so that
         * the test can see what the adapter does while the release runs.
         *
         * The wait is bounded: a test that fails before it opens the gate
         * must not turn into a run that never ends.
         */
        struct timespec deadline;

        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 10;
        pthread_mutex_lock(&release_gate_lock);
        release_waiting = 1;
        pthread_cond_broadcast(&release_gate_changed);
        while (release_gate_closed) {
            if (pthread_cond_timedwait(&release_gate_changed,
                                       &release_gate_lock, &deadline) != 0) {
                break;
            }
        }
        release_waiting = 0;
        pthread_mutex_unlock(&release_gate_lock);
    }
    if (release_fails) {
        /*
         * The call reports a failure.  Nothing else happens, and nothing here
         * says what the camera did with the plane.
         */
        return -1;
    }
    if (release_reenters_image_set && !release_reentered) {
        /*
         * The delivery is made once, so that the call does not re-enter
         * itself for ever.
         */
        release_reentered = 1;
        if (image_set_callback != 0) {
            build_still_set();
            image_set_callback((int)CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL,
                               &still_set, 0, 0u);
        }
    }
    return 0;
}

uint32_t RegCallbackClient(char *name, capture_library_notify_callback notify,
                           capture_library_image_callback image,
                           capture_library_image_set_callback image_set)
{
    if (fail_register) {
        /* The library's slot is signed on the client side: a refusal is a
         * negative slot. */
        return (uint32_t)-1;
    }
    /*
     * The library's registration guard wants a name that is not null and is
     * no longer than nine bytes, and refuses a longer one.  The stub checks
     * that and nothing else: it keeps no list of the names it would take,
     * because nothing says that any name in particular is a role the
     * library reserves.
     */
    if (name == 0 || strlen(name) > 9u) {
        return (uint32_t)-1;
    }
    snprintf(last_client_name, sizeof(last_client_name), "%s", name);
    last_client_name_length = (int)strlen(name);
    notify_callback = notify;
    image_callback = image;
    image_set_callback = image_set;
    client_count += 1;
    return 0u;
}

/* What the test inspects. */
int stub_last_usr_id(void) { return last_usr_id; }
uint32_t stub_last_usr_value(void) { return last_usr_value; }
int stub_last_var_id(void) { return last_var_id; }
uint32_t stub_last_var_value(void) { return last_var_value; }
uint32_t stub_last_command(void) { return last_command; }
uint32_t stub_last_cap(void) { return last_cap; }
uint32_t stub_last_max(void) { return last_max; }
uint32_t stub_last_remaining(void) { return last_remaining; }
uint64_t stub_last_free(void) { return last_free; }
int stub_still_count(void) { return still_count; }
int stub_client_count(void) { return client_count; }
int stub_create_count(void) { return create_count; }
int stub_destroy_count(void) { return destroy_count; }
int stub_last_release_type(void) { return last_release_type; }
uint32_t stub_last_release_address(void) { return last_release_address; }
int stub_release_count(void) { return release_count; }
int stub_var_refused_count(void) { return var_refused_count; }
int stub_quality_write_count(void) { return quality_write_count; }
int stub_last_quality_api_id(void) { return last_quality_api_id; }
uint32_t stub_last_quality_api_value(void) { return last_quality_api_value; }
int stub_last_client_name_length(void) { return last_client_name_length; }
const char *stub_last_client_name(void) { return last_client_name; }
int stub_setting_write_count(void) { return setting_write_count; }
int stub_capture_mode_write_count(void) { return capture_mode_write_count; }
int stub_setting_call_count(void) { return setting_call_count; }
