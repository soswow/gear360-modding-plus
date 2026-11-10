#ifndef CAPTURE_SESSION_H
#define CAPTURE_SESSION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable policy core for one caller-owned capture session. The phase2 goal
 * is our process hosting the stock capture library with the stock app absent,
 * retaining sensor RAW and optional stock JPEG from the same software trigger.
 * This core has no device I/O; a real backend still has to provide that path.
 *
 * The 48-byte request and encoded-set publication route are known statically
 * (submit.forty-eight-byte-request, completion.normal-graph-publishes-encoded-set).
 * submit.stock-control-frame-historical-20261023 describes a historical request,
 * not a completion. Earlier timeouts establish neither remote execution stage
 * nor their cause. A stock-app driver is diagnostic phase1 work, not this goal.
 *
 * The backend owns setting-domain translation, initialization, request
 * association, sensor provenance, layout, synchronization and buffer lifetime.
 * The core passes through backend assertions; synthetic tests do not verify
 * sensor samples or a native capture backend. Calls require caller serialization;
 * collecting metadata together does not make this core thread-safe. Public
 * structs grow in this revision; callers and backends must be rebuilt together.
 */

typedef struct capture_session capture_session;


typedef enum capture_session_status {
    CAPTURE_SESSION_OK = 0,
    /* A null pointer, or a session that is not open. */
    CAPTURE_SESSION_INVALID_ARGUMENT = 1,
    /* The backend refused an operation; see the backend's own report. */
    CAPTURE_SESSION_BACKEND_ERROR = 2,
    /* The session is already open, or already has a capture in flight, or
     * it is latched after a malformed completion, or it still holds a
     * result the caller has not released. */
    CAPTURE_SESSION_BAD_STATE = 3,
    /* The request was refused locally, before anything was sent. */
    CAPTURE_SESSION_REFUSED = 4,
    /* The completion could not be understood. */
    CAPTURE_SESSION_MALFORMED_COMPLETION = 5,
    /* The backend reported a completion, but not for the still we sent. */
    CAPTURE_SESSION_UNASSOCIATED_COMPLETION = 6,
    /* The session has no room left for another capture in flight. */
    CAPTURE_SESSION_NO_CAPACITY = 7,
    /* The wait for the completion exceeded the caller's bound.  The still
     * is still in flight; it was neither cancelled nor completed. */
    CAPTURE_SESSION_TIMEOUT = 8,
    /* The monotonic clock could not be read, so the wait could not be
     * bounded.  The still is still in flight, if it was submitted. */
    CAPTURE_SESSION_CLOCK_ERROR = 9
} capture_session_status;

/*
 * A setting in the camera's own settings store, addressed by its *native*
 * id - the id the camera's settings tool uses, not the Linux-side wire id.
 * The two numbering systems differ and the backend owns the translation.
 */
typedef struct capture_session_setting {
    uint32_t native_id;
    uint32_t value;
} capture_session_setting;

/* The native ids this component knows by name. */
#define CAPTURE_SESSION_ISO UINT32_C(5)
#define CAPTURE_SESSION_SHUTTER UINT32_C(0x101)
/*
 * The image quality.
 *
 * The camera's own user-data id for the image quality.  Its value is the
 * camera's own position, 0 to 7, or the native tagged value that names the
 * same position; the backend owns the translation into the form the
 * library takes.
 */
#define CAPTURE_SESSION_IMAGE_QUALITY UINT32_C(4)
#define CAPTURE_SESSION_DRIVE UINT32_C(10)
#define CAPTURE_SESSION_SHOOTING_MODE UINT32_C(1)
#define CAPTURE_SESSION_RAW_QUALITY UINT32_C(122)
#define CAPTURE_SESSION_RAW_SAVE UINT32_C(123)
#define CAPTURE_SESSION_SSIF_RAW UINT32_C(124)
#define CAPTURE_SESSION_RAW_BIT UINT32_C(125)
#define CAPTURE_SESSION_RAW_PACK UINT32_C(126)

