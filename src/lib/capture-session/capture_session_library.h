#ifndef CAPTURE_SESSION_LIBRARY_H
#define CAPTURE_SESSION_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#include "capture_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The backend that hosts the camera's own capture library.
 *
 * **This is an experimental component.**  It is host-tested against a stub
 * of the library; it has not been run against the camera, and the gaps are
 * listed at the end of this file.
 *
 * Our program becomes the client, the library provides the transport, the
 * capture context and the still.  The stock application is not involved,
 * and nothing is written to the inter-processor channel by us.
 *
 * The library's own C interface is used, not its internals.  The interface
 * was recovered from the pinned library
 * (sha256 fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7)
 * with Ghidra 12.1.3, and the evidence is in
 * output/decompile/rea/pass-minimax-next-edge-20261023/backend-report.md.
 *
 * The recovered facade exposes client creation, settings, subscriptions and
 * still submission. mm_camera_create calls realization but discards its
 * status; success therefore does not establish initialization. The still
 * facade takes a scalar command-table index (21 selects 0x01010016).
 * Remaining argument-to-body wiring in this adapter is experimental.
 *
 * What is not established, and what this backend therefore does not claim:
 *
 *   - which notification id carries the completion;
 *   - that the set's first image is the sensor's own samples: the firmware
 *     calls that image its "Raw Compress" image, and a native name is not
 *     a provenance.  Mapping that image into the session's `sensor_data` is
 *     this component's own experimental mapping, not a proven sensor output;
 *   - the geometry of those samples.  The descriptor states the width, the
 *     height and two stride words, but the stride words' unit is not
 *     established, so the reported stride stays zero and the number of bits
 *     a sample occupies stays unknown - and unknown is zero.
 */

/*
 * The library's still commands, as the session's request body carries them.
 *
 * The plain still is 0x01010001 and the device-shutter still is 0x01010016.
 * The AE bracket 0x01010005 is the third supported wire identifier: the
 * firmware names capture type 5 `E_CAP_CMD_STL_AE_BRACKET`. That name does
 * not establish an exposure count, per-shot effects or the caller-owned
 * sensor RAW required by PROJECT_GOAL.md.
 *
 * The body's command word says which one was asked for, and the backend
 * maps it.
 */
#define CAPTURE_LIBRARY_STILL_COMMAND_PLAIN   0x01010001u
#define CAPTURE_LIBRARY_STILL_COMMAND_BRACKET 0x01010005u
#define CAPTURE_LIBRARY_STILL_COMMAND         0x01010016u

/*
 * And the *index* of each command, which is what the library's trigger
 * takes.  The third argument is an index into the library's own table, not
 * a pointer to a command id: the library resolves the index and refuses
 * anything outside one to twenty-eight.
 *
 * The table is the library's own, read from the helper that resolves it
 * (`FUN_00042e18`): index 1 selects 0x01010001, index 5 selects
 * 0x01010005, index 21 selects 0x01010016, index 22 selects 0x01010017 and
 * index 23 is **refused**. The low byte of a command word is the firmware's
 * capture type, and the two numberings agree: type 1 is
 * `E_CAP_CMD_STL_NORMAL`, type 5 `E_CAP_CMD_STL_AE_BRACKET` and type 22
 * `E_CAP_CMD_STL_LOCK3A_NORMAL`.
 */
#define CAPTURE_LIBRARY_STILL_PLAIN_INDEX   1u
#define CAPTURE_LIBRARY_STILL_BRACKET_INDEX 5u
#define CAPTURE_LIBRARY_STILL_COMMAND_INDEX 21u
#define CAPTURE_LIBRARY_STILL_MULTI_INDEX   22u

/* The library's command range, checked by mm_camera_capture_still. */
#define CAPTURE_LIBRARY_COMMAND_FIRST 0x1010001u
#define CAPTURE_LIBRARY_COMMAND_LAST  0x101001du

/*
 * The image structure the library's own converter produces.  It is **not**
 * what a client receives on the image route - see the block below - but it
 * is kept here because its layout is the one the library's converter
 * writes, and the offsets after the address set matter.
 */
typedef struct capture_library_addr_set {
    uint32_t type;         /* the remapped address type: 0, 1, 3, 4, 2, 5, 6, 7 */
    uint32_t physical[4];  /* +0x04 */
    uint32_t mapped[4];    /* +0x14 */
} capture_library_addr_set;

