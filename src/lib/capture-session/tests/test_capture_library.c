/*
 * Host tests for the library backend.
 *
 * The backend is driven against the stub library, so the whole path - the
 * initializer, the settings, the trigger, the still's own set and the copy
 * out of it - is exercised with no camera, no device and no network.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "capture_session.h"
#include "capture_session_library.h"

/*
 * The sanitizer's own answer to "is this address poisoned?".
 *
 * The test needs it to say that a buffer was really freed and not only
 * forgotten, and the header is the sanitizer's own.  It is included only
 * when the sanitizer is on, because the query does not exist without it.
 */
#ifndef __has_feature
#define __has_feature(feature) 0
#endif
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define TEST_HAS_ASAN 1
#endif

static int failures;
static int checks;

#define CHECK(condition)                                                       \
    do {                                                                       \
        checks += 1;                                                            \
        if (!(condition)) {                                                    \
            failures += 1;                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);        \
        }                                                                      \
    } while (0)

/*
 * What the stub recorded.  The stub is a shared object that the backend
 * opens at run time, so the test reads its accessors the same way: by
 * resolving the names, not by linking against it.
 */
static void *stub;

static int stub_last_usr_id(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_last_usr_id");
    return fn();
}

static uint32_t stub_last_usr_value(void)
{
    uint32_t (*fn)(void) = (uint32_t (*)(void))dlsym(stub, "stub_last_usr_value");
    return fn();
}

static int stub_last_var_id(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_last_var_id");
    return fn();
}

static uint32_t stub_last_var_value(void)
{
    uint32_t (*fn)(void) = (uint32_t (*)(void))dlsym(stub, "stub_last_var_value");
    return fn();
}

static uint32_t stub_last_command(void)
{
    uint32_t (*fn)(void) = (uint32_t (*)(void))dlsym(stub, "stub_last_command");
    return fn();
}

static uint32_t stub_last_cap(void)
{
    uint32_t (*fn)(void) = (uint32_t (*)(void))dlsym(stub, "stub_last_cap");
    return fn();
}

static uint32_t stub_last_max(void)
{
    uint32_t (*fn)(void) = (uint32_t (*)(void))dlsym(stub, "stub_last_max");
    return fn();
}

static uint32_t stub_last_remaining(void)
{
    uint32_t (*fn)(void) = (uint32_t (*)(void))dlsym(stub, "stub_last_remaining");
    return fn();
}

static uint64_t stub_last_free(void)
{
    uint64_t (*fn)(void) = (uint64_t (*)(void))dlsym(stub, "stub_last_free");
    return fn();
}

static int stub_still_count(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_still_count");
    return fn();
}

static int stub_client_count(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_client_count");
    return fn();
}

static int stub_create_count(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_create_count");
    return fn();
}

static int stub_destroy_count(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_destroy_count");
    return fn();
}

static int stub_release_count(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_release_count");
    return fn();
}

static int stub_last_capture_mode(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_last_capture_mode");
    return fn();
}

static uint32_t stub_last_release_address(void)
{
    uint32_t (*fn)(void) =
        (uint32_t (*)(void))dlsym(stub, "stub_last_release_address");
    return fn();
}

static void stub_reset_in(void *library)
{
    void (*fn)(void) = (void (*)(void))dlsym(library, "stub_reset");
    fn();
}

static void stub_set_first_image_size_in(void *library, uint32_t size)
{
    void (*fn)(uint32_t) =
        (void (*)(uint32_t))dlsym(library, "stub_set_first_image_size");
    fn(size);
}

static void stub_set_main_image_is_jpeg_in(void *library, int is_jpeg)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_main_image_is_jpeg");
    fn(is_jpeg);
}

static int stub_last_release_type(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_last_release_type");
    return fn();
}

static int stub_notification_registered(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_notification_registered");
    return fn();
}

static unsigned long long stub_raw_address_in(void *library)
{
    unsigned long long (*fn)(void) =
        (unsigned long long (*)(void))dlsym(library, "stub_raw_address");
    return fn();
}

static unsigned long long stub_jpeg_address_in(void *library)
{
    unsigned long long (*fn)(void) =
        (unsigned long long (*)(void))dlsym(library, "stub_jpeg_address");
    return fn();
}

static int stub_image_topics_registered(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_image_topics_registered");
    return fn();
}

static int stub_image_set_topics_registered(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_image_set_topics_registered");
    return fn();
}

static void stub_set_still_set_type_in(void *library, uint32_t type)
{
    void (*fn)(uint32_t) =
        (void (*)(uint32_t))dlsym(library, "stub_set_still_set_type");
    fn(type);
}

static void stub_set_deliver_still_set_in(void *library, int deliver)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_deliver_still_set");
    fn(deliver);
}

static void stub_set_deliver_unrelated_set_in(void *library, int deliver)
{
    void (*fn)(int) =
        (void (*)(int))dlsym(library, "stub_set_deliver_unrelated_set");
    fn(deliver);
}

static void stub_deliver_still_set_now_in(void *library)
{
    void (*fn)(void) = (void (*)(void))dlsym(library, "stub_deliver_still_set_now");
    fn();
}

static void stub_set_fail_create_in(void *library, int fail)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_fail_create");
    fn(fail);
}

static void stub_set_fail_register_in(void *library, int fail)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_fail_register");
    fn(fail);
}

static int stub_var_refused_count(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_var_refused_count");
    return fn();
}

static int stub_quality_write_count(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_quality_write_count");
    return fn();
}

static int stub_last_quality_api_id(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_last_quality_api_id");
    return fn();
}

static uint32_t stub_last_quality_api_value(void)
{
    uint32_t (*fn)(void) =
        (uint32_t (*)(void))dlsym(stub, "stub_last_quality_api_value");
    return fn();
}

static int stub_last_client_name_length(void)
{
    int (*fn)(void) =
        (int (*)(void))dlsym(stub, "stub_last_client_name_length");
    return fn();
}

static const char *stub_last_client_name(void)
{
    const char *(*fn)(void) =
        (const char *(*)(void))dlsym(stub, "stub_last_client_name");
    return fn();
}

static void stub_set_still_send_fails_in(void *library, int fail)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_still_send_fails");
    CHECK(fn != 0);
    if (fn != 0) {
        fn(fail);
    }
}

static void stub_set_usr_status_in(void *library, int status)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_usr_status");
    CHECK(fn != 0);
    if (fn != 0) {
        fn(status);
    }
}

static void stub_set_var_status_in(void *library, int status)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_var_status");
    CHECK(fn != 0);
    if (fn != 0) {
        fn(status);
    }
}

static int stub_setting_call_count(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_setting_call_count");
    return fn();
}

static int stub_setting_write_count(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_setting_write_count");
    return fn();
}

static int stub_capture_mode_write_count(void)
{
    int (*fn)(void) =
        (int (*)(void))dlsym(stub, "stub_capture_mode_write_count");
    return fn();
}

/* Make the stub's release report a failure. */
static void stub_set_release_fails_in(void *library, int fail)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_release_fails");
    CHECK(fn != 0);
    if (fn != 0) {
        fn(fail);
    }
}

/* Hold the stub's release in flight, or let it go. */
static void stub_set_release_gate_in(void *library, int closed)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_release_gate");
    CHECK(fn != 0);
    if (fn != 0) {
        fn(closed);
    }
}

/* Whether a release is waiting for the gate to be opened right now. */
static int stub_release_waiting(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_release_waiting");
    CHECK(fn != 0);
    if (fn == 0) {
        return 0;
    }
    return fn();
}

/* Hold the stub's still call in flight, or let it go. */
static void stub_set_capture_gate_in(void *library, int closed)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_capture_gate");
    CHECK(fn != 0);
    if (fn != 0) {
        fn(closed);
    }
}

/*
 * Wait until the stub's still call is at its gate.
 *
 * The wait is the stub's own bounded timed wait on the gate's condition
 * variable, and the call notifies it when it enters the gate: the test
 * waits for the state it needs instead of polling for it.
 */
static int stub_wait_for_capture_gate(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_wait_for_capture_gate");
    CHECK(fn != 0);
    if (fn == 0) {
        return 0;
    }
    return fn();
}

/* Hold a mutating call of the stub in flight, or let it go. */
static void stub_set_mutating_gate_in(void *library, int closed)
{
    void (*fn)(int) = (void (*)(int))dlsym(library, "stub_set_mutating_gate");
    CHECK(fn != 0);
    if (fn != 0) {
        fn(closed);
    }
}

/*
 * Wait until a mutating call of the stub is at its gate.
 *
 * The stub's own bounded timed wait, notified by the call itself: the test
 * waits for the state it needs instead of polling for it.
 */
static int stub_wait_for_mutating_gate(void)
{
    int (*fn)(void) = (int (*)(void))dlsym(stub, "stub_wait_for_mutating_gate");
    CHECK(fn != 0);
    if (fn == 0) {
        return 0;
    }
    return fn();
}

/* Make a setting deliver the still's set from inside itself. */
static void stub_set_setting_delivers_still_in(void *library, int deliver)
{
    void (*fn)(int) =
        (void (*)(int))dlsym(library, "stub_set_setting_delivers_still");
    CHECK(fn != 0);
    if (fn != 0) {
        fn(deliver);
    }
}

/* How many releases the stub saw while the still's call was on the stack. */
static int stub_release_inside_capture_count(void)
{
    int (*fn)(void) =
        (int (*)(void))dlsym(stub, "stub_release_inside_capture_count");
    CHECK(fn != 0);
    if (fn == 0) {
        return 0;
    }
    return fn();
}

/* How many releases the stub saw while a setting's call was on the stack. */
static int stub_release_inside_setting_count(void)
{
    int (*fn)(void) =
        (int (*)(void))dlsym(stub, "stub_release_inside_setting_count");
    CHECK(fn != 0);
    if (fn == 0) {
        return 0;
    }
    return fn();
}

/*
 * The copy the test installs.
 *
 * The protocol's mapped address is a 32-bit address in the camera's own
 * address space, which this host cannot hand out, so the test installs the
 * copy it can use.  There are two buffers in the still's set - the raw
 * candidate and the encoded still - and the copy finds the one the address
 * names.  On the camera the default copy reads the address directly.
 */
static uint32_t test_raw_address;
static uint32_t test_jpeg_address;
static uint64_t test_raw_pointer;
static uint64_t test_jpeg_pointer;
/*
 * Where the last copy of the raw candidate landed.
 *
 * The backend allocates that buffer itself and frees it when it is done with
 * it, so the test keeps the address to be able to ask the sanitizer whether
 * the buffer was really freed and not only forgotten.
 */
static void *test_raw_copy_destination;

/* How often the adapter read the first image's plane. */
static int test_raw_copy_count;

static void test_copy(void *destination, uint32_t address, size_t count)
{
    if (address == test_raw_address) {
        test_raw_copy_count += 1;
        test_raw_copy_destination = destination;
        memcpy(destination, (const void *)(uintptr_t)test_raw_pointer, count);
    } else if (address == test_jpeg_address) {
        memcpy(destination, (const void *)(uintptr_t)test_jpeg_pointer, count);
    } else {
        /* An address the test does not know: fill it with a pattern that
         * cannot be mistaken for either buffer. */
        memset(destination, 0xa5, count);
    }
}

