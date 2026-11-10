/*
 * The backend that hosts the camera's own capture library.
 *
 * **This is an experimental component, not a working camera backend.**  It
 * is host-tested against a stub of the library; nothing here has been run
 * against the camera, and the gaps are listed at the end of this file.
 *
 * The program becomes a client of the library: the library provides the
 * transport, the capture context and the still.  The stock application is
 * not involved and nothing is written to the inter-processor channel by
 * us.
 *
 * The library's interface was recovered from the pinned library with
 * Ghidra; the evidence and the reasoning are in
 * output/decompile/rea/pass-minimax-next-edge-20261023/backend-report.md.
 * The short version:
 *
 *   mm_camera_create(&handle)      creates the client and realizes it
 *   mm_camera_realize(handle)      the capture module's power-on
 *   RegCallbackClient(...)         registers the image callback
 *   mm_camera_set_usr_attributes   the settings
 *   mm_camera_capture_still(...)   the trigger
 *
 * **Where the still's result comes from.**
 *
 * The still's result arrives on the *image-set* route, not on the image
 * route.  The image topics are the liveview, the display and the movie
 * frames, and the stock photo consumer subscribes to the image-set topics
 * and never subscribes to the image topics at all.  So:
 *
 *   - the image route records what the library delivers and what the image
 *     object says about it, and copies nothing.
 *   - the image-set route carries the still.  The library hands the set
 *     over; the set's first descriptor is the firmware's own "Raw Compress"
 *     image for that still, and the set's main descriptor is the encoded
 *     still.  The backend copies both during the callback, because the
 *     library's own copy of the set lives on its stack and dies when the
 *     callback returns.
 *
 * **What the backend does not claim.**  That the first image holds the
 * sensor's own samples: the firmware calls it its "Raw Compress" image,
 * and a native name is not a provenance.  The mapping of that image into
 * the session's `sensor_data` is this component's own experimental
 * mapping, not a proven sensor output.  The descriptor states geometry
 * words, but the stride words' unit is not established, so the stride is
 * reported as zero, and the number of bits a sample occupies stays
 * unknown.
 *
 * **The gaps.**  The native request is not correlated with the delivery
 * that answers it - arrival order is a heuristic, not an identity.  The
 * bounds of the copied bytes rest on the descriptor's own size word.  The
 * release of the first image's plane is the library's own release call
 * with the type and the address the descriptor named; whether that is what
 * the native route wants is not established.  And the host is closed by
 * releasing what this process owns: the shared object stays loaded and
 * nothing here shows that the capture module was powered off.
 */
#include "capture_session_library.h"

#include "capture_libc.h"
#include "encoded_set.h"

/*
 * The library allows one camera client at a time - it refuses a second
 * with "too many camera opened" - and the callbacks it delivers are plain
 * function pointers with the library's own context, not ours.  The host
 * therefore keeps itself in one place, and the callbacks find it there.
 */
static capture_library_host *the_host;

/*
 * The live host's identity, and the counter that hands the identities out.
 *
 * A host that is closed is freed, and a freed host's address can be given to
 * the next one.  So the address alone cannot say whether the host a callback
 * is holding is still the live one: the identity can, because every open is a
 * different one.  Zero means there is no live host.
 *
 * This matters for the calls that are made **outside** the lock - the
 * release of the still's first image - because the host may be closed and
 * freed while such a call is in flight, and a host that is not there any more
 * must not be written to.
 */
static uint32_t the_host_identity;
static uint32_t next_host_identity;

/*
 * Whether this process may open another host.
 *
 * A host that is closed with a still request still outstanding poisons the
 * process: the native request cannot be shown to be cancelled, and the
 * library's registrations survive the client, so a delivery that arrives
 * later would land in the next host - and the native route cannot be asked
 * which request it belongs to.  So this process opens no further host.  A
 * fresh process is the way back, and that is the honest answer: nothing
 * here can prove that the camera is quiet.
 */
static int host_reuse_is_forbidden;

/*
 * A release of the still's first image that is **queued** rather than made.
 *
 * The release is a call into the library, and so is the adapter's own
 * mutating call - the still's submission, a setting, the capture mode.  A
 * release made from inside such a call would put two calls into the library
 * at once, so the release waits for that call to return.
 *
 * The payload lives here and not in the host: the host may be closed and
 * freed while the call is still running, and the thread that owns that call
 * then has to make the release with what was captured, without reading a
 * host that is not there any more.
 */
static int deferred_release_pending;
static capture_library_release_capture_buffer deferred_release_fn;
static uint32_t deferred_release_address;
static uint32_t deferred_release_identity;
static int deferred_release_verbose;

/*
 * Why the last open failed.
 *
 * A failed open has no host to carry the reason - the host is freed - so
 * the reason is kept here for the caller to ask for.
 */
static char last_open_error[256];

/*
 * The backend's lock.
 *
 * The camera's C library is installed without its headers, and whether its
 * pthread support is there is not known, so the lock is built from the
 * compiler's own atomic compare-and-swap and needs nothing from the C
 * library.  It is a global and it outlives every host, so a callback that
 * arrives while a host is being closed finds a lock that is still valid.
 *
 * Every callback reads the global host pointer after it has taken this
 * lock, and the close clears the pointer under the lock, so a callback
 * that starts after the close finds no host.  The host itself is freed
 * after the lock is released, so a callback that was already inside the
 * library when the close began is not covered by this: the native
 * delivery order is not known here, and this component does not guess it.
 */
static volatile int the_lock;

static void backend_lock(void)
{
    while (__sync_lock_test_and_set(&the_lock, 1) != 0) {
        /* spin */
    }
}

static void backend_unlock(void)
{
    __sync_lock_release(&the_lock);
}

/*
 * Whether the host is still the live one, under the lock.
 *
 * A host that is closed is freed, and a freed host's address can be given to
 * a new one, so the address alone cannot say whether the host a thread is
 * holding is still there.  The identity can, because every open is a
 * different one.
 */
static int host_is_live_under_lock(const capture_library_host *host,
                                   uint32_t identity)
{
    return (the_host == host && the_host_identity == identity);
}

/*
 * What one delivered image is, as the library stated it.
 *
 * The bytes and the description are published together, so a collector
 * cannot see one without the other.
 */
typedef struct capture_library_delivered {
    size_t size;
    uint32_t address;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t raw_pixel_bit;
    int format;
} capture_library_delivered;

/*
 * The still's result, as the library's own set described it.
 *
 * Two buffers: the first image - the firmware's "Raw Compress" image for
 * this still - and the main image, which is the encoded still.  They are
 * copied separately because they are separate buffers, and the session
 * hands each of them back on its own.
 */
typedef struct capture_library_still_result {
    int valid;
    uint32_t set_type;
    /* The first image. */
    void *raw_bytes;
    size_t raw_size;
    uint32_t raw_address;
    uint32_t raw_buffer_size;
    uint32_t raw_width;
    uint32_t raw_height;
    uint32_t raw_stride_x;
    uint32_t raw_stride_y;
    /* The main image, when it is a JPEG stream. */
    void *jpeg_bytes;
    size_t jpeg_size;
    uint32_t jpeg_address;
    uint32_t jpeg_buffer_size;
} capture_library_still_result;

struct capture_library_host {
    void *library;
    capture_library_mm_camera_create create;
    capture_library_mm_camera_destroy destroy;
    capture_library_mm_camera_realize realize;
    capture_library_mm_camera_start start;
    capture_library_mm_camera_stop stop;
    capture_library_mm_camera_set_usr_attributes set_usr;
    capture_library_mm_camera_set_var_attributes set_var;
    capture_library_mm_camera_set_notification_callback set_notification;
    capture_library_mm_camera_set_notification_id set_notification_id;
    capture_library_mm_camera_capture_still capture_still;
    capture_library_reg_callback_client reg_client;
    capture_library_reg_image_callback_id reg_image_id;
    capture_library_reg_image_set_callback_id reg_image_set_id;
    capture_library_release_capture_buffer release_buffer;
    /*
     * The library's own capture-mode setter.  It is optional, and it is
     * exposed under its own name rather than as a camera setting: the link
     * between the capture mode and the raw configuration is not
     * established.
     */
    capture_library_set_capture_mode_user_data set_capture_mode_user_data;
    /*
     * The library's own description of its image object.  Every one of these
     * is optional; without them the geometry stays unknown.
     */
    capture_library_image_get_u32_fn image_get_width;
    capture_library_image_get_u32_fn image_get_height;
    capture_library_image_get_u32_fn image_get_raw_stride;
    capture_library_image_get_u32_fn image_get_raw_pixel_bit;
    capture_library_image_get_u32_fn image_get_image_format;
    capture_library_image_get_buffer_fn image_get_buffer;
    capture_library_buffer_get_address_fn buffer_get_virt_address;
    capture_library_buffer_get_size_fn buffer_get_size;

    unsigned int handle;
    int client_slot;
    int opened;
    /*
     * This host's identity.  It is different for every open, so it can say
     * whether a host that a callback is holding is still the live one after
     * the address has been given to a new host.
     */
    uint32_t identity;

    /*
     * The image route's latest delivery, which nobody has taken yet.
     *
     * It is the liveview.  It is copied because the pool is reused, and it
     * is recorded because the library's own accessors describe it, but it
     * is **never** the still's completion: a preview must not answer a
     * still request.
     */
    /*
     * The still's result, waiting to be collected, and the one the session
     * has taken and still owns.
     *
     * They are separate, and that is the point: the next preview may
     * replace the waiting one, but it may never free the one the caller
     * holds.
     *
     * The waiting slot is filled once per request: the first validated
     * result is latched, and a second eligible set is refused before its
     * payload is read, so nothing replaces or frees the copy that is waiting.
     */
    capture_library_still_result waiting;
    capture_library_still_result held;
    /*
     * Image-set delivery counts used by an experimental arrival-order
     * heuristic. A delivery after submit may still belong to older work;
     * these counters cannot establish request identity.
     */
    uint32_t sets_delivered;
    uint32_t sets_at_submit;
    uint32_t images_delivered;
    int awaiting_completion;
    int completion_seen;
    /*
     * Whether the first validated result of the current request is latched.
     * The first result is the one this host keeps; a second set that cannot
     * be associated with the request does not replace it and does not make
     * the library's plane be released twice.  Cleared when a request is
     * armed, so the next request latches its own result.
     */
    int result_latched;
    /*
     * Whether this host needs recovery: an extra set arrived that could not
     * be associated with the request.  The host makes no further mutating
     * request.  This is not the `abandoned` state: the bytes that were
     * copied are still the caller's to collect and to release.
     */
    int recovery_required;
    /*
     * Whether the native release of the still's first image is in flight.
     * The release is a call into the library, made with the lock dropped
     * because the library may deliver a callback from inside it; a request
     * made in that window would race it.  This is the native call's state,
     * not the private copy's ownership.
     */
    int native_release_in_progress;
    /*
     * Whether one of this adapter's mutating native calls is reserved: the
     * admission check that lets it through and the reservation itself are
     * made in the same locked step, so a callback that arrives between the
     * check and the call cannot slip in - it finds the reservation and
     * queues its release instead of making one.
     *
     * The reservation is cleared when the call returns, in the same locked
     * step in which a queued release is claimed.
     */
    int native_mutation_reserved;
    /*
     * Whether the outcome of the native release is unresolved.  A nonzero
     * status from the library's release call is not a statement that the
     * plane was released, so the address may be released again by a later
     * call and the host is not reused.
     */
    int native_release_uncertain;
    /*
     * Set when this host was left with a native request outstanding.
     *
     * Three things put a host in this state.  A close that found a still
     * request still outstanding, a still's send that reported an error, and
     * a setting's send that reported an error: a native send status can be
     * -1 while the request is still queued, so such a failure is not a
     * statement that the request was never sent.
     *
     * In every case the native request cannot be shown to be cancelled -
     * nothing here asks the camera to drop it - and the native route cannot
     * be asked which request a later delivery belongs to.  So the host is
     * not reused after that, it makes no further mutating request, and
     * the process does not open another host either.
     */
    int abandoned;
    /* What the delivered image has to be released with. */
    int image_type;
    uint32_t image_address;
    /* And what the delivered image says about itself: the format is one
     * for raw and two for ycc, and the mask is the bits a sample
     * occupies - 0x3fff is a fourteen-bit sample. */
    int image_format;
    uint32_t image_raw_mask;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t image_stride;
    uint32_t image_raw_pixel_bit;
    /* And what the image object's own buffer says about itself, next to
     * what the delivered block says. */
    uint32_t image_buffer_address;
    uint32_t image_buffer_size;
    int image_set_event;
    uint32_t image_set_address;
    uint32_t image_set_size;