/*
 * The block the image route actually delivers.
 *
 * The library's dispatcher builds a 0x88-byte buffer on its own stack and
 * hands the *address* of that buffer to the client.  The buffer is dead when
 * the call returns, so a copy has to be taken during the callback and the
 * pointer must never be kept.
 *
 * **The offsets below are the native ones, and the layout says so.**
 * The library writes the address type at 0x40, the physical mapping input at
 * 0x44, and two separate 32-bit mapped addresses at 0x54 and 0x58.  A plain
 * list of six words would put them at 0, 4, 8, 12, 16 and 20 instead, and
 * the struct would then read words the library never wrote.  A stub built
 * from the same struct cannot catch that - it would agree with itself - so
 * the layout is spelled out with padding and checked by the compiler.
 *
 * The word at 0x58 is **not** the end of the mapped range.  It is a second
 * mapped address, computed as the mapped base plus the width times the
 * height, while the mapper was asked for the width times the height times
 * three halves.  Its role is unresolved, so this struct does not use it, and
 * the difference between the two words is not treated as a byte count.
 *
 * The width and the height are **not** in this block's recovered layout, so
 * this struct does not carry them: the image object the callback also hands
 * over is where the library states them.
 *
 * The still's first image adds one verified field: its byte size at
 * descriptor-relative +0x74 (absolute +0x7c inside the encoded set), read
 * alongside the mapped address, and the library fills the mapped word at
 * descriptor-relative +0x54 (absolute +0x5c) from the physical word before
 * the callback fires. The firmware's own conversion-error text names the
 * first image SRW ("srw address convert error."), so the bytes are the
 * Samsung RAW (the LRC output). See
 * next-edge/still-path/IMAGE-SET-EXTRACTION.md.
 */
typedef struct capture_library_rect_block {
    uint8_t reserved_00[0x40];
    uint32_t addr_type;       /* +0x40: written by the library as 7, virtual only */
    uint32_t source_addr;     /* +0x44: the address the library mapped */
    uint8_t reserved_48[0x0c];
    uint32_t mapped_addr;     /* +0x54: the 32-bit mapped address, 0 if unmapped */
    uint32_t mapped_second;   /* +0x58: a second mapped address, role unresolved */
    uint8_t reserved_5c[0x2c];
} capture_library_rect_block;

/* The size of the block the image route delivers. */
#define CAPTURE_LIBRARY_RECT_BLOCK_SIZE 0x88u

/*
 * The compiler checks the offsets, so a layout change cannot pass silently.
 * A negative array size is a compile error, and that is the point.
 */
typedef char capture_library_rect_addr_type_is_0x40[
    (offsetof(capture_library_rect_block, addr_type) == 0x40u) ? 1 : -1];
typedef char capture_library_rect_source_is_0x44[
    (offsetof(capture_library_rect_block, source_addr) == 0x44u) ? 1 : -1];
typedef char capture_library_rect_mapped_is_0x54[
    (offsetof(capture_library_rect_block, mapped_addr) == 0x54u) ? 1 : -1];
typedef char capture_library_rect_second_is_0x58[
    (offsetof(capture_library_rect_block, mapped_second) == 0x58u) ? 1 : -1];
typedef char capture_library_rect_size_is_0x88[
    (sizeof(capture_library_rect_block) == CAPTURE_LIBRARY_RECT_BLOCK_SIZE)
        ? 1 : -1];

/* The callbacks the library's client registration takes. */
typedef void (*capture_library_notify_callback)(int event, void *data,
                                                 unsigned int size);
/*
 * The image route delivers (id, block, image object, size): argument two is
 * the library's stack block, argument three is the library's own C++ image
 * object, and argument four is always zero on this route.
 */
typedef void (*capture_library_image_callback)(int id, void *rect_block,
                                                void *image_object,
                                                unsigned int size);
typedef void (*capture_library_image_set_callback)(int id, void *image_set_slot,
                                                   void *data, unsigned int size);

/*
 * The library's own description of its C++ image object.
 *
 * The image callback hands the image object over as its third argument, and
 * the library exports the accessors that describe such an object.  These are
 * calls into the library, so what they say is the library's own reading of
 * the image - the geometry of the samples, and the address and the size of
 * the buffer that holds them.
 *
 * The names are C++ names, and the library exports them under their mangled
 * spellings.  They are read from the pinned library
 * (`libcapture-fw-prod.so`, SHA-256 fba89473...c8cc7) with `nm -D`, so they
 * are the library's exported names and not a guess.
 *
 * Every one of them is optional.  A library that does not export one leaves
 * that number unknown, and unknown is zero in this backend - it is never a
 * guessed value.
 */
