/*
 * Host tests for the capture session.
 *
 * The fake backend records what the session asked of it, so the tests can
 * check the exact bytes the session would put on the wire, the order of the
 * calls, and the state machine - with no camera, no device and no network.
 *
 * The tests state what the session owes the caller - the bytes it hands
 * back, what it refuses, and what it does with a completion - rather than
 * repeating what the implementation happens to do.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "capture_session.h"
#include "still_request.h"

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

/* ------------------------------------------------------------------ */
/* A synthetic monotonic clock.                                        */
/* ------------------------------------------------------------------ */

/*
 * The timeout paths are measured on the monotonic clock, so the tests
 * replace it with one they control.  That makes the waits deterministic -
 * the tests do not depend on how fast the machine is - and it makes a clock
 * failure reproducible exactly where a test wants it.
 *
 * `clock_armed` says whether the clock fails at all; `clock_allow` is how
 * many readings still succeed before it does.
 */
static int clock_armed;
static int clock_allow;
static uint32_t clock_millis;

int clock_gettime(clockid_t clock_id, struct timespec *out)
{
    (void)clock_id;
    if (clock_armed && clock_allow <= 0) {
        return -1;
    }
    if (clock_armed) {
        clock_allow -= 1;
    }
    clock_millis += 1u;
    out->tv_sec = (time_t)(clock_millis / 1000u);
    out->tv_nsec = (long)((clock_millis % 1000u) * 1000000u);
    return 0;
}

static void clock_reset(void)
{
    clock_armed = 0;
    clock_allow = 0;
}

/* ------------------------------------------------------------------ */
/* The fake backend.                                                  */
/* ------------------------------------------------------------------ */

#define FAKE_MAX_SETTINGS 8
#define FAKE_MAX_STILLS 4

typedef struct fake_setting {
    uint32_t native_id;
    uint32_t value;
} fake_setting;

typedef struct fake_backend {
    int open_calls;
    int close_calls;
    int fail_open;
    int open_allocates_then_fails;
    int partial_resource;
    int fail_set;
    int fail_submit;
    int fail_collect;
    int fail_release;
    int fail_release_jpeg;
    int retention;
    fake_setting settings[FAKE_MAX_SETTINGS];
    int setting_count;
    uint8_t bodies[FAKE_MAX_STILLS][CAPTURE_SESSION_STILL_BODY_SIZE];
    size_t body_sizes[FAKE_MAX_STILLS];
    int still_count;
    int pending_completion;
    void *pending_sensor;
    size_t pending_sensor_size;
    void *pending_jpeg;
    size_t pending_jpeg_size;
    int release_calls;
    void *released[4];
    size_t released_sizes[4];
    /* How often the backend was asked for a completion. */
    int collect_calls;
    /* How often it was asked through the extended collector. */
    int collect_image_calls;
    /* The geometry the extended collector hands out with a completion. */
    uint32_t pending_width;
    uint32_t pending_height;
    uint32_t pending_stride;
    uint32_t pending_bits;
    /* The extended collector changes its own geometry right after it has
     * handed a completion out.  A session that asked for the geometry
     * again instead of latching it would see these changed values. */
    int change_geometry_after_collect;
    /* A buffer the backend really owns: it is freed when it is released,
     * so a second release of it is a real double free. */
    void *owned_buffer;
    size_t owned_buffer_size;
    int owned_frees;
} fake_backend;

static int fake_open(void *self, capture_session_error *error)
{
    fake_backend *fake = (fake_backend *)self;
    fake->open_calls += 1;
    if (fake->open_allocates_then_fails) {
        /* The backend opened something and then failed: its close has to
         * be able to undo that. */
        fake->partial_resource = 1;
        if (error != 0) {
            error->code = 7;
            error->message = "open refused";
        }
        return -1;
    }
    if (fake->fail_open) {
        if (error != 0) {
            error->code = 7;
            error->message = "open refused";
        }
        return -1;
    }
    return 0;
}

static int fake_set_setting(void *self, uint32_t native_id, uint32_t value,
                             capture_session_error *error)
{
    fake_backend *fake = (fake_backend *)self;
    if (fake->fail_set) {
        if (error != 0) {
            error->code = 9;
            error->message = "set refused";
        }
        return -1;
    }
    if (fake->setting_count < FAKE_MAX_SETTINGS) {
        fake->settings[fake->setting_count].native_id = native_id;
        fake->settings[fake->setting_count].value = value;
        fake->setting_count += 1;
    }
    return 0;
}

static int fake_set_retention(void *self, int keep, capture_session_error *error)
{
    fake_backend *fake = (fake_backend *)self;
    (void)error;
    fake->retention = keep;
    return 0;
}

static int fake_submit(void *self, const uint8_t *body, size_t body_size,
                       capture_session_error *error)
{
    fake_backend *fake = (fake_backend *)self;
    if (fake->fail_submit) {
        if (error != 0) {
            error->code = 11;
            error->message = "submit refused";
        }
        return -1;
    }
    if (fake->still_count >= FAKE_MAX_STILLS) {
        return -1;
    }
    memcpy(fake->bodies[fake->still_count], body, body_size);
    fake->body_sizes[fake->still_count] = body_size;
    fake->still_count += 1;
    return 0;
}

static int fake_collect(void *self, void **out_sensor, size_t *out_sensor_size,
                        void **out_jpeg, size_t *out_jpeg_size,
                        capture_session_error *error)
{
    fake_backend *fake = (fake_backend *)self;
    if (fake->fail_collect) {
        if (error != 0) {
            error->code = 13;
            error->message = "collect failed";
        }
        return -1;
    }
    fake->collect_calls += 1;
    if (!fake->pending_completion) {
        return 0;
    }
    fake->pending_completion = 0;
    *out_sensor = fake->pending_sensor;
    *out_sensor_size = fake->pending_sensor_size;
    *out_jpeg = fake->pending_jpeg;
    *out_jpeg_size = fake->pending_jpeg_size;
    return 1;
}

/*
 * The extended collector: the bytes of the completion and the geometry of
 * those samples come out of one call, so the two belong to the same
 * completion.
 */