static int image_calls;

static void on_image(capture_session *session, const capture_session_image *image,
                     void *user_data)
{
    (void)session;
    (void)user_data;
    image_calls += 1;
    CHECK(image->sensor_data != 0);
    CHECK(image->sensor_size_bytes == 64u);
}

static void test_the_backend_takes_a_still_and_copies_the_samples(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    const uint8_t *bytes;
    size_t i;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    /*
     * The protocol's mapped address is a 32-bit address in the camera's
     * own address space, which this host cannot hand out, so the test
     * installs the copy it can use.  On the camera the default copy reads
     * the address directly.
     */
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    CHECK(host != 0);
    /* The set-type diagnostic starts at zero: this host has decoded no set
     * yet, and a null host answers zero as well. */
    CHECK(capture_library_host_last_set_type(host) == 0u);
    CHECK(capture_library_host_last_set_type(0) == 0u);
    /* The client was created once, and a client was registered. */
    CHECK(stub_create_count() == 1);
    CHECK(stub_client_count() == 1);
    /* The notification callback is registered, so the completion can be
     * heard as well as the image. */
    CHECK(stub_notification_registered() == 1);
    /* Registering a client is not subscribing: the topics are subscribed
     * by the id calls, and without them the callbacks never fire. */
    CHECK(stub_image_topics_registered() == 3);
    /* The image-set topics are the still's own results, and all of them
     * are subscribed so that a run shows what else the library publishes. */
    CHECK(stub_image_set_topics_registered() == 6);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.remaining_shots = 2448u;
    config.free_space_bytes = 7477198848ull;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The exposure goes out in the library's own vocabulary: the ISO is
     * user-data id 4 there, and the shutter is variable id 2. */
    CHECK(capture_session_set_exposure(&session, 74u, 0x1234u) == CAPTURE_SESSION_OK);
    CHECK(stub_last_usr_id() == CAPTURE_LIBRARY_SETTING_ISO);
    CHECK(stub_last_usr_value() == 74u);
    CHECK(stub_last_var_id() == CAPTURE_LIBRARY_SETTING_SHUTTER);
    CHECK(stub_last_var_value() == 0x1234u);

    /* The raw switch is not a setting the library's interface names, and
     * the backend says so rather than guessing at an id. */
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_SSIF_RAW, 1u) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    /* And the retention switch is refused: this implementation has no
     * verified way to ask the camera to keep the samples.  The backend says
     * so up front, before any library is opened, because the answer is a
     * property of the backend and not of a loaded library. */
    CHECK(capture_library_host_supports_sensor_retention() == 0);
    CHECK(capture_session_set_sensor_retention(&session, 1) ==
          CAPTURE_SESSION_BACKEND_ERROR);

    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);

    /* The trigger carried the command's *index*, not a pointer to it: the
     * device-shutter still 0x01010016 is index twenty-one. */
    CHECK(stub_last_command() == CAPTURE_LIBRARY_STILL_COMMAND_INDEX);
    CHECK(stub_last_command() == 21u);
    CHECK(stub_last_cap() == 1u);
    CHECK(stub_last_max() == 1u);
    CHECK(stub_last_remaining() == 2448u);
    CHECK(stub_last_free() == 7477198848ull);
    CHECK(stub_still_count() == 1);

    /* The still's own set arrived, and the samples are the first image's
     * bytes, in memory this process owns. */
    CHECK(image_calls == 1);
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);
    /* And the diagnostic names the set that was decoded last.  The samples
     * came with that set, so it was not the plain one, and it is not zero
     * either: something was decoded. */
    CHECK(capture_library_host_last_set_type(host) != 0u);
    CHECK(capture_library_host_last_set_type(host) !=
          CAPTURE_LIBRARY_SET_TYPE_PLAIN);
    if (result.sensor_data != 0) {
        bytes = (const uint8_t *)result.sensor_data;
        for (i = 0; i < 64u; ++i) {
            if (bytes[i] != (uint8_t)(i + 1u)) {
                CHECK(0);
                break;
            }
        }
        CHECK(1);
    }

    /* The geometry is the set's own: the first image's descriptor states
     * the width, the height and the two strides, and the capture library's
     * own diagnostic print names those four words. */
    CHECK(result.width == 4u);
    CHECK(result.height == 2u);
    /* The stride words' unit is not established, so the reported stride is
     * zero rather than one of those words read as a byte count. */
    CHECK(result.stride_bytes == 0u);
    /* The number of bits a sample occupies is not among them, so it stays
     * unknown, and unknown is zero. */
    CHECK(result.bits_per_sample == 0u);

    /* The encoded still came with it, and it is a JPEG stream. */
    CHECK(result.jpeg_data != 0);
    CHECK(result.jpeg_size_bytes == 4u);
    if (result.jpeg_data != 0) {
        bytes = (const uint8_t *)result.jpeg_data;
        CHECK(bytes[0] == 0xffu && bytes[1] == 0xd8u);
        CHECK(bytes[2] == 0xffu && bytes[3] == 0xd9u);
    }

    /* The first image's plane is released with the capture route's own
     * release, with the type that release selects and with the address the
     * descriptor named - the release matches on the address, so a wrong
     * address would release nothing. */
    CHECK(stub_release_count() == 1);
    CHECK(stub_last_release_type() == CAPTURE_LIBRARY_RELEASE_TYPE_FIRST_IMAGE);
    CHECK(stub_last_release_address() == (uint32_t)test_raw_address);

    /* The release gives back the copies this process made.  The library's
     * own release is not called again: the plane has already gone back. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(stub_release_count() == 1);

    capture_session_close(&session);
    capture_library_host_close(host);
    CHECK(stub_destroy_count() == 1);
    dlclose(stub);
}

/*
 * The shutter.
 *
 * The library's variable facade takes the camera's own variable id, 0x100 to
 * 0x12b, and the shutter's is 0x101.  The stub refuses the ids outside that
 * range, so a backend that wrote the index the id ends in - 2 - would be
 * caught here: the write would be refused, and the test would see it.
 *
 * The unit the camera stores the shutter in is not established, so the
 * number in this test is only a number.
 */
static void test_the_shutter_goes_out_with_the_camera_s_own_variable_id(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.remaining_shots = 2448u;
    config.free_space_bytes = 7477198848ull;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_SHUTTER,
                                       0x1234u) == CAPTURE_SESSION_OK);
    /* The facade took the id: nothing was refused. */
    CHECK(stub_var_refused_count() == 0);
    /* And it took the camera's own variable id, not the index it ends in. */
    CHECK(stub_last_var_id() == 0x101);
    CHECK(stub_last_var_id() == (int)CAPTURE_LIBRARY_SETTING_SHUTTER);
    CHECK(stub_last_var_value() == 0x1234u);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * The name the client registers under.
 *
 * The library's registration guard wants a name that is not null and is no
 * longer than nine bytes, and the stub refuses a longer one - so an open
 * that succeeds says the name passed the guard.  The stub keeps no list of
 * the names it would take: nothing says that any name in particular is a
 * role the library reserves, so the test only says what the guard says.
 */
static void test_the_client_registers_under_a_name_the_library_accepts(void)
{
    capture_library_host *host = 0;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }

    /* The client was registered, so the guard took the name. */
    CHECK(stub_client_count() == 1);
    CHECK(stub_last_client_name_length() >= 0);
    CHECK(stub_last_client_name_length() <= 9);
    /* And it is not the twelve-byte name that the guard would refuse. */
    CHECK(stub_last_client_name_length() != 12);
    CHECK(strcmp(stub_last_client_name(), "capture-host") != 0);

    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * The image quality.
 *
 * The camera's positions are 0 to 7 and the library takes them tagged, and
 * the mapping between the two is not the identity: the positions go to
 * 0x30001, 0x30002, 0x30003, 0x30000 and then to their own number.
 *
 * The native values 0x40000 to 0x40007 name the same eight positions, and
 * the library does not take them as they are, so they are converted too.
 *
 * The stub refuses a quality value outside 0x30000 to 0x30007, so a backend
 * that wrote the value in the wrong form would be caught here.
 */
