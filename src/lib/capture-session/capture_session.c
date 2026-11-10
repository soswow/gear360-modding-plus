#include "capture_session.h"

#include "capture_libc.h"

#include "still_request.h"

/*
 * A monotonic millisecond clock, for the blocking capture's timeout.  The
 * clock is only used to bound a wait, so a failure to read it leaves the
 * wait unbounded rather than guessing: the reader reports the failure, and
 * a zero is never mistaken for a time.
 */
static int now_milliseconds(uint32_t *out_ms)
{
    capture_timespec now;

    if (out_ms == 0) {
        return -1;
    }
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    *out_ms = (uint32_t)((uint64_t)now.tv_sec * 1000u +
                          (uint64_t)now.tv_nsec / 1000000u);
    return 0;
}


static void capture_session_forget_image(capture_session *session)
{
    session->image_sensor = 0;
    session->image_sensor_size = 0u;
    session->image_jpeg = 0;
    session->image_jpeg_size = 0u;
    session->image_width = 0u;
    session->image_height = 0u;
    session->image_stride_bytes = 0u;
    session->image_bits_per_sample = 0u;
}

/* Latch backend-provided layout with this completion, without re-querying it. */
static void capture_session_commit_image(capture_session *session,
                                          const capture_session_image *image)
{
    session->image_sensor = (void *)image->sensor_data;
    session->image_sensor_size = image->sensor_size_bytes;
    session->image_jpeg = (void *)image->jpeg_data;
    session->image_jpeg_size = image->jpeg_size_bytes;
    session->image_width = image->width;
    session->image_height = image->height;
    session->image_stride_bytes = image->stride_bytes;
    session->image_bits_per_sample = image->bits_per_sample;
}

/*
 * The session's own view of the completion it holds.  The notification and
 * the blocking collection both use it, so a caller sees the same metadata
 * whichever way it looks at the completion.
 */
static void capture_session_view_image(const capture_session *session,
                                        capture_session_image *image)
{
    memset(image, 0, sizeof(*image));
    image->sensor_data = (const void *)session->image_sensor;
    image->sensor_size_bytes = session->image_sensor_size;
    image->jpeg_data = (const void *)session->image_jpeg;
    image->jpeg_size_bytes = session->image_jpeg_size;
    image->width = session->image_width;
    image->height = session->image_height;
    image->stride_bytes = session->image_stride_bytes;
    image->bits_per_sample = session->image_bits_per_sample;
}

/* The value a session carries once capture_session_init has prepared it. */
#define CAPTURE_SESSION_INITIALISED 0x4350534eu

/*
 * A session is usable only if the caller prepared it with
 * capture_session_init.  A session that was never prepared is caller
 * misuse; this component does not claim to detect arbitrary unprepared
 * storage, it only refuses a session that is not open.
 */
static int capture_session_is_open(const capture_session *session)
{
    return session != 0 && session->open;
}

/*
 * The image callback runs with the session's own state held.  A callback
 * that re-enters the session would change the state the callback is being
 * given - it could release the very bytes it was handed, or submit another
 * still in the middle of a completion - so every entry point refuses to run
 * while the callback is on the stack.
 */
static int capture_session_is_reentrant(const capture_session *session)
{
    return session->in_callback;
}

/*
 * One output of a completion: either absent - a null pointer and a zero
 * size - or present, with a non-zero size.  A pointer without a size, or a
 * size without a pointer, is not a pair the session can hand over.
 */
static int capture_session_output_is_well_formed(const void *data, size_t size)
{
    if (data == 0) {
        return size == 0u;
    }
    return size != 0u;
}

/*
 * Give the held bytes back to the backend.  Each buffer is returned on its
 * own, because the backend's release takes one buffer - except when the
 * backend handed out the exact same buffer for both outputs: that is one
 * buffer held twice, and it is given back once.  A buffer whose release is
 * refused stays with the session, so the caller can try again instead of
 * losing the bytes silently.
 */