static int fake_collect_image(void *self, capture_session_image *out,
                              capture_session_error *error)
{
    fake_backend *fake = (fake_backend *)self;
    if (fake->fail_collect) {
        if (error != 0) {
            error->code = 13;
            error->message = "collect failed";
        }
        return -1;
    }
    fake->collect_image_calls += 1;
    if (!fake->pending_completion) {
        return 0;
    }
    fake->pending_completion = 0;
    out->sensor_data = fake->pending_sensor;
    out->sensor_size_bytes = fake->pending_sensor_size;
    out->jpeg_data = fake->pending_jpeg;
    out->jpeg_size_bytes = fake->pending_jpeg_size;
    out->width = fake->pending_width;
    out->height = fake->pending_height;
    out->stride_bytes = fake->pending_stride;
    out->bits_per_sample = fake->pending_bits;
    if (fake->change_geometry_after_collect) {
        /* The backend's own state moves on as soon as the completion has
         * been handed out.  A session that read the geometry again later
         * would see these values instead of the ones it was given. */
        fake->pending_width = 1u;
        fake->pending_height = 2u;
        fake->pending_stride = 3u;
        fake->pending_bits = 4u;
    }
    return 1;
}

static int fake_release(void *self, void *buffer, size_t size,
                        capture_session_error *error)
{
    fake_backend *fake = (fake_backend *)self;
    (void)error;
    if (fake->fail_release) {
        return -1;
    }
    if (fake->fail_release_jpeg && buffer == fake->pending_jpeg) {
        return -1;
    }
    if (fake->release_calls < 4) {
        fake->released[fake->release_calls] = buffer;
        fake->released_sizes[fake->release_calls] = size;
    }
    fake->release_calls += 1;
    if (fake->owned_buffer != 0 && buffer == fake->owned_buffer) {
        /* The backend's own storage goes back.  A second release of the
         * same buffer is a real double free, and the sanitizer says so. */
        fake->owned_frees += 1;
        free(fake->owned_buffer);
    }
    return 0;
}

static void fake_close(void *self)
{
    fake_backend *fake = (fake_backend *)self;
    fake->close_calls += 1;
    /* The rollback of whatever a failed open had opened. */
    fake->partial_resource = 0;
}

static void fake_backend_init(fake_backend *fake,
                               capture_session_backend *backend)
{
    memset(fake, 0, sizeof(*fake));
    /*
     * The session copies the backend structure whole and reads every field
     * of it, so the structure has to start out zeroed: a field left unset
     * would otherwise be whatever the storage happened to hold.
     */
    memset(backend, 0, sizeof(*backend));
    backend->open = fake_open;
    backend->set_setting = fake_set_setting;
    backend->set_sensor_retention = fake_set_retention;
    backend->submit_still = fake_submit;
    backend->collect_completion = fake_collect;
    backend->release_buffer = fake_release;
    backend->close = fake_close;
    backend->self = fake;
}

/* The same backend with the extended collector instead of the old one. */
static void fake_backend_init_extended(fake_backend *fake,
                                        capture_session_backend *backend)
{
    fake_backend_init(fake, backend);
    backend->collect_completion = 0;
    backend->collect_image = fake_collect_image;
}

/* ------------------------------------------------------------------ */
/* The tests.                                                          */
/* ------------------------------------------------------------------ */

static int image_calls;
static size_t image_sensor_size;
static size_t image_jpeg_size;
static int image_had_sensor;
static int image_had_jpeg;
/* The geometry the callback was handed. */
static uint32_t image_width;
static uint32_t image_height;
static uint32_t image_stride;
static uint32_t image_bits;

/* What the callback may try to do. */
static int callback_reenter;
static int callback_close;

static void on_image(capture_session *session, const capture_session_image *image,
                     void *user_data)
{
    (void)user_data;
    image_calls += 1;
    image_sensor_size = image->sensor_size_bytes;
    image_jpeg_size = image->jpeg_size_bytes;
    image_had_sensor = image->sensor_data != 0;
    image_had_jpeg = image->jpeg_data != 0;
    image_width = image->width;
    image_height = image->height;
    image_stride = image->stride_bytes;
    image_bits = image->bits_per_sample;

    if (callback_close) {
        /* The callback asks for the close.  It is not carried out inside
         * the callback; it is carried out when the callback returns. */
        capture_session_close(session);
    }

    if (callback_reenter) {
        /* The callback tries to use the session it was called with.  Every
         * one of those calls has to be refused, and the refusal has to
         * leave the session and its bytes alone. */
        CHECK(capture_session_release_image(session) ==
              CAPTURE_SESSION_BAD_STATE);
        CHECK(capture_session_trigger_still(session) ==
              CAPTURE_SESSION_BAD_STATE);
        CHECK(capture_session_poll(session) == CAPTURE_SESSION_BAD_STATE);
        CHECK(capture_session_set_setting(session, 1u, 1u) ==
              CAPTURE_SESSION_BAD_STATE);
        CHECK(capture_session_set_exposure(session, 74u, 0u) ==
              CAPTURE_SESSION_BAD_STATE);
        CHECK(capture_session_set_sensor_retention(session, 1) ==
              CAPTURE_SESSION_BAD_STATE);
        CHECK(capture_session_capture(session, 10u, 0) ==
              CAPTURE_SESSION_BAD_STATE);
    }
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    return (uint64_t)read_u32(bytes) | ((uint64_t)read_u32(bytes + 4) << 32);
}

/*
 * The body the stock application was observed to send on 23 October 2026:
 * command 0x01010016, cap 1, max 1, remaining 2448 at +0x14 and free space
 * 7477198848 at +0x18, everything else zero.  The session must build
 * exactly these bytes.
 */