/*
 * What the caller receives for one completed still.
 *
 * `sensor_data` is the sensor's own samples for this still, as the backend
 * obtained them.  The geometry fields describe how the backend says those
 * bytes are laid out; a backend that cannot state the geometry must leave
 * them zero rather than guess.  The JPEG, when present, is the camera's own
 * processed output for the same still.
 *
 * The geometry is the backend's assertion, not something the session
 * verified against the bytes:
 *
 *   - `width`, `height` and `stride_bytes` are what the backend states
 *     about the samples it just delivered.  The session does not check
 *     them, and a state that does not fit the samples is not caught here;
 *   - `bits_per_sample` is the number of *significant* bits in a sample.  It
 *     says nothing about how the samples are stored: not the packing, not
 *     the bit order, not whether the samples are contiguous or how the
 *     padding is arranged.  A fourteen-bit sample is fourteen significant
 *     bits; this field does not say how those fourteen bits are laid out
 *     in the buffer.
 *
 * All four fields are zero when the geometry is unknown.  Zero means
 * "unknown", not "a sample of zero width".
 *
 * Lifetime: the bytes belong to the backend and stay valid until the
 * caller releases them with capture_session_release_image, or until the
 * session is closed.  A release is per buffer: an output that was given
 * back is the backend's again, and a buffer whose release the backend
 * refuses stays with the session.  A later capture does not take the
 * remaining bytes back: the next trigger is refused while the caller
 * still holds them.  A caller that needs the bytes longer must copy
 * them.
 *
 * Both outputs are returned to the backend by one release call: the
 * samples and the JPEG are two buffers, and the backend is given each of
 * them.  The one exception the seam allows is the exact same pointer with
 * the same size for both: that is one buffer held twice, and it is given
 * back once.
 */
typedef struct capture_session_image {
    const void *sensor_data;
    size_t sensor_size_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t bits_per_sample;
    /* The JPEG of the same still, when the caller asked for it. */
    const void *jpeg_data;
    size_t jpeg_size_bytes;
} capture_session_image;

typedef void (*capture_session_image_callback)(capture_session *session,
                                               const capture_session_image *image,
                                               void *user_data);

/* A backend error, filled in by the backend. */
typedef struct capture_session_error {
    int code;                /* backend-specific, 0 when there was none */
    const char *message;     /* static text, owned by the backend */
} capture_session_error;

/*
 * The transport seam.
 *
 * Every function returns 0 on success and nonzero on failure, and fills
 * `error` when it fails.  The session never interprets a backend's error
 * code; it only reports that the operation failed.
 *
 * The session copies this structure whole and then reads every field of
 * it.  A backend therefore has to zero the structure before it fills the
 * fields in: a field left unset would otherwise be whatever the storage
 * happened to hold, and the session would call it.
 */
typedef struct capture_session_backend {
    /* Open the transport and prepare a capture context. */
    int (*open)(void *self, capture_session_error *error);
    /* Write one value into the camera's settings store, by native id. */
    int (*set_setting)(void *self, uint32_t native_id, uint32_t value,
                       capture_session_error *error);
    /* Ask the camera to keep the sensor data of the next still. */
    int (*set_sensor_retention)(void *self, int keep,
                                capture_session_error *error);
    /* Submit one still.  `body` is the 48-byte request body. */
    int (*submit_still)(void *self, const uint8_t *body, size_t body_size,
                        capture_session_error *error);
    /*
     * Collect the next completion, if there is one.  Returns 1 when a
     * completion was produced, 0 when there was nothing to collect and -1
     * on a transport failure.  On 1, `out_sensor`/`out_sensor_size` and
     * `out_jpeg`/`out_jpeg_size` receive the bytes of that still; the
     * backend owns them.
     *
     * This is the collector that carries no geometry: a completion taken
     * through it has unknown geometry, and the session reports zeros.
     * A backend that can state the geometry of the samples it delivers
     * uses collect_image instead - see below.  Exactly one of the two
     * collectors has to be given: this one or collect_image, never both
     * and never neither.
     *
     * The two outputs are separate backend-owned buffers, and each is given
     * back on its own.  One exception is allowed and is defined here: the
     * backend may hand out the *exact same* pointer and the same size for
     * both outputs.  That is one buffer, held twice, and it is released
     * once.  The same pointer with two different sizes is not a pair the
     * session can hand over, and it is treated as a malformed completion.
     *
     * Partially overlapping or interior pointers are outside this seam's
     * contract: the two outputs are independent, so a backend must not
     * hand out one pointer into the middle of the other's storage.
     */
    int (*collect_completion)(void *self,
                              void **out_sensor, size_t *out_sensor_size,
                              void **out_jpeg, size_t *out_jpeg_size,
                              capture_session_error *error);
    /*
     * Give back a buffer the backend was holding, if any.  One buffer per
     * call: a completion carries the sensor samples and the JPEG as two
     * separate buffers, and each is given back on its own - unless the
     * backend handed out the same buffer for both, in which case that one
     * buffer is given back once.
     */
    int (*release_buffer)(void *self, void *buffer, size_t size,
                          capture_session_error *error);
    /*
     * Close the transport.  The session calls this after a failed open
     * attempt as well as after a successful one, so that a failed open
     * leaves nothing running:  `close` therefore has to tolerate a backend
     * that opened only partly, and to clean up whatever it did open.
     */
    void (*close)(void *self);
    void *self;
    /*
     * The backend must synchronize its own producers so all output fields
     * describe one held completion. This core does not synchronize threads.
     *
     * The extended collector: collect the next completion *and* the
     * geometry of its samples in one call, so that the geometry belongs to
     * that completion instead of to a later reading of the backend.
     *
     * The return value means the same as collect_completion's: 1 when a
     * completion was produced, 0 when there was nothing to collect and -1
     * on a transport failure.  On 1, `out` receives the bytes of that
     * still - the samples and, when there is one, the JPEG - together with
     * the geometry the backend states for those samples.  The bytes stay
     * the backend's.  The geometry is the backend's assertion, and the
     * session passes it on as it stands; it does not check it against the
     * bytes and it does not complete it.  A backend that cannot state the
     * geometry leaves those four fields zero.
     *
     * The samples and the JPEG are two separate backend-owned buffers, and
     * each is given back on its own.  The exception collect_completion
     * describes - the exact same pointer and the same size for both - is
     * allowed here too, and means one buffer held twice.
     *
     * This collector is optional, but exactly one collector has to be
     * given: this one or collect_completion, never both and never
     * neither.  A backend that gives both, or neither, is refused when
     * the session is opened - before the transport is opened at all.
     */
    int (*collect_image)(void *self, capture_session_image *out,
                         capture_session_error *error);
} capture_session_backend;