#define CAPTURE_LIBRARY_IMAGE_GET_WIDTH "_ZN9CCapImage8GetWidthEv"
#define CAPTURE_LIBRARY_IMAGE_GET_HEIGHT "_ZN9CCapImage9GetHeightEv"
#define CAPTURE_LIBRARY_IMAGE_GET_RAW_STRIDE "_ZN9CCapImage12GetRawStrideEv"
#define CAPTURE_LIBRARY_IMAGE_GET_RAW_PIXEL_BIT "_ZN9CCapImage14GetRawPixelBitEv"
#define CAPTURE_LIBRARY_IMAGE_GET_IMAGE_FORMAT "_ZN9CCapImage14GetImageFormatEv"
#define CAPTURE_LIBRARY_IMAGE_GET_BUFFER "_ZN9CCapImage9GetBufferEv"
#define CAPTURE_LIBRARY_BUFFER_GET_VIRT_ADDRESS \
    "_ZN10CCapBuffer14getVirtAddressEPNS_9uCAP_ADDRE"
#define CAPTURE_LIBRARY_BUFFER_GET_SIZE "_ZN10CCapBuffer7getSizeEv"

typedef uint32_t (*capture_library_image_get_u32_fn)(void *image);
typedef void *(*capture_library_image_get_buffer_fn)(void *image);
typedef void (*capture_library_buffer_get_address_fn)(void *buffer,
                                                      void *address);
typedef uint32_t (*capture_library_buffer_get_size_fn)(void *buffer);

/*
 * The image set the library delivers for the still.
 *
 * The still's result arrives on the **image-set** route, not on the image
 * route: the image topics are the liveview and the movie, and the stock
 * photo consumer subscribes to the image-set topics and never to the image
 * topics at all.  So the still's own result is a set.
 *
 * The library's client callback hands the set over as its second argument.
 * Its first word is the *basic set*, and the basic set begins with the
 * encoded-set constituent: the set type, then the four image descriptors
 * (the optional first image, the main image, the screen image and the
 * thumbnail), each 0x88 bytes.
 *
 * The constituent's layout is the accepted one (fact
 * raw.encoded-set-descriptor-writer), and the first word of the set
 * pointing at the basic set is a proposal-level reading of the stock photo
 * consumer (photo-completion-proposal.md, F1).  The backend therefore
 * validates what it decodes - the set type has to be one of the three the
 * serializer writes - and refuses a set it cannot make sense of, rather
 * than copying bytes out of it.
 */
typedef struct capture_library_image_set {
    void *basic_set;         /* +0x00: the basic set, which starts with the
                                encoded-set constituent */
    uint8_t reserved_04[0x2fc];
} capture_library_image_set;

/* The size of the encoded-set constituent the basic set begins with. */
#define CAPTURE_LIBRARY_SET_CONSTITUENT_SIZE 0x228u

/*
 * The set types the firmware's serializer writes.  They come from the
 * order sheet's configuration byte: four, five and six become three, seven
 * and eight become two, and every other value becomes one.  A type of one
 * is the plain configuration and carries no first image; a type of two or
 * three is the raw configuration and does.
 */
#define CAPTURE_LIBRARY_SET_TYPE_PLAIN 1u
#define CAPTURE_LIBRARY_SET_TYPE_RAW_TWO 2u
#define CAPTURE_LIBRARY_SET_TYPE_RAW_THREE 3u

/*
 * The library's C interface, as the library exports it.  These are the
 * names the library exports, so a host program that resolves them by name
 * cannot pick up the wrong function.
 */
typedef int (*capture_library_mm_camera_create)(unsigned int *out_handle);
typedef int (*capture_library_mm_camera_destroy)(unsigned int handle);
typedef int (*capture_library_mm_camera_realize)(unsigned int handle);
typedef int (*capture_library_mm_camera_unrealize)(unsigned int handle);
typedef int (*capture_library_mm_camera_start)(unsigned int handle);
typedef int (*capture_library_mm_camera_stop)(unsigned int handle);
typedef int (*capture_library_mm_camera_set_usr_attributes)(unsigned int handle,
                                                            int id, uint32_t value);
typedef int (*capture_library_mm_camera_set_var_attributes)(unsigned int handle,
                                                            int id, uint32_t value);
/*
 * The notification callback.  The arguments are the handle, the callback
 * and the notification id - the application's own wrapper calls it that way
 * - and a null callback is how the notification is removed.
 */
typedef int (*capture_library_mm_camera_set_notification_callback)(unsigned int handle,
                                                                   void *callback,
                                                                   uint32_t id);
/*
 * The notification ids are a zero-terminated list: the library walks it and
 * stops at the zero or at anything above 0x47.  The application subscribes
 * to every id there is, and the list below is that list, in the order the
 * application gives it.
 */
typedef int (*capture_library_mm_camera_set_notification_id)(unsigned int handle,
                                                              int first_id, ...);
/*
 * The trigger.  The third argument is the **index** of the command in the
 * library's own table, not a pointer to a command id: the library resolves
 * the index and refuses anything outside one to twenty-eight.
 */