static void test_still_body_matches_the_stock_frame(void)
{
    static const uint8_t expected[CAPTURE_SESSION_STILL_BODY_SIZE] = {
        0x16, 0x00, 0x01, 0x01,  /* +0x00 command */
        0x01, 0x00, 0x00, 0x00,  /* +0x04 cap */
        0x01, 0x00, 0x00, 0x00,  /* +0x08 max */
        0x00, 0x00, 0x00, 0x00,  /* +0x0c */
        0x00, 0x00, 0x00, 0x00,  /* +0x10 */
        0x90, 0x09, 0x00, 0x00,  /* +0x14 remaining 2448 */
        0x00, 0x00, 0xad, 0xbd,  /* +0x18 free space, low */
        0x01, 0x00, 0x00, 0x00,  /* +0x1c free space, high */
        0x00, 0x00, 0x00, 0x00,  /* +0x20 */
        0x00, 0x00, 0x00, 0x00,  /* +0x24 */
        0x00, 0x00, 0x00, 0x00,  /* +0x28 */
        0x00, 0x00, 0x00, 0x00   /* +0x2c */
    };
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.still_command = STILL_REQUEST_COMMAND_SHARED_PATH;
    config.remaining_shots = 2448u;
    config.free_space_bytes = 7477198848ull;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.still_count == 1);
    CHECK(fake.body_sizes[0] == CAPTURE_SESSION_STILL_BODY_SIZE);
    CHECK(memcmp(fake.bodies[0], expected, sizeof(expected)) == 0);

    /* And the fields are readable where the stock frame had them. */
    CHECK(read_u32(fake.bodies[0] + 0x00) == 0x01010016u);
    CHECK(read_u32(fake.bodies[0] + 0x04) == 1u);
    CHECK(read_u32(fake.bodies[0] + 0x08) == 1u);
    CHECK(read_u32(fake.bodies[0] + 0x14) == 2448u);
    CHECK(read_u64(fake.bodies[0] + 0x18) == 7477198848ull);

    capture_session_close(&session);
    CHECK(fake.close_calls == 1);
}

static void test_failed_open_leaves_nothing_running(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    fake.fail_open = 1;
    memset(&config, 0, sizeof(config));
    config.backend = &backend;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_BACKEND_ERROR);
    /*
     * A failed open leaves nothing running, and the session is the one
     * that says so: the backend is closed again, so a backend that opened
     * partly can undo it.
     */
    CHECK(fake.close_calls == 1);
    CHECK(fake.open_calls == 1);
    /* The session is closed, and it refuses work as a closed session. */
    CHECK(capture_session_set_setting(&session, 5u, 74u) ==
          CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_INVALID_ARGUMENT);
    /* And it can be opened again once the backend is willing. */
    fake.fail_open = 0;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    CHECK(fake.open_calls == 2);
    capture_session_close(&session);
    CHECK(fake.close_calls == 2);
}

static void test_a_partly_opened_backend_is_rolled_back(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    fake.open_allocates_then_fails = 1;
    memset(&config, 0, sizeof(config));
    config.backend = &backend;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_BACKEND_ERROR);
    /* The backend had opened something; the close rolled it back, and the
     * session is closed. */
    CHECK(fake.close_calls == 1);
    CHECK(fake.partial_resource == 0);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_INVALID_ARGUMENT);
}

static void test_exposure_order_and_zero_means_leave_alone(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_set_exposure(&session, 74u, 0x1234u) == CAPTURE_SESSION_OK);
    CHECK(fake.setting_count == 2);
    CHECK(fake.settings[0].native_id == CAPTURE_SESSION_ISO);
    CHECK(fake.settings[0].value == 74u);
    CHECK(fake.settings[1].native_id == CAPTURE_SESSION_SHUTTER);
    CHECK(fake.settings[1].value == 0x1234u);

    /* A zero leaves that one alone. */
    fake.setting_count = 0;
    CHECK(capture_session_set_exposure(&session, 0u, 0x5678u) == CAPTURE_SESSION_OK);
    CHECK(fake.setting_count == 1);
    CHECK(fake.settings[0].native_id == CAPTURE_SESSION_SHUTTER);

    capture_session_close(&session);
}

static void test_one_still_at_a_time(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    /* A second still before the first completes is refused: the camera
     * reuses the capture's block. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.still_count == 1);

    capture_session_close(&session);
}

static void test_settings_are_refused_while_a_still_is_in_flight(void)
{
    static uint8_t sensor[64] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);

    /* A change now would affect the still that is in flight, and the
     * session does not queue it for a later one. */
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(capture_session_set_exposure(&session, 74u, 0x1234u) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(capture_session_set_sensor_retention(&session, 1) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.setting_count == 0);
    CHECK(fake.retention == 0);

    /* Once the completion has been collected, the change is allowed. */
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_OK);
    CHECK(fake.setting_count == 1);

    capture_session_close(&session);
}