typedef struct capture_session_config {
    /*
     * The backend.  Required.  The structure has to be zeroed before its
     * fields are filled in, and it has to carry exactly one collector:
     * collect_completion or collect_image, never both and never neither.
     */
    const capture_session_backend *backend;
    /*
     * The command word of the still request.  Zero selects the library's
     * plain request (0x01010001); the device-shutter preset (0x01010016) is
     * the other supported variant.  Both attempt to set the drive mode to
     * single before they submit.
     */
    uint32_t still_command;
    /* The number of shots the card can still hold, and its free space.
     * These go into the still body.  Zero means "unknown", and zero is
     * what the body then carries: the seam has no channel through which a
     * backend could fill them in. */
    uint32_t remaining_shots;
    uint64_t free_space_bytes;
    /* Called with the image of each completed still. */
    capture_session_image_callback on_image;
    void *user_data;
} capture_session_config;

/* The still body size, in bytes. */
#define CAPTURE_SESSION_STILL_BODY_SIZE ((size_t)48u)

/*
 * Prepare a session for use.  This has to be called before
 * capture_session_open, and it is the only way a session becomes valid.
 *
 * The call writes the whole session; it reads none of it, so it may be
 * called on fresh caller storage.  Calling it on a session that is already
 * open, or from inside the image callback, is caller misuse: the session is
 * wiped and a transport that was open is stranded.  Close the session
 * first.
 *
 * A session is caller-owned storage and holds no resources of its own until
 * it is opened.  Preparing a session is the caller's obligation: this
 * component does not claim to detect storage that was never prepared.
 */
void capture_session_init(capture_session *session);

/*
 * Open a session.  The backend is opened if it has not been opened already;
 * on failure the backend is closed again, so a failed open leaves nothing
 * running.  A backend's close therefore has to tolerate a backend that
 * opened only partly.
 *
 * A backend that carries no collector, or both collectors, is refused with
 * CAPTURE_SESSION_INVALID_ARGUMENT, and the refusal happens before the
 * transport is opened: nothing runs and nothing has to be closed.
 */
capture_session_status capture_session_open(capture_session *session,
                                             const capture_session_config *config);

/*
 * Close a session.  The backend is closed.  The bytes of the last
 * completed still are handed back to the backend before the transport goes
 * away, and so are the resources of a completion the session rejected:
 * the session never used them, but the backend is the only one that can
 * reclaim them.  A backend that refuses one of those hand-backs loses it
 * with the transport.  Closing also clears a latched completion fault;
 * opening the session again starts clean.
 *
 * Any in-flight capture is abandoned: this component does not promise that
 * a submitted still will complete, and a close that waits for a completion
 * could wait for ever.
 *
 * A close requested from inside the image callback is carried out as soon
 * as the callback returns, so the transport is not torn down under the
 * callback's own feet.
 */