typedef int (*capture_library_mm_camera_capture_still)(unsigned int handle,
                                                        void *param2,
                                                        uint32_t command_index,
                                                        uint32_t cap, uint32_t max,
                                                        uint32_t p6, uint32_t p7,
                                                        uint32_t p8, uint32_t p9,
                                                        uint32_t p10, uint32_t p11,
                                                        uint32_t p12, int p13, int p14,
                                                        uint32_t p15, uint32_t p16,
                                                        uint32_t p17);
/*
 * The release.  The library's own `ReleaseCaptureBuffer(type, address)`
 * converts the address to the physical one and sends the release to the
 * firmware, so the type is the image's own format - one is raw, two is
 * ycc - and the address is the image's mapped address.
 */
typedef int (*capture_library_release_capture_buffer)(int type, uint32_t address);

/*
 * The release type of the still's *first* image.
 *
 * The firmware's own capture route registers the first image's plane in
 * the capture-buffer list, and the release with type one selects that list
 * and matches on the address (fact raw.encoded-capture-release-route).  So
 * the type is one for that plane, and the release helper requires an exact
 * address match - a release that does not match releases nothing.
 */
#define CAPTURE_LIBRARY_RELEASE_TYPE_FIRST_IMAGE 1

/*
 * The library's own capture mode.
 *
 * The capture library exports its capture-mode setter as a C++ member
 * function, `_ZN17CAttributeHandler22setCaptureModeUserDataEh`.  The
 * setter's own body never reads its object - it writes a user-data byte and
 * returns - so a host can call it with a null object.
 *
 * The mode is a byte of zero to four.  The library says this much and no
 * more about it:
 *
 *   mode 0 writes user data 0 with the value 0
 *   mode 1 writes user data 0 with the value 11
 *   mode 2 writes user data 0 with the value 1
 *   mode 3 writes user data 0 with the value 8
 *   mode 4 writes user data 0x3a with the value 1
 *
 * Mode four is the odd one out - it writes a *different* user data - and
 * the firmware's raw configuration is the one where the configuration byte
 * is at least four.  That is a well-supported guess and not a fact: the
 * link has to be closed on the firmware side.  So this is exposed under its
 * own name and is not wired to the session's raw setting.
 *
 * The reading is in
 * ../../../output/decompile/rea/pass-minimax-next-edge-20261023/capture-mode-findings.md.
 */
typedef int (*capture_library_set_capture_mode_user_data)(void *self,
                                                          unsigned char mode);

/* The name the capture library exports the capture-mode setter under. */
#define CAPTURE_LIBRARY_SET_CAPTURE_MODE_SYMBOL \
    "_ZN17CAttributeHandler22setCaptureModeUserDataEh"

#define CAPTURE_LIBRARY_CAPTURE_MODE_PLAIN 0u
#define CAPTURE_LIBRARY_CAPTURE_MODE_FOUR 4u
#define CAPTURE_LIBRARY_CAPTURE_MODE_MAX 4u

/*
 * Camera-global preference byte offset, set before adapter initialization.
 * Its value does not establish successful initialization, sole ownership,
 * or which client will receive events.
 */
#define CAPTURE_LIBRARY_POWER_ON_PREF 0x1268u

/*
 * The notification ids the stock application subscribes to: every id the
 * library knows, zero-terminated.
 */
#define CAPTURE_LIBRARY_NOTIFICATION_IDS                                                  \
    10, 0xb, 0xc, 0x30, 0xd, 0xe, 0xf, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,        \
    0x18, 0x19, 1, 2, 3, 4, 5, 6, 7, 0x1a, 0x1b, 0x1c, 0x1e, 0x1d, 0x20, 0x22,          \
    0x39, 0x43, 0x3d, 0x26, 0x3b, 0x27, 0x28, 0x29, 0x2a, 0x2c, 0x2d, 0x2b, 0x2e,        \
    0x38, 0x41, 0x42, 0x33, 0x34, 0x35, 0x36, 0x3a, 0x3c, 8, 9, 0x3f, 0x44, 0x45,        \
    0x46, 0x47, 0
typedef uint32_t (*capture_library_reg_callback_client)(
    char *name, capture_library_notify_callback notify,
    capture_library_image_callback image,
    capture_library_image_set_callback image_set);
/*
 * Registering a client is not the same as subscribing to a topic.  These
 * two calls are what put the client in the publisher's list, and without
 * them the callbacks never fire.
 */
typedef int (*capture_library_reg_image_callback_id)(uint32_t slot, uint32_t id);
typedef int (*capture_library_reg_image_set_callback_id)(uint32_t slot, uint32_t id);