static void test_completion_reaches_the_caller_with_both_outputs(void)
{
    static uint8_t sensor[64] = {0};
    static uint8_t jpeg[32] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    image_calls = 0;
    image_had_sensor = 0;
    image_had_jpeg = 0;

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    /* Nothing has completed yet: a poll is not a failure. */
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 0);
    CHECK(capture_session_completed_count(&session) == 0u);

    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_jpeg = jpeg;
    fake.pending_jpeg_size = sizeof(jpeg);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);
    CHECK(image_had_sensor == 1);
    CHECK(image_had_jpeg == 1);
    CHECK(image_sensor_size == sizeof(sensor));
    CHECK(image_jpeg_size == sizeof(jpeg));
    CHECK(capture_session_completed_count(&session) == 1u);

    /* The caller still holds this result, so the next still is refused:
     * nothing is released, nothing is submitted, and the count does not
     * move. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.release_calls == 0);
    CHECK(fake.still_count == 1);
    CHECK(capture_session_completed_count(&session) == 1u);

    /* The caller releases the result explicitly, and then the next still
     * goes through: both buffers go back, because a completion carries
     * two of them. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 2);
    CHECK(fake.released[0] == (void *)sensor);
    CHECK(fake.released[1] == (void *)jpeg);
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.still_count == 2);

    capture_session_close(&session);
}

static void test_release_returns_both_buffers(void)
{
    static uint8_t sensor[64] = {0};
    static uint8_t jpeg[32] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_jpeg = jpeg;
    fake.pending_jpeg_size = sizeof(jpeg);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);

    /* One release gives both buffers back, each on its own. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 2);
    CHECK(fake.released[0] == (void *)sensor);
    CHECK(fake.released_sizes[0] == sizeof(sensor));
    CHECK(fake.released[1] == (void *)jpeg);
    CHECK(fake.released_sizes[1] == sizeof(jpeg));

    /* A second release has nothing to give back. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 2);

    capture_session_close(&session);
}

static void test_a_refused_release_keeps_the_bytes_and_stops_the_next_shot(void)
{
    static uint8_t sensor[64] = {0};
    static uint8_t jpeg[32] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_jpeg = jpeg;
    fake.pending_jpeg_size = sizeof(jpeg);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);

    /* The backend refuses the JPEG's release. */
    fake.fail_release_jpeg = 1;
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_BACKEND_ERROR);
    /* The sensor samples went back; the JPEG did not, so it is still held. */
    CHECK(fake.release_calls == 1);
    CHECK(fake.released[0] == (void *)sensor);

    /* The refused buffer is still the caller's, and the session still
     * holds it: a shot may not be submitted over bytes the backend still
     * holds.  The refusal releases nothing and submits nothing. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.release_calls == 1);
    CHECK(fake.still_count == 1);
    CHECK(capture_session_completed_count(&session) == 1u);
    /* The refused buffer is still the session's, and still described: the
     * samples went back, the JPEG did not. */
    CHECK(session.image_sensor == 0);
    CHECK(session.image_sensor_size == 0u);
    CHECK(session.image_jpeg == (void *)jpeg);
    CHECK(session.image_jpeg_size == sizeof(jpeg));

    /* The refused buffer can be given back on a second attempt. */
    fake.fail_release_jpeg = 0;
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 2);
    CHECK(fake.released[1] == (void *)jpeg);
    /* And only then is the next shot allowed. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.still_count == 2);

    capture_session_close(&session);
}

static void test_close_gives_the_held_bytes_back(void)
{
    static uint8_t sensor[64] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);

    /* Closing gives the bytes back before the transport goes away. */
    capture_session_close(&session);
    CHECK(fake.release_calls == 1);
    CHECK(fake.released[0] == (void *)sensor);
    CHECK(fake.close_calls == 1);

    /* And the closed session refuses to release again: there is nothing
     * left to release. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_INVALID_ARGUMENT);
}

static void test_the_callback_cannot_reenter_the_session(void)
{
    static uint8_t sensor[64] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    image_calls = 0;
    callback_reenter = 1;

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);

    /* The callback's own re-entrant calls were refused above; the
     * completion itself still stands. */
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);
    CHECK(fake.release_calls == 0);
    CHECK(fake.still_count == 1);
    CHECK(capture_session_completed_count(&session) == 1u);

    /* And the session still holds the bytes the callback was given. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 1);

    callback_reenter = 0;
    capture_session_close(&session);
}

static void test_a_close_from_the_callback_closes_after_it(void)
{
    static uint8_t sensor[64] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    image_calls = 0;
    callback_close = 1;

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);

    /* The callback asks for the close: the transport is not torn down
     * under the callback's own feet, and the close is carried out when
     * the callback returns. */
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(image_calls == 1);
    CHECK(fake.close_calls == 1);
    /* The bytes the callback was given went back with the transport. */
    CHECK(fake.release_calls == 1);
    CHECK(fake.released[0] == (void *)sensor);

    callback_close = 0;
}

static void test_a_late_completion_is_still_delivered(void)
{
    static uint8_t sensor[128] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    clock_reset();
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* Nothing completes in time. */
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 20u, &result) == CAPTURE_SESSION_TIMEOUT);
    CHECK(result.sensor_data == 0);
    CHECK(image_calls == 0);

    /* The still is still in flight, so a second capture is refused: the
     * camera has no cancel for the first one. */
    CHECK(capture_session_capture(&session, 20u, &result) == CAPTURE_SESSION_BAD_STATE);

    /* The completion arrives late and is still delivered. */
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);
    CHECK(image_sensor_size == sizeof(sensor));
    CHECK(capture_session_completed_count(&session) == 1u);

    /* The late completion's bytes are the caller's now, so they are
     * released explicitly before the next capture is asked for. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);

    /* And the session is usable again now that the still was collected
     * and its result given back. */
    fake.pending_completion = 1;
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(result.sensor_data == sensor);
    CHECK(image_calls == 2);

    capture_session_close(&session);
}

static void test_a_clock_failure_before_the_submission_sends_nothing(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    clock_reset();
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The clock cannot be read, so the wait cannot be bounded. */
    clock_armed = 1;
    clock_allow = 0;
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_CLOCK_ERROR);
    /* Nothing was submitted: the session could not measure the wait, so
     * it did not start one. */
    CHECK(fake.still_count == 0);
    CHECK(result.sensor_data == 0);
    clock_reset();

    capture_session_close(&session);
}

static void test_a_clock_failure_mid_wait_keeps_the_still_in_flight(void)
{
    static uint8_t sensor[64] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    clock_reset();
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The first reading (before the submission) succeeds; the next one
     * fails, in the middle of the wait. */
    clock_armed = 1;
    clock_allow = 1;
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_CLOCK_ERROR);
    /* The still was submitted and was neither cancelled nor completed. */
    CHECK(fake.still_count == 1);
    CHECK(image_calls == 0);
    CHECK(result.sensor_data == 0);
    CHECK(capture_session_completed_count(&session) == 0u);
    /* The still is still in flight, so the caller can poll for it. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);
    clock_reset();

    /* And the late completion is still delivered. */
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);
    CHECK(capture_session_completed_count(&session) == 1u);

    capture_session_close(&session);
}

static void test_a_zero_timeout_polls_once_and_keeps_the_still(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    clock_reset();
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* Zero means one non-blocking poll: the still is submitted, the poll
     * finds nothing, and the still stays in flight. */
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 0u, &result) == CAPTURE_SESSION_TIMEOUT);
    CHECK(fake.still_count == 1);
    CHECK(result.sensor_data == 0);
    CHECK(capture_session_completed_count(&session) == 0u);
    /* The still is still in flight. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);

    capture_session_close(&session);
}

static void test_sensor_retention_is_passed_through(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_set_sensor_retention(&session, 1) == CAPTURE_SESSION_OK);
    CHECK(fake.retention == 1);
    CHECK(capture_session_set_sensor_retention(&session, 0) == CAPTURE_SESSION_OK);
    CHECK(fake.retention == 0);

    capture_session_close(&session);
}

static void test_bad_arguments_are_refused(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;

    CHECK(capture_session_open(0, &config) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_open(&session, 0) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    /* A second open on an open session is a state error, not a crash. */
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_BAD_STATE);
    capture_session_close(&session);
    /* A closed session refuses everything. */
    CHECK(capture_session_set_setting(&session, 124u, 1u) ==
          CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_INVALID_ARGUMENT);
}