    int heard_from_the_camera;
    int last_notification;
    uint32_t last_notification_data;
    uint32_t last_notification_size;
    uint32_t notifications_seen;
    /* Latest decoded set type; protected by backend_lock, not request-associated. */
    uint32_t last_set_type;

    capture_library_copy_fn copy;
    int verbose;
    char error[256];
};

/* A little-endian word out of the still body. */
static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* The default copy: the samples are at the address, in this process. */
static void default_copy(void *destination, uint32_t address, size_t count)
{
    memcpy(destination, (const void *)(uintptr_t)address, count);
}

/*
 * The same, for the callers that are not already under the lock: the
 * session's own calls into the backend.
 */
static void host_set_error(capture_library_host *host, const char *what)
{
    backend_lock();
    snprintf(host->error, sizeof(host->error), "%s", what);
    backend_unlock();
}

static void host_error(capture_library_host *host, const char *what)
{
    snprintf(host->error, sizeof(host->error), "%s", what);
    if (host->verbose) {
        fprintf(stderr, "capture-host: %s\n", host->error);
    }
}

/*
 * The reason the last open failed.
 *
 * A failed open has no host to carry the reason - the host is freed - so
 * the reason is kept here for the caller to ask for.  It is written under
 * the lock, because a callback writes the host's error under the same lock.
 */
static void set_the_open_error(const char *what)
{
    backend_lock();
    snprintf(last_open_error, sizeof(last_open_error), "%s", what);
    backend_unlock();
}

/*
 * Keep the reason an open failed.
 *
 * The open ends with the host freed, so the reason has to be copied out of
 * it before the cleanup, or the caller is left with nothing but a failed
 * open.  The copy is made under the lock, because a callback writes the
 * host's error under the same lock and the reason has to be read as a
 * whole.
 */
static void keep_the_open_error(capture_library_host *host)
{
    backend_lock();
    snprintf(last_open_error, sizeof(last_open_error), "%s",
             (host->error[0] != '\0') ? host->error
                                      : "the capture library could not be hosted");
    backend_unlock();
}

/*
 * The open failed: keep the reason, clean the host up, and report it.
 *
 * Every failure after the library is loaded goes through here, so that no
 * path can lose its reason with the host.
 */
static int open_failed(capture_library_host *host, const char *what)
{
    host_error(host, what);
    keep_the_open_error(host);
    capture_library_host_close(host);
    return -1;
}

/*
 * The settings, in the camera's own ids, and the ids the library's
 * interface takes.  The library's own vocabulary is the one the inter-
 * processor frames use, so the ISO is id 4 there while the camera's
 * settings store calls it 5.  The translation lives in the library for
 * everything the library knows; this table is only for the camera's ids
 * that the library's interface does not name.
 */
static int library_setting_for(uint32_t native_id, int *is_variable)
{
    *is_variable = 0;
    switch (native_id) {
    case CAPTURE_SESSION_ISO:
        /* The library's ISO handler sends attribute 0x1104, which is
         * user-data id 4. */
        return CAPTURE_LIBRARY_SETTING_ISO;
    case CAPTURE_SESSION_IMAGE_QUALITY:
        /* The library's quality handler sends attribute 0x1103, which is
         * user-data id 3.  The value it takes is the camera's own position,
         * and the library wants its own tagged form, so the value is
         * converted before the write. */
        return CAPTURE_LIBRARY_SETTING_QUALITY;
    case CAPTURE_SESSION_SHUTTER:
        /* The library's variable facade takes the camera's own variable id,
         * 0x100 to 0x12b, and keeps the id it is given: 0x101 stays 0x101
         * and the call emits the message 0x1302 with four bytes of data.
         * The index 2 that the id ends in is not an id this facade takes. */
        *is_variable = 1;
        return CAPTURE_LIBRARY_SETTING_SHUTTER;
    default:
        return -1;
    }
}

/*
 * The image quality's value, in the form the library takes.
 *
 * The camera's positions are 0 to 7, and the library takes them tagged: it
 * accepts 0x30000 to 0x30007.  The RTOS mapping between the two is not the
 * identity, and this is it:
 *
 *   position 0 -> 0x30001    position 4 -> 0x30004
 *   position 1 -> 0x30002    position 5 -> 0x30005
 *   position 2 -> 0x30003    position 6 -> 0x30006
 *   position 3 -> 0x30000    position 7 -> 0x30007
 *
 * The native values 0x40000 to 0x40007 name the same eight positions, and
 * the library does not take them as they are, so they are converted too.
 *
 * The **whole** 32-bit value is looked at.  A value with anything in bits
 * 8 to 15, a value whose tag is not one of the two that are accepted here,
 * and a position outside 0 to 7 are all refused.  Nothing is masked into a
 * position: a value that is not one of the camera's positions must not
 * become one by losing a bit.
 *
 * This says how the value is written.  It does not say that the camera
 * reads it back, that it has an effect, or that the camera then keeps the
 * still's samples: none of those is established here.
 */
static int quality_api_value(uint32_t value, uint32_t *api_value)
{
    static const uint32_t positions[8] = {
        0x30001u, 0x30002u, 0x30003u, 0x30000u,
        0x30004u, 0x30005u, 0x30006u, 0x30007u
    };
    uint32_t tag;
    uint32_t position;

    if ((value & 0x0000ff00u) != 0u) {
        /* Bits 8 to 15 carry nothing in any of the forms. */
        return -1;
    }
    position = value & 0xffu;
    if (position > 7u) {
        return -1;
    }
    tag = value & 0xffff0000u;
    if (tag != 0u && tag != 0x00040000u) {
        /* Neither the bare position nor the native tagged form. */
        return -1;
    }
    *api_value = positions[position];
    return 0;
}

/*
 * What the library's own image object says about itself.
 *
 * The image callback hands the image object over as its third argument, and
 * the library exports the accessors that describe such an object.  These are
 * calls into the library, not a guess about how some structure is laid out,
 * so what they say is the library's own reading of the image.
 *
 * Every accessor is optional.  A library that does not export one leaves that
 * number unknown, and unknown is zero here - never a guessed value.
 */
static void read_image_object(capture_library_host *host, void *image_object,
                              capture_library_delivered *out)
{
    void *buffer;
    uint32_t address[4];
    uint32_t size;

    if (image_object == 0) {
        return;
    }
    if (host->image_get_width != 0) {
        out->width = host->image_get_width(image_object);
    }
    if (host->image_get_height != 0) {
        out->height = host->image_get_height(image_object);
    }
    if (host->image_get_raw_stride != 0) {
        out->stride = host->image_get_raw_stride(image_object);
    }
    if (host->image_get_raw_pixel_bit != 0) {
        out->raw_pixel_bit = host->image_get_raw_pixel_bit(image_object);
    }
    if (host->image_get_image_format != 0) {
        out->format = (int)host->image_get_image_format(image_object);
    }

    if (host->image_get_buffer == 0) {
        return;
    }
    buffer = host->image_get_buffer(image_object);
    if (buffer == 0) {
        return;
    }
    if (host->buffer_get_size != 0) {
        size = host->buffer_get_size(buffer);
        out->size = size;
    }
    if (host->buffer_get_virt_address != 0) {
        memset(address, 0, sizeof(address));
        host->buffer_get_virt_address(buffer, address);
        /*
         * The mapped address is sixteen bytes wide: four 32-bit words.  The
         * first word is this process's pointer to the samples, and the words
         * after it are the other planes - they are *not* the upper half of a
         * sixty-four bit pointer, and they are not read as one.
         */
        out->address = address[0];
    }
}

/*
 * The image callback.  The library's type for it is
 * void (*)(int, stCAPTURE_IMAGE *, void *, unsigned int).
 *
 * **This route is the liveview.**  The image topics are the liveview, the
 * display and the movie frames; the stock photo consumer does not subscribe
 * to them at all, and the still's result comes on the image-set route
 * instead.  So what arrives here is described and copied nothing.
 *
 * The callback and the session run on different threads, so what this
 * callback does with the host is done under the backend's lock.  A result
 * the caller already holds is a *different* one and is never touched here.
 */
static void host_on_image(int id, void *rect_block, void *image_object,
                          unsigned int size)
{
    capture_library_host *host;
    const capture_library_rect_block *block;
    capture_library_delivered delivered;
    uint32_t address;

    (void)size;

    backend_lock();
    host = the_host;
    if (host == 0 || rect_block == 0) {
        backend_unlock();
        return;
    }
    block = (const capture_library_rect_block *)rect_block;

    /*
     * The image object is the library's own object for this image.  What it
     * says about the image is the geometry and the size of the buffer that
     * holds the samples.
     */
    memset(&delivered, 0, sizeof(delivered));
    read_image_object(host, image_object, &delivered);

    /*
     * Where the bytes are: the image's own buffer when the library said so,
     * and otherwise the address the delivered block names.
     *
     * The block's second mapped word is **not** used to work out how many
     * bytes there are.  It is a second mapped address, computed as the
     * mapped base plus the width times the height, and the difference
     * between the two words is not a byte count.
     */
    address = (delivered.address != 0u) ? delivered.address : block->mapped_addr;

    if (host->verbose) {
        fprintf(stderr,
                "capture-host: image topic %d, %ux%u, stride %u, %u bits, "
                "format %d, %lu bytes at 0x%08x\n",
                id, delivered.width, delivered.height, delivered.stride,
                delivered.raw_pixel_bit, delivered.format,
                (unsigned long)delivered.size, address);
    }

    /*
     * **Nothing is copied here.**
     *
     * This route is the liveview, the display and the movie.  The still's
     * samples come from the set's first descriptor, so a copy of every
     * preview frame would copy bytes nobody asked for.
     *
     * What the image object says about the image is recorded, because it is
     * the library's own reading of the image.  The address and the size are
     * recorded too, and the size is the extent - the library's own
     * statement of how many bytes the image has.  The block's second
     * mapped word is **not** used to work out how many bytes there are: it
     * is a second mapped address, computed as the mapped base plus the
     * width times the height, and the difference between the two words is
     * not a byte count.
     */
    host->images_delivered += 1u;
    host->image_type = 0;
    host->image_address = 0u;
    host->image_width = delivered.width;
    host->image_height = delivered.height;
    host->image_stride = delivered.stride;
    host->image_raw_pixel_bit = delivered.raw_pixel_bit;
    host->image_format = delivered.format;
    host->image_buffer_address = address;
    host->image_buffer_size = delivered.size;

    backend_unlock();
}

static void host_on_notify(int event, void *data, unsigned int size)
{
    capture_library_host *host;

    backend_lock();
    host = the_host;
    if (host == 0) {
        backend_unlock();
        return;
    }
    /* The notification carries the event's id and its payload.  For a
     * capture the payload is the capture object, so the event that names a
     * completed capture is the one whose payload looks like one. */
    host->last_notification = event;
    host->last_notification_data = (uint32_t)(uintptr_t)data;
    host->last_notification_size = size;
    host->notifications_seen += 1u;
    host->heard_from_the_camera = 1;
    /*
     * Record notification arrival while a request is pending. Neither its
     * timing nor this flag establishes a matching completed still.
     */
    if (host->awaiting_completion) {
        host->completion_seen = 1;
    }
    if (host->verbose) {
        fprintf(stderr, "capture-host: notification %d, 0x%08x, %lu bytes\n",
                event, host->last_notification_data, (unsigned long)size);
    }
    backend_unlock();
}