static capture_session_status capture_session_return_buffers(capture_session *session)
{
    capture_session_error error;

    if (session->image_sensor != 0) {
        memset(&error, 0, sizeof(error));
        if (session->backend.release_buffer != 0) {
            if (session->backend.release_buffer(session->backend.self,
                                                session->image_sensor,
                                                session->image_sensor_size,
                                                &error) != 0) {
                return CAPTURE_SESSION_BACKEND_ERROR;
            }
        }
        if (session->image_jpeg == session->image_sensor) {
            /* One buffer, held twice: it goes back once. */
            session->image_jpeg = 0;
            session->image_jpeg_size = 0u;
        }
        session->image_sensor = 0;
        session->image_sensor_size = 0u;
    }
    if (session->image_jpeg != 0) {
        memset(&error, 0, sizeof(error));
        if (session->backend.release_buffer != 0) {
            if (session->backend.release_buffer(session->backend.self,
                                                session->image_jpeg,
                                                session->image_jpeg_size,
                                                &error) != 0) {
                return CAPTURE_SESSION_BACKEND_ERROR;
            }
        }
        session->image_jpeg = 0;
        session->image_jpeg_size = 0u;
    }
    return CAPTURE_SESSION_OK;
}

/*
 * The resources of a completion the session rejected.  The session never
 * used them, but the backend is the only one that can reclaim them, so
 * they are handed back when the session is closed.  A buffer the backend
 * handed out twice is one buffer: it is recorded, and given back, once.
 */
static void capture_session_remember_rejected(capture_session *session,
                                               const void *sensor,
                                               size_t sensor_size,
                                               const void *jpeg,
                                               size_t jpeg_size)
{
    session->rejected_sensor = 0;
    session->rejected_sensor_size = 0u;
    session->rejected_jpeg = 0;
    session->rejected_jpeg_size = 0u;

    if (sensor != 0 && sensor_size != 0u) {
        session->rejected_sensor = (void *)sensor;
        session->rejected_sensor_size = sensor_size;
    }
    if (jpeg != 0 && jpeg_size != 0u && jpeg != sensor) {
        session->rejected_jpeg = (void *)jpeg;
        session->rejected_jpeg_size = jpeg_size;
    }
}

/* Give the rejected resources back to the backend. */
static void capture_session_return_rejected(capture_session *session)
{
    capture_session_error error;

    if (session->rejected_sensor != 0) {
        memset(&error, 0, sizeof(error));
        if (session->backend.release_buffer != 0) {
            (void)session->backend.release_buffer(session->backend.self,
                                                  session->rejected_sensor,
                                                  session->rejected_sensor_size,
                                                  &error);
        }
        session->rejected_sensor = 0;
        session->rejected_sensor_size = 0u;
    }
    if (session->rejected_jpeg != 0) {
        memset(&error, 0, sizeof(error));
        if (session->backend.release_buffer != 0) {
            (void)session->backend.release_buffer(session->backend.self,
                                                  session->rejected_jpeg,
                                                  session->rejected_jpeg_size,
                                                  &error);
        }
        session->rejected_jpeg = 0;
        session->rejected_jpeg_size = 0u;
    }
}

/* The close itself: the bytes go back, then the transport goes away. */
static void capture_session_close_now(capture_session *session)
{
    session->close_pending = 0;
    if (!session->open) {
        return;
    }
    /*
     * The bytes the session still holds are returned before the transport
     * goes away.  A backend that refuses the release loses them with the
     * transport, which is the least bad outcome: the alternative is to keep
     * a closed session holding bytes nobody can reach.
     */
    (void)capture_session_return_buffers(session);
    /* So are the resources of a completion the session rejected: the
     * backend is the only one that can reclaim them. */
    capture_session_return_rejected(session);
    if (session->backend_open && session->backend.close != 0) {
        session->backend.close(session->backend.self);
    }
    session->backend_open = 0;
    session->open = 0;
    session->in_flight = 0;
    session->completion_fault = 0;
    capture_session_forget_image(session);
}

void capture_session_init(capture_session *session)
{
    if (session == 0) {
        return;
    }
    /*
     * The session is written, not read: this may be called on fresh
     * caller storage.  Calling it on an open session, or from inside the
     * image callback, is caller misuse - close the session first.
     */
    memset(session, 0, sizeof(*session));
    session->initialised = CAPTURE_SESSION_INITIALISED;
}