static void test_a_zeroed_session_is_refused(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;

    /*
     * Storage that was never prepared - zeroed, as a caller might leave
     * it.  This is a defined case, not a promise that arbitrary
     * unprepared storage is detected: preparing the session is the
     * caller's obligation.
     */
    memset(&session, 0, sizeof(session));
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;

    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(fake.open_calls == 0);
    CHECK(capture_session_set_setting(&session, 5u, 74u) ==
          CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_set_exposure(&session, 74u, 0u) ==
          CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_set_sensor_retention(&session, 1) ==
          CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_capture(&session, 10u, 0) ==
          CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_release_image(&session) ==
          CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(capture_session_completed_count(&session) == 0u);
    /* A close of a session that was never opened does nothing. */
    capture_session_close(&session);
    CHECK(fake.close_calls == 0);
}

static void test_a_backend_without_the_needed_entry_points_is_refused(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;

    backend.collect_completion = 0;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(fake.open_calls == 0);

    backend.collect_completion = fake_collect;
    backend.submit_still = 0;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(fake.open_calls == 0);

    backend.submit_still = fake_submit;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    capture_session_close(&session);
}

static void test_a_malformed_completion_is_discarded(void)
{
    static uint8_t sensor[64] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    image_calls = 0;
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* A size without a pointer. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = 0;
    fake.pending_sensor_size = sizeof(sensor);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_MALFORMED_COMPLETION);
    CHECK(image_calls == 0);
    CHECK(capture_session_completed_count(&session) == 0u);
    /* The bytes were not accepted, so they were not released either:
     * they stay the backend's. */
    CHECK(fake.release_calls == 0);

    /* The session is latched; closing and opening it again starts clean. */
    capture_session_close(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* A pointer without a size. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = 0u;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_MALFORMED_COMPLETION);
    CHECK(image_calls == 0);
    CHECK(capture_session_completed_count(&session) == 0u);
    CHECK(fake.release_calls == 0);

    capture_session_close(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* A completion that carries neither output. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = 0;
    fake.pending_sensor_size = 0u;
    fake.pending_jpeg = 0;
    fake.pending_jpeg_size = 0u;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_MALFORMED_COMPLETION);
    CHECK(image_calls == 0);
    CHECK(capture_session_completed_count(&session) == 0u);

    capture_session_close(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* And a well-formed completion is accepted as before. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);
    CHECK(capture_session_completed_count(&session) == 1u);

    capture_session_close(&session);
}

static void test_a_malformed_completion_latches_the_session(void)
{
    static uint8_t sensor[64] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = 0;
    fake.pending_sensor_size = sizeof(sensor);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_MALFORMED_COMPLETION);

    /* The session is latched: it submits nothing, configures nothing and
     * collects nothing, so a rejected completion cannot be followed by a
     * duplicate still. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.still_count == 1);
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(capture_session_set_exposure(&session, 74u, 0u) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(capture_session_set_sensor_retention(&session, 1) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.setting_count == 0);
    CHECK(fake.retention == 0);
    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 100u, &result) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.still_count == 1);

    /* A repeated poll does not consume a queued late result. */
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.collect_calls == 1);
    CHECK(fake.pending_completion == 1);

    /* Closing lets the backend clean up, and opening the session again
     * starts clean. */
    capture_session_close(&session);
    CHECK(fake.close_calls == 1);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.still_count == 2);
    capture_session_close(&session);
}

static void test_an_aliased_completion_is_released_once(void)
{
    uint8_t *buffer = (uint8_t *)malloc(64u);
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    CHECK(buffer != 0);
    image_calls = 0;
    fake_backend_init(&fake, &backend);
    /* The backend really owns this buffer, and really frees it when it is
     * released: a second release of it would be a double free. */
    fake.owned_buffer = buffer;
    fake.owned_buffer_size = 64u;
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    /* The backend hands out the exact same buffer for both outputs. */
    fake.pending_completion = 1;
    fake.pending_sensor = buffer;
    fake.pending_sensor_size = fake.owned_buffer_size;
    fake.pending_jpeg = buffer;
    fake.pending_jpeg_size = fake.owned_buffer_size;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);

    /* One buffer held twice is given back once. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 1);
    CHECK(fake.owned_frees == 1);
    /* And a second release has nothing to give back. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 1);
    CHECK(fake.owned_frees == 1);

    capture_session_close(&session);
}

static void test_an_aliased_completion_with_two_sizes_is_malformed(void)
{
    uint8_t *buffer = (uint8_t *)malloc(64u);
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    CHECK(buffer != 0);
    image_calls = 0;
    fake_backend_init(&fake, &backend);
    fake.owned_buffer = buffer;
    fake.owned_buffer_size = 64u;
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    /* The same pointer with two different sizes is not a pair the session
     * can hand over. */
    fake.pending_completion = 1;
    fake.pending_sensor = buffer;
    fake.pending_sensor_size = 64u;
    fake.pending_jpeg = buffer;
    fake.pending_jpeg_size = 32u;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_MALFORMED_COMPLETION);
    CHECK(image_calls == 0);
    CHECK(capture_session_completed_count(&session) == 0u);

    /* The rejected buffer goes back to the backend once, not twice. */
    capture_session_close(&session);
    CHECK(fake.owned_frees == 1);
    CHECK(fake.release_calls == 1);
}

static void test_a_jpeg_only_completion_is_accepted(void)
{
    static uint8_t jpeg[32] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    image_calls = 0;
    image_had_sensor = 0;
    image_had_jpeg = 0;
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The samples are absent and the JPEG is present: that is a pair that
     * is well formed, not a malformed completion. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = 0;
    fake.pending_sensor_size = 0u;
    fake.pending_jpeg = jpeg;
    fake.pending_jpeg_size = sizeof(jpeg);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);
    CHECK(image_had_sensor == 0);
    CHECK(image_had_jpeg == 1);
    CHECK(capture_session_completed_count(&session) == 1u);

    capture_session_close(&session);
}