/* Whether the topic id is the encoded still's, in any of its spellings. */
static int topic_is_the_encoded_still(int topic)
{
    uint32_t id = (uint32_t)topic;

    return id == CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL ||
           id == CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL_INTERNAL ||
           id == CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL_CONVERTED;
}

/*
 * Whether the copied bytes are a JPEG stream.
 *
 * The set's main image is the encoded still - but "the main image of an
 * encoded set" is not by itself proof that the bytes are a JPEG, and the
 * caller asked for the camera's JPEG of the still.  So the bytes are
 * checked: a JPEG stream starts with the start-of-image marker and ends
 * with the end-of-image marker.  Bytes that are not a JPEG stream are not
 * offered as one.
 */
static int looks_like_a_jpeg(const void *bytes, size_t size)
{
    const uint8_t *data = (const uint8_t *)bytes;

    if (data == 0 || size < 4u) {
        return 0;
    }
    if (data[0] != 0xffu || data[1] != 0xd8u) {
        return 0;
    }
    if (data[size - 2u] != 0xffu || data[size - 1u] != 0xd9u) {
        return 0;
    }
    return 1;
}

/*
 * The image-set callback.  This is where the still arrives.
 *
 * The library's type for it is
 * void (*)(int, stCAPTURE_IMG_SET *, void *, unsigned int).
 *
 * The set's first word is the basic set, and the basic set begins with the
 * encoded-set constituent: the set type, then the four image descriptors.
 * The constituent lives on the library's own stack for the length of the
 * call, so everything is copied here.
 *
 * The first descriptor is the firmware's "Raw Compress" image for this
 * still, and it is only there in the raw configuration - the set type says
 * so, because the serializer writes the type from the order sheet's
 * configuration byte.  The main descriptor is the encoded still.
 */
static void host_on_image_set(int topic, void *image_set, void *data,
                              unsigned int size)
{
    capture_library_host *host;
    const capture_library_image_set *set;
    const uint8_t *constituent;
    encoded_set_metadata metadata;
    capture_library_still_result result;
    const encoded_set_image_descriptor *first;
    const encoded_set_image_descriptor *main_image;
    uint32_t raw_address;
    uint32_t raw_size;
    uint32_t jpeg_address;
    uint32_t jpeg_size;
    int is_ours;
    capture_library_release_capture_buffer release;
    uint32_t release_address;
    uint32_t release_identity;
    int release_verbose;

    (void)data;

    backend_lock();
    host = the_host;
    if (host == 0) {
        backend_unlock();
        return;
    }

    host->sets_delivered += 1u;
    host->image_set_event = topic;
    host->image_set_address = (uint32_t)(uintptr_t)image_set;
    host->image_set_size = size;

    /*
     * The topic decides this, and it is decided **before the set is
     * touched**.
     *
     * The camera publishes several sets for one still - the YCC set, the
     * MPO set, the UD-YCC set, the expansion set - and this component only
     * interprets the payload of the encoded still's topic.  A set on any
     * other topic is therefore ignored **before its payload is
     * interpreted**: its pointer is not dereferenced and its descriptor is
     * not decoded.  What those other payloads contain is not established
     * here, and this component does not read them to find out.
     */
    if (!topic_is_the_encoded_still(topic)) {
        if (host->verbose) {
            fprintf(stderr,
                    "capture-host: image set topic %d: not the still's "
                    "encoded set; its payload was not read\n",
                    topic);
        }
        backend_unlock();
        return;
    }

    /*
     * The first validated result is latched, and a second one is refused.
     *
     * The check comes before the `is_ours` test, because collecting the
     * result clears `awaiting_completion`: a duplicate that arrives after
     * the collection is still a duplicate, and it must still be refused.
     * Before any request the latch is zero, and a request that is armed
     * clears it. Native request association remains unproved.
     *
     * The refusal reads no payload: the pointer is not dereferenced, the
     * descriptor is not decoded, the latched copy is not freed and the
     * library's plane is not released a second time.  What that plane's
     * retirement is, is left unestablished.
     *
     * The host then needs recovery, and so does this process: an
     * unattributable set means the native obligations are unresolved, and
     * the library's registrations survive the client, so a later delivery
     * would land in the next host.
     */
    if (host->result_latched) {
        host->recovery_required = 1;
        host_reuse_is_forbidden = 1;
        host_error(host, "a second still's set arrived after the first one was "
                         "latched; its payload was not read, the first result "
                         "is kept, and this host needs recovery");
        backend_unlock();
        return;
    }

    /* Arrival after arming is an experimental eligibility heuristic. Earlier
     * work may still arrive here; this counter cannot identify a request. */
    is_ours = host->awaiting_completion &&
              host->sets_delivered > host->sets_at_submit;

    if (!is_ours) {
        if (host->verbose) {
            fprintf(stderr,
                    "capture-host: image set topic %d arrived before our "
                    "submission; it is not taken as the answer to it\n",
                    topic);
        }
        backend_unlock();
        return;
    }

    if (image_set == 0) {
        host_error(host, "the library delivered an image set with no set");
        backend_unlock();
        return;
    }
    set = (const capture_library_image_set *)image_set;
    constituent = (const uint8_t *)set->basic_set;
    if (constituent == 0) {
        host_error(host, "the delivered image set has no basic set");
        backend_unlock();
        return;
    }
    if (encoded_set_decode(constituent, CAPTURE_LIBRARY_SET_CONSTITUENT_SIZE,
                           &metadata) != ENCODED_SET_OK) {
        host_error(host, "the delivered image set does not decode");
        backend_unlock();
        return;
    }
    if (metadata.set_type != CAPTURE_LIBRARY_SET_TYPE_PLAIN &&
        metadata.set_type != CAPTURE_LIBRARY_SET_TYPE_RAW_TWO &&
        metadata.set_type != CAPTURE_LIBRARY_SET_TYPE_RAW_THREE) {
        snprintf(host->error, sizeof(host->error),
                 "the delivered image set has the unknown type %lu",
                 (unsigned long)metadata.set_type);
        if (host->verbose) {
            fprintf(stderr, "capture-host: %s\n", host->error);
        }
        backend_unlock();
        return;
    }

    /* Preserve the diagnostic even when this decoded set is refused below. */
    host->last_set_type = metadata.set_type;

    if (metadata.set_type == CAPTURE_LIBRARY_SET_TYPE_PLAIN) {
        /*
         * The plain configuration: the firmware did not put a first image
         * in the set at all.  So the still carries no raw image, and the
         * backend publishes no completion - a completion with no samples
         * would be a malformed one, and the session would latch on it.
         */
        host_error(host, "the still's set carries no first image: the raw "
                         "configuration is not on");
        backend_unlock();
        return;
    }

    memset(&result, 0, sizeof(result));
    result.set_type = metadata.set_type;

    /* The first image: the firmware's own image for this still. */
    first = &metadata.images[ENCODED_SET_IMAGE_FIRST];
    raw_address = first->first_virtual_word;
    raw_size = first->buffer_size_word;
    if (raw_address == 0u || raw_size == 0u) {
        host_error(host, "the still's first image states no buffer");
        backend_unlock();
        return;
    }
    result.raw_bytes = malloc(raw_size);
    if (result.raw_bytes == 0) {
        host_error(host, "no memory for the still's first image");
        backend_unlock();
        return;
    }
    host->copy(result.raw_bytes, raw_address, raw_size);
    result.raw_size = raw_size;
    result.raw_address = raw_address;
    result.raw_buffer_size = raw_size;
    /*
     * The first image's own geometry, as the descriptor states it.  The
     * capture library's own diagnostic print names these four words: the
     * width and the height in pixels, and the two strides.  The stride
     * words' **unit is not established** - the print names them, but
     * nothing here shows that they count bytes rather than elements - so
     * they are kept as the raw words they are, and the stride this
     * component reports stays zero.
     */
    result.raw_width = first->width;
    result.raw_height = first->height;
    result.raw_stride_x = first->stride_x;
    result.raw_stride_y = first->stride_y;

    /* The main image: the encoded still. */
    main_image = &metadata.images[ENCODED_SET_IMAGE_MAIN];
    jpeg_address = main_image->first_virtual_word;
    jpeg_size = main_image->buffer_size_word;
    if (jpeg_address != 0u && jpeg_size != 0u) {
        void *jpeg = malloc(jpeg_size);

        if (jpeg != 0) {
            host->copy(jpeg, jpeg_address, jpeg_size);
            if (looks_like_a_jpeg(jpeg, jpeg_size)) {
                result.jpeg_bytes = jpeg;
                result.jpeg_size = jpeg_size;
                result.jpeg_address = jpeg_address;
                result.jpeg_buffer_size = jpeg_size;
            } else {
                /*
                 * The main image is not a JPEG stream.  It is not offered as
                 * one, and it is not kept: the caller asked for the camera's
                 * JPEG of the still, and this is not it.
                 */
                free(jpeg);
            }
        }
    }

    result.valid = 1;

    /*
     * The first image's plane is the one the capture route releases: the
     * firmware registers it in the capture-buffer list and the release
     * matches on the address.  The copy above is this process's own, so the
     * library's buffer can go back now.
     *
     * The release is a call into the library, and the library may deliver a
     * callback from inside it.  The lock is not reentrant, so a callback
     * that came from inside the release would deadlock on it.
     *
     * So the release is made with the lock dropped.  Everything that is
     * needed for the call is taken here, under the lock, because the host may
     * be closed and freed while the call is in flight and must not be read
     * then.
     */
    release = host->release_buffer;
    release_address = raw_address;
    release_identity = host->identity;
    release_verbose = host->verbose;

    /*
     * The waiting slot is empty here: the first result of this request is the
     * one that fills it, and a second eligible set was refused above.  The
     * frees are left in place as a guard, not as a rule: they must never
     * free the copy the caller holds, and that copy is in the other slot.
     */
    if (host->waiting.raw_bytes != 0) {
        free(host->waiting.raw_bytes);
    }
    if (host->waiting.jpeg_bytes != 0) {
        free(host->waiting.jpeg_bytes);
    }
    host->waiting = result;
    /*
     * The result is latched before the release is made, so that a delivery
     * the library makes from inside the release finds the latch.
     */
    host->result_latched = 1;
    if (host->verbose) {
        fprintf(stderr,
                "capture-host: still set type %lu, first image %lu bytes at "
                "0x%08x, main image %lu bytes\n",
                (unsigned long)result.set_type, (unsigned long)result.raw_size,
                result.raw_address, (unsigned long)result.jpeg_size);
    }

    /*
     * One of this adapter's own native calls is running right now, so the
     * release is **queued** and not made here: the release is a call into
     * the library too, and the thread that owns the running call makes the
     * release once that call has returned.
     *
     * The samples above are already copied, and the copy is the caller's:
     * the queue holds the release, not the result.
     */
    if (release != 0 && host->native_mutation_reserved) {
        deferred_release_pending = 1;
        deferred_release_fn = release;
        deferred_release_address = release_address;
        deferred_release_identity = release_identity;
        deferred_release_verbose = release_verbose;
        backend_unlock();
        return;
    }

    /*
     * The release is a call into the library made with the lock dropped, so
     * the host says that it is in flight for as long as the call lasts.
     */
    host->native_release_in_progress = (release != 0) ? 1 : 0;

    backend_unlock();

    if (release != 0) {
        int failed;

        failed = release(CAPTURE_LIBRARY_RELEASE_TYPE_FIRST_IMAGE,
                         release_address);
        backend_lock();
        if (failed != 0) {
            /*
             * The outcome of the release is unresolved: the library's own
             * call says nothing about what the camera did with the plane, so
             * a later call would release the same address twice.  This
             * process opens no further host, and the release is not retried.
             * The quarantine is the process's own state, so it is written
             * whether or not the host is still there.
             */
            host_reuse_is_forbidden = 1;
        }
        /*
         * The host may have been closed and freed while the release was in
         * flight, and a freed host's address can be given to a new one, so
         * the host is only written to when it is provably the same live
         * host.  The identity says so; the address alone does not.
         */
        if (the_host == host && the_host_identity == release_identity) {
            host->native_release_in_progress = 0;
            if (failed != 0) {
                host->native_release_uncertain = 1;
                host_error(host, "the library refused to release the still's "
                                 "first image; whether the plane was released "
                                 "is unresolved, so this host makes no further "
                                 "request and this process opens no further "
                                 "host");
            }
        } else if (failed != 0 && release_verbose) {
            fprintf(stderr, "capture-host: the library refused to release "
                    "the still's first image\n");
        }
        backend_unlock();
    }
}