static void test_the_image_quality_goes_out_in_the_library_s_own_form(void)
{
    static const uint32_t expected[8] = {
        0x30001u, 0x30002u, 0x30003u, 0x30000u,
        0x30004u, 0x30005u, 0x30006u, 0x30007u
    };
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    uint32_t position;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.remaining_shots = 2448u;
    config.free_space_bytes = 7477198848ull;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    for (position = 0u; position < 8u; ++position) {
        /* The position, as it is. */
        CHECK(capture_session_set_setting(&session,
                                           CAPTURE_SESSION_IMAGE_QUALITY,
                                           position) == CAPTURE_SESSION_OK);
        /* The library's own id for the quality, and its own form of the
         * value. */
        CHECK(stub_last_quality_api_id() == 3);
        CHECK(stub_last_quality_api_id() == CAPTURE_LIBRARY_SETTING_QUALITY);
        CHECK(stub_last_quality_api_value() == expected[position]);
        CHECK(stub_last_quality_api_value() >=
              CAPTURE_LIBRARY_QUALITY_API_FIRST);
        CHECK(stub_last_quality_api_value() <=
              CAPTURE_LIBRARY_QUALITY_API_LAST);

        /* The native tagged form names the same position, and is written
         * the same way. */
        CHECK(capture_session_set_setting(&session,
                                           CAPTURE_SESSION_IMAGE_QUALITY,
                                           CAPTURE_LIBRARY_QUALITY_NATIVE_FIRST +
                                               position) == CAPTURE_SESSION_OK);
        CHECK(stub_last_quality_api_value() == expected[position]);
    }
    CHECK(stub_quality_write_count() == 16);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A quality that is not one of the camera's positions.
 *
 * The whole 32-bit value is looked at.  A value with anything in bits 8 to
 * 15, a value whose tag is not one of the two that are accepted, and a
 * position outside 0 to 7 are refused - and refused whole.  Nothing is
 * trimmed down to a position: a value that is not one of the camera's
 * positions must not become one by losing a bit.
 */
static void test_an_image_quality_that_is_not_a_position_is_refused(void)
{
    static const uint32_t refused[] = {
        0x00000300u,   /* bits 8 to 15 carry nothing */
        0x0000ff05u,   /* the same, with a position that would fit */
        0x00030100u,   /* the library's own tag is not one the caller may use */
        0x00030005u,   /* the same, with a position that would fit */
        0x00000008u,   /* a position outside the eight */
        0x00040008u,   /* the native form of a position outside the eight */
        0x00010005u,   /* a tag that is neither of the two */
        0x00040105u,   /* the native tag with stray bits */
        0xffff0005u    /* a tag that is neither of the two */
    };
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    size_t i;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.remaining_shots = 2448u;
    config.free_space_bytes = 7477198848ull;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    for (i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        CHECK(capture_session_set_setting(&session,
                                           CAPTURE_SESSION_IMAGE_QUALITY,
                                           refused[i]) ==
              CAPTURE_SESSION_BACKEND_ERROR);
    }
    /* Not one of them was written: the library never saw a value. */
    CHECK(stub_quality_write_count() == 0);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * The plain still.
 *
 * The session's plain still is 0x01010001, and the library's table holds it
 * at index one.  The backend maps the command word in the request body to
 * that index; it does not send the device-shutter still's index whatever
 * the body says.
 */
static void test_the_plain_still_uses_its_own_index(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    /* Zero selects the session's plain still. */
    config.still_command = 0u;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(stub_last_command() == CAPTURE_LIBRARY_STILL_PLAIN_INDEX);
    CHECK(stub_last_command() == 1u);

    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A preview does not answer a still.
 *
 * The image route is the liveview, and the stub delivers a frame on it
 * after the submission.  The still has no set, so there is no completion:
 * the capture runs out of time rather than being answered by the frame.
 */
static void test_a_preview_does_not_answer_a_still(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    stub_set_deliver_still_set_in(stub, 0);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    /* The frame on the image route is not a completion, so the wait runs
     * out. */
    CHECK(capture_session_capture(&session, 50u, &result) == CAPTURE_SESSION_TIMEOUT);
    CHECK(result.sensor_data == 0);
    CHECK(result.sensor_size_bytes == 0u);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A still whose set carries no first image.
 *
 * That is the plain configuration: the firmware puts no raw image in the
 * set.  So there is no completion to publish - a completion with no
 * samples would be a malformed one - and the wait runs out.
 */
static void test_a_plain_set_carries_no_raw_image(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    stub_set_still_set_type_in(stub, CAPTURE_LIBRARY_SET_TYPE_PLAIN);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    /* Nothing has been decoded yet. */
    CHECK(capture_library_host_last_set_type(host) == 0u);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 50u, &result) == CAPTURE_SESSION_TIMEOUT);
    CHECK(result.sensor_data == 0);
    /* The set arrived and it was the plain one, even though no completion
     * followed it: the backend refuses to publish a completion for a set
     * with no first image, and the diagnostic records the type before that
     * refusal. */
    CHECK(capture_library_host_last_set_type(host) ==
          CAPTURE_LIBRARY_SET_TYPE_PLAIN);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * The set-type diagnostic.
 *
 * It says which set was decoded last, and nothing else.  The test says that
 * against the stub:
 *
 *   - it is zero before this host has decoded anything, and zero for a null
 *     host;
 *   - a set that answers no request is not decoded, so it does not move the
 *     diagnostic;
 *   - the type it reports is the type the set itself carries;
 *   - it is not reset by a later submission, so it can be stale with respect
 *     to a still that is still outstanding.
 */
static void test_the_latest_set_type_is_a_diagnostic(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    capture_session_status status;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);

    /* Nothing has been decoded yet, and a null host answers zero. */
    CHECK(capture_library_host_last_set_type(host) == 0u);
    CHECK(capture_library_host_last_set_type(0) == 0u);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* A set that answers no request is not decoded: the callback skips it,
     * so the diagnostic does not move. */
    stub_deliver_still_set_now_in(stub);
    CHECK(capture_library_host_last_set_type(host) == 0u);

    /* A still whose set is a raw one: the diagnostic names the type the set
     * itself carries. */
    stub_set_still_set_type_in(stub, CAPTURE_LIBRARY_SET_TYPE_RAW_THREE);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_OK);
    CHECK(capture_library_host_last_set_type(host) ==
          CAPTURE_LIBRARY_SET_TYPE_RAW_THREE);

    /* The bytes go back before the next completion, and the next still is
     * one that gets no set at all. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    stub_set_deliver_still_set_in(stub, 0);
    memset(&result, 0, sizeof(result));
    status = capture_session_capture(&session, 50u, &result);
    /* The wait runs out, because no set arrives for that still.  A session
     * that refused a second still instead would say so, and the diagnostic
     * would be untouched by that as well. */
    CHECK(status == CAPTURE_SESSION_TIMEOUT ||
          status == CAPTURE_SESSION_BAD_STATE);
    /* And the submission did not reset the diagnostic: it still names the
     * last set that was decoded, which is now stale with respect to the
     * still that is outstanding. */
    CHECK(capture_library_host_last_set_type(host) ==
          CAPTURE_LIBRARY_SET_TYPE_RAW_THREE);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A library that does not export the image accessors.
 *
 * The accessors are optional, and a library without them must still work:
 * the still's samples arrive, and they arrive with the geometry the *set*
 * states - which is the still's own geometry, not the liveview's, so the
 * accessors are not what the completion's geometry comes from.
 */
static void test_the_set_survives_a_library_without_the_accessors(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session session;
    capture_session_config config;
    capture_session_image result;
    void *plain = 0;

    plain = dlopen("./libstub-capture-plain.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(plain != 0);
    stub_reset_in(plain);
    CHECK(capture_library_host_open(&host, "./libstub-capture-plain.so", 0) == 0);
    if (host == 0) {
        dlclose(plain);
        return;
    }
    test_raw_pointer = stub_raw_address_in(plain);
    test_jpeg_pointer = stub_jpeg_address_in(plain);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.remaining_shots = 2448u;
    config.free_space_bytes = 7477198848ull;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);

    /* The samples still arrive. */
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);
    /* And the geometry is the set's own, exactly as it is with the
     * accessors: the completion's geometry does not come from the image
     * route at all.  The stride stays zero for the same reason. */
    CHECK(result.width == 4u);
    CHECK(result.height == 2u);
    CHECK(result.stride_bytes == 0u);
    CHECK(result.bits_per_sample == 0u);

    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(plain);
}

/*
 * The AE bracket.
 *
 * The command word 0x01010005 is the firmware's capture type 5, which the
 * firmware itself names `E_CAP_CMD_STL_AE_BRACKET`, and the library's own
 * table holds it at index five.  The backend maps it, so that a caller can
 * ask the camera for a bracket rather than for a single still.
 */
static void test_the_bracket_command_uses_its_own_index(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND_BRACKET;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(stub_last_command() == CAPTURE_LIBRARY_STILL_BRACKET_INDEX);
    CHECK(stub_last_command() == 5u);

    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}


/*
 * A command the library's table does not hold.
 *
 * The backend must refuse it rather than turn it into an index by a guess,
 * and nothing must reach the library: the trigger is never called.
 */
static void test_an_unmapped_command_is_refused(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    /* The plain still's index is one and the device-shutter's is
     * twenty-one; this is neither, and it is not the AE bracket's five. */
    config.still_command = 0x01010002u;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    /* The trigger is refused, so the session reports the backend's error
     * and the library is never asked for a still. */
    CHECK(capture_session_capture(&session, 50u, &result) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    CHECK(stub_still_count() == 0);
    CHECK(result.sensor_data == 0);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A first image that states no size.
 *
 * The extent of a copy is the buffer's own size, so a descriptor that
 * states none leaves the extent unresolved, and nothing is copied: a copy
 * of the wrong length is worse than no copy.  So there is no completion.
 */
static void test_a_first_image_without_a_size_is_refused(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    stub_set_first_image_size_in(stub, 0u);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 50u, &result) == CAPTURE_SESSION_TIMEOUT);
    CHECK(result.sensor_data == 0);
    CHECK(result.sensor_size_bytes == 0u);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A main image that is not a JPEG.
 *
 * The set's main image is the YCbCr image, so it is not the camera's JPEG
 * and it is not offered as one.  The completion still arrives - with the
 * samples - and the JPEG is simply not there.
 */
static void test_a_main_image_that_is_not_a_jpeg_is_not_offered_as_one(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    stub_set_main_image_is_jpeg_in(stub, 0);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    /* The samples are there. */
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);
    /* And the main image is not offered as a JPEG. */
    CHECK(result.jpeg_data == 0);
    CHECK(result.jpeg_size_bytes == 0u);

    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * The close, while a callback is running.
 *
 * The library delivers its callbacks from its own thread, and the close may
 * happen while one is in flight.  The callback must find no host once the
 * close has published that, and the host must not be freed while a callback
 * is using it.  This test runs a thread that delivers liveview frames in a
 * loop while the main thread closes the host, and the sanitizers would
 * report a use-after-free or a double free if the lock were not doing its
 * job.
 */
static volatile int delivering;

static void *deliver_frames(void *argument)
{
    void (*deliver)(void) = (void (*)(void))argument;

    while (delivering) {
        deliver();
    }
    return 0;
}

static void test_the_close_while_a_callback_is_running(void)
{
    capture_library_host *host = 0;
    pthread_t thread;
    void (*deliver)(void);

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    deliver = (void (*)(void))dlsym(stub, "stub_deliver_preview");
    CHECK(deliver != 0);
    if (deliver == 0) {
        capture_library_host_close(host);
        dlclose(stub);
        return;
    }

    delivering = 1;
    CHECK(pthread_create(&thread, 0, deliver_frames, (void *)deliver) == 0);
    /* The host is closed while the thread is delivering. */
    capture_library_host_close(host);
    delivering = 0;
    CHECK(pthread_join(thread, 0) == 0);

    dlclose(stub);
}

/*
 * A release that delivers a callback from inside itself.
 *
 * The release of the still's first image is a call into the library, and the
 * library may deliver the still's set again from inside such a call.  The
 * backend's lock is not reentrant, so a delivery that arrives that way must
 * not be made with the lock held: the delivery would wait for a lock that
 * the thread itself holds, and the process would stop there.
 *
 * The delivery that arrives that way is an extra set: the first result is
 * latched, so it is refused before its payload is read, the plane is not
 * released a second time, and this process is quarantined.  The test
 * therefore runs in a child of its own.
 *
 * The test makes the stub deliver the set from inside the release and takes
 * the still on a thread of its own, so that a wait that never ends is seen as
 * a failure of the test rather than as a test run that never ends.
 */
static void stub_set_release_reenters_image_set_in(void *library, int enable)
{
    void (*fn)(int) =
        (void (*)(int))dlsym(library, "stub_set_release_reenters_image_set");
    CHECK(fn != 0);
    if (fn != 0) {
        fn(enable);
    }
}

/*
 * The flag that says the capture came back.
 *
 * It is written by the thread that takes the still and read by the thread
 * that waits for it, so it is guarded by a mutex.  A volatile word is not a
 * way to share a value between threads: it says that the compiler keeps the
 * read, and nothing more, and the two threads would still be racing.
 */
static pthread_mutex_t reentry_lock = PTHREAD_MUTEX_INITIALIZER;
static int reentry_finished;
static capture_session_status reentry_status;
static capture_session_image reentry_image;
static capture_session *reentry_session;

static void set_reentry_finished(void)
{
    pthread_mutex_lock(&reentry_lock);
    reentry_finished = 1;
    pthread_mutex_unlock(&reentry_lock);
}

static int read_reentry_finished(void)
{
    int finished;

    pthread_mutex_lock(&reentry_lock);
    finished = reentry_finished;
    pthread_mutex_unlock(&reentry_lock);
    return finished;
}

static void *take_the_still(void *argument)
{
    (void)argument;
    memset(&reentry_image, 0, sizeof(reentry_image));
    reentry_status =
        capture_session_capture(reentry_session, 1000u, &reentry_image);
    set_reentry_finished();
    return 0;
}

static void test_a_release_that_delivers_a_callback_does_not_stop_the_capture(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    pthread_t thread;
    const uint8_t *bytes;
    size_t i;
    int waited;
    capture_library_host_health health;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);

    /* The release delivers the still's set again, from inside itself. */
    stub_set_release_reenters_image_set_in(stub, 1);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.remaining_shots = 2448u;
    config.free_space_bytes = 7477198848ull;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    reentry_session = &session;
    pthread_mutex_lock(&reentry_lock);
    reentry_finished = 0;
    pthread_mutex_unlock(&reentry_lock);
    CHECK(pthread_create(&thread, 0, take_the_still, 0) == 0);

    /*
     * The capture is given two seconds, which is far more than it needs.  A
     * backend that waits for a lock its own thread holds never comes back, so
     * the wait ends and the test says so instead of the run hanging.
     */
    for (waited = 0; waited < 200 && !read_reentry_finished(); ++waited) {
        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = 10000000L;
        nanosleep(&pause, 0);
    }
    if (!read_reentry_finished()) {
        checks += 1;
        failures += 1;
        printf("FAIL %s:%d: the capture did not come back from a release that "
               "delivered a callback\n", __FILE__, __LINE__);
        fflush(stdout);
        /* The thread is waiting for a lock that it holds itself, so it can
         * never be joined and the process ends here. */
        _exit(1);
    }
    CHECK(pthread_join(thread, 0) == 0);

    /* The still came back, and it is the still's own samples: the delivery
     * that came from inside the release did not take the copy away. */
    CHECK(reentry_status == CAPTURE_SESSION_OK);
    CHECK(reentry_image.sensor_data != 0);
    CHECK(reentry_image.sensor_size_bytes == 64u);
    CHECK(image_calls == 1);
    /* The release was called once.  The delivery that came from inside the
     * release is an extra set: the first result is latched, so it is refused
     * before its payload is read, and it does not make the library's plane be
     * released a second time.  Without the count the test could pass without
     * ever reaching the path it is about. */
    CHECK(stub_release_count() == 1);
    /* And the host says that it needs recovery, because the extra set could
     * not be associated with the request. */
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.recovery_required == 1);
    CHECK(health.may_mutate == 0);
    if (reentry_image.sensor_data != 0) {
        bytes = (const uint8_t *)reentry_image.sensor_data;
        for (i = 0; i < 64u; ++i) {
            if (bytes[i] != (uint8_t)(i + 1u)) {
                CHECK(0);
                break;
            }
        }
        CHECK(1);
    }

    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);

    /* And this process opens no further host: the extra set left the native
     * obligations unresolved. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);

    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A second set that is eligible, after the first result was latched.
 *
 * The native route hands out no request id, so a second set that arrived
 * after the submission cannot be associated with the request: it may be a
 * duplicate publication of the same still, a second still, or a late
 * delivery of older work.  The first result is therefore the one the host
 * keeps, and the second set is refused before its payload is read.
 *
 * What has to hold:
 *
 *   - the latched copy is not replaced and not freed;
 *   - the library's plane is not released a second time;
 *   - the bytes that come out are the first delivery's bytes - the stub
 *     gives every delivery a different pattern, so a copy that had been
 *     replaced would show the second one's;
 *   - the copy stays collectable: it is the caller's;
 *   - the host needs recovery, and this process opens no further host.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static void test_a_second_eligible_set_keeps_the_first_result(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    capture_session_error error;
    capture_library_host_health health;
    const uint8_t *bytes;
    size_t i;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The request goes out and no completion comes with it, so the wait gives
     * up and the request is still outstanding. */
    stub_set_deliver_still_set_in(stub, 0);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 50u, &result) ==
          CAPTURE_SESSION_TIMEOUT);

    /* The first delivery: the still's set.  It is copied and the library's
     * plane is released. */
    stub_deliver_still_set_now_in(stub);
    CHECK(test_raw_copy_destination != 0);
    CHECK(stub_release_count() == 1);

    /* The second delivery: an extra set that cannot be associated with the
     * request.  It takes no copy of its own - its payload is not read - and
     * it does not release the plane a second time. */
    stub_deliver_still_set_now_in(stub);
    CHECK(stub_release_count() == 1);

    /* The result that comes out is the first delivery's: the stub gives
     * every delivery a different pattern, so a replaced copy would carry
     * the second one's bytes.  It is still collectable: the extra set does
     * not take the copy the caller owns away. */
    memset(&result, 0, sizeof(result));
    memset(&error, 0, sizeof(error));
    CHECK(backend.collect_image(backend.self, &result, &error) == 1);
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);
    if (result.sensor_data != 0) {
        bytes = (const uint8_t *)result.sensor_data;
        for (i = 0; i < 64u; ++i) {
            if (bytes[i] != (uint8_t)(i + 1u)) {
                CHECK(0);
                break;
            }
        }
        CHECK(1);
    }

    /* And the host says that it needs recovery, and that it may not make
     * another request. */
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.recovery_required == 1);
    CHECK(health.may_mutate == 0);
    CHECK(health.has_result == 1);

    /* A new session on this host is refused, and no request reaches the
     * library. */
    capture_session_close(&session);
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) != CAPTURE_SESSION_OK);
    CHECK(stub_still_count() == 1);

    /* And this process opens no further host: the extra set leaves the
     * native obligations unresolved. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);
    CHECK(capture_library_host_last_error() != 0);

    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A duplicate that arrives after the result was collected.
 *
 * Collecting the result clears the state that says a completion is awaited,
 * so the arrival-order heuristic no longer calls the set ours.  The
 * duplicate is still a duplicate: the first result was latched, and a set
 * that cannot be associated with the request must not be taken quietly.
 *
 * So the host needs recovery, and this process opens no further host.
 */