void capture_session_close(capture_session *session);

/*
 * Write one setting into the camera's store, by native id.  This is the
 * only way the session changes a setting; the session never changes one
 * behind the caller's back.
 *
 * A setting is refused while a capture is in flight: the change would
 * affect that still, and the session does not queue changes for a later
 * still.  Close the still's window first - collect its completion.  A
 * setting is refused for the same reason while the session is latched
 * after a malformed completion.
 */
capture_session_status capture_session_set_setting(capture_session *session,
                                                    uint32_t native_id,
                                                    uint32_t value);

/*
 * Set the exposure for the next still: the ISO value and the shutter value,
 * by native id.  Zero for either means "leave that one alone".
 *
 * Like a single setting, this is refused while a capture is in flight.
 */
capture_session_status capture_session_set_exposure(capture_session *session,
                                                    uint32_t iso,
                                                    uint32_t shutter);

/*
 * Ask the camera to keep the sensor data of the next still, so that it can
 * be read out.  When `keep` is zero the session asks the backend to stop
 * asking for it.
 *
 * Like a setting, this is refused while a capture is in flight, and it is
 * not queued for a later still.
 */
capture_session_status capture_session_set_sensor_retention(capture_session *session,
                                                           int keep);

/*
 * Submit one still.  The body is built by the session from the config's
 * command word and storage figures, and handed to the backend.
 *
 * This is a submission, not a completion.  The caller must poll with
 * capture_session_poll until the image callback has fired; a submission that
 * is never collected is not a failure of the session, but it is not a
 * capture either.
 *
 * A trigger is refused with CAPTURE_SESSION_BAD_STATE while the session
 * still holds the bytes of a previous completion - that is, while the
 * caller has not released them with capture_session_release_image.  The
 * refusal happens before anything is released and before anything is
 * submitted: the held bytes and the metadata committed with them stay the
 * caller's, unchanged. The caller must release every held output before
 * another trigger is accepted.
 */
capture_session_status capture_session_trigger_still(capture_session *session);

/*
 * Collect whatever the camera has finished.  When a completion arrives, the
 * image callback is invoked with the sensor samples and, if the backend has
 * one, the JPEG of that same still.
 *
 * The completion's metadata is committed with it: the pointers, the sizes
 * and the geometry the backend stated for the samples go in together, and
 * the callback is given that committed metadata.  The blocking return for that
 * request sees the same metadata.
 * Nothing is asked of the backend about the geometry again.
 *
 * Returns CAPTURE_SESSION_OK when the poll succeeded, whether or not
 * anything completed.
 *
 * A completion is checked before anything is committed.  Each of the two
 * outputs has to be absent (a null pointer and a zero size) or present (a
 * pointer and a non-zero size); a null pointer with a size, a pointer with
 * no size, a completion that carries neither output, and the same pointer
 * handed out with two different sizes are malformed and are reported as
 * CAPTURE_SESSION_MALFORMED_COMPLETION.
 *
 * A malformed completion latches the session.  Nothing is committed: the
 * completed count does not move, the callback is not fired, and the bytes
 * are not accepted - they stay the backend's.  While the latch is on, the
 * session submits nothing, configures nothing and collects nothing: those
 * calls are refused with CAPTURE_SESSION_BAD_STATE, so a rejected
 * completion cannot be followed by a duplicate still and a repeated poll
 * cannot consume a queued late result.  The latch is cleared by closing
 * the session, which hands the rejected resources back to the backend so
 * that it can reclaim them; opening the session again starts clean.
 *
 * The check says that the pair is well formed.  It does not say that the
 * completion belongs to this session's still, and it says nothing about
 * what the bytes are.  Latching a fault is not a resynchronisation: the
 * session does not try to work out which still the rejected completion was
 * for, and it does not reset itself.
 *
 * The callback runs with the session's state held.  A callback that calls
 * back into the session is refused with CAPTURE_SESSION_BAD_STATE, because
 * it would change the state the callback is being given - it could release
 * the very bytes it was handed, or submit another still in the middle of a
 * completion.  The one exception is capture_session_close, which is
 * carried out when the callback returns.
 */
capture_session_status capture_session_poll(capture_session *session);