/*
 * The client-visible topic ids.
 *
 * The image topics are the liveview, the display and the movie frames; the
 * image-set topics are the still's encoded, MPO, YCC, UD-YCC, MV-metadata
 * and expansion sets.  **The still's result is an image set, not an
 * image**, and the image topics carry the sensor's own samples never.
 */
#define CAPTURE_LIBRARY_TOPIC_IMAGE_LIVEVIEW_DISPLAY0 0x3030001u
#define CAPTURE_LIBRARY_TOPIC_IMAGE_LIVEVIEW_DISPLAY1 0x3030002u
#define CAPTURE_LIBRARY_TOPIC_IMAGE_MOVIE              0x3030003u
#define CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL       0x4010005u
#define CAPTURE_LIBRARY_TOPIC_SET_MPO_STILL           0x4010003u
#define CAPTURE_LIBRARY_TOPIC_SET_YCC_STILL           0x4010001u
#define CAPTURE_LIBRARY_TOPIC_SET_UD_YCC_STILL        0x4010006u
#define CAPTURE_LIBRARY_TOPIC_SET_MV_META_STILL       0x4010007u
#define CAPTURE_LIBRARY_TOPIC_SET_EXPANSION_STILL     0x4010008u

/*
 * The same set in the library's own internal spelling.
 *
 * The library converts a public topic id into an internal one before it
 * publishes, and the id that reaches the client is the converted one.  The
 * encoded still's id therefore has three spellings - the public one above,
 * the internal one, and the converted one - and the backend accepts the
 * still's set under any of them rather than under one guess.
 */
#define CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL_INTERNAL 0x3010005u
#define CAPTURE_LIBRARY_TOPIC_SET_ENCODED_STILL_CONVERTED 0x2010005u

/*
 * The name the client registers under.
 *
 * The library's registration guard requires a name that is not null and is
 * no longer than nine bytes; a longer name is refused.  Nothing more is
 * known about the name: the guard does not name a role, and no name in
 * particular is required.  This one is five bytes long, so it passes the
 * guard, and that is the whole of the claim made about it.
 */
#define CAPTURE_LIBRARY_CLIENT_NAME "Photo"

/*
 * The library's own setting ids, in the vocabulary mm_camera takes.
 *
 * The user-attribute call takes the id as it is: the ISO is id 4 and the
 * image quality is id 3, and the library's own converter turns the value
 * into the camera's.
 */
#define CAPTURE_LIBRARY_SETTING_ISO 4
#define CAPTURE_LIBRARY_SETTING_QUALITY 3

/*
 * The shutter is **not** a user attribute, and it does not go through the
 * same call.
 *
 * It goes through the library's *variable* call, and that call takes the
 * **camera's own variable id**.  The facade accepts the ids 0x100 to 0x12b
 * and refuses anything outside that range before the write leaves this
 * process, and its converter preserves the shutter id: 0x101 stays 0x101,
 * and the call then emits the message 0x1302 with four bytes of data.
 *
 * So the shutter is written with 0x101, the id the portable layer already
 * names (CAPTURE_SESSION_SHUTTER).  The index 2 that this id ends in is
 * **not** an id this facade accepts: the native index is not the facade's
 * id, and writing it would be refused.
 *
 * The unit the camera stores the shutter in is not established here, so the
 * number that is written is not given one.
 */
#define CAPTURE_LIBRARY_SETTING_SHUTTER 0x101u

/*
 * The image quality's values, in the two forms they come in.
 *
 * The camera's own positions are 0 to 7.  The library takes them tagged and
 * accepts 0x30000 to 0x30007, which it keeps as they are for the wire.  The
 * native values 0x40000 to 0x40007 name the same eight positions, and the
 * library does not take them as they are: they are converted to the
 * library's form before the write.
 *
 * The inverse of the RTOS map is explicit: positions 0, 1, 2 and 3 map to
 * 0x30001, 0x30002, 0x30003 and 0x30000; positions 4 through 7 map to
 * 0x30004 through 0x30007. The backend implements this conversion.
 */
#define CAPTURE_LIBRARY_QUALITY_API_FIRST 0x30000u
#define CAPTURE_LIBRARY_QUALITY_API_LAST 0x30007u
#define CAPTURE_LIBRARY_QUALITY_NATIVE_FIRST 0x40000u
#define CAPTURE_LIBRARY_QUALITY_NATIVE_LAST 0x40007u

/*
 * The statuses the library's user-attribute and variable facades return.
 *
 * Zero is the local Send return, not proof of peer receipt or effect.
 * -2 is a handle the library does not know and -3 is a value or an id
 * its own converter refuses; both of those
 * are decided **before** the send, so neither of them sent anything.
 *
 * Transport errors are collapsed into -1 by the pinned facade; an unknown
 * nonzero status is conservatively unresolved. A transport error does not
 * establish "nothing was sent": the same queued-send uncertainty
 * the still's trigger has applies to a setting.
 */