static void test_a_duplicate_after_the_collection_needs_recovery(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    capture_library_host_health health;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The still comes back and is collected. */
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(result.sensor_data != 0);
    CHECK(stub_release_count() == 1);
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);

    /* Nothing is wrong with the host yet: the result was collected and the
     * request was answered. */
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.recovery_required == 0);
    CHECK(health.may_mutate == 1);
    CHECK(health.has_result == 0);

    /* Now the duplicate arrives.  The completion is no longer awaited, so the
     * heuristic does not call the set ours - and the host still needs
     * recovery, because the first result was latched. */
    stub_deliver_still_set_now_in(stub);
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.recovery_required == 1);
    CHECK(health.may_mutate == 0);
    /* And the plane was not released a second time. */
    CHECK(stub_release_count() == 1);

    /* This process opens no further host. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A native release that reported a failure.
 *
 * The release of the still's first image is the library's own call, and its
 * status says nothing about what the camera did with the plane.  A nonzero
 * status is not a statement that the plane was released, and it is not a
 * statement that it was not: so the outcome is unresolved, the plane may
 * still be the library's, and a later release of the same address would be a
 * second release of the same buffer.
 *
 * What has to hold:
 *
 *   - the bytes that were copied are not hidden behind the failure: they
 *     are collected and released as they are, because they are the only
 *     result this component has;
 *   - the host takes no further request, no further setting and no further
 *     capture mode, and it is not opened for another session;
 *   - the release is not retried;
 *   - and this process opens no further host, because the library's
 *     registrations survive the client and a delivery that arrives later
 *     would land in the next one.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static void test_a_failed_native_release_keeps_the_result_and_blocks_the_host(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    capture_library_host_health health;
    const uint8_t *bytes;
    size_t i;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    /* The library's release call reports a failure. */
    stub_set_release_fails_in(stub, 1);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The still comes back, and the release of its first image fails. */
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(stub_release_count() == 1);
    CHECK(image_calls == 1);

    /* The bytes are still there: the failure is about the native plane, not
     * about the copy this process owns. */
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);
    if (result.sensor_data != 0) {
        bytes = (const uint8_t *)result.sensor_data;
        for (i = 0; i < 64u; ++i) {
            if (bytes[i] != (uint8_t)(i + 1u)) {
                CHECK(0);
                break;
            }
        }
        CHECK(1);
    }

    /* And they are the caller's to release. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);

    /* The host says that the release's outcome is unresolved, and that it
     * may not make another request. */
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.release_unresolved == 1);
    CHECK(health.recovery_required == 0);
    CHECK(health.may_mutate == 0);

    /* A further request is refused before it reaches the library, and it is
     * not retried: the release count does not move either. */
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    CHECK(stub_still_count() == 1);
    CHECK(stub_release_count() == 1);

    /* A setting is refused before it reaches the library. */
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    CHECK(stub_setting_call_count() == 0);

    /* And so is the capture mode. */
    CHECK(capture_library_host_set_capture_mode(host, 1u) != 0);
    CHECK(stub_capture_mode_write_count() == 0);

    /* And the host is not opened for another session. */
    capture_session_close(&session);
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) != CAPTURE_SESSION_OK);

    /* And this process opens no further host. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "outstanding") != 0);

    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A request made while the native release is in flight.
 *
 * The release is a call into the library, and the library may deliver a
 * callback from inside it - so the adapter makes that call with its lock
 * dropped.  While the call is running, the adapter is inside the library,
 * and a request made now would race it.
 *
 * The test holds the release in flight - the stub waits - and asks the
 * backend for a request, a setting and a capture mode while it waits.  All
 * three have to be refused before they reach the library.
 *
 * The delivery is made on a thread of its own, because the release is made
 * from inside the delivery.
 */
static void *deliver_the_set_from_another_thread(void *argument)
{
    void (*deliver)(void) = (void (*)(void))argument;

    deliver();
    return 0;
}