static void *host_symbol(void *library, const char *name, capture_library_host *host)
{
    void *symbol;

    if (library == 0) {
        return 0;
    }
    symbol = dlsym(library, name);
    if (symbol == 0 && host != 0) {
        char text[256];
        snprintf(text, sizeof(text), "the library does not export %s", name);
        host_error(host, text);
    }
    return symbol;
}

/*
 * An optional symbol.  Its absence is not an error: without it the backend
 * says less, and what it says stays true.
 */
static void *host_optional_symbol(void *library, const char *name)
{
    if (library == 0) {
        return 0;
    }
    return dlsym(library, name);
}

int capture_library_host_open_ex(capture_library_host **out_host,
                                  const char *library_path, int verbose,
                                  int create_client)
{
    capture_library_host *host;
    const char *name;

    if (out_host == 0) {
        return -1;
    }
    *out_host = 0;
    set_the_open_error("");

    /*
     * The library's path is required.
     *
     * A null or empty path is refused here, with a reason, rather than
     * falling through to a lookup that cannot work: the entry points are
     * resolved with dlsym on the handle this open obtains, and a handle
     * there is none.  The linked-library case - the library this program
     * was linked against - is **not** supported by this component, and the
     * header does not promise it: a process-wide lookup would bind to
     * whichever object in the process's global scope exports the name,
     * which on the camera may be a different build of it, and choosing one
     * of those by guess is not something this component does.
     */
    if (library_path == 0 || library_path[0] == '\0') {
        set_the_open_error("the capture library's path is required");
        return -1;
    }

    host = (capture_library_host *)calloc(1u, sizeof(*host));
    if (host == 0) {
        set_the_open_error("no memory for the capture host");
        return -1;
    }
    host->verbose = verbose;
    host->client_slot = -1;
    host->copy = default_copy;

    /*
     * The host is published before anything can call back into it, so that
     * a callback that arrives while the client is being created still finds
     * it.
     *
     * The publication is under the lock, like every other access to the
     * global.  And a second host is refused: the library allows one camera
     * client at a time, and a second host would quietly take the global
     * away from the live one, so the live host's callbacks would publish
     * into the wrong host.
     *
     * And a host that abandoned a request refuses any host at all: see
     * host_reuse_is_forbidden.
     */
    backend_lock();
    if (host_reuse_is_forbidden) {
        backend_unlock();
        free(host);
        set_the_open_error("a host in this process was left with a native "
                           "request outstanding - a still closed with one, or "
                           "a send that reported an error and left the outcome "
                           "unresolved, or a native release whose outcome is "
                           "unresolved; nothing here shows that such a request "
                           "can be cancelled, so this process opens no further "
                           "host");
        return -1;
    }
    if (deferred_release_pending) {
        backend_unlock();
        free(host);
        set_the_open_error("a native release of the still's first image is "
                           "still queued behind a native call in this process, "
                           "so this process opens no further host");
        return -1;
    }
    if (the_host != 0) {
        backend_unlock();
        free(host);
        set_the_open_error("another host is already open in this process");
        return -1;
    }
    if (next_host_identity == UINT32_MAX) {
        /*
         * The identity counter is exhausted.
         *
         * The identity is what says that a host a callback is holding is
         * still the live one, and it only says that while every open has a
         * different one.  A counter that wrapped would hand out an
         * identity that was used before, or the zero that means "no live
         * host", and the guarantee would be gone without anything saying so.
         *
         * So the open is refused instead.  There is no knob to wind the
         * counter on: it is a count of opens, and the only way past it is a
         * process that has not made this many.
         */
        backend_unlock();
        free(host);
        set_the_open_error("the host identity counter is exhausted; this "
                           "process has opened more hosts than the identity "
                           "can tell apart");
        return -1;
    }
    the_host = host;
    host->identity = ++next_host_identity;
    the_host_identity = host->identity;
    backend_unlock();

    name = library_path;
    if (verbose) {
        fprintf(stderr, "capture-host: entering dlopen\n");
    }
    host->library = dlopen(name, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    if (verbose) {
        fprintf(stderr, "capture-host: dlopen returned %s\n",
                host->library != 0 ? "a handle" : "null");
    }
    if (host->library == 0) {
        snprintf(host->error, sizeof(host->error), "cannot open %s", name);
        keep_the_open_error(host);
        backend_lock();
        if (the_host == host) {
            the_host = 0;
            the_host_identity = 0;
        }
        backend_unlock();
        free(host);
        return -1;
    }

    host->create = (capture_library_mm_camera_create)
        host_symbol(host->library, "mm_camera_create", host);
    host->destroy = (capture_library_mm_camera_destroy)
        host_symbol(host->library, "mm_camera_destroy", host);
    host->realize = (capture_library_mm_camera_realize)
        host_symbol(host->library, "mm_camera_realize", host);
    host->start = (capture_library_mm_camera_start)
        host_symbol(host->library, "mm_camera_start", host);
    host->stop = (capture_library_mm_camera_stop)
        host_symbol(host->library, "mm_camera_stop", host);
    host->set_usr = (capture_library_mm_camera_set_usr_attributes)
        host_symbol(host->library, "mm_camera_set_usr_attributes", host);
    host->set_var = (capture_library_mm_camera_set_var_attributes)
        host_symbol(host->library, "mm_camera_set_var_attributes", host);
    host->set_notification = (capture_library_mm_camera_set_notification_callback)
        host_symbol(host->library, "mm_camera_set_notification_callback", host);
    host->set_notification_id = (capture_library_mm_camera_set_notification_id)
        host_symbol(host->library, "mm_camera_set_notification_id", host);
    host->capture_still = (capture_library_mm_camera_capture_still)
        host_symbol(host->library, "mm_camera_capture_still", host);
    host->reg_client = (capture_library_reg_callback_client)
        host_symbol(host->library, "RegCallbackClient", host);
    host->reg_image_id = (capture_library_reg_image_callback_id)
        host_symbol(host->library, "RegImageCallbackId", host);
    host->reg_image_set_id = (capture_library_reg_image_set_callback_id)
        host_symbol(host->library, "RegImageSetCallbackId", host);
    host->release_buffer = (capture_library_release_capture_buffer)
        host_symbol(host->library, "ReleaseCaptureBuffer", host);
    /*
     * The capture-mode setter.  It is optional: a library that does not
     * export it simply cannot be asked for a capture mode, and the backend
     * says so.
     */
    host->set_capture_mode_user_data = (capture_library_set_capture_mode_user_data)
        host_optional_symbol(host->library,
                             CAPTURE_LIBRARY_SET_CAPTURE_MODE_SYMBOL);
    /*
     * The image object's own description.  These are C++ symbols and they are
     * optional: they are what turns the image object the callback hands over
     * into the geometry of the samples and the address of the buffer that
     * holds them, and without them the geometry simply stays unknown.
     */
    host->image_get_width = (capture_library_image_get_u32_fn)
        host_optional_symbol(host->library, CAPTURE_LIBRARY_IMAGE_GET_WIDTH);
    host->image_get_height = (capture_library_image_get_u32_fn)
        host_optional_symbol(host->library, CAPTURE_LIBRARY_IMAGE_GET_HEIGHT);
    host->image_get_raw_stride = (capture_library_image_get_u32_fn)
        host_optional_symbol(host->library, CAPTURE_LIBRARY_IMAGE_GET_RAW_STRIDE);
    host->image_get_raw_pixel_bit = (capture_library_image_get_u32_fn)
        host_optional_symbol(host->library, CAPTURE_LIBRARY_IMAGE_GET_RAW_PIXEL_BIT);
    host->image_get_image_format = (capture_library_image_get_u32_fn)
        host_optional_symbol(host->library, CAPTURE_LIBRARY_IMAGE_GET_IMAGE_FORMAT);
    host->image_get_buffer = (capture_library_image_get_buffer_fn)
        host_optional_symbol(host->library, CAPTURE_LIBRARY_IMAGE_GET_BUFFER);
    host->buffer_get_virt_address = (capture_library_buffer_get_address_fn)
        host_optional_symbol(host->library, CAPTURE_LIBRARY_BUFFER_GET_VIRT_ADDRESS);
    host->buffer_get_size = (capture_library_buffer_get_size_fn)
        host_optional_symbol(host->library, CAPTURE_LIBRARY_BUFFER_GET_SIZE);

    if (host->create == 0 || host->capture_still == 0 || host->reg_client == 0) {
        if (host->error[0] == '\0') {
            host_error(host, "the capture library does not export the entry "
                             "points this backend needs");
        }
        keep_the_open_error(host);
        capture_library_host_close(host);
        return -1;
    }

    if (!create_client) {
        /*
         * This stage resolves symbols and skips the explicit client-create
         * path below. dlopen still runs library/dependency initializers;
         * accepted native evidence includes property construction and a
         * dependency plugin's readonly sysfs probes. Symbol availability
         * does not establish capture readiness or absence of side effects.
         * Close keeps the shared object mapped until process exit.
         */
        *out_host = host;
        if (verbose) {
            fprintf(stderr, "capture-host: the capture library is loaded; no "
                            "client was created; loading it may have run the "
                            "library's own constructors and those of the "
                            "libraries it depends on\n");
        }
        return 0;
    }

    /*
     * This open attempts initialization once through mm_camera_create.
     * That entry calls realization but discards its status; the shared
     * preference can also skip per-process adapter initialization. Neither
     * a returned handle nor the bridge's success-looking return proves
     * that the required services are ready.
     *
     * The preference that guards the power-on is **not** read.  It is
     * shared across processes, so its value cannot say which process
     * initialised the module, and the getter dereferences it without
     * checking that its mapping succeeded.
     *
     * So this component does not know whether this client is the one that
     * got the initialisation, and it does not infer it either: a returned
     * handle does not establish that the module was initialised, and an
     * event arriving does not establish a device effect.
     */
    {
        int create_status;
        if (verbose) {
            fprintf(stderr, "capture-host: entering mm_camera_create\n");
        }
        create_status = host->create(&host->handle);
        if (verbose) {
            fprintf(stderr, "capture-host: mm_camera_create returned %d; "
                            "readiness unverified\n", create_status);
        }
        if (create_status != 0) {
            return open_failed(host, "the capture library refused to create a client");
        }
    }
    host->opened = 1;

    /* Register the client, so the library delivers the image to us.  The name
     * has to pass the library's registration guard, which wants a name that
     * is not null and is no longer than nine bytes. */
    if (verbose) {
        fprintf(stderr, "capture-host: entering RegCallbackClient\n");
    }
    host->client_slot = (int)host->reg_client(CAPTURE_LIBRARY_CLIENT_NAME,
                                               host_on_notify,
                                              host_on_image, host_on_image_set);
    if (host->client_slot < 0) {
        return open_failed(host,
                           "the capture library refused a client registration");
    }
    if (verbose) {
        fprintf(stderr, "capture-host: client slot %d\n", host->client_slot);
    }

    /*
     * The topics.  Registering the client only stores the three callback
     * pointers; the subscription - the thing that puts this client in the
     * publisher's list - is made by the id calls.
     *
     * The image topics are the liveview and the movie.  They are subscribed
     * so that the run shows what the library delivers; the backend records
     * what they describe and copies nothing, and a delivery on them never
     * answers a still.
     */
    if (host->reg_image_id != 0) {
        if (host->reg_image_id(host->client_slot,
                               CAPTURE_LIBRARY_TOPIC_IMAGE_LIVEVIEW_DISPLAY0) != 0 ||
            host->reg_image_id(host->client_slot,
                               CAPTURE_LIBRARY_TOPIC_IMAGE_LIVEVIEW_DISPLAY1) != 0 ||
            host->reg_image_id(host->client_slot,
                               CAPTURE_LIBRARY_TOPIC_IMAGE_MOVIE) != 0) {
            return open_failed(host, "the library refused the image topics");
        }
    }
    /*
     * The image-set topics are the still's own results.  The encoded still
     * is the one the still's set arrives on, and it is the only one whose
     * refusal stops the host: the others are subscribed so that a run
     * shows what else the library publishes for the same still.
     */
    if (host->reg_image_set_id != 0) {
        if (host->reg_image_set_id(host->client_slot,
                                   CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL) != 0) {
            return open_failed(host, "the library refused the encoded still topic");
        }
        (void)host->reg_image_set_id(host->client_slot,
                                     CAPTURE_LIBRARY_TOPIC_SET_YCC_STILL);
        (void)host->reg_image_set_id(host->client_slot,
                                     CAPTURE_LIBRARY_TOPIC_SET_MPO_STILL);
        (void)host->reg_image_set_id(host->client_slot,
                                     CAPTURE_LIBRARY_TOPIC_SET_UD_YCC_STILL);
        (void)host->reg_image_set_id(host->client_slot,
                                     CAPTURE_LIBRARY_TOPIC_SET_MV_META_STILL);
        (void)host->reg_image_set_id(host->client_slot,
                                     CAPTURE_LIBRARY_TOPIC_SET_EXPANSION_STILL);
    }

    /*
     * The notification callback and the notification ids.  The ids are the
     * ones the stock application subscribes to - every id the library
     * knows - and the list is zero-terminated, which is how the library
     * reads it.
     */
    if (host->set_notification != 0) {
        if (host->set_notification(host->handle, (void *)host_on_notify, 0u) != 0) {
            return open_failed(host,
                               "the library refused the notification callback");
        }
    }
    if (host->set_notification_id != 0) {
        if (host->set_notification_id(host->handle,
                                      CAPTURE_LIBRARY_NOTIFICATION_IDS) != 0) {
            return open_failed(host,
                               "the library refused the notification ids");
        }
    }

    *out_host = host;
    return 0;
}

int capture_library_host_open(capture_library_host **out_host,
                               const char *library_path, int verbose)
{
    return capture_library_host_open_ex(out_host, library_path, verbose, 1);
}

int capture_library_host_load(capture_library_host **out_host,
                               const char *library_path, int verbose)
{
    return capture_library_host_open_ex(out_host, library_path, verbose, 0);
}

/*
 * Whether this backend can ask the camera to keep the sensor samples.
 *
 * No: **this implementation has no verified way to ask**.  The still's set
 * carries a first image when the camera's own raw configuration is on, and
 * the camera's raw switch - the one the settings store calls SSIF_RAW - is
 * among the candidates for what turns that configuration on, but nothing
 * here establishes that SSIF_RAW alone controls this sheet's
 * configuration, and the library's interface names no call that has been
 * verified to write it.  So the ask is refused rather than answered with a
 * guessed id.
 *
 * That is a statement about this implementation.  It is **not** a claim
 * that no library interface can do it: the interface recovered from the
 * pinned library has not been searched exhaustively for such a call, and
 * the pinned library is the one build whose interface was read.
 *
 * The answer is a property of the backend and not of a loaded library, so
 * a caller can ask it before the library is opened at all.
 */
int capture_library_host_supports_sensor_retention(void)
{
    return 0;
}

void capture_library_host_close(capture_library_host *host)
{
    int destroy_client = 1;

    if (host == 0) {
        return;
    }
    /*
     * The global host pointer is cleared under the lock, so a callback
     * that arrives afterwards finds no host at all.  The host itself is
     * freed after the lock is released, and the native delivery order is
     * not known here, so a callback that was already inside the library
     * when the close began is not covered by this.  Nothing here fixes
     * that by guessing at the native behaviour.
     */
    backend_lock();
    if (host->awaiting_completion) {
        /*
         * A request was still outstanding.  The session was not closed, so
         * nothing cleared the state that says so, and the native request
         * cannot be shown to be cancelled.  This process opens no further
         * host: the library's registrations survive, so a delivery that
         * arrives later would land in the next one.
         */
        host->abandoned = 1;
        host_reuse_is_forbidden = 1;
    }
    if (host->native_release_in_progress) {
        /*
         * A native release was still in flight, so its outcome was not
         * established.  The quarantine is set here, before the release
         * returns: another context must not be opened while that call is
         * still running, and a later successful return does not take the
         * quarantine back.  Nothing here certifies a recovery: the process
         * exit is not one.
         */
        host_reuse_is_forbidden = 1;
        destroy_client = 0;
    }
    if (host->native_mutation_reserved) {
        /*
         * One of this adapter's own native calls was still running, so its
         * outcome was not established either: the command may still be
         * queued.  The host is not reused and this process opens no further
         * host.
         *
         * The call is not cancelled - nothing here asks the camera to drop
         * it - and the thread that owns it holds what it needs, and writes
         * to the host only if the host is provably still the live one.
         */
        host->abandoned = 1;
        host_reuse_is_forbidden = 1;
        destroy_client = 0;
    }
    if (deferred_release_pending) {
        /*
         * A release is still queued behind a call that is running.  It is
         * owed and it is not made yet, so the process's reuse is forbidden
         * here as well.
         */
        host_reuse_is_forbidden = 1;
        destroy_client = 0;
    }
    if (the_host == host) {
        the_host = 0;
        the_host_identity = 0;
    }
    if (host->waiting.raw_bytes != 0) {
        free(host->waiting.raw_bytes);
        host->waiting.raw_bytes = 0;
    }
    if (host->waiting.jpeg_bytes != 0) {
        free(host->waiting.jpeg_bytes);
        host->waiting.jpeg_bytes = 0;
    }
    if (host->held.raw_bytes != 0) {
        free(host->held.raw_bytes);
        host->held.raw_bytes = 0;
    }
    if (host->held.jpeg_bytes != 0) {
        free(host->held.jpeg_bytes);
        host->held.jpeg_bytes = 0;
    }
    backend_unlock();

    if (destroy_client && host->opened && host->destroy != 0) {
        /* The client is destroyed but the capture module is *not* powered
         * off: that path waits for a power-off event. */
        (void)host->destroy(host->handle);
    } else if (!destroy_client) {
        /*
         * The close happened while native work was active, pending or in
         * flight, so the client is **not** destroyed.
         *
         * Destroying the client while a call into the library is running is
         * not established as safe, and this component does not guess at the
         * library's own teardown.  The native client obligation is therefore
         * left unrecovered, the shared object stays loaded, and this
         * process opens no further host.
         *
         * The destroy is not retried later: nothing here knows when the
         * native work ends, and a late destroy would be a second teardown
         * of a client whose state is not established.  Nor does the
         * process's exit recover it - the client's teardown is not
         * established, and the capture module's power state is not
         * established either.
         */
        if (host->verbose) {
            fprintf(stderr,
                    "capture-host: the host is closed while native work is "
                    "active, pending or in flight; the native client is not "
                    "destroyed and that obligation is left unrecovered\n");
        }
    }
    /*
     * The library is **not** unloaded.  The client's registrations survive
     * the client's destruction, and the library's entry points stay
     * registered with the external bridge library, so unloading the code
     * under them would leave those registrations pointing at nothing.
     *
     * This component does not claim that the process's exit shuts anything
     * down: the shared object stays loaded, and whether the capture module
     * is powered off at exit is not established here.
     */
    free(host);
}

const char *capture_library_host_error(const capture_library_host *host)
{
    if (host == 0) {
        return "no host";
    }
    return host->error;
}

const char *capture_library_host_last_error(void)
{
    return last_open_error;
}

/*
 * The statuses the library's setting facades return.
 *
 * Zero is the local Send return, not proof of peer receipt or effect.
 * -2 is a handle the library does not know and -3 is a value or an id
 * its own converter refuses; both are
 * decided before the send, so neither of them sent anything.
 *
 * Nonzero transport statuses are collapsed to -1 by the pinned facade.
 * Unknown nonzero statuses are conservatively treated as unresolved.
 */
#define SETTING_STATUS_OK CAPTURE_LIBRARY_STATUS_OK
#define SETTING_STATUS_INVALID_HANDLE CAPTURE_LIBRARY_STATUS_INVALID_HANDLE
#define SETTING_STATUS_REFUSED CAPTURE_LIBRARY_STATUS_REFUSED

/*
 * Quarantine this host, and forbid this process from opening another one.
 *
 * The host made a call into the library whose outcome is unresolved: the
 * native request may still be queued.  So the host is not used again - it
 * makes no further mutating request - and the process opens no further
 * host, because the library's registrations survive the client and a
 * delivery that arrives later would land in the next one.
 *
 * Nothing here is a cancellation: nothing asks the camera to drop the
 * request, and nothing shows that it was dropped.
 *
 * The host may have been closed and freed while the native call was
 * running, so the identity decides whether there is still a host to
 * quarantine.  The process's own quarantine is written either way.
 *
 * The host's "a completion may still arrive" state is **not** touched here.
 * A setting that may still be queued is not a still, and nothing waits for
 * a completion of it.
 */
static void quarantine_the_host(capture_library_host *host, uint32_t identity)
{
    backend_lock();
    if (host_is_live_under_lock(host, identity)) {
        host->abandoned = 1;
    }
    host_reuse_is_forbidden = 1;
    backend_unlock();
}

/*
 * Whether a nonzero status from a setting call leaves the send unresolved.
 *
 * The two statuses the library refuses before the send - the handle it does
 * not know and the value or id its converter refuses - sent nothing, so the
 * host is not left in doubt about them.  Every other nonzero status is
 * conservatively unresolved, like the still's trigger.
 */
static int setting_send_is_uncertain(int status)
{
    if (status == SETTING_STATUS_INVALID_HANDLE) {
        return 0;
    }
    if (status == SETTING_STATUS_REFUSED) {
        return 0;
    }
    return 1;
}

/*
 * The reasons the backend's own error carries.
 *
 * Nine is the quarantine that an unresolved native outcome puts a host in.
 * Ten is the window in which the native release of the still's first image is
 * in flight.  Eleven is the host that needs recovery because an extra result
 * arrived.  Twelve is a release that is queued behind a native call that is
 * still running, and thirteen is a native call that is running now.
 */
#define CAPTURE_LIBRARY_ERROR_QUARANTINED 9
#define CAPTURE_LIBRARY_ERROR_RELEASE_IN_FLIGHT 10
#define CAPTURE_LIBRARY_ERROR_RECOVERY_REQUIRED 11
#define CAPTURE_LIBRARY_ERROR_RELEASE_PENDING 12
#define CAPTURE_LIBRARY_ERROR_NATIVE_CALL_IN_FLIGHT 13

/*
 * The reasons a setting failure carries.
 *
 * They are static strings and not the host's own error buffer: the host may
 * be closed and freed while the native call is running, and the caller's
 * error may outlive it.  A message that pointed into a freed host would
 * dangle.
 */
static const char variable_send_uncertain_reason[] =
    "the library reported a failed variable setting, but the value may "
    "still have been sent; the outcome is unresolved, so the host is "
    "quarantined and this process opens no further host";
static const char variable_refused_reason[] =
    "the library refused the variable setting before it sent it";
static const char user_send_uncertain_reason[] =
    "the library reported a failed user setting, but the value may still "
    "have been sent; the outcome is unresolved, so the host is quarantined "
    "and this process opens no further host";
static const char user_refused_reason[] =
    "the library refused the user setting before it sent it";

/*
 * Why a mutating request may be refused.
 *
 * The quarantine is the pre-existing one: an earlier native outcome is
 * unresolved.  The states after it are this component's own:
 *
 *   - a native release is in flight: the release of the still's first image
 *     is a call into the library, made with the lock dropped because the
 *     library may deliver a callback from inside it, and a request made in
 *     that window would race it.
 *
 *   - a native release is queued: the callback received the still's set
 *     while a mutating native call was reserved, so the release waits for
 *     that call to return.  A request made in that window would be a second
 *     call into the library while the first one is still running.
 *
 *   - a native call is in flight: the call this host made into the library
 *     has not returned yet, and the library's own dispatch is what delivers
 *     the callbacks.
 *
 *   - the outcome of that release is unresolved: a nonzero status from the
 *     library's release call is not a statement that the plane was released,
 *     so a later call would release the same address twice.
 *
 *   - an extra result arrived: a set that could not be associated with the
 *     request, so this host cannot say what the camera published and the
 *     caller has to recover it.
 */
#define HOST_MAY_MUTATE 0
#define HOST_IS_QUARANTINED 1
#define HOST_RELEASE_IN_FLIGHT 2
#define HOST_RELEASE_UNRESOLVED 3
#define HOST_NEEDS_RECOVERY 4
#define HOST_RELEASE_PENDING 5
#define HOST_NATIVE_CALL_IN_FLIGHT 6

/*
 * The host's state, read under the lock.
 *
 * These four states are written by callback threads under the lock, so they
 * are read under it too.
 */
static int host_mutation_state_under_lock(const capture_library_host *host)
{
    if (host->abandoned) {
        return HOST_IS_QUARANTINED;
    }
    if (host->native_release_in_progress) {
        return HOST_RELEASE_IN_FLIGHT;
    }
    if (deferred_release_pending) {
        /*
         * A release is queued behind a call that is still running.  It is
         * owed and it is not made yet, so a mutating request made now would
         * be a second call into the library while the first one runs.
         */
        return HOST_RELEASE_PENDING;
    }
    if (host->native_mutation_reserved) {
        /*
         * One of this adapter's own native calls is reserved and has not
         * returned.
         */
        return HOST_NATIVE_CALL_IN_FLIGHT;
    }
    if (host->native_release_uncertain) {
        return HOST_RELEASE_UNRESOLVED;
    }
    if (host->recovery_required) {
        return HOST_NEEDS_RECOVERY;
    }
    return HOST_MAY_MUTATE;
}

/*
 * The same, for the callers that do not hold the lock.
 *
 * The lock is released before the caller does anything else, so that no lock
 * is held across a call into the native library, which can reenter.
 */
static int host_mutation_state(capture_library_host *host)
{
    int state;

    backend_lock();
    state = host_mutation_state_under_lock(host);
    backend_unlock();
    return state;
}

/*
 * The reason a state refuses a request, and the code it carries.
 *
 * The quarantine's own wording belongs to each caller, so this covers only
 * the three states this component added.
 */
static const char *host_mutation_refusal_text(int state, int *code)
{
    if (state == HOST_IS_QUARANTINED) {
        *code = CAPTURE_LIBRARY_ERROR_QUARANTINED;
        return "this host is quarantined after an unresolved native operation";
    }
    if (state == HOST_RELEASE_IN_FLIGHT) {
        *code = CAPTURE_LIBRARY_ERROR_RELEASE_IN_FLIGHT;
        return "the native release of the still's first image is still in "
               "flight, so this host makes no further request until it has "
               "finished";
    }
    if (state == HOST_RELEASE_PENDING) {
        *code = CAPTURE_LIBRARY_ERROR_RELEASE_PENDING;
        return "the native release of the still's first image is queued "
               "behind a native call that is still running, so this host makes "
               "no further request until that call has returned";
    }
    if (state == HOST_NATIVE_CALL_IN_FLIGHT) {
        *code = CAPTURE_LIBRARY_ERROR_NATIVE_CALL_IN_FLIGHT;
        return "a native call into the library is still running, so this host "
               "makes no further request until it has returned";
    }
    if (state == HOST_RELEASE_UNRESOLVED) {
        *code = CAPTURE_LIBRARY_ERROR_QUARANTINED;
        return "the outcome of the native release of the still's first image "
               "is unresolved, so this host makes no further request";
    }
    if (state == HOST_NEEDS_RECOVERY) {
        *code = CAPTURE_LIBRARY_ERROR_RECOVERY_REQUIRED;
        return "an extra still's set arrived after the first one was latched "
               "and it could not be associated with the request, so this host "
               "needs recovery and makes no further request";
    }
    *code = 0;
    return 0;
}

/*
 * What a mutating native call needs, taken under the lock together with
 * the reservation.
 *
 * The host may be closed and freed while the call is running, so the call
 * must not read the host then.  Everything it needs is therefore captured
 * here, in the same locked step in which the reservation is taken.
 */
typedef struct host_native_call {
    /* This host's identity: what says whether the host is still the live
     * one when the call returns. */
    uint32_t identity;
    /* The client the call is made on. */
    unsigned int handle;
    /* Whether the host reports what it does; the host may be gone by the
     * time the call returns, so it is read now. */
    int verbose;
    capture_library_mm_camera_capture_still capture_still;
    capture_library_mm_camera_set_usr_attributes set_usr;
    capture_library_mm_camera_set_var_attributes set_var;
    capture_library_set_capture_mode_user_data set_capture_mode;
} host_native_call;

/*
 * Whether the call being reserved arms a request.
 *
 * The arming has to happen in the same locked step as the reservation: a
 * callback that arrives after the reservation finds the request armed, and
 * one that arrived before it would not have been ours to take.
 */
#define HOST_CALL_PLAIN 0
#define HOST_CALL_ARMS_REQUEST 1

/*
 * Reserve one of this adapter's mutating native calls, or refuse it.
 *
 * The admission check and the reservation are made in the **same locked
 * step**: a check made in one step and a call made in another leaves a
 * window in which the library's own callback arrives, and the callback
 * would then make a native release while the call is running.  With the
 * reservation taken here, such a callback finds it and queues its release
 * instead.
 *
 * Everything the call needs is taken with them, because the host may be
 * closed and freed while the call runs.
 *
 * Returns 0 when the reservation was taken, and -1 when the host refused;
 * the reason is then in the host's own error, and the code in `code`.
 */
static int host_reserve_native_call(capture_library_host *host, int *code,
                                    const char **reason_out,
                                    host_native_call *call, int arms_request)
{
    const char *reason;
    int state;

    backend_lock();
    state = host_mutation_state_under_lock(host);
    reason = host_mutation_refusal_text(state, code);
    if (reason == 0) {
        host->native_mutation_reserved = 1;
        call->identity = host->identity;
        call->handle = host->handle;
        call->verbose = host->verbose;
        call->capture_still = host->capture_still;
        call->set_usr = host->set_usr;
        call->set_var = host->set_var;
        call->set_capture_mode = host->set_capture_mode_user_data;
        if (arms_request != 0) {
            /*
             * Arm the experimental arrival-order heuristic before the
             * submission.  A delayed earlier result can still be mistaken
             * for this request.
             *
             * The latch of the previous request is cleared here as well:
             * this is a new request, and it latches its own first result.
             */
            host->sets_at_submit = host->sets_delivered;
            host->awaiting_completion = 1;
            host->completion_seen = 0;
            host->result_latched = 0;
        }
        *reason_out = 0;
    } else {
        snprintf(host->error, sizeof(host->error), "%s", reason);
        *reason_out = reason;
    }
    backend_unlock();
    return (reason == 0) ? 0 : -1;
}

/*
 * Record a reason on the host, if it is still the live one.
 *
 * The host may have been closed and freed while the native call was
 * running, and a freed host's address can be given to a new one, so the
 * identity is what says whether the host is still there.
 */
static void host_set_error_if_live(capture_library_host *host, uint32_t identity,
                                   const char *what)
{
    backend_lock();
    if (host_is_live_under_lock(host, identity)) {
        host_error(host, what);
    }
    backend_unlock();
}

/*
 * The end of one of this adapter's mutating native calls.
 *
 * The reservation is cleared and a release that the callback queued while
 * the call was running is claimed, both under the lock and in one step: a
 * callback that arrives after this finds no reservation, so it releases the
 * plane itself.
 *
 * The claimed release is then made with the lock dropped - it is a call into
 * the library - and the host is written to afterwards only when the
 * identity says it is still the live one.  The host may have been closed
 * and freed while the call was running, so the queued release is made with
 * what was captured, never with a host that is not there any more.
 */
static void host_end_native_call(capture_library_host *host, uint32_t identity)
{
    capture_library_release_capture_buffer release = 0;
    uint32_t address = 0u;
    int verbose = 0;
    int claimed = 0;

    backend_lock();
    if (host_is_live_under_lock(host, identity)) {
        host->native_mutation_reserved = 0;
    }
    if (deferred_release_pending && deferred_release_identity == identity) {
        deferred_release_pending = 0;
        release = deferred_release_fn;
        address = deferred_release_address;
        verbose = deferred_release_verbose;
        deferred_release_fn = 0;
        deferred_release_address = 0u;
        deferred_release_identity = 0u;
        deferred_release_verbose = 0;
        claimed = 1;
        if (host_is_live_under_lock(host, identity)) {
            /*
             * The release is in flight now, so the host takes no further
             * mutating request until it has returned.
             */
            host->native_release_in_progress = 1;
        }
    }
    backend_unlock();

    if (!claimed) {
        return;
    }

    {
        int failed;

        failed = release(CAPTURE_LIBRARY_RELEASE_TYPE_FIRST_IMAGE, address);
        backend_lock();
        if (failed != 0) {
            /*
             * The outcome of the release is unresolved: the library's own
             * call says nothing about what the camera did with the plane, so
             * a later call would release the same address twice.  This
             * process opens no further host, and the release is not retried.
             * The quarantine is the process's own state, so it is written
             * whether or not the host is still there.
             */
            host_reuse_is_forbidden = 1;
        }
        if (host_is_live_under_lock(host, identity)) {
            host->native_release_in_progress = 0;
            if (failed != 0) {
                host->native_release_uncertain = 1;
                host_error(host, "the library refused to release the still's "
                                 "first image; whether the plane was released "
                                 "is unresolved, so this host makes no further "
                                 "request and this process opens no further "
                                 "host");
            }
        } else if (failed != 0 && verbose) {
            fprintf(stderr, "capture-host: the library refused to release "
                    "the still's first image\n");
        }
        backend_unlock();
    }
}

/* ------------------------------------------------------------------ */
/* The session backend.                                                */
/* ------------------------------------------------------------------ */

static int backend_open(void *self, capture_session_error *error)
{
    capture_library_host *host = (capture_library_host *)self;
    const char *reason;
    int state;
    int code = 0;

    if (host == 0 || !host->opened) {
        if (error != 0) {
            error->code = 1;
            error->message = "the capture library is not hosted";
        }
        return -1;
    }
    state = host_mutation_state(host);
    if (state == HOST_IS_QUARANTINED) {
        if (error != 0) {
            error->code = CAPTURE_LIBRARY_ERROR_QUARANTINED;
            error->message = "this host is quarantined: the outcome of an "
                             "earlier native request is unresolved, so it is "
                             "not reused";
        }
        return -1;
    }
    /*
     * A host that needs recovery, or whose native release is unresolved, is
     * not opened for a new session either.  The bytes that were copied stay
     * the caller's - they are collected through the session that is already
     * open - but this host does not take another request.
     */
    reason = host_mutation_refusal_text(state, &code);
    if (reason != 0) {
        host_set_error(host, reason);
        if (error != 0) {
            error->code = code;
            error->message = host->error;
        }
        return -1;
    }
    return 0;
}

static int backend_set_setting(void *self, uint32_t native_id, uint32_t value,
                                capture_session_error *error)
{
    capture_library_host *host = (capture_library_host *)self;
    int is_variable;
    int library_id;
    const char *reason;
    int state;
    int code = 0;

    if (host == 0) {
        if (error != 0) {
            error->code = 1;
            error->message = "no host";
        }
        return -1;
    }
    state = host_mutation_state(host);
    if (state == HOST_IS_QUARANTINED) {
        /*
         * A quarantined host makes no further mutating request.
         *
         * The host is quarantined because the outcome of an earlier native
         * request is unresolved: it may still be queued.  That may be a
         * still's command or a setting's value.  A setting written now
         * would change what the camera does with a request whose outcome is
         * not known, so the host takes none - and the library is not asked
         * at all.
         */
        if (error != 0) {
            error->code = CAPTURE_LIBRARY_ERROR_QUARANTINED;
            error->message = "this host is quarantined: the outcome of an "
                             "earlier native request is unresolved, so it takes "
                             "no further setting";
        }
        return -1;
    }
    /*
     * The same for a host whose native release is unresolved or whose result
     * needs recovery: a setting written now would change what the camera
     * does with a request whose outcome this host cannot state.
     */
    reason = host_mutation_refusal_text(state, &code);
    if (reason != 0) {
        host_set_error(host, reason);
        if (error != 0) {
            error->code = code;
            error->message = host->error;
        }
        return -1;
    }
    library_id = library_setting_for(native_id, &is_variable);
    if (library_id < 0) {
        /*
         * The library's interface does not name this setting.
         *
         * The camera's raw switches are the firmware's own user data: 122
         * is USERDATA_RAWQUALITY, 123 USERDATA_RAW_SAVE, 124
         * USERDATA_SSIF_RAW, 125 USERDATA_RAW_BIT and 126
         * USERDATA_RAW_PACK.  The library's interface does not name a way
         * to write them, and the library's own user-data numbering is not
         * the firmware's - the library's ISO is the attribute 0x1104,
         * whose low byte is 4, while the firmware's ISO is 5.  So a
         * guessed id would be a guess, and the setting is refused.
         */
        host_set_error(host, "the library's interface does not name that "
                             "setting");
        if (error != 0) {
            error->code = 1;
            error->message = host->error;
        }
        return -1;
    }
    if (native_id == CAPTURE_SESSION_IMAGE_QUALITY) {
        uint32_t api_value;

        /*
         * The quality is the camera's own position, and the library takes
         * its own tagged form.  A value that is not one of the camera's
         * positions is refused here, and it is refused whole: it is not
         * trimmed down to something the library would take.
         */
        if (quality_api_value(value, &api_value) != 0) {
            host_set_error(host, "the image quality is not one of the "
                                 "camera's eight positions");
            if (error != 0) {
                error->code = 3;
                error->message = host->error;
            }
            return -1;
        }
        value = api_value;
    }
    if (is_variable) {
        int status;
        host_native_call call;

        if (host->set_var == 0) {
            host_set_error(host, "the library does not export the variable "
                                 "settings");
            if (error != 0) {
                error->code = 2;
                error->message = host->error;
            }
            return -1;
        }
        /*
         * The check, the reservation and what the call needs are taken in
         * one locked step: the host may be freed while the call runs.
         */
        if (host_reserve_native_call(host, &code, &reason, &call,
                                     HOST_CALL_PLAIN) != 0) {
            if (error != 0) {
                error->code = code;
                error->message = reason;
            }
            return -1;
        }

        status = call.set_var(call.handle, library_id, value);
        host_end_native_call(host, call.identity);
        if (status != SETTING_STATUS_OK) {
            const char *why;

            if (setting_send_is_uncertain(status)) {
                /*
                 * The send failed and it is not known whether the value went
                 * out.  The same queued-send uncertainty the still's trigger
                 * has applies here, so the host is quarantined: it takes no
                 * further setting, no capture mode and no request, and this
                 * process opens no further host.
                 *
                 * The value is not written again: a second write would be a
                 * second value, and the first may yet reach the camera.
                 */
                why = variable_send_uncertain_reason;
                quarantine_the_host(host, call.identity);
                host_set_error_if_live(host, call.identity, why);
            } else {
                /*
                 * The library refused the value before it sent anything, so
                 * the host is not left in doubt and stays usable.
                 */
                why = variable_refused_reason;
                host_set_error_if_live(host, call.identity, why);
            }
            if (error != 0) {
                error->code = 2;
                error->message = why;
            }
            return -1;
        }
        return 0;
    }
    if (host->set_usr == 0) {
        host_set_error(host, "the library does not export the user settings");
        if (error != 0) {
            error->code = 3;
            error->message = host->error;
        }
        return -1;
    }
    {
        int status;
        host_native_call call;

        /*
         * The check, the reservation and what the call needs are taken in
         * one locked step: the host may be freed while the call runs.
         */
        if (host_reserve_native_call(host, &code, &reason, &call,
                                     HOST_CALL_PLAIN) != 0) {
            if (error != 0) {
                error->code = code;
                error->message = reason;
            }
            return -1;
        }

        status = call.set_usr(call.handle, library_id, value);
        host_end_native_call(host, call.identity);
        if (status != SETTING_STATUS_OK) {
            const char *why;

            if (setting_send_is_uncertain(status)) {
                why = user_send_uncertain_reason;
                quarantine_the_host(host, call.identity);
                host_set_error_if_live(host, call.identity, why);
            } else {
                why = user_refused_reason;
                host_set_error_if_live(host, call.identity, why);
            }
            if (error != 0) {
                error->code = 3;
                error->message = why;
            }
            return -1;
        }
    }
    return 0;
}

static int backend_set_sensor_retention(void *self, int keep,
                                        capture_session_error *error)
{
    capture_library_host *host = (capture_library_host *)self;

    (void)keep;
    if (host == 0) {
        if (error != 0) {
            error->code = 6;
            error->message = "no host";
        }
        return -1;
    }
    /*
     * The sensor retention switch.
     *
     * The still's set carries a first image when the camera's own raw
     * configuration is on, and the set's type says whether it is there.  So
     * the samples come with the still, and there is no separate switch to
     * set here.
     *
     * This implementation has **no verified way to ask** the camera to keep
     * them: the camera's own raw switch is among the candidates for what
     * turns that configuration on, but nothing here establishes that
     * SSIF_RAW alone controls this sheet's configuration, and the library's
     * interface names no call that has been verified to write it.  So the
     * ask is refused rather than answered with a guessed id.
     */
    host_set_error(host,
                   "this implementation has no verified way to ask the camera "
                   "to keep the sensor samples; the still's set carries a "
                   "first image when the camera's own raw configuration is "
                   "on");
    if (error != 0) {
        error->code = 6;
        error->message = host->error;
    }
    return -1;
}

static int backend_submit_still(void *self, const uint8_t *body,
                                size_t body_size, capture_session_error *error)
{
    capture_library_host *host = (capture_library_host *)self;
    uint32_t command;
    uint32_t command_index;
    uint32_t cap;
    uint32_t max;
    uint32_t remaining;
    uint64_t free_space;
    const char *reason;
    int state;
    int code = 0;
    host_native_call call;
    int status = 0;
    /*
     * The reason a failed send carries.  It is a static string, and not the
     * host's own error buffer, because the host may be closed and freed
     * while the native call was running: the caller's error may outlive the
     * host it names.
     */
    static const char *const still_send_failed_reason =
        "the library reported a failed still request, but the command may "
        "still be queued; the outcome is unresolved, so the host is "
        "quarantined and this process opens no further host";

    if (host == 0 || body == 0 || body_size < 0x20u) {
        if (error != 0) {
            error->code = 4;
            error->message = "the still request is not usable";
        }
        return -1;
    }
    state = host_mutation_state(host);
    if (state == HOST_IS_QUARANTINED) {
        if (error != 0) {
            error->code = CAPTURE_LIBRARY_ERROR_QUARANTINED;
            error->message = "this host is quarantined: the outcome of an "
                             "earlier native request is unresolved, so it takes "
                             "no further request";
        }
        return -1;
    }
    /*
     * A host whose native release is unresolved, or whose result needs
     * recovery, takes no further request: the camera may still be acting on
     * what this host sent, and this host cannot say what it published.
     */
    reason = host_mutation_refusal_text(state, &code);
    if (reason != 0) {
        host_set_error(host, reason);
        if (error != 0) {
            error->code = code;
            error->message = host->error;
        }
        return -1;
    }

    /*
     * The request body says which still was asked for, in its first word.
     * The library's trigger takes the **index** of the command in the
     * library's own table, not a pointer to the command: the library
     * resolves the index and refuses anything outside one to twenty-eight
     * (fact submit.mm-camera-command-is-an-index).
     *
     * The commands the backend maps are the plain still, whose index is
     * one, the AE bracket, whose index is five, and the device-shutter
     * still, whose index is twenty-one.  Any other command word is refused
     * here rather than being turned into an index by a guess.
     */
    command = read_u32(body + 0x00);
    if (command == CAPTURE_LIBRARY_STILL_COMMAND_PLAIN) {
        command_index = CAPTURE_LIBRARY_STILL_PLAIN_INDEX;
    } else if (command == CAPTURE_LIBRARY_STILL_COMMAND_BRACKET) {
        command_index = CAPTURE_LIBRARY_STILL_BRACKET_INDEX;
    } else if (command == CAPTURE_LIBRARY_STILL_COMMAND) {
        command_index = CAPTURE_LIBRARY_STILL_COMMAND_INDEX;
    } else {
        host_set_error(host,
                       "the library's command table does not hold that still "
                       "command");
        if (error != 0) {
            error->code = 4;
            error->message = host->error;
        }
        return -1;
    }

    if (command_index < 1u || command_index > 0x1cu) {
        host_set_error(host, "the library does not accept that command index");
        if (error != 0) {
            error->code = 4;
            error->message = host->error;
        }
        return -1;
    }

    cap = read_u32(body + 0x04);
    max = read_u32(body + 0x08);
    remaining = read_u32(body + 0x14);
    free_space = (uint64_t)read_u32(body + 0x18) |
                 ((uint64_t)read_u32(body + 0x1c) << 32);

    /*
     * Arm the experimental arrival-order heuristic before submission.
     * A delayed earlier result can still be mistaken for this request.
     *
     * The state is checked again under the lock, here, because this is the
     * last point before the request goes out - and the check, the
     * reservation and the arming are made in that one locked step.
     */
    if (host_reserve_native_call(host, &code, &reason, &call,
                                 HOST_CALL_ARMS_REQUEST) != 0) {
        if (error != 0) {
            error->code = code;
            error->message = reason;
        }
        return -1;
    }

    /*
     * Third argument is the scalar facade command-table index. The
     * remaining positional argument mapping is experimental and is not
     * established by stub tests. Do not execute this native path merely
     * because the host tests pass.
     */
    status = call.capture_still(call.handle, 0, command_index, cap, max, 0u,
                                remaining, 0u,
                                (uint32_t)(free_space & 0xffffffffu),
                                (uint32_t)(free_space >> 32), 0u, 0u, 0, 0, 0u, 0u,
                                0u);
    /*
     * The reservation ends here, and a release the callback queued is
     * claimed and made after the call has returned.
     */
    host_end_native_call(host, call.identity);
    if (status != 0) {
        /*
         * The native send reported a failure, and that is **not** a
         * statement that the command was not sent.
         *
         * The native send status can be -1 while the command is still
         * queued, and a later wake can transmit the queued work.  So the
         * outcome of this request is unresolved: it may never be sent, and it
         * may be sent and answered after this call has failed.
         *
         * So nothing here treats the request as certainly unsent.  It stays
         * outstanding - the state that says a completion may still arrive is
         * not cleared - the host is quarantined, and this process opens no
         * further host: the library's registrations survive the client, so a
         * delivery that arrives later would land in the next one.
         *
         * The request is **not** retried either.  A retry would be a second
         * request, and the first one may yet be answered.
         *
         * What this does not say: that the request can be cancelled, or that
         * the camera is in a state a later run could pick up.  Nothing here
         * shows either.
         */
        quarantine_the_host(host, call.identity);
        host_set_error_if_live(host, call.identity, still_send_failed_reason);
        if (error != 0) {
            error->code = 5;
            error->message = still_send_failed_reason;
        }
        return -1;
    }
    return 0;
}

static int backend_collect_image(void *self, capture_session_image *out,
                                 capture_session_error *error)
{
    capture_library_host *host = (capture_library_host *)self;

    if (host == 0 || out == 0) {
        if (error != 0) {
            error->code = 8;
            error->message = "there is no host to collect from";
        }
        return -1;
    }
    if (host_mutation_state(host) == HOST_IS_QUARANTINED) {
        if (error != 0) {
            error->code = CAPTURE_LIBRARY_ERROR_QUARANTINED;
            error->message = "this host is quarantined: the outcome of an "
                             "earlier native request is unresolved, so it hands "
                             "out no result";
        }
        return -1;
    }
    /*
     * The result that was copied is handed out whatever the host's own state
     * is otherwise.  A native release whose outcome is unresolved, and an
     * extra result that needs recovery, are both about further work: the
     * bytes that were copied are the caller's, and hiding them behind those
     * states would throw away the only result this component has.
     */

    backend_lock();
    if (!host->waiting.valid) {
        backend_unlock();
        return 0;
    }
    if (host->held.raw_bytes != 0 || host->held.jpeg_bytes != 0) {
        /*
         * The previous result is still held, so the caller still has its
         * bytes.  The session releases the previous still's bytes before it
         * submits the next one, so this cannot happen through the session -
         * and if it does, the held bytes are not freed, because the caller
         * may still be reading them.
         */
        backend_unlock();
        snprintf(host->error, sizeof(host->error),
                 "the backend still holds the previous still's bytes");
        if (error != 0) {
            error->code = 8;
            error->message = host->error;
        }
        return -1;
    }

    /*
     * The result moves from waiting to held.  The bytes stay valid until
     * the session hands them back, and a later still cannot take them
     * away: the two are separate slots, and the copy is freed only by the
     * release.
     */
    host->held = host->waiting;
    memset(&host->waiting, 0, sizeof(host->waiting));
    host->awaiting_completion = 0;

    memset(out, 0, sizeof(*out));
    /*
     * The samples.
     *
     * **This is an experimental mapping, not a proven sensor output.**  The
     * bytes are the first image of the still's set - the one the firmware
     * calls its "Raw Compress" image - and a native name is not a
     * provenance.  Whether those bytes are the sensor's own samples, and in
     * what layout, is not established by anything in this component.
     */
    out->sensor_data = host->held.raw_bytes;
    out->sensor_size_bytes = host->held.raw_size;
    /*
     * The geometry is the set's own: the first image's descriptor states the
     * width, the height and the two strides, and the capture library's own
     * diagnostic print names those four words.  The number of bits a sample
     * occupies is not among them, so it stays unknown - and unknown is zero.
     *
     * The stride stays zero as well.  The descriptor states two stride
     * words, but their **unit is not established**, so this component does
     * not report one of them as a stride in bytes: the words are kept
     * internally as they are, and the reported stride is zero rather than a
     * number whose unit is a guess.
     */
    out->width = host->held.raw_width;
    out->height = host->held.raw_height;
    out->stride_bytes = 0u;
    out->bits_per_sample = 0u;
    out->jpeg_data = host->held.jpeg_bytes;
    out->jpeg_size_bytes = host->held.jpeg_size;

    backend_unlock();
    return 1;
}

static int backend_release_buffer(void *self, void *buffer, size_t size,
                                  capture_session_error *error)
{
    capture_library_host *host = (capture_library_host *)self;

    if (host == 0 || buffer == 0) {
        if (error != 0) {
            error->code = 7;
            error->message = "there is no host, or no buffer";
        }
        return -1;
    }

    backend_lock();
    if (host->held.raw_bytes != 0 && buffer == host->held.raw_bytes &&
        size == host->held.raw_size) {
        free(host->held.raw_bytes);
        host->held.raw_bytes = 0;
        host->held.raw_size = 0u;
        backend_unlock();
        return 0;
    }
    if (host->held.jpeg_bytes != 0 && buffer == host->held.jpeg_bytes &&
        size == host->held.jpeg_size) {
        free(host->held.jpeg_bytes);
        host->held.jpeg_bytes = 0;
        host->held.jpeg_size = 0u;
        backend_unlock();
        return 0;
    }
    backend_unlock();

    /*
     * The buffer is not one of ours.  The session hands back exactly the
     * pointers it was given, so this is a mismatch, and it is reported
     * rather than guessed at.
     */
    host_set_error(host,
                   "the backend was asked to release a buffer it does not hold");
    if (error != 0) {
        error->code = 7;
        error->message = host->error;
    }
    return -1;
}

static void backend_close(void *self)
{
    capture_library_host *host = (capture_library_host *)self;

    if (host == 0) {
        return;
    }
    /*
     * The close of a session.
     *
     * What is freed here is this component's own: the bytes of a result
     * that arrived and was never collected, and the state that says a
     * request is outstanding.  The session hands back the bytes it holds
     * before it calls this, so the held slot is normally empty; it is
     * cleared here anyway, so that a close can never leave a buffer behind.
     *
     * **This does not cancel the native request.**  Nothing here shows the
     * camera that the request is off, and the native route cannot be asked
     * which request a later delivery belongs to.  So a close that found a
     * request outstanding makes the host fail closed: the host is not
     * reused, and this process opens no further host.
     */
    backend_lock();
    if (host->awaiting_completion) {
        host->abandoned = 1;
        host_reuse_is_forbidden = 1;
        if (host->verbose) {
            fprintf(stderr,
                    "capture-host: the session is closed with a still request "
                    "still outstanding; the host is not reused\n");
        }
    }
    if (host->native_release_in_progress) {
        /*
         * A native release was still in flight when the session was closed,
         * so its outcome was not established: the collection may already have
         * cleared the outstanding request.  This process opens no further
         * host, and the host is not reused.
         */
        host->abandoned = 1;
        host_reuse_is_forbidden = 1;
        if (host->verbose) {
            fprintf(stderr,
                    "capture-host: the session is closed while a native "
                    "release is in flight; the host is not reused\n");
        }
    }
    if (host->native_mutation_reserved) {
        /*
         * One of this adapter's own native calls was still running when the
         * session was closed, so its outcome was not established either.
         */
        host->abandoned = 1;
        host_reuse_is_forbidden = 1;
        if (host->verbose) {
            fprintf(stderr,
                    "capture-host: the session is closed while a native call "
                    "is running; the host is not reused\n");
        }
    }
    if (deferred_release_pending) {
        /*
         * A release is still queued behind a call that is running: it is
         * owed and it is not made yet.
         */
        host->abandoned = 1;
        host_reuse_is_forbidden = 1;
    }
    host->awaiting_completion = 0;
    host->completion_seen = 0;
    if (host->waiting.raw_bytes != 0) {
        free(host->waiting.raw_bytes);
    }
    host->waiting.raw_bytes = 0;
    host->waiting.raw_size = 0u;
    if (host->waiting.jpeg_bytes != 0) {
        free(host->waiting.jpeg_bytes);
    }
    host->waiting.jpeg_bytes = 0;
    host->waiting.jpeg_size = 0u;
    host->waiting.valid = 0;
    if (host->held.raw_bytes != 0) {
        free(host->held.raw_bytes);
    }
    host->held.raw_bytes = 0;
    host->held.raw_size = 0u;
    if (host->held.jpeg_bytes != 0) {
        free(host->held.jpeg_bytes);
    }
    host->held.jpeg_bytes = 0;
    host->held.jpeg_size = 0u;
    host->held.valid = 0;
    backend_unlock();
}

void capture_library_host_set_copy(capture_library_host *host,
                                   capture_library_copy_fn copy)
{
    if (host == 0) {
        return;
    }
    backend_lock();
    host->copy = (copy != 0) ? copy : default_copy;
    backend_unlock();
}

int capture_library_host_set_capture_mode(capture_library_host *host,
                                          uint32_t mode)
{
    const char *reason;
    int state;
    int code = 0;
    host_native_call call;
    int status;

    if (host == 0) {
        return -1;
    }
    state = host_mutation_state(host);
    if (state == HOST_IS_QUARANTINED) {
        /*
         * A quarantined host makes no further mutating request: the
         * capture mode changes what the camera does with a request whose
         * outcome is unresolved, so the host takes none.
         */
        host_set_error(host, "this host is quarantined: the outcome of an "
                             "earlier native request is unresolved, so it takes "
                             "no further capture mode");
        return -1;
    }
    /*
     * And so does a host whose native release is unresolved, or whose result
     * needs recovery.
     */
    reason = host_mutation_refusal_text(state, &code);
    if (reason != 0) {
        host_set_error(host, reason);
        return -1;
    }
    if (mode > CAPTURE_LIBRARY_CAPTURE_MODE_MAX) {
        /*
         * The library refuses a mode above four and writes nothing, so the
         * refusal happens here where it can be reported.
         */
        host_set_error(host, "the library's capture mode goes from zero to "
                             "four");
        return -1;
    }
    if (host->set_capture_mode_user_data == 0) {
        host_set_error(host, "the library does not export a capture-mode "
                             "setter");
        return -1;
    }
    /*
     * The setter is a C++ member function whose own body never reads its
     * object, so a null object is what it is given.
     *
     * The check, the reservation and what the call needs are taken in one
     * locked step: the host may be freed while the call runs.
     */
    if (host_reserve_native_call(host, &code, &reason, &call,
                                 HOST_CALL_PLAIN) != 0) {
        return -1;
    }

    status = call.set_capture_mode(0, (unsigned char)mode);
    /*
     * The reservation ends here, and a release the callback queued is
     * claimed and made after the call has returned.
     */
    host_end_native_call(host, call.identity);
    if (status != 0) {
        host_set_error_if_live(host, call.identity,
                               "the library refused that capture mode");
        return -1;
    }
    if (call.verbose) {
        fprintf(stderr, "capture-host: the capture mode is %lu\n",
                (unsigned long)mode);
    }
    return 0;
}

int capture_library_host_heard_from_the_camera(const capture_library_host *host)
{
    int heard;

    if (host == 0) {
        return 0;
    }
    backend_lock();
    heard = host->heard_from_the_camera;
    backend_unlock();
    return heard;
}

uint32_t capture_library_host_last_set_type(const capture_library_host *host)
{
    uint32_t type;

    if (host == 0) {
        return 0u;
    }
    backend_lock();
    type = host->last_set_type;
    backend_unlock();
    return type;
}

/*
 * What this host's own state says about reuse.
 *
 * This is a read-only observation of the host's bookkeeping.  It says whether
 * a result was copied, whether the host may make another request, and which
 * of the two states keeps it from making one.  It says nothing about the
 * camera: a host that may make a request is not a host whose request will be
 * answered, and a host that may not is not necessarily broken.
 */
int capture_library_host_read_health(const capture_library_host *host,
                                     capture_library_host_health *out)
{
    if (host == 0 || out == 0) {
        return -1;
    }
    backend_lock();
    memset(out, 0, sizeof(*out));
    /*
     * A result that was copied is either waiting to be collected or held by
     * the session: both are this process's own bytes, and both are the
     * caller's to release.
     */
    out->has_result = (host->waiting.raw_bytes != 0 ||
                       host->waiting.jpeg_bytes != 0 ||
                       host->held.raw_bytes != 0 ||
                       host->held.jpeg_bytes != 0) ? 1 : 0;
    out->recovery_required = host->recovery_required;
    out->release_unresolved = host->native_release_uncertain;
    out->release_in_progress = host->native_release_in_progress;
    out->release_pending = deferred_release_pending ? 1 : 0;
    /*
     * The one answer the caller needs: may this host be used for another
     * request?  A quarantined host, a host whose native release is
     * unresolved and a host that needs recovery all may not.
     */
    out->may_mutate =
        (host_mutation_state_under_lock(host) == HOST_MAY_MUTATE) ? 1 : 0;
    backend_unlock();
    return 0;
}

void capture_library_host_backend(capture_library_host *host,
                                   capture_session_backend *backend)
{
    if (host == 0 || backend == 0) {
        return;
    }
    memset(backend, 0, sizeof(*backend));
    backend->open = backend_open;
    backend->set_setting = backend_set_setting;
    backend->set_sensor_retention = backend_set_sensor_retention;
    backend->submit_still = backend_submit_still;
    backend->collect_image = backend_collect_image;
    backend->release_buffer = backend_release_buffer;
    backend->close = backend_close;
    backend->self = host;
}