#define CAPTURE_LIBRARY_STATUS_OK 0
#define CAPTURE_LIBRARY_STATUS_INVALID_HANDLE (-2)
#define CAPTURE_LIBRARY_STATUS_REFUSED (-3)

typedef struct capture_library_host capture_library_host;

/*
 * The backend's own rules, and the assumptions behind them.
 *
 * **The lock is not reentrant.** The library delivers its callbacks from
 * its own dispatch, and a callback must not call into the library while it
 * holds the lock: the inner call would wait for a lock the outer call
 * holds.  So the native release operation - the
 * release of the still's first image - is made with the lock dropped, and
 * everything it needs is taken under the lock before it is. Native image
 * accessors are still called under the lock; their reentry is unproved. The lock is
 * built from the compiler's own atomic compare-and-swap so that it needs
 * nothing from the camera's C library, and that is also what keeps a
 * thread-local depth counter out of reach.
 *
 * **A second host is refused while one is live.** The library allows one
 * camera client, and a second host would take the global away from the
 * live one, so the live host's callbacks would publish into the wrong
 * host.
 *
 * **The library's registrations survive the client's destruction.** So a
 * host opened again in the same process can be handed deliveries that
 * belonged to the earlier one: the client's callback carries the topic and
 * nothing that says which registration it came from, so the backend cannot
 * tell them apart. The close clears the global, so a delivery after a close
 * lands nowhere; the exposure is a reopen in the same process, and the
 * camera ladder uses a separate process for each stage.
 *
 * **A preview never answers a still.** Only a set on the encoded still's
 * topic is read, and only a set that arrived after the submission is taken
 * as its answer.  That ordering is this component's heuristic; the native
 * route does not hand out a request id to match against, so the
 * correlation of a request with the delivery that answers it is an open
 * gap.
 *
 * **The extent of a copy is the buffer's own size.** A size that the
 * library did not state leaves the extent unresolved, and nothing is
 * copied: a copy of the wrong length is worse than no copy.
 *
 * **A host that abandoned a request is not reused.** Closing a session with
 * a still request still outstanding frees this component's own bytes - the
 * result that arrived and was never collected - and clears the state that
 * says a request is outstanding. It does **not** cancel the native request:
 * nothing here shows the camera that the request is off, and the native
 * route cannot be asked which request a later delivery belongs to. So the
 * host is not reused after that, no further request is taken on it, and the
 * process opens no further host either - the library's registrations
 * survive, so a delivery that arrives later would land in the next one. A
 * fresh process avoids reusing this process's registrations; it does not
 * establish that the camera is ready.
 *
 * **A still request whose native send failed is not certainly unsent.** The
 * native send status can be -1 while the command is still queued, and a
 * later wake can transmit the queued work. So a failed send leaves the
 * outcome unresolved: the request stays outstanding, the host is quarantined
 * the way an abandoned host is, it makes no further mutating request -
 * no setting, no capture mode, no further request - and the process opens
 * no further host. The request is not retried either, because a retry would
 * be a second request and the first may yet be answered. A request refused
 * *before* the library was called is not in this state: nothing was sent,
 * so nothing is unresolved. Nothing here shows that the request can be
 * cancelled, and a fresh process is not claimed to recover the camera - it
 * only stops this process from acting on an outcome it cannot see.
 *
 * **A setting whose native send failed is in the same state.** The setting
 * facades share the queued-send uncertainty with the still's trigger: they
 * return 0 for a local Send return of zero, they collapse a transport failure
 * into -1, and the two statuses they refuse *before* the send - -2 for a
 * handle the library does not know and -3 for a value or an id its converter
 * refuses - sent nothing at all. So a -2 or a -3 leaves the host usable,
 * while any other nonzero status leaves the outcome unresolved and the host
 * is quarantined as above: it takes no further setting, no capture mode and
 * no request, and the process opens no further host. No still was issued, so
 * nothing is outstanding and no completion is waited for; the quarantine is
 * about the configuration, not about a still. The value is not retried
 * either, and the same limits apply: nothing here shows that a queued write
 * can be taken back.
 *
 * **The first result is latched, and an extra set is refused.** The native
 * route hands out no request id, so a set that arrived after the submission
 * cannot be associated with the request: it may be a duplicate publication
 * of the same still, a second still, or a late delivery of older work. So
 * the first result is the one the host keeps. The extra set is refused
 * before its payload is read: the latched copy is not replaced or freed, and
 * the library's plane is not released a second time. What the retirement of
 * that extra plane is, is left unestablished. The host is then marked as
 * needing recovery - it takes no further setting, no capture mode and no
 * request - and this process opens no further host, because an
 * unattributable set leaves the native obligations unresolved.
 *
 * **A native release that reported a failure leaves its outcome unresolved.**
 * The release of the still's first image is the library's own call, and its
 * status says nothing about what the camera did with the plane: a nonzero
 * status is not a statement that the plane was released, and it is not a
 * statement that it was not. So a later call would release the same address
 * twice, and this host takes no further setting, no capture mode and no
 * request - and the process opens no further host. The release is not
 * retried. The bytes that were copied are not affected: they stay the
 * caller's, and they are collected and released as they are.
 *
 * **Closing while a native release is in flight forbids the process reuse
 * too.** The release's outcome was then never established, and the
 * outstanding-request rule does not cover it: the collection may already
 * have cleared the request. The quarantine is set before the release
 * returns, and a later successful return does not take it back.
 *
 * **A mutating call of this adapter is reserved, and the release a callback
 * owes is queued behind it.** The release of the still's first image is a
 * call into the library, and so are this adapter's own mutating calls - the
 * still's submission, a setting, the capture mode. A release made from
 * inside one of those calls would be two calls into the library at the same
 * time. Each of those calls is therefore **reserved** under the lock in the
 * same step in which it is admitted, and a callback that arrives while the
 * reservation is held copies the samples at once and **queues** the release
 * instead of making it. The thread that owns the call claims the queued
 * release when the call returns, makes it with the lock dropped, and writes
 * to the host afterwards only when the identity says the host is still the
 * live one. So the plane is released exactly once, and never while one of
 * this adapter's own calls is running. Nothing here claims anything about
 * calls this adapter does not make.
 *
 * **A queued release and a running call both block mutations.** A host
 * with a release queued behind one of the adapter's calls, and a host with
 * such a call running, take no request, no setting and no capture mode. The
 * same two states stop this process from opening a further host while a
 * release is still owed.
 *
 * **A close while native work is active fails closed.** The close does not
 * cancel the call, and nothing here shows that it was cancelled, so the host
 * is not reused and this process opens no further host. The thread that
 * owns the call holds everything the call needs, and it writes to the host
 * only when the identity says the host is still the live one - a closed
 * host's address can be given to a new one, so the address alone does not
 * say. The shared object stays loaded, so a queued release can still be
 * made after the close.
 *
 * **A close while native work is active leaves the native client alone.**
 * Destroying the client while a call into the library is running is not
 * established as safe, so the client is not destroyed: that native client
 * obligation is left unrecovered, the destroy is not retried later, and the
 * process's exit is not claimed to recover it. The capture module's power
 * state is not established either.
 *
 * **A zero status is a local completion, never proof of retirement.** The
 * release call returning zero says that the library's own call returned
 * zero. It does not show that the camera released the buffer, that the plane
 * was reclaimed, or that the native route acted at all. Nothing here adds a
 * retry, and nothing here waits for an acknowledgement that the interface
 * does not offer.
 */