static void test_a_request_is_refused_while_the_native_release_runs(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    capture_session_error error;
    capture_library_host_health health;
    pthread_t thread;
    void (*deliver)(void);
    uint8_t body[CAPTURE_SESSION_STILL_BODY_SIZE];
    int waited;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The request goes out and no completion comes with it, so the wait gives
     * up and the request is still outstanding. */
    stub_set_deliver_still_set_in(stub, 0);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 50u, &result) ==
          CAPTURE_SESSION_TIMEOUT);

    /* The delivery is made on a thread of its own, and the release it makes
     * is held in flight. */
    deliver = (void (*)(void))dlsym(stub, "stub_deliver_still_set_now");
    CHECK(deliver != 0);
    if (deliver == 0) {
        capture_session_close(&session);
        capture_library_host_close(host);
        dlclose(stub);
        return;
    }
    stub_set_release_gate_in(stub, 1);
    CHECK(pthread_create(&thread, 0, deliver_the_set_from_another_thread,
                         (void *)deliver) == 0);

    /* The wait for the release to be in flight is bounded, so that a
     * backend that never starts it is a failure of the test and not a run
     * that never ends. */
    for (waited = 0; waited < 200 && !stub_release_waiting(); ++waited) {
        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = 10000000L;
        nanosleep(&pause, 0);
    }
    CHECK(stub_release_waiting() == 1);

    /* The host says that a native release is in flight. */
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.release_in_progress == 1);

    /* A request is refused before it reaches the library. */
    memset(body, 0, sizeof(body));
    body[0] = 0x16u;
    body[1] = 0x00u;
    body[2] = 0x01u;
    body[3] = 0x01u;
    memset(&error, 0, sizeof(error));
    CHECK(backend.submit_still(backend.self, body, sizeof(body), &error) != 0);
    CHECK(error.code == 10);
    CHECK(stub_still_count() == 1);

    /* And so is a setting. */
    memset(&error, 0, sizeof(error));
    CHECK(backend.set_setting(backend.self, CAPTURE_SESSION_ISO, 74u,
                               &error) != 0);
    CHECK(error.code == 10);
    CHECK(stub_setting_call_count() == 0);

    /* And so is the capture mode. */
    CHECK(capture_library_host_set_capture_mode(host, 1u) != 0);
    CHECK(stub_capture_mode_write_count() == 0);

    /* The release is let go, and the delivery finishes. */
    stub_set_release_gate_in(stub, 0);
    CHECK(pthread_join(thread, 0) == 0);

    /* The release is over, and the result is there: the host is usable again
     * for a request, and the still that was in flight comes back. */
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.release_in_progress == 0);
    CHECK(health.may_mutate == 1);
    CHECK(stub_release_count() == 1);
    memset(&result, 0, sizeof(result));
    memset(&error, 0, sizeof(error));
    CHECK(backend.collect_image(backend.self, &result, &error) == 1);
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * Closing the session while a native release is in flight.
 *
 * The release of the still's first image is a call into the library, and
 * closing the session while it runs means its outcome was never
 * established.  The collection may already have cleared the outstanding
 * request, so the outstanding-request rule says nothing here: this is a
 * second rule, for a release that was still running.
 *
 * So this process opens no further host - not while the release is still
 * running, and not after it returns successfully either.  A successful
 * return is a local completion, not a proof of retirement, and the process
 * exit is not a recovery.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static void test_a_session_close_during_a_native_release_forbids_process_reuse(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    pthread_t thread;
    void (*deliver)(void);
    int waited;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The request goes out and no completion comes with it, so the wait gives
     * up and the request is still outstanding. */
    stub_set_deliver_still_set_in(stub, 0);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 50u, &result) ==
          CAPTURE_SESSION_TIMEOUT);

    /* The delivery is made on a thread of its own, and the release it makes
     * is held in flight. */
    deliver = (void (*)(void))dlsym(stub, "stub_deliver_still_set_now");
    CHECK(deliver != 0);
    if (deliver == 0) {
        capture_session_close(&session);
        capture_library_host_close(host);
        dlclose(stub);
        return;
    }
    stub_set_release_gate_in(stub, 1);
    CHECK(pthread_create(&thread, 0, deliver_the_set_from_another_thread,
                         (void *)deliver) == 0);
    for (waited = 0; waited < 200 && !stub_release_waiting(); ++waited) {
        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = 10000000L;
        nanosleep(&pause, 0);
    }
    CHECK(stub_release_waiting() == 1);

    /* The result is collected while the release is still running, so the
     * outstanding request is cleared and only the release is left. */
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);

    /* The session is closed while the release is in flight. */
    capture_session_close(&session);

    /* This process opens no further host, already: the release is still
     * running. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);

    /* And the release is let go.  It succeeds, and the quarantine stays: a
     * successful return is not a retirement, and it does not take the
     * quarantine back. */
    stub_set_release_gate_in(stub, 0);
    CHECK(pthread_join(thread, 0) == 0);
    CHECK(stub_release_count() == 1);
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);

    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * Closing the host while a native release is in flight.
 *
 * The same rule, on the other close: the host is freed while the release is
 * still running, so its outcome was never established and nothing here
 * certifies a recovery.  The quarantine is set before the release returns,
 * so another context cannot be opened in between - and a later successful
 * return does not take it back.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static void test_a_host_close_during_a_native_release_forbids_process_reuse(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    capture_session_error error;
    pthread_t thread;
    void (*deliver)(void);
    int waited;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The request goes out and no completion comes with it, so the wait gives
     * up and the request is still outstanding. */
    stub_set_deliver_still_set_in(stub, 0);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 50u, &result) ==
          CAPTURE_SESSION_TIMEOUT);

    /* The delivery is made on a thread of its own, and the release it makes
     * is held in flight. */
    deliver = (void (*)(void))dlsym(stub, "stub_deliver_still_set_now");
    CHECK(deliver != 0);
    if (deliver == 0) {
        capture_session_close(&session);
        capture_library_host_close(host);
        dlclose(stub);
        return;
    }
    stub_set_release_gate_in(stub, 1);
    CHECK(pthread_create(&thread, 0, deliver_the_set_from_another_thread,
                         (void *)deliver) == 0);
    for (waited = 0; waited < 200 && !stub_release_waiting(); ++waited) {
        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = 10000000L;
        nanosleep(&pause, 0);
    }
    CHECK(stub_release_waiting() == 1);

    /* The result is collected while the release is still running, so the
     * outstanding request is cleared and only the release is left. */
    memset(&result, 0, sizeof(result));
    memset(&error, 0, sizeof(error));
    CHECK(backend.collect_image(backend.self, &result, &error) == 1);

    /* The host is closed while the release is in flight.  The session is not
     * closed afterwards: its backend is the host that was just freed. */
    capture_library_host_close(host);

    /* The native client is not destroyed while a call into the library is
     * still running: that teardown is not established as safe here, so the
     * obligation is left unrecovered and the quarantine stays. */
    CHECK(stub_destroy_count() == 0);

    /* This process opens no further host, already: the release is still
     * running. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);

    /* And the release is let go.  It succeeds, and the quarantine stays. */
    stub_set_release_gate_in(stub, 0);
    CHECK(pthread_join(thread, 0) == 0);
    CHECK(stub_release_count() == 1);
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);

    dlclose(stub);
}

static void test_a_missing_library_is_refused(void)
{
    capture_library_host *host = 0;

    CHECK(capture_library_host_open(&host, "./no-such-library.so", 0) != 0);
    CHECK(host == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "no-such-library.so") != 0);
}

/*
 * The library's path is required.
 *
 * A null or an empty path used to fall through to a lookup that cannot
 * work - the entry points are resolved on the handle the open obtains, and
 * there is none - so the open always failed, with no reason.  It is refused
 * now, with a reason the caller can read.
 */
static void test_a_pathless_library_is_refused(void)
{
    capture_library_host *host = 0;

    CHECK(capture_library_host_open(&host, 0, 0) != 0);
    CHECK(host == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "path is required") != 0);

    host = 0;
    CHECK(capture_library_host_open(&host, "", 0) != 0);
    CHECK(host == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "path is required") != 0);
}

/*
 * The library's own capture mode.
 *
 * The mode is a byte of zero to four, and mode four is the one that writes
 * a different user data from the other four.  The backend exposes it under
 * its own name - it is not the camera's raw switch, because the link
 * between the two is not established - and it refuses a mode the library
 * would refuse.
 */
static void test_the_capture_mode_reaches_the_library(void)
{
    capture_library_host *host = 0;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }

    CHECK(capture_library_host_set_capture_mode(host,
                                                 CAPTURE_LIBRARY_CAPTURE_MODE_FOUR) == 0);
    CHECK(stub_last_capture_mode() == 4);

    /* A mode above four is refused before the library is asked, because the
     * library refuses it and writes nothing. */
    CHECK(capture_library_host_set_capture_mode(host, 5u) != 0);
    CHECK(stub_last_capture_mode() == 4);

    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * Another topic's set is never read.
 *
 * The camera publishes several sets for one still - the YCC set, the MPO
 * set, the UD-YCC set, the expansion set - and only the encoded still's
 * payload is an image set whose first word is the basic set that begins
 * with the encoded-set constituent.  The others carry another payload
 * shape, so the topic has to be decided **before** the payload is touched:
 * a backend that decodes first and filters afterwards reads a payload that
 * is not an image set, which on the camera is a wild read.
 *
 * The stub delivers the expansion topic with a deliberately invalid
 * pointer, so a backend that reads it does not merely get the wrong
 * answer - it faults, and the sanitizers see it.  The still's own set is
 * delivered afterwards, so the test also says that the encoded set is
 * still the one that answers.
 */
static void test_an_unrelated_topic_is_never_read(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    const uint8_t *bytes;
    size_t i;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    /* The expansion topic's set is delivered as well, with a payload that
     * is not an image set at all. */
    stub_set_deliver_unrelated_set_in(stub, 1);

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);

    /* The completion is the still's own set, and the samples are the first
     * image's own bytes - not anything from the other topic. */
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);
    if (result.sensor_data != 0) {
        bytes = (const uint8_t *)result.sensor_data;
        for (i = 0; i < 64u; ++i) {
            if (bytes[i] != (uint8_t)(i + 1u)) {
                CHECK(0);
                break;
            }
        }
        CHECK(1);
    }

    /* And the other topic's set released nothing: only the still's own
     * first image is released, once. */
    CHECK(stub_release_count() == 1);

    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A host that finished cleanly is reused.
 *
 * The rule is about the abandoned request, not about every close: a session
 * that collected its result and closed leaves nothing outstanding, so the
 * host takes another session.  Without this the fail-closed rule would stop
 * every host after its first still, which is not what it says.
 */
static void test_a_host_that_finished_cleanly_is_reused(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);

    capture_library_host_backend(host, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    /* The first session takes a still and collects it. */
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(result.sensor_data != 0);
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);

    /* And the host takes a second one. */
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(result.sensor_data != 0);
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);

    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * Run a test in a child process.
 *
 * One of the backend's rules is process-wide: a close that abandoned a still
 * request stops this process from opening another host, because the
 * library's registrations survive the client and a delivery that arrives
 * later would land in the next one.  A test that abandons a request cannot
 * therefore run here without breaking every test that comes after it, so it
 * runs in a child of its own and reports with its exit status.
 */
static void run_in_a_child(void (*test)(void))
{
    pid_t child;
    int status = 0;
    int failures_before;

    fflush(stdout);
    child = fork();
    if (child < 0) {
        CHECK(0);
        return;
    }
    if (child == 0) {
        /* The child reports its own failures only: it inherits the
         * parent's count, and that count is not the child's. */
        failures_before = failures;
        test();
        fflush(stdout);
        /* _exit and not exit: the child's own teardown is not this test's
         * business, and the sanitizer's exit-time check belongs to the
         * parent. */
        _exit(failures == failures_before ? 0 : 1);
    }
    (void)waitpid(child, &status, 0);
    CHECK(WIFEXITED(status) != 0);
    CHECK(WEXITSTATUS(status) == 0);
}

/*
 * A close that abandoned a request leaves nothing behind.
 *
 * The case: a still is submitted, the wait gives up, the completion arrives
 * late, and the session is closed before the result is collected.  A close
 * that did nothing would leave that result in the host, and a second session
 * on the same host would collect the first session's bytes as if they were
 * its own - with no new completion at all.
 *
 * Three things have to hold:
 *
 *   - the close frees the bytes of the result that was never collected, and
 *     clears the state that says a request is outstanding;
 *   - the host is not reused after a close that abandoned a request, because
 *     the native request cannot be shown to be cancelled: the adapter fails
 *     closed rather than guess;
 *   - and this process opens no further host either, because the library's
 *     registrations survive and a late delivery would land in the next one.
 *
 * **This test poisons the process**, so it runs last: after it, no host can
 * be opened here.
 */