capture_session_status capture_session_open(capture_session *session,
                                             const capture_session_config *config)
{
    capture_session_error error;
    capture_session_status status;

    if (session == 0 || config == 0 || config->backend == 0) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    if (session->initialised != CAPTURE_SESSION_INITIALISED) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    if (session->open) {
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (capture_session_is_reentrant(session)) {
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (config->backend->open == 0 || config->backend->submit_still == 0) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    /*
     * Exactly one collector.  A backend that gives both of them leaves the
     * session with two ways to collect a completion and no rule for
     * choosing between them; a backend that gives neither leaves it with
     * no way to collect one at all.  Both are refused here, before the
     * transport is opened: nothing runs, and nothing has to be closed
     * again afterwards.
     */
    if ((config->backend->collect_completion != 0) ==
        (config->backend->collect_image != 0)) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }

    memset(session, 0, sizeof(*session));
    session->initialised = CAPTURE_SESSION_INITIALISED;
    session->backend = *config->backend;
    session->still_command = config->still_command;
    session->remaining_shots = config->remaining_shots;
    session->free_space_bytes = config->free_space_bytes;
    session->on_image = config->on_image;
    session->user_data = config->user_data;

    memset(&error, 0, sizeof(error));
    if (session->backend.open(session->backend.self, &error) != 0) {
        /*
         * A failed open leaves nothing running: the backend is closed
         * again, and its close has to tolerate a backend that opened only
         * partly and to clean up whatever it did open.  The session stays
         * closed and the caller may open it again.
         */
        if (session->backend.close != 0) {
            session->backend.close(session->backend.self);
        }
        session->backend_open = 0;
        session->open = 0;
        return CAPTURE_SESSION_BACKEND_ERROR;
    }
    session->backend_open = 1;
    session->open = 1;
    status = CAPTURE_SESSION_OK;
    return status;
}

void capture_session_close(capture_session *session)
{
    if (session == 0) {
        return;
    }
    if (capture_session_is_reentrant(session)) {
        /*
         * The close is requested from inside the callback.  It is carried
         * out as soon as the callback returns, so the transport is not torn
         * down under the callback's own feet.
         */
        session->close_pending = 1;
        return;
    }
    capture_session_close_now(session);
}

capture_session_status capture_session_set_setting(capture_session *session,
                                                    uint32_t native_id,
                                                    uint32_t value)
{
    capture_session_error error;

    if (!capture_session_is_open(session)) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    if (capture_session_is_reentrant(session)) {
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->in_flight) {
        /* A setting written now would affect the still that is in flight.
         * The session does not queue a change for a later still, so the
         * change is refused until the completion has been collected. */
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->completion_fault) {
        /* The session is latched after a malformed completion: it
         * configures nothing until it is closed. */
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->backend.set_setting == 0) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    memset(&error, 0, sizeof(error));
    if (session->backend.set_setting(session->backend.self, native_id, value,
                                     &error) != 0) {
        return CAPTURE_SESSION_BACKEND_ERROR;
    }
    return CAPTURE_SESSION_OK;
}

capture_session_status capture_session_set_exposure(capture_session *session,
                                                    uint32_t iso,
                                                    uint32_t shutter)
{
    capture_session_status status;

    if (!capture_session_is_open(session)) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    if (capture_session_is_reentrant(session)) {
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->in_flight) {
        /* A setting written now would affect the still that is in flight. */
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->completion_fault) {
        /* The session is latched after a malformed completion. */
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (iso != 0u) {
        status = capture_session_set_setting(session, CAPTURE_SESSION_ISO, iso);
        if (status != CAPTURE_SESSION_OK) {
            return status;
        }
    }
    if (shutter != 0u) {
        status = capture_session_set_setting(session, CAPTURE_SESSION_SHUTTER, shutter);
        if (status != CAPTURE_SESSION_OK) {
            return status;
        }
    }
    return CAPTURE_SESSION_OK;
}

capture_session_status capture_session_set_sensor_retention(capture_session *session,
                                                           int keep)
{
    capture_session_error error;

    if (!capture_session_is_open(session)) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    if (capture_session_is_reentrant(session)) {
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->in_flight) {
        /* The retention switch would affect the still that is in flight. */
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->completion_fault) {
        /* The session is latched after a malformed completion. */
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->backend.set_sensor_retention == 0) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    memset(&error, 0, sizeof(error));
    if (session->backend.set_sensor_retention(session->backend.self, keep,
                                               &error) != 0) {
        return CAPTURE_SESSION_BACKEND_ERROR;
    }
    session->retain_sensor = keep ? 1 : 0;
    return CAPTURE_SESSION_OK;
}

capture_session_status capture_session_trigger_still(capture_session *session)
{
    still_request_options options;
    still_request_storage_info storage;
    uint8_t body[CAPTURE_SESSION_STILL_BODY_SIZE];
    capture_session_error error;

    if (!capture_session_is_open(session)) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    if (capture_session_is_reentrant(session)) {
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->in_flight) {
        /* One still at a time: the completion of the previous one has to be
         * collected before the next one is submitted, because the camera
         * reuses the capture's block. */
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->completion_fault) {
        /* The session is latched after a malformed completion: it submits
         * nothing until it is closed, so a rejected completion cannot be
         * followed by a duplicate still. */
        return CAPTURE_SESSION_BAD_STATE;
    }
    /* Held results survive until explicit caller release or close.  Refuse
     * before any backend release or submission can invalidate their bytes. */
    if (session->image_sensor != 0 || session->image_jpeg != 0) {
        return CAPTURE_SESSION_BAD_STATE;
    }

    memset(&options, 0, sizeof(options));
    options.command = session->still_command;
    options.cap = STILL_REQUEST_CAP_DEFAULT;
    options.max = STILL_REQUEST_MAX_DEFAULT;
    storage.remaining_count = session->remaining_shots;
    storage.free_space_bytes = session->free_space_bytes;
    if (still_request_encode_ex(body, sizeof(body), &storage, &options) != 0) {
        return CAPTURE_SESSION_REFUSED;
    }

    memset(&error, 0, sizeof(error));
    if (session->backend.submit_still(session->backend.self, body, sizeof(body),
                                      &error) != 0) {
        return CAPTURE_SESSION_BACKEND_ERROR;
    }
    session->in_flight = 1;
    return CAPTURE_SESSION_OK;
}

capture_session_status capture_session_poll(capture_session *session)
{
    void *sensor = 0;
    size_t sensor_size = 0u;
    void *jpeg = 0;
    size_t jpeg_size = 0u;
    capture_session_error error;
    capture_session_image image;
    int collected;

    if (!capture_session_is_open(session)) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    if (capture_session_is_reentrant(session)) {
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->completion_fault) {
        /* The session is latched after a malformed completion.  Nothing is
         * collected: a repeated poll must not consume a queued late
         * result, and the backend keeps what it still holds until the
         * session is closed. */
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (!session->in_flight) {
        return CAPTURE_SESSION_OK;
    }

    memset(&error, 0, sizeof(error));
    memset(&image, 0, sizeof(image));
    if (session->backend.collect_image != 0) {
        /*
         * The extended collector: the bytes of the completion and the
         * geometry the backend states for them come out of one call, so
         * the two belong to the same completion.  Nothing is asked of the
         * backend about the geometry again - not for the notification,
         * not for a later look at the completion and not for the release.
         */
        collected = session->backend.collect_image(session->backend.self,
                                                   &image, &error);
        if (collected < 0) {
            return CAPTURE_SESSION_BACKEND_ERROR;
        }
        if (collected == 0) {
            return CAPTURE_SESSION_OK;
        }
        sensor = (void *)image.sensor_data;
        sensor_size = image.sensor_size_bytes;
        jpeg = (void *)image.jpeg_data;
        jpeg_size = image.jpeg_size_bytes;
    } else {
        collected = session->backend.collect_completion(session->backend.self,
                                                        &sensor, &sensor_size,
                                                        &jpeg, &jpeg_size,
                                                        &error);
        if (collected < 0) {
            return CAPTURE_SESSION_BACKEND_ERROR;
        }
        if (collected == 0) {
            return CAPTURE_SESSION_OK;
        }
        /*
         * The collector that carries no geometry: the samples' geometry
         * stays unknown, and the four geometry fields stay zero rather
         * than guessed.
         */
        image.sensor_data = (const void *)sensor;
        image.sensor_size_bytes = sensor_size;
        image.jpeg_data = (const void *)jpeg;
        image.jpeg_size_bytes = jpeg_size;
    }

    /*
     * The completion is checked before anything is committed.  A pair that
     * is not a pair, a completion that carries neither output, and the
     * same pointer handed out with two different sizes are malformed.
     * Nothing is committed: the count does not move, the callback is not
     * fired, and the bytes are not accepted - they stay the backend's,
     * and the backend is given them back when the session is closed.  The
     * geometry is not committed either: a rejected completion leaves no
     * metadata behind.
     *
     * The session latches: it submits nothing, configures nothing and
     * collects nothing until it is closed.  Latching is not a
     * resynchronisation - the session does not try to work out which
     * still the rejected completion was for.
     */
    if (!capture_session_output_is_well_formed(sensor, sensor_size) ||
        !capture_session_output_is_well_formed(jpeg, jpeg_size) ||
        (sensor == 0 && jpeg == 0) ||
        (sensor != 0 && sensor == jpeg && sensor_size != jpeg_size)) {
        session->in_flight = 0;
        session->completion_fault = 1;
        capture_session_remember_rejected(session, sensor, sensor_size,
                                          jpeg, jpeg_size);
        return CAPTURE_SESSION_MALFORMED_COMPLETION;
    }

    session->in_flight = 0;
    session->completed += 1u;
    capture_session_commit_image(session, &image);

    if (session->on_image != 0) {
        capture_session_view_image(session, &image);
        session->in_callback = 1;
        session->on_image(session, &image, session->user_data);
        session->in_callback = 0;
    }

    if (session->close_pending) {
        /* The callback asked for the close; it is carried out now, and the
         * bytes it was given are handed back with it. */
        capture_session_close_now(session);
        return CAPTURE_SESSION_BAD_STATE;
    }
    return CAPTURE_SESSION_OK;
}

capture_session_status capture_session_capture(capture_session *session,
                                               uint32_t timeout_ms,
                                               capture_session_image *out_image)
{
    capture_session_status status;
    uint32_t started = 0u;
    uint32_t waited;
    uint32_t now;

    if (!capture_session_is_open(session)) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    if (capture_session_is_reentrant(session)) {
        return CAPTURE_SESSION_BAD_STATE;
    }

    /*
     * A bounded wait is measured on the clock, so the clock is read before
     * anything is submitted: a clock that cannot be read means nothing is
     * sent at all.
     */
    if (timeout_ms != 0u) {
        if (now_milliseconds(&started) != 0) {
            return CAPTURE_SESSION_CLOCK_ERROR;
        }
    }

    status = capture_session_trigger_still(session);
    if (status != CAPTURE_SESSION_OK) {
        /*
         * The request was refused, so there is no result and nothing to
         * report: the caller's structure is left as it was, and a result
         * the session still holds - the one that made the trigger
         * impossible - stays exactly as the caller received it.
         */
        return status;
    }

    if (out_image != 0) {
        memset(out_image, 0, sizeof(*out_image));
    }

    /* The wait is bounded by the caller's timeout.  The poll does the
     * waiting, so a backend that blocks in collect_completion blocks here
     * too: the timeout is a bound on the session's own loop, not on the
     * backend. */
    for (;;) {
        status = capture_session_poll(session);
        if (status != CAPTURE_SESSION_OK) {
            return status;
        }
        if (!session->open) {
            /* The session was closed while the completion was delivered. */
            return CAPTURE_SESSION_BAD_STATE;
        }
        if (!session->in_flight) {
            /* The completion arrived. */
            if (out_image != 0) {
                /*
                 * The same committed metadata the callback was given for
                 * this completion: the bytes, the sizes and the geometry
                 * all come out of the session's own copy.
                 */
                capture_session_view_image(session, out_image);
            }
            return CAPTURE_SESSION_OK;
        }
        if (timeout_ms == 0u) {
            /* No bound was asked for: one non-blocking poll, and the still
             * is still in flight.  The caller polls for it. */
            return CAPTURE_SESSION_TIMEOUT;
        }
        if (now_milliseconds(&now) != 0) {
            /* The wait can no longer be bounded.  The still stays in
             * flight: nothing was cancelled and nothing completed. */
            return CAPTURE_SESSION_CLOCK_ERROR;
        }
        /* Unsigned 32-bit arithmetic: the difference is correct across the
         * counter's wrap, for any wait shorter than the counter's range. */
        waited = now - started;
        if (waited >= timeout_ms) {
            /* The still is still in flight at the camera: the camera has
             * no cancel for it.  The caller can keep polling for it. */
            return CAPTURE_SESSION_TIMEOUT;
        }
    }
}

capture_session_status capture_session_release_image(capture_session *session)
{
    if (!capture_session_is_open(session)) {
        return CAPTURE_SESSION_INVALID_ARGUMENT;
    }
    if (capture_session_is_reentrant(session)) {
        return CAPTURE_SESSION_BAD_STATE;
    }
    if (session->image_sensor == 0 && session->image_jpeg == 0) {
        return CAPTURE_SESSION_OK;
    }
    return capture_session_return_buffers(session);
}

uint32_t capture_session_completed_count(const capture_session *session)
{
    if (session == 0) {
        return 0u;
    }
    return session->completed;
}