/*
 * Copying the samples out of the delivered image.
 *
 * The protocol's mapped address is a **32-bit** address in the camera's own
 * address space, which is exactly right on the camera and cannot be
 * expressed on a 64-bit host.  The default copy reads the address
 * directly; a host that cannot use it installs its own.
 */
typedef void (*capture_library_copy_fn)(void *destination, uint32_t address,
                                         size_t count);

/*
 * Load the capture library and resolve required entry points while skipping
 * the explicit client-create path. Library/dependency initializers may run;
 * this stage establishes symbol availability, not capture readiness or
 * absence of device effects. Close leaves the object mapped until exit.
 */
int capture_library_host_load(capture_library_host **host,
                               const char *library_path, int verbose);

/*
 * Whether this backend can ask the camera to keep the sensor samples of a
 * still.
 *
 * The answer is a property of the backend and not of a loaded library, so a
 * caller can ask it **before** the library is opened.
 *
 * For the library host the answer is **no**: this implementation has no
 * verified way to ask.  The still's set carries a first image when the
 * camera's own raw configuration is on, and the camera's raw switch - the
 * one the settings store calls SSIF_RAW - is among the candidates for what
 * turns that configuration on, but nothing here establishes that SSIF_RAW
 * alone controls this sheet's configuration, and the library's interface
 * names no call that has been verified to write it.  The ask is refused
 * rather than answered with a guessed id.
 *
 * That is a statement about this implementation, not a claim that no
 * library interface can do it: the recovered interface has not been
 * searched exhaustively for such a call.
 *
 * A caller that needs the samples has to know this before it opens the
 * library, because the capability cannot be added by a later step.
 */
int capture_library_host_supports_sensor_retention(void);