static void test_a_close_that_abandoned_a_request_leaves_nothing_behind(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The still is submitted and no completion comes, so the wait gives up
     * and the request is still outstanding. */
    stub_set_deliver_still_set_in(stub, 0);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 50u, &result) ==
          CAPTURE_SESSION_TIMEOUT);

    /* The completion arrives late: the callback fills the host's waiting
     * slot with a copy of the still's first image, and the native plane is
     * released at the delivery. */
    stub_deliver_still_set_now_in(stub);
    CHECK(test_raw_copy_destination != 0);
    CHECK(stub_release_count() == 1);

    /* The session is closed before that result was collected. */
    capture_session_close(&session);

    /* The bytes of the result that was never collected are gone: a freed
     * heap block is a poisoned address, and that is what the sanitizer
     * says about this one. */
#ifdef TEST_HAS_ASAN
    CHECK(__asan_address_is_poisoned(test_raw_copy_destination) != 0);
#else
    CHECK(1);
#endif

    /* And the host is not reused: a second session on it is refused. */
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) != CAPTURE_SESSION_OK);

    /* And this process opens no further host either. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "outstanding") != 0);

    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A still request whose native send failed.
 *
 * The native send status can be -1 while the command is still queued, and a
 * later wake can transmit the queued work.  So a failed send is **not** a
 * cancelled request: the outcome is unresolved, and the request may yet be
 * answered.
 *
 * The stub reports the send error, and the still's set arrives afterwards.
 * That is the counterexample to treating the failure as "not sent": the
 * command went out even though the send said it did not.
 *
 * What has to hold then:
 *
 *   - the request is not retried;
 *   - no setting and no capture mode reaches the library any more, because
 *     a quarantined host makes no further call into it;
 *   - the quarantined host hands out no result, so the unresolved outcome is
 *     not published as if it were an answer;
 *   - this process opens no further host;
 *   - the cleanup still works, and it frees what the late delivery left.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static void test_a_failed_still_send_quarantines_the_host(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    capture_session_error error;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    /* The native send fails, and the command is still queued. */
    stub_set_still_send_fails_in(stub, 1);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The send fails, and the session says the submission failed. */
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_BACKEND_ERROR);

    /* The library was asked once: the request was not retried, and nothing
     * else reached the library. */
    CHECK(stub_still_count() == 1);
    CHECK(stub_setting_write_count() == 0);
    CHECK(stub_capture_mode_write_count() == 0);

    /* A setting is refused before it reaches the library. */
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    CHECK(stub_setting_write_count() == 0);
    CHECK(stub_last_usr_id() == -1);

    /* And so is the capture mode. */
    CHECK(capture_library_host_set_capture_mode(host, 1u) != 0);
    CHECK(stub_capture_mode_write_count() == 0);
    CHECK(stub_last_capture_mode() == -1);

    /* And a further request is refused before it reaches the library. */
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    CHECK(stub_still_count() == 1);

    /* The late delivery: the command went out after all.  The host takes
     * the bytes, because the outcome is unresolved and the copy is this
     * component's own. */
    stub_deliver_still_set_now_in(stub);
    CHECK(test_raw_copy_destination != 0);

    /* But the quarantined host hands out no result, and the session is
     * never given one. */
    memset(&result, 0, sizeof(result));
    memset(&error, 0, sizeof(error));
    CHECK(backend.collect_image(backend.self, &result, &error) != 0);
    CHECK(error.code == 9);
    CHECK(result.sensor_data == 0);
    CHECK(image_calls == 0);

    /* This process opens no further host, and the reason says which rule
     * refused it: the process-wide one, not the rule that keeps two hosts
     * apart.  The host is still open here, so that rule would refuse the
     * open too - and the reason would say so. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "outstanding") != 0);

    /* The cleanup still works, and the bytes the late delivery left are
     * gone: a freed heap block is a poisoned address, and that is what the
     * sanitizer says about this one. */
    capture_session_close(&session);
    capture_library_host_close(host);
#ifdef TEST_HAS_ASAN
    CHECK(__asan_address_is_poisoned(test_raw_copy_destination) != 0);
#else
    CHECK(1);
#endif

    /* And the rule outlives the host: with nothing of this run left, this
     * process still opens no further host. */
    second = 0;
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "outstanding") != 0);

    dlclose(stub);
}

/*
 * A request that was refused before the library was called.
 *
 * The backend refuses a still command its own table does not hold, and that
 * refusal happens before anything is sent: the library is never asked.  So
 * nothing is outstanding and nothing is unresolved, and the host must not be
 * quarantined for it.  This process may carry on and open another host.
 *
 * The test runs in a child, because a backend that quarantined here would
 * poison the process it ran in.
 */
static void test_a_request_refused_before_the_library_is_not_quarantined(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    /* A command the library's own table does not hold. */
    config.still_command = 0x01010099u;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_BACKEND_ERROR);

    /* The library was never asked for a still. */
    CHECK(stub_still_count() == 0);

    /* And the host is not quarantined: it is closed, and this process opens
     * another host afterwards. */
    capture_session_close(&session);
    capture_library_host_close(host);
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) == 0);
    CHECK(second != 0);
    if (second != 0) {
        capture_library_host_close(second);
    }
    dlclose(stub);
}

/*
 * A setting whose native send failed.
 *
 * The setting facades share the queued-send uncertainty with the still's
 * trigger: they return 0 for a local Send return of zero, and they collapse a
 * transport failure into -1.  A transport failure does not say that nothing
 * was sent, so the outcome of the write is unresolved and the host is
 * quarantined: it makes no further mutating request, and this process
 * opens no further host.
 *
 * The stub reports the transport failure.  What has to hold:
 *
 *   - the value is not written again;
 *   - no setting, no capture mode and no still reach the library after it;
 *   - no still was issued, so nothing is outstanding: a set that arrives
 *     anyway is nobody's, and the host takes no copy of it;
 *   - the cleanup still works, and this process opens no further host.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static void test_an_uncertain_setting_send_quarantines_the_host(int facade,
                                                               int status)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The transport fails on the way out. */
    if (facade == 1) {
        /* The variable facade: the shutter. */
        stub_set_var_status_in(stub, status);
        CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_SHUTTER,
                                           0x1234u) ==
              CAPTURE_SESSION_BACKEND_ERROR);
    } else {
        /* The user facade: the ISO. */
        stub_set_usr_status_in(stub, status);
        CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO,
                                           74u) ==
              CAPTURE_SESSION_BACKEND_ERROR);
    }
    /* The library was asked once. */
    CHECK(stub_setting_call_count() == 1);

    /* The value is not written again: the write is refused before it
     * reaches the library. */
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    CHECK(stub_setting_call_count() == 1);

    /* And nothing else reaches the library either. */
    CHECK(capture_library_host_set_capture_mode(host, 1u) != 0);
    CHECK(stub_capture_mode_write_count() == 0);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    CHECK(stub_still_count() == 0);

    /* No still was issued, so nothing is outstanding: a set that arrives
     * anyway is nobody's, and the host takes no copy of it. */
    stub_deliver_still_set_now_in(stub);
    CHECK(test_raw_copy_destination == 0);

    /* The cleanup still works. */
    capture_session_close(&session);
    capture_library_host_close(host);

    /* And this process opens no further host. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "outstanding") != 0);

    dlclose(stub);
}

/* The transport failure the library collapses its transport statuses into. */
static void test_an_uncertain_user_setting_quarantines_the_host(void)
{
    test_an_uncertain_setting_send_quarantines_the_host(0, -1);
}

/* The same, on the variable facade. */
static void test_an_uncertain_variable_setting_quarantines_the_host(void)
{
    test_an_uncertain_setting_send_quarantines_the_host(1, -1);
}

/*
 * A status this component does not know.
 *
 * The library's facades collapse a transport failure into -1, and its two
 * refusals before the send are -2 and -3.  A status outside that is not
 * classified here, and it is not assumed to be a refusal that sent nothing:
 * an unknown nonzero status leaves the outcome unresolved, so the host is
 * quarantined as it is for a transport failure.
 */
static void test_an_unknown_setting_status_is_treated_as_uncertain(void)
{
    test_an_uncertain_setting_send_quarantines_the_host(0, -7);
}

/*
 * A setting the library refused before it sent anything.
 *
 * The library refuses a handle it does not know and a value or an id its
 * converter does not take, and both are decided **before** the send: nothing
 * went out, so nothing is unresolved and the host stays usable.  The host
 * takes another setting, it takes a still, and this process opens another
 * host afterwards.
 */
static void test_a_setting_refused_before_the_send_keeps_the_host_usable(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The handle is not one the library knows: it refused before the send. */
    stub_set_usr_status_in(stub, CAPTURE_LIBRARY_STATUS_INVALID_HANDLE);
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    CHECK(stub_setting_call_count() == 1);

    /* The host is not quarantined: the next setting reaches the library. */
    stub_set_usr_status_in(stub, CAPTURE_LIBRARY_STATUS_OK);
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_OK);
    CHECK(stub_setting_call_count() == 2);
    CHECK(stub_last_usr_id() == CAPTURE_LIBRARY_SETTING_ISO);

    /* And the converter's refusal is the same: it refused before the send. */
    stub_set_usr_status_in(stub, CAPTURE_LIBRARY_STATUS_REFUSED);
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    CHECK(stub_setting_call_count() == 3);
    stub_set_usr_status_in(stub, CAPTURE_LIBRARY_STATUS_OK);
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_OK);
    CHECK(stub_setting_call_count() == 4);

    /* And the host still takes a still. */
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_OK);
    CHECK(stub_still_count() == 1);
    CHECK(image_calls == 1);

    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);
    capture_library_host_close(host);

    /* And this process opens another host: nothing was left unresolved. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) == 0);
    CHECK(second != 0);
    if (second != 0) {
        capture_library_host_close(second);
    }
    dlclose(stub);
}

/*
 * An open that failed late still says why.
 *
 * The steps that can refuse after the library is loaded - the client cannot
 * be created, the registration is refused, the topics or the notifications
 * are refused - fill the host's error and then free the host.  The reason
 * has to be copied out before that cleanup, or the caller is left with a
 * failed open and no reason at all.
 */
static void test_an_open_that_failed_late_still_says_why(void)
{
    capture_library_host *host = 0;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    stub_reset_in(stub);

    /* The client cannot be created. */
    stub_set_fail_create_in(stub, 1);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) != 0);
    CHECK(host == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "create a client") != 0);

    /* And the registration is refused. */
    stub_set_fail_create_in(stub, 0);
    stub_set_fail_register_in(stub, 1);
    host = 0;
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) != 0);
    CHECK(host == 0);
    CHECK(capture_library_host_last_error() != 0);
    CHECK(strstr(capture_library_host_last_error(), "registration") != 0);

    /* And a normal close is not an open failure: the reason is empty
     * again after an open that worked. */
    stub_set_fail_register_in(stub, 0);
    host = 0;
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    CHECK(host != 0);
    if (host != 0) {
        capture_library_host_close(host);
    }
    CHECK(capture_library_host_last_error()[0] == '\0');

    dlclose(stub);
}