/*
 * Take one still and wait for it.  This is the simple blocking call: it
 * submits, waits for the completion, and hands the caller the result.
 *
 * `timeout_ms` bounds the wait:
 *
 *   - zero means "one non-blocking poll": the still is submitted, the
 *     session polls once, and if the completion has not arrived by then the
 *     call reports CAPTURE_SESSION_TIMEOUT;
 *   - a non-zero bound is measured on the monotonic clock.  The clock is
 *     read *before* the submission, so a clock that cannot be read is
 *     reported as CAPTURE_SESSION_CLOCK_ERROR and nothing is submitted at
 *     all.  If the clock fails while the session is waiting, the call
 *     reports CAPTURE_SESSION_CLOCK_ERROR rather than guessing.
 *
 * A timeout and a clock error are not cancellations and not completions:
 * the still is still in flight at the camera - it was neither cancelled nor
 * completed - and the session keeps it that way, so the caller can keep
 * polling for it afterwards.  The camera has no cancel for a submitted
 * still, so the session does not pretend to have one.
 *
 * The result holds pointers into the backend's storage, valid until
 * capture_session_release_image or until the session is closed.  A further
 * capture does not take them back: while the caller holds this result, the
 * next capture is refused with CAPTURE_SESSION_BAD_STATE and the result
 * stays exactly as it is here - the same bytes, the same sizes and the
 * same geometry.  Its metadata is the metadata of the completion: the same
 * bytes, the same sizes and the same geometry the image callback was given
 * for that completion.
 *
 * A refused call reports the refusal and leaves `out_image` as the caller
 * left it: there is no new result to report, and the one the session still
 * holds is the caller's own.
 */
capture_session_status capture_session_capture(capture_session *session,
                                               uint32_t timeout_ms,
                                               capture_session_image *out_image);

/*
 * Release the bytes of the last completed still back to the backend.  The
 * caller calls this when it is done with the result: until it does, the
 * session holds the bytes, and the next trigger is refused with
 * CAPTURE_SESSION_BAD_STATE.  The release is the caller's, not the
 * session's: nothing else takes the bytes back, and the backend is free to
 * reuse the storage afterwards.
 *
 * Both outputs are returned: the sensor samples and the JPEG are two
 * buffers, and each is given back on its own.  A backend that refuses a
 * release keeps that buffer, and the session keeps it too, so the caller
 * can try again.  A refusal is per buffer: an output that was given back
 * successfully is gone, and the buffer the backend refused stays with the
 * session - so the next trigger stays refused with
 * CAPTURE_SESSION_BAD_STATE until that buffer has been released.
 */
capture_session_status capture_session_release_image(capture_session *session);

/* The number of stills this session has submitted and collected. */
uint32_t capture_session_completed_count(const capture_session *session);

/*
 * The session's storage.
 *
 * The session performs no allocation, so the caller owns the memory.  The
 * layout below is private: its contents may change, and the caller must not
 * read or write them.  Declaring the object by value is the simplest way for
 * both C and C++ callers to get correctly sized and aligned storage.
 */
struct capture_session {
    uint32_t initialised;
    capture_session_backend backend;
    int backend_open;
    int open;
    int in_flight;
    int retain_sensor;
    /* The image callback is on the stack: the session refuses to be
     * re-entered while it is. */
    int in_callback;
    /* A close was asked for from inside the callback; it is carried out
     * when the callback returns. */
    int close_pending;
    /* A malformed completion latched the session: nothing is submitted,
     * configured or collected until the session is closed. */
    int completion_fault;
    /* The resources of the rejected completion, handed back to the backend
     * when the session is closed. */
    void *rejected_sensor;
    size_t rejected_sensor_size;
    void *rejected_jpeg;
    size_t rejected_jpeg_size;
    uint32_t still_command;
    uint32_t remaining_shots;
    uint64_t free_space_bytes;
    capture_session_image_callback on_image;
    void *user_data;
    uint32_t completed;
    void *image_sensor;
    size_t image_sensor_size;
    void *image_jpeg;
    size_t image_jpeg_size;
    /*
     * The geometry the backend stated for the samples of the completion
     * the session holds.  It is committed with the pointers and the sizes,
     * so the notification and a later look at the completion see the same
     * metadata.  It is zero when the completion was collected through the
     * collector that carries no geometry.
     */
    uint32_t image_width;
    uint32_t image_height;
    uint32_t image_stride_bytes;
    uint32_t image_bits_per_sample;
};

#ifdef __cplusplus
}
#endif

#endif /* CAPTURE_SESSION_H */