/*
 * Open the capture library and bring the capture module up.
 *
 * `library_path` is the shared object to open, and it is **required**: a
 * null or empty path is refused with a reason.  The adapter resolves the
 * library's entry points with dlsym on the handle this open obtains, and
 * there is no process-wide lookup: the linked-library case - the library
 * this program was linked against - is not supported by this component, and
 * it does not promise it.  A process-wide lookup would bind to whichever
 * object in the process's global scope exports the name, which on the
 * camera may be a different build of it.
 *
 * Returns 0 on success.  On failure the client this call created is
 * destroyed and the buffers this process owns are freed; the shared object
 * stays loaded, and nothing here establishes that the capture module was
 * powered off.  The reason for the failure is asked for with
 * capture_library_host_last_error().
 */
int capture_library_host_open(capture_library_host **host,
                               const char *library_path, int verbose);

/*
 * Why the last open failed, or an empty string when it did not fail.
 *
 * A failed open has no host to carry the reason - the host is freed - so it
 * is kept here, and it survives that cleanup: every failure after the
 * library is loaded copies the reason out before the host goes away.  A
 * normal close is not an open failure and does not write here.
 */
const char *capture_library_host_last_error(void);

/*
 * Close the host.  The client is *not* unrealized: the library's power-off
 * path waits for a power-off event, and a host program must not be able to
 * wedge the capture system.  The shared object stays loaded for the life of
 * the process, and this component does not claim that the process's exit
 * shuts the capture module down.
 *
 * Closing a host is safe after the session on it has been closed.  If a
 * still request was still outstanding, the process opens no further host:
 * see the rule below.
 */
void capture_library_host_close(capture_library_host *host);

/* Install the copy used to take the samples out of a delivered image. */
void capture_library_host_set_copy(capture_library_host *host,
                                   capture_library_copy_fn copy);

/*
 * Ask the library for a capture mode.
 *
 * This is the library's own switch, and it is **not** the camera's raw
 * switch: the two are not the same thing until the firmware side has been
 * read, and nothing in the session's settings path calls this.  It is
 * exposed so that a camera run can try a mode and observe what the still's
 * set does.
 *
 * Returns 0 on success.  A mode above four is refused here, because the
 * library refuses it too and writes nothing.
 */
int capture_library_host_set_capture_mode(capture_library_host *host,
                                          uint32_t mode);

/* Fill in the backend that talks to the hosted library. */
void capture_library_host_backend(capture_library_host *host,
                                   capture_session_backend *backend);

/* The last error the host recorded, for the caller's own report. */
const char *capture_library_host_error(const capture_library_host *host);

/*
 * Whether any callback has arrived for this host. This observation does
 * not establish successful initialization, exclusive reception or request
 * association.
 */
int capture_library_host_heard_from_the_camera(const capture_library_host *host);

/*
 * Latest image-set type decoded by this host's callback, including a plain set
 * refused after decoding. Returns zero before any decoded set or for NULL.
 * It is not reset on submission and may be stale. It does not establish
 * request association, capture completion, sensor provenance or ownership.
 */
uint32_t capture_library_host_last_set_type(const capture_library_host *host);

/*
 * What this host's own bookkeeping says about reuse.
 *
 * This is a read-only observation.  It says whether a result was copied, and
 * whether the host may make another request - it says nothing about the
 * camera: a host that may make a request is not a host whose request will be
 * answered.
 */
typedef struct capture_library_host_health {
    /*
     * A result was copied: it is waiting to be collected, or the session
     * holds it.  Those bytes are the caller's until the session hands them
     * back.
     */
    int has_result;
    /*
     * An extra set arrived after the first result was latched and it could
     * not be associated with the request.  The host is not reusable and the
     * caller has to recover it.
     */
    int recovery_required;
    /*
     * The outcome of the native release of the still's first image is
     * unresolved: the library's release call reported a failure, and that is
     * not a statement that the native plane was released.
     */
    int release_unresolved;
    /* A native release is in flight right now. */
    int release_in_progress;
    /*
     * A native release is queued behind a native call that is still
     * running: the callback received the still's set while a mutating call
     * was reserved, so the release waits for that call to return.  It is
     * owed, and it is not made yet.
     */
    int release_pending;
    /*
     * Whether this host may make a further mutating request.  It may not
     * when the host is quarantined, when an extra result arrived, when the
     * native release's outcome is unresolved, or while a native release is
     * in flight.
     */
    int may_mutate;
} capture_library_host_health;

/*
 * Read the host's own state.  Returns 0 on success, and -1 for a null host
 * or a null destination.
 */
int capture_library_host_read_health(const capture_library_host *host,
                                     capture_library_host_health *out);

#ifdef __cplusplus
}
#endif

#endif /* CAPTURE_SESSION_LIBRARY_H */