/*
 * The release of the first image waits for the still's own call.
 *
 * The stub delivers the still's set from inside the still's call, and the
 * release of the first image's plane is a call into the library as well.
 * A release made from inside that call would be two calls into the library
 * at the same time.
 *
 * So the release is queued while the still's call runs, and it is made once
 * that call has returned.  The stub counts the releases that were made
 * while a call of each kind was on the stack, and that count has to be
 * zero: a release made from inside the still's call is the failure this
 * test is about.
 *
 * The samples themselves are not affected: they are copied at the delivery,
 * before the release is made, and they are the caller's.
 */
static void test_the_release_waits_for_the_still_s_own_call(void)
{
    capture_library_host *host = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    const uint8_t *bytes;
    size_t i;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);

    /* The plane was released, exactly once ... */
    CHECK(stub_release_count() == 1);
    CHECK(stub_last_release_type() == CAPTURE_LIBRARY_RELEASE_TYPE_FIRST_IMAGE);
    CHECK(stub_last_release_address() == (uint32_t)test_raw_address);
    /* ... and not while the still's own call was still on the stack. */
    CHECK(stub_release_inside_capture_count() == 0);
    CHECK(stub_release_inside_setting_count() == 0);

    /* And the samples are the still's own: they were copied at the
     * delivery, and the release does not touch this process's copy. */
    CHECK(image_calls == 1);
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);
    if (result.sensor_data != 0) {
        bytes = (const uint8_t *)result.sensor_data;
        for (i = 0; i < 64u; ++i) {
            if (bytes[i] != (uint8_t)(i + 1u)) {
                CHECK(0);
                break;
            }
        }
        CHECK(1);
    }

    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * A callback that arrives during a setting's own call.
 *
 * The library may deliver a callback from inside any call into it, and a
 * setting is one of this adapter's own calls.  So the delivery can arrive
 * while the setting is still running: the samples are copied at once, and
 * the release of the first image's plane - a call into the library as well -
 * is queued instead of being made from inside the setting.
 *
 * The release is made once the setting has returned, and it is made once.
 * The duplicate that follows is refused before its payload is read: the
 * latched copy is not replaced and the plane is not released a second time.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static void test_a_callback_during_a_setting_queues_the_release(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;
    capture_session_error error;
    capture_library_host_health health;
    const uint8_t *bytes;
    size_t i;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    capture_session_init(&session);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = CAPTURE_LIBRARY_STILL_COMMAND;
    config.on_image = on_image;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The request goes out and no completion comes with it, so the wait
     * gives up and the request is still outstanding. */
    stub_set_deliver_still_set_in(stub, 0);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 50u, &result) ==
          CAPTURE_SESSION_TIMEOUT);

    /* Now the library delivers the still's set from inside a setting's
     * call.  The setting is a mutating native call, so the release that
     * the delivery owes is queued, not made. */
    stub_set_setting_delivers_still_in(stub, 1);
    memset(&error, 0, sizeof(error));
    CHECK(backend.set_setting(backend.self, CAPTURE_SESSION_ISO, 74u,
                               &error) == 0);
    CHECK(stub_setting_call_count() == 1);
    CHECK(stub_last_usr_id() == CAPTURE_LIBRARY_SETTING_ISO);

    /* The samples were copied at the delivery ... */
    CHECK(test_raw_copy_destination != 0);
    /* ... and the plane was released exactly once, and not from inside the
     * setting's own call. */
    CHECK(stub_release_count() == 1);
    CHECK(stub_release_inside_setting_count() == 0);
    CHECK(stub_release_inside_capture_count() == 0);

    /* The result is collectable, and it is the delivery's own bytes: the
     * stub gives every delivery a different pattern, so a copy that had
     * been replaced or freed would not carry these. */
    memset(&result, 0, sizeof(result));
    memset(&error, 0, sizeof(error));
    CHECK(backend.collect_image(backend.self, &result, &error) == 1);
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);
    if (result.sensor_data != 0) {
        bytes = (const uint8_t *)result.sensor_data;
        for (i = 0; i < 64u; ++i) {
            if (bytes[i] != (uint8_t)(i + 1u)) {
                CHECK(0);
                break;
            }
        }
        CHECK(1);
    }

    /* A second delivery: a duplicate that cannot be associated with the
     * request.  It is refused before its payload is read, so the plane is
     * not released a second time. */
    stub_deliver_still_set_now_in(stub);
    CHECK(stub_release_count() == 1);
    CHECK(stub_release_inside_setting_count() == 0);

    /* And the host says that it needs recovery, and that it may not make
     * another request. */
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.recovery_required == 1);
    CHECK(health.may_mutate == 0);
    CHECK(health.release_pending == 0);
    CHECK(health.release_in_progress == 0);

    /* And this process opens no further host. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);
    CHECK(capture_library_host_last_error() != 0);

    capture_session_close(&session);
    capture_library_host_close(host);
    dlclose(stub);
}

/*
 * The backend and the body of the request the thread of its own sends.
 */
static capture_session_backend threaded_backend;
static int threaded_submit_status = -1;

/*
 * Submit a still on a thread of its own.
 *
 * The body is the one the session would have encoded: the adapter reads
 * the still's command from its first word, and the stub records the rest.
 */
static void *submit_the_still_on_another_thread(void *argument)
{
    uint8_t body[CAPTURE_SESSION_STILL_BODY_SIZE];
    capture_session_error error;

    (void)argument;
    memset(body, 0, sizeof(body));
    body[0] = (uint8_t)(CAPTURE_LIBRARY_STILL_COMMAND & 0xffu);
    body[1] = (uint8_t)((CAPTURE_LIBRARY_STILL_COMMAND >> 8) & 0xffu);
    body[2] = (uint8_t)((CAPTURE_LIBRARY_STILL_COMMAND >> 16) & 0xffu);
    body[3] = (uint8_t)((CAPTURE_LIBRARY_STILL_COMMAND >> 24) & 0xffu);
    memset(&error, 0, sizeof(error));
    threaded_submit_status =
        threaded_backend.submit_still(threaded_backend.self, body,
                                      sizeof(body), &error);
    return 0;
}

/*
 * A close while a native call is running.
 *
 * The host may be closed while one of the adapter's own native calls is
 * running: the close does not cancel the call, and nothing here shows that
 * it was cancelled.  So the close fails closed - the host is not reused, and
 * this process opens no further host - and the thread that owns the call
 * must not touch the freed host when the call returns.
 *
 * The test holds the still's call at the stub's gate, closes the host while
 * the call is in flight - after the still's set was delivered, so a release
 * is queued - and then lets the call go.  The sanitizers would report a
 * use-after-free if the thread that owns the call wrote to the host after
 * the close had freed it.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static void test_a_close_during_a_native_call_does_not_use_a_freed_host(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_error error;
    capture_library_host_health health;
    pthread_t thread;

    image_calls = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    threaded_backend = backend;
    threaded_submit_status = -1;

    /* The still's call is held at the stub's gate, with the still's set
     * already delivered. */
    stub_set_capture_gate_in(stub, 1);
    CHECK(pthread_create(&thread, 0, submit_the_still_on_another_thread, 0) ==
          0);

    /* The call is at the gate: the stub's own wait, notified by the call
     * when it enters the gate. */
    CHECK(stub_wait_for_capture_gate() == 1);

    /* The still's set was delivered while the call was running, so a
     * release is queued behind it: the host says so, and it takes no
     * further mutating request while that release is owed. */
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.release_pending == 1);
    CHECK(health.release_in_progress == 0);
    CHECK(health.may_mutate == 0);
    memset(&error, 0, sizeof(error));
    CHECK(backend.set_setting(backend.self, CAPTURE_SESSION_ISO, 74u,
                               &error) != 0);
    CHECK(error.code == 12);
    CHECK(stub_setting_call_count() == 0);

    /* The host is closed while the native call is still running. */
    capture_library_host_close(host);

    /* And the call is let go. */
    stub_set_capture_gate_in(stub, 0);
    CHECK(pthread_join(thread, 0) == 0);
    CHECK(threaded_submit_status == 0);

    /* The release that the delivery owed was still made - once, and after
     * the call had returned. */
    CHECK(stub_release_count() == 1);
    CHECK(stub_release_inside_capture_count() == 0);

    /* And the native client was not destroyed: the close happened while a
     * native call was running. */
    CHECK(stub_destroy_count() == 0);

    /* And this process opens no further host: the close happened while a
     * native call was running, so its outcome was not established. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);
    CHECK(capture_library_host_last_error() != 0);

    dlclose(stub);
}

/*
 * A setting that fails after the host was closed.
 *
 * The host may be closed while a setting's native call is running, and the
 * reason the caller gets afterwards must not point into a host that is
 * gone: the error's message is static text owned by the backend, and the
 * host's own buffer is freed with the host.
 *
 * The test holds the setting's call at the stub's gate, closes the host
 * while it runs, and then lets the call fail.  The reason is read here,
 * and the sanitizers would report a use-after-free if it pointed into the
 * freed host.
 *
 * Both facades are covered: the variable one and the user one fail
 * differently and carry different codes.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static capture_session_backend threaded_setting_backend;
static capture_session_error threaded_setting_error;
static uint32_t threaded_setting_id;
static int threaded_setting_status = -99;

static void *write_the_setting_on_another_thread(void *argument)
{
    (void)argument;
    memset(&threaded_setting_error, 0, sizeof(threaded_setting_error));
    threaded_setting_status =
        threaded_setting_backend.set_setting(threaded_setting_backend.self,
                                             threaded_setting_id, 74u,
                                             &threaded_setting_error);
    return 0;
}

static void test_a_failed_setting_after_the_host_closed_keeps_its_reason(int facade)
{
    capture_library_host *host = 0;
    pthread_t thread;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    capture_library_host_backend(host, &threaded_setting_backend);

    /* The write fails with a transport failure, so the outcome is
     * unresolved and the host is quarantined. */
    if (facade == 0) {
        stub_set_usr_status_in(stub, -1);
        threaded_setting_id = CAPTURE_SESSION_ISO;
    } else {
        stub_set_var_status_in(stub, -1);
        threaded_setting_id = CAPTURE_SESSION_SHUTTER;
    }

    /* The call is held in flight, so the host can be closed while it runs. */
    stub_set_mutating_gate_in(stub, 1);
    threaded_setting_status = -99;
    CHECK(pthread_create(&thread, 0, write_the_setting_on_another_thread, 0) ==
          0);
    /* The call is at the gate: the stub's own wait, notified by the call
     * when it enters the gate. */
    CHECK(stub_wait_for_mutating_gate() == 1);

    /* The host is closed while the native call is running. */
    capture_library_host_close(host);

    /* And the call is let go; it fails. */
    stub_set_mutating_gate_in(stub, 0);
    CHECK(pthread_join(thread, 0) == 0);
    CHECK(threaded_setting_status == -1);

    /* The reason the caller got is static text, and it is read here with
     * the host it would have come from already freed. */
    CHECK(threaded_setting_error.message != 0);
    if (threaded_setting_error.message != 0) {
        CHECK(strstr(threaded_setting_error.message, "unresolved") != 0);
    }
    if (facade == 0) {
        CHECK(threaded_setting_error.code == 3);
    } else {
        CHECK(threaded_setting_error.code == 2);
    }

    /* And the busy close did not destroy the native client. */
    CHECK(stub_destroy_count() == 0);

    dlclose(stub);
}