static void test_backend_failures_are_reported(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    fake.fail_set = 1;
    CHECK(capture_session_set_exposure(&session, 74u, 0u) ==
          CAPTURE_SESSION_BACKEND_ERROR);
    fake.fail_set = 0;

    fake.fail_submit = 1;
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BACKEND_ERROR);
    fake.fail_submit = 0;
    /* A refused submission leaves nothing in flight. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);

    fake.fail_collect = 1;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_BACKEND_ERROR);
    fake.fail_collect = 0;

    capture_session_close(&session);
}

static void test_blocking_capture_returns_the_result(void)
{
    static uint8_t sensor[128] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    clock_reset();
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The fake completes the still on the first collect. */
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);

    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(result.sensor_data == sensor);
    CHECK(result.sensor_size_bytes == sizeof(sensor));
    CHECK(image_calls == 1);
    CHECK(capture_session_completed_count(&session) == 1u);

    capture_session_close(&session);
}

static void test_blocking_capture_times_out_without_faking_a_result(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    clock_reset();
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* Nothing ever completes. */
    memset(&result, 0, sizeof(result));
    result.sensor_data = (const void *)1;
    CHECK(capture_session_capture(&session, 20u, &result) == CAPTURE_SESSION_TIMEOUT);
    /* The result is not invented. */
    CHECK(result.sensor_data == 0);
    CHECK(result.sensor_size_bytes == 0u);
    CHECK(image_calls == 0);
    /* The still is still in flight: it was neither cancelled nor
     * completed. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(capture_session_completed_count(&session) == 0u);

    capture_session_close(&session);
}

/*
 * The extended collector hands the geometry of the samples out with the
 * completion, and the session latches it there: the notification and a
 * later look at the same completion see the same metadata, and the backend
 * is not asked about the geometry again.
 */
static void test_the_extended_collector_carries_the_geometry(void)
{
    static uint8_t sensor[64] = {0};
    static uint8_t jpeg[32] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    image_width = 0u;
    image_height = 0u;
    image_stride = 0u;
    image_bits = 0u;
    clock_reset();
    fake_backend_init_extended(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* The backend states the geometry of the samples it hands out, and
     * then moves its own state on. */
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_jpeg = jpeg;
    fake.pending_jpeg_size = sizeof(jpeg);
    fake.pending_width = 4000u;
    fake.pending_height = 3000u;
    fake.pending_stride = 8000u;
    fake.pending_bits = 14u;
    fake.change_geometry_after_collect = 1;
    fake.pending_completion = 1;

    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);

    /* The completion was collected once, and once only. */
    CHECK(fake.collect_image_calls == 1);
    CHECK(image_calls == 1);
    /* The notification carries the geometry of that completion. */
    CHECK(image_width == 4000u);
    CHECK(image_height == 3000u);
    CHECK(image_stride == 8000u);
    CHECK(image_bits == 14u);
    /* The blocking view carries the same metadata: it is the metadata
     * latched with the completion, not a second reading of the backend. */
    CHECK(result.sensor_data == sensor);
    CHECK(result.sensor_size_bytes == sizeof(sensor));
    CHECK(result.jpeg_data == jpeg);
    CHECK(result.jpeg_size_bytes == sizeof(jpeg));
    CHECK(result.width == 4000u);
    CHECK(result.height == 3000u);
    CHECK(result.stride_bytes == 8000u);
    CHECK(result.bits_per_sample == 14u);
    CHECK(result.width == image_width);
    CHECK(result.height == image_height);
    CHECK(result.stride_bytes == image_stride);
    CHECK(result.bits_per_sample == image_bits);
    /* The backend really did change its geometry after the completion was
     * handed out, so a later reading of it would have been a different
     * one. */
    CHECK(fake.pending_width == 1u);
    CHECK(fake.pending_height == 2u);
    CHECK(fake.pending_stride == 3u);
    CHECK(fake.pending_bits == 4u);

    /* Releasing the bytes asks the backend for nothing else. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.collect_image_calls == 1);

    capture_session_close(&session);
}

/*
 * The old collector carries no geometry, and the session does not guess
 * one: the geometry of such a completion is unknown, and unknown is zero.
 */
static void test_the_legacy_collector_reports_no_geometry(void)
{
    static uint8_t sensor[64] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    image_width = 0u;
    image_height = 0u;
    image_stride = 0u;
    image_bits = 0u;
    clock_reset();
    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_completion = 1;

    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);
    CHECK(fake.collect_calls == 1);
    /* The bytes are there; the geometry is not, and it is not invented. */
    CHECK(result.sensor_data == sensor);
    CHECK(image_width == 0u);
    CHECK(image_height == 0u);
    CHECK(image_stride == 0u);
    CHECK(image_bits == 0u);
    CHECK(result.width == 0u);
    CHECK(result.height == 0u);
    CHECK(result.stride_bytes == 0u);
    CHECK(result.bits_per_sample == 0u);

    capture_session_close(&session);
}

/*
 * Exactly one collector: both of them leaves the session with no rule for
 * choosing between them, and neither leaves it with no way to collect a
 * completion at all.  Both are refused before the transport is opened.
 */
static void test_exactly_one_collector_is_required(void)
{
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    fake_backend_init(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;

    /* Both collectors: the session refuses it, and the transport is never
     * opened, so there is nothing to close again either. */
    backend.collect_image = fake_collect_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(fake.open_calls == 0);
    CHECK(fake.close_calls == 0);

    /* Neither collector: there would be no way to collect a completion. */
    backend.collect_completion = 0;
    backend.collect_image = 0;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_INVALID_ARGUMENT);
    CHECK(fake.open_calls == 0);
    CHECK(fake.close_calls == 0);

    /* Exactly one: the session opens and works. */
    backend.collect_completion = fake_collect;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    CHECK(fake.open_calls == 1);
    capture_session_close(&session);
    CHECK(fake.close_calls == 1);

    /* The extended collector alone is equally acceptable. */
    backend.collect_completion = 0;
    backend.collect_image = fake_collect_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);
    CHECK(fake.open_calls == 2);
    capture_session_close(&session);
}

/*
 * A malformed extended completion is rejected and latches the session, the
 * same way a malformed completion through the old collector does.  Nothing
 * of it is committed - not the bytes, and not the geometry.
 */