/* The user facade. */
static void test_a_failed_user_setting_after_the_host_closed(void)
{
    test_a_failed_setting_after_the_host_closed_keeps_its_reason(0);
}

/* The variable facade. */
static void test_a_failed_variable_setting_after_the_host_closed(void)
{
    test_a_failed_setting_after_the_host_closed_keeps_its_reason(1);
}

/*
 * The capture mode, on a verbose host, closed while the call is running.
 *
 * A verbose host prints what it does, and the print about the capture mode
 * happens after the native call has returned.  The host may be closed
 * while that call runs, so the flag that says whether to print has to be
 * read before the call, not after it.
 *
 * The test holds the mode's call at the stub's gate, closes the host while
 * it runs, and then lets the call succeed.  The sanitizers would report a
 * use-after-free if the print had read the freed host.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static int threaded_mode_status = -99;

static void *set_the_capture_mode_on_another_thread(void *argument)
{
    capture_library_host *host = (capture_library_host *)argument;

    threaded_mode_status = capture_library_host_set_capture_mode(host, 1u);
    return 0;
}

static void test_a_close_during_the_capture_mode_keeps_the_verbose_report(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    pthread_t thread;

    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    /* A verbose host: it reports what it does. */
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 1) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }

    stub_set_mutating_gate_in(stub, 1);
    threaded_mode_status = -99;
    CHECK(pthread_create(&thread, 0, set_the_capture_mode_on_another_thread,
                         (void *)host) == 0);
    /* The call is at the gate: the stub's own wait, notified by the call
     * when it enters the gate. */
    CHECK(stub_wait_for_mutating_gate() == 1);

    /* The host is closed while the native call is running. */
    capture_library_host_close(host);

    /* And the call is let go; it succeeds. */
    stub_set_mutating_gate_in(stub, 0);
    CHECK(pthread_join(thread, 0) == 0);
    CHECK(threaded_mode_status == 0);
    CHECK(stub_capture_mode_write_count() == 1);

    /* The busy close did not destroy the native client. */
    CHECK(stub_destroy_count() == 0);

    /* And this process opens no further host. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);

    dlclose(stub);
}

/*
 * A deferred release that the library refuses.
 *
 * The still's set is delivered inside the adapter's own reserved call, so
 * the release of the first image's plane is queued rather than made: the
 * plane is not released while that call is running.  When the call returns,
 * the queued release is made - and the library refuses it.
 *
 * A refused release says nothing about what the camera did with the plane,
 * so the outcome stays unresolved: the host may not mutate, this process
 * opens no further host, and the release is not tried again.  The bytes
 * that were copied before the release are the caller's, and they stay
 * usable: they are collected and released locally.
 *
 * The immediate-release failure test does not reach this branch: there the
 * release is made from the callback itself.
 *
 * The quarantine is process-wide, so the test runs in a child of its own.
 */
static void test_a_deferred_release_that_the_library_refuses(void)
{
    capture_library_host *host = 0;
    capture_library_host *second = 0;
    capture_session_backend backend;
    capture_session_image result;
    capture_session_error error;
    capture_library_host_health health;
    const uint8_t *bytes;
    pthread_t thread;
    size_t i;

    image_calls = 0;
    test_raw_copy_count = 0;
    stub = dlopen("./libstub-capture.so", RTLD_NOW | RTLD_GLOBAL);
    CHECK(stub != 0);
    if (stub == 0) {
        return;
    }
    stub_reset_in(stub);
    CHECK(capture_library_host_open(&host, "./libstub-capture.so", 0) == 0);
    if (host == 0) {
        dlclose(stub);
        return;
    }
    test_raw_pointer = stub_raw_address_in(stub);
    test_jpeg_pointer = stub_jpeg_address_in(stub);
    test_raw_address = (uint32_t)test_raw_pointer;
    test_jpeg_address = (uint32_t)test_jpeg_pointer;
    capture_library_host_set_copy(host, test_copy);
    test_raw_copy_destination = 0;

    capture_library_host_backend(host, &backend);
    threaded_backend = backend;
    threaded_submit_status = -99;

    /* The library refuses the release. */
    stub_set_release_fails_in(stub, 1);

    /* The still's call is held at the gate with the still's set already
     * delivered, so the release is queued while that call runs. */
    stub_set_capture_gate_in(stub, 1);
    CHECK(pthread_create(&thread, 0, submit_the_still_on_another_thread, 0) ==
          0);
    /* The call is at the gate: the stub's own wait, notified by the call
     * when it enters the gate. */
    CHECK(stub_wait_for_capture_gate() == 1);

    /* The samples were copied at the delivery, and the release is still
     * pending: the plane is not released while the adapter's own call
     * runs. */
    CHECK(test_raw_copy_count == 1);
    CHECK(stub_release_count() == 0);
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.release_pending == 1);
    CHECK(health.release_in_progress == 0);
    CHECK(health.release_unresolved == 0);

    /* And the call is let go: the queued release is made, and it fails. */
    stub_set_capture_gate_in(stub, 0);
    CHECK(pthread_join(thread, 0) == 0);
    CHECK(threaded_submit_status == 0);
    CHECK(stub_release_count() == 1);

    /* The outcome is unresolved: the pending release is gone, the release
     * was made once, and the host may not mutate. */
    CHECK(capture_library_host_read_health(host, &health) == 0);
    CHECK(health.release_pending == 0);
    CHECK(health.release_in_progress == 0);
    CHECK(health.release_unresolved == 1);
    CHECK(health.may_mutate == 0);

    /* The bytes that were copied before the release are the caller's, and
     * they are still good: a refused release says nothing about them. */
    memset(&result, 0, sizeof(result));
    memset(&error, 0, sizeof(error));
    CHECK(backend.collect_image(backend.self, &result, &error) == 1);
    CHECK(result.sensor_data != 0);
    CHECK(result.sensor_size_bytes == 64u);
    if (result.sensor_data != 0) {
        bytes = (const uint8_t *)result.sensor_data;
        for (i = 0; i < 64u; ++i) {
            if (bytes[i] != (uint8_t)(i + 1u)) {
                CHECK(0);
                break;
            }
        }
        CHECK(1);
    }
    /* And they are released locally. */
    CHECK(backend.release_buffer(backend.self, (void *)result.sensor_data,
                                 result.sensor_size_bytes, &error) == 0);

    /* No later request reaches the library, and the release is not tried
     * again: the payload was read once, and the plane was offered once. */
    memset(&error, 0, sizeof(error));
    CHECK(backend.set_setting(backend.self, CAPTURE_SESSION_ISO, 74u,
                               &error) != 0);
    CHECK(stub_setting_call_count() == 0);
    CHECK(stub_release_count() == 1);
    CHECK(test_raw_copy_count == 1);

    /* And this process opens no further host. */
    CHECK(capture_library_host_open(&second, "./libstub-capture.so", 0) != 0);
    CHECK(second == 0);
    CHECK(capture_library_host_last_error() != 0);

    /* The close is an idle one now: the native work is done, so the
     * client is destroyed as it is for any ordinary close. */
    capture_library_host_close(host);
    CHECK(stub_destroy_count() == 1);

    dlclose(stub);
}

int main(void)
{
    test_the_backend_takes_a_still_and_copies_the_samples();
    test_the_plain_still_uses_its_own_index();
    test_the_bracket_command_uses_its_own_index();
    test_the_set_survives_a_library_without_the_accessors();
    test_a_missing_library_is_refused();
    test_a_pathless_library_is_refused();
    test_an_open_that_failed_late_still_says_why();
    test_the_capture_mode_reaches_the_library();
    test_an_unmapped_command_is_refused();
    test_a_main_image_that_is_not_a_jpeg_is_not_offered_as_one();
    test_an_unrelated_topic_is_never_read();
    test_the_close_while_a_callback_is_running();
    test_a_request_is_refused_while_the_native_release_runs();
    test_a_host_that_finished_cleanly_is_reused();
    test_the_shutter_goes_out_with_the_camera_s_own_variable_id();
    test_the_client_registers_under_a_name_the_library_accepts();
    test_the_image_quality_goes_out_in_the_library_s_own_form();
    test_an_image_quality_that_is_not_a_position_is_refused();
    test_the_release_waits_for_the_still_s_own_call();

    /*
     * The tests that leave a still request outstanding run in a child of
     * their own.  A close that abandoned a request stops this process from
     * opening another host - the native request cannot be shown to be
     * cancelled - so a test that abandons one cannot run here without
     * breaking every test that comes after it.
     *
     * The same is true of a test that leaves a native obligation
     * unresolved: an extra result that could not be associated with the
     * request, and a release whose outcome was never established.
     */
    run_in_a_child(test_a_preview_does_not_answer_a_still);
    run_in_a_child(test_a_plain_set_carries_no_raw_image);
    run_in_a_child(test_the_latest_set_type_is_a_diagnostic);
    run_in_a_child(test_a_first_image_without_a_size_is_refused);
    run_in_a_child(test_a_close_that_abandoned_a_request_leaves_nothing_behind);
    run_in_a_child(test_a_failed_still_send_quarantines_the_host);
    run_in_a_child(test_a_failed_native_release_keeps_the_result_and_blocks_the_host);
    run_in_a_child(test_a_release_that_delivers_a_callback_does_not_stop_the_capture);
    run_in_a_child(test_a_second_eligible_set_keeps_the_first_result);
    run_in_a_child(test_a_duplicate_after_the_collection_needs_recovery);
    run_in_a_child(test_a_session_close_during_a_native_release_forbids_process_reuse);
    run_in_a_child(test_a_host_close_during_a_native_release_forbids_process_reuse);
    run_in_a_child(test_a_request_refused_before_the_library_is_not_quarantined);
    run_in_a_child(test_an_uncertain_user_setting_quarantines_the_host);
    run_in_a_child(test_an_uncertain_variable_setting_quarantines_the_host);
    run_in_a_child(test_an_unknown_setting_status_is_treated_as_uncertain);
    run_in_a_child(test_a_setting_refused_before_the_send_keeps_the_host_usable);
    run_in_a_child(test_a_callback_during_a_setting_queues_the_release);
    run_in_a_child(test_a_close_during_a_native_call_does_not_use_a_freed_host);
    run_in_a_child(test_a_failed_user_setting_after_the_host_closed);
    run_in_a_child(test_a_failed_variable_setting_after_the_host_closed);
    run_in_a_child(test_a_close_during_the_capture_mode_keeps_the_verbose_report);
    run_in_a_child(test_a_deferred_release_that_the_library_refuses);

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