static void test_a_malformed_extended_completion_latches_the_session(void)
{
    static uint8_t sensor[64] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;

    image_calls = 0;
    clock_reset();
    fake_backend_init_extended(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* A size without a pointer is not a pair the session can hand over. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = 0;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_width = 4000u;
    fake.pending_height = 3000u;
    fake.pending_stride = 8000u;
    fake.pending_bits = 14u;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_MALFORMED_COMPLETION);
    CHECK(image_calls == 0);
    CHECK(capture_session_completed_count(&session) == 0u);
    /* Nothing was committed, so nothing was released either: the bytes
     * stay the backend's. */
    CHECK(fake.release_calls == 0);

    /* The session is latched: it submits nothing, configures nothing and
     * collects nothing. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(capture_session_set_setting(&session, CAPTURE_SESSION_ISO, 74u) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(capture_session_set_exposure(&session, 74u, 0u) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(capture_session_set_sensor_retention(&session, 1) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.still_count == 1);
    CHECK(fake.setting_count == 0);

    /* Closing clears the latch, and the session opens clean again. */
    capture_session_close(&session);
    CHECK(fake.close_calls == 1);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    /* And a well-formed extended completion is accepted as before. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_width = 4000u;
    fake.pending_height = 3000u;
    fake.pending_stride = 8000u;
    fake.pending_bits = 14u;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);
    CHECK(image_width == 4000u);
    CHECK(image_height == 3000u);
    CHECK(image_stride == 8000u);
    CHECK(image_bits == 14u);
    CHECK(capture_session_completed_count(&session) == 1u);

    capture_session_close(&session);
}

/*
 * A malformed extended tuple that carries a buffer hands that buffer back
 * to the backend once, and it is the backend's to reclaim.
 */
static void test_a_malformed_extended_tuple_gives_the_buffer_back_once(void)
{
    uint8_t *buffer = (uint8_t *)malloc(64u);
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_init(&session);

    CHECK(buffer != 0);
    image_calls = 0;
    fake_backend_init_extended(&fake, &backend);
    fake.owned_buffer = buffer;
    fake.owned_buffer_size = 64u;
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    /* The same pointer with two different sizes is not a pair the session
     * can hand over. */
    fake.pending_completion = 1;
    fake.pending_sensor = buffer;
    fake.pending_sensor_size = 64u;
    fake.pending_jpeg = buffer;
    fake.pending_jpeg_size = 32u;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_MALFORMED_COMPLETION);
    CHECK(image_calls == 0);
    CHECK(capture_session_completed_count(&session) == 0u);

    /* The rejected buffer goes back to the backend once, not twice. */
    capture_session_close(&session);
    CHECK(fake.owned_frees == 1);
    CHECK(fake.release_calls == 1);
}

/*
 * The extended completion takes part in the established ownership rules:
 * the caller releases the previous one's bytes explicitly before the next
 * still is asked for, and the geometry belongs to the completion rather
 * than to the session.
 */
static void test_the_extended_completion_releases_and_resubmits(void)
{
    static uint8_t sensor[64] = {0};
    static uint8_t jpeg[32] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;

    image_calls = 0;
    clock_reset();
    fake_backend_init_extended(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_jpeg = jpeg;
    fake.pending_jpeg_size = sizeof(jpeg);
    fake.pending_width = 4000u;
    fake.pending_height = 3000u;
    fake.pending_stride = 8000u;
    fake.pending_bits = 14u;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);

    /* The caller still holds this result, so the next still is refused:
     * nothing is released and nothing is submitted. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);
    CHECK(fake.release_calls == 0);
    CHECK(fake.still_count == 1);
    CHECK(capture_session_completed_count(&session) == 1u);

    /* The caller releases the result explicitly, and then the next still
     * goes through: both buffers go back, because a completion carries
     * two of them. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 2);
    CHECK(fake.released[0] == (void *)sensor);
    CHECK(fake.released[1] == (void *)jpeg);
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.still_count == 2);

    /* The geometry belongs to the completion, not to the session: the
     * second completion carries its own. */
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_jpeg = jpeg;
    fake.pending_jpeg_size = sizeof(jpeg);
    fake.pending_width = 1000u;
    fake.pending_height = 800u;
    fake.pending_stride = 2000u;
    fake.pending_bits = 12u;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 2);
    CHECK(image_width == 1000u);
    CHECK(image_height == 800u);
    CHECK(image_stride == 2000u);
    CHECK(image_bits == 12u);

    capture_session_close(&session);
}

/*
 * The explicit-release rule on the asynchronous path: a completion the
 * caller has not released blocks the next trigger, and the refusal changes
 * nothing - nothing is released, nothing is submitted, the count does not
 * move, and the held bytes and their metadata are untouched.
 */
static void test_a_trigger_is_refused_while_the_result_is_held(void)
{
    static uint8_t sensor[64] = {0};
    static uint8_t jpeg[32] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;

    image_calls = 0;
    image_width = 0u;
    image_height = 0u;
    image_stride = 0u;
    image_bits = 0u;
    clock_reset();
    fake_backend_init_extended(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    fake.pending_completion = 1;
    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_jpeg = jpeg;
    fake.pending_jpeg_size = sizeof(jpeg);
    fake.pending_width = 4000u;
    fake.pending_height = 3000u;
    fake.pending_stride = 8000u;
    fake.pending_bits = 14u;
    CHECK(capture_session_poll(&session) == CAPTURE_SESSION_OK);
    CHECK(image_calls == 1);

    /* What the caller holds before the refusal: the session's own fields
     * carry the bytes and the metadata committed with them. */
    CHECK(session.image_sensor == (void *)sensor);
    CHECK(session.image_sensor_size == sizeof(sensor));
    CHECK(session.image_jpeg == (void *)jpeg);
    CHECK(session.image_jpeg_size == sizeof(jpeg));
    CHECK(session.image_width == 4000u);
    CHECK(session.image_height == 3000u);
    CHECK(session.image_stride_bytes == 8000u);
    CHECK(session.image_bits_per_sample == 14u);

    /* The caller still holds the result, so the next still is refused. */
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_BAD_STATE);
    /* The refusal releases nothing and submits nothing. */
    CHECK(fake.release_calls == 0);
    CHECK(fake.still_count == 1);
    /* The completed count does not move, and the callback does not fire
     * again. */
    CHECK(capture_session_completed_count(&session) == 1u);
    CHECK(image_calls == 1);

    /* The bytes and the metadata committed with them are untouched: the
     * same bytes, the same sizes and the same geometry are still held. */
    CHECK(session.image_sensor == (void *)sensor);
    CHECK(session.image_sensor_size == sizeof(sensor));
    CHECK(session.image_jpeg == (void *)jpeg);
    CHECK(session.image_jpeg_size == sizeof(jpeg));
    CHECK(session.image_width == 4000u);
    CHECK(session.image_height == 3000u);
    CHECK(session.image_stride_bytes == 8000u);
    CHECK(session.image_bits_per_sample == 14u);

    /* An explicit release gives the bytes back, and only then does the
     * next still go through. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 2);
    CHECK(fake.released[0] == (void *)sensor);
    CHECK(fake.released[1] == (void *)jpeg);
    CHECK(session.image_sensor == 0);
    CHECK(session.image_jpeg == 0);
    CHECK(capture_session_trigger_still(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.still_count == 2);

    capture_session_close(&session);
}

/*
 * The explicit-release rule on the blocking path: the caller holds the
 * result of a blocking capture, so the next blocking capture is refused,
 * and the result the caller already has is left exactly as it was.
 */
static void test_a_blocking_capture_is_refused_while_the_result_is_held(void)
{
    static uint8_t sensor[64] = {0};
    static uint8_t jpeg[32] = {0};
    fake_backend fake;
    capture_session_backend backend;
    capture_session_config config;
    capture_session session;
    capture_session_image result;

    image_calls = 0;
    clock_reset();
    fake_backend_init_extended(&fake, &backend);
    memset(&config, 0, sizeof(config));
    config.backend = &backend;
    config.on_image = on_image;
    capture_session_init(&session);
    CHECK(capture_session_open(&session, &config) == CAPTURE_SESSION_OK);

    fake.pending_sensor = sensor;
    fake.pending_sensor_size = sizeof(sensor);
    fake.pending_jpeg = jpeg;
    fake.pending_jpeg_size = sizeof(jpeg);
    fake.pending_width = 4000u;
    fake.pending_height = 3000u;
    fake.pending_stride = 8000u;
    fake.pending_bits = 14u;
    fake.pending_completion = 1;

    memset(&result, 0, sizeof(result));
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(result.sensor_data == (const void *)sensor);
    CHECK(result.sensor_size_bytes == sizeof(sensor));
    CHECK(result.jpeg_data == (const void *)jpeg);
    CHECK(result.jpeg_size_bytes == sizeof(jpeg));
    CHECK(result.width == 4000u);
    CHECK(result.bits_per_sample == 14u);
    CHECK(image_calls == 1);
    CHECK(fake.still_count == 1);
    CHECK(capture_session_completed_count(&session) == 1u);

    /* The caller still holds that result, so the next blocking capture is
     * refused - and the result the caller already has is left alone. */
    CHECK(capture_session_capture(&session, 1000u, &result) ==
          CAPTURE_SESSION_BAD_STATE);
    CHECK(result.sensor_data == (const void *)sensor);
    CHECK(result.sensor_size_bytes == sizeof(sensor));
    CHECK(result.jpeg_data == (const void *)jpeg);
    CHECK(result.jpeg_size_bytes == sizeof(jpeg));
    CHECK(result.width == 4000u);
    CHECK(result.height == 3000u);
    CHECK(result.stride_bytes == 8000u);
    CHECK(result.bits_per_sample == 14u);
    /* The refusal released nothing, submitted nothing and did not move the
     * count. */
    CHECK(fake.release_calls == 0);
    CHECK(fake.still_count == 1);
    CHECK(capture_session_completed_count(&session) == 1u);
    CHECK(image_calls == 1);

    /* The session still holds exactly that result. */
    CHECK(session.image_sensor == (void *)sensor);
    CHECK(session.image_sensor_size == sizeof(sensor));
    CHECK(session.image_jpeg == (void *)jpeg);
    CHECK(session.image_jpeg_size == sizeof(jpeg));
    CHECK(session.image_width == 4000u);
    CHECK(session.image_height == 3000u);
    CHECK(session.image_stride_bytes == 8000u);
    CHECK(session.image_bits_per_sample == 14u);

    /* An explicit release gives the bytes back, and the next blocking
     * capture goes through. */
    CHECK(capture_session_release_image(&session) == CAPTURE_SESSION_OK);
    CHECK(fake.release_calls == 2);
    fake.pending_completion = 1;
    CHECK(capture_session_capture(&session, 1000u, &result) == CAPTURE_SESSION_OK);
    CHECK(fake.still_count == 2);
    CHECK(image_calls == 2);

    capture_session_close(&session);
}

int main(void)
{
    clock_reset();
    test_still_body_matches_the_stock_frame();
    test_failed_open_leaves_nothing_running();
    test_a_partly_opened_backend_is_rolled_back();
    test_exposure_order_and_zero_means_leave_alone();
    test_one_still_at_a_time();
    test_settings_are_refused_while_a_still_is_in_flight();
    test_completion_reaches_the_caller_with_both_outputs();
    test_release_returns_both_buffers();
    test_a_refused_release_keeps_the_bytes_and_stops_the_next_shot();
    test_close_gives_the_held_bytes_back();
    test_the_callback_cannot_reenter_the_session();
    test_a_close_from_the_callback_closes_after_it();
    test_a_late_completion_is_still_delivered();
    test_a_clock_failure_before_the_submission_sends_nothing();
    test_a_clock_failure_mid_wait_keeps_the_still_in_flight();
    test_a_zero_timeout_polls_once_and_keeps_the_still();
    test_sensor_retention_is_passed_through();
    test_bad_arguments_are_refused();
    test_a_zeroed_session_is_refused();
    test_a_backend_without_the_needed_entry_points_is_refused();
    test_a_malformed_completion_is_discarded();
    test_a_malformed_completion_latches_the_session();
    test_an_aliased_completion_is_released_once();
    test_an_aliased_completion_with_two_sizes_is_malformed();
    test_a_jpeg_only_completion_is_accepted();
    test_backend_failures_are_reported();
    test_blocking_capture_returns_the_result();
    test_blocking_capture_times_out_without_faking_a_result();
    test_the_extended_collector_carries_the_geometry();
    test_the_legacy_collector_reports_no_geometry();
    test_exactly_one_collector_is_required();
    test_a_malformed_extended_completion_latches_the_session();
    test_a_malformed_extended_tuple_gives_the_buffer_back_once();
    test_the_extended_completion_releases_and_resubmits();
    test_a_trigger_is_refused_while_the_result_is_held();
    test_a_blocking_capture_is_refused_while_the_result_is_held();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
