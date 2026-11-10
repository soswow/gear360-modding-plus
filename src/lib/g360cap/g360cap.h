/*
 * g360cap: capture a still on the Samsung Gear 360 (SM-R210) and keep it.
 *
 * Our own process hosts the camera's capture library
 * (/usr/lib/libcapture-fw-prod.so) with the stock application stopped. A
 * call to g360_capture() sends one still, waits for the camera's finished
 * image set, copies the images into memory this library allocated, and gives
 * the camera's buffers back the way the stock photo library does. The caller
 * then owns the copies until g360_still_free().
 *
 * What each still delivers today: the main JPEG, the screen-size JPEG and the
 * thumbnail JPEG, each a complete JPEG stream (FF D8 ... FF D9). When the
 * camera's image set carries its first image (set type 2 or 3, the RAW
 * qualities), that image is copied as well; it is the camera's compressed
 * "SRW" stream, not yet decoded or proven to be sensor RAW.
 *
 * Rules a caller must follow:
 *  - One session per process. The capture library's callbacks carry no user
 *    pointer, so this library keeps one global session.
 *  - The executable must link libm.so.6 and libdevice-node.so.0.1 directly
 *    (not only through dlopen); otherwise the gyro history stays empty and
 *    the camera's still fails. g360_open() checks it.
 *  - The stock application must be stopped gracefully first
 *    (systemctl --no-block stop di-camera-app.service). Never restart it with
 *    systemctl start; keep working on the same boot.
 *  - There is no teardown of the capture library: g360_close() frees this
 *    library's own state, and process exit ends the camera session.
 *  - Calls are not thread-safe; call from one thread.
 *
 * A segmentation fault anywhere in a process that hosts the capture library
 * makes the camera's kernel reboot the whole camera. This library therefore
 * checks every address it did not allocate against /proc/self/maps before
 * reading it.
 */
#ifndef G360CAP_H
#define G360CAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum g360_status {
    G360_OK = 0,
    G360_E_ARGUMENT = 1,       /* a null pointer or an out-of-range option */
    G360_E_STATE = 2,          /* not open, already open, or latched after a failure */
    G360_E_LOAD = 3,           /* a camera library or one of its symbols is missing */
    G360_E_GYRO = 4,           /* the gyro history is empty (libdevice-node not linked) */
    G360_E_APEX = 5,           /* the APEX/user-spec check refused or failed */
    G360_E_INIT = 6,           /* adapter, create or registration failed */
    G360_E_BRIDGE = 7,         /* the RTOS bridge did not come alive */
    G360_E_CLOCK = 8,          /* the monotonic clock or sleep failed */
    G360_E_SEND = 9,           /* the still call returned nonzero */
    G360_E_TIMEOUT = 10,       /* no image set (or no capture-ready) in time */
    G360_E_SET = 11,           /* the image set could not be read safely */
    G360_E_MEMORY = 12,        /* an allocation for a copy failed */
    G360_E_IO = 13,            /* a file could not be written completely */
    G360_E_IMAGE = 14,         /* an image was absent, unreadable or not a valid JPEG */
    G360_E_RELEASE = 15,       /* a buffer give-back call returned nonzero */
    G360_E_NO_SET = 16         /* the camera reported capture-ready but sent no image set */
} g360_status;

const char *g360_status_text(int status);

typedef struct g360_open_options {
    /*
     * Optional log file, appended and synced after every line so that a run
     * which takes the camera down still leaves its record. NULL: no log.
     */
    const char *log_path;
    /* Card free space in bytes, reported to the camera with every still. */
    uint64_t free_bytes;
    /* The "shots remaining" figure sent with every still; 0 means 2440,
       the stock app's value in the recorded still. */
    uint32_t remaining_shots;
    /*
     * Nonzero: register the library's callback client and subscribe it to
     * the still-expansion topic (0x04010008) before the capture library's
     * Beauty object is created, so that the library is called before the
     * Beauty object answers the camera's gyro request. Needed for
     * g360_capture_options.frame. Zero keeps the start-up order of 4z-a.
     */
    uint32_t frame_pause;
} g360_open_options;

/*
 * Loads the capture library, runs the gyro and APEX checks, initializes the
 * library's adapter, creates the camera handle, registers this library's
 * callbacks and waits for the RTOS bridge. Sends no still.
 */
int g360_open(const g360_open_options *options);

typedef enum g360_image_kind {
    G360_IMAGE_MAIN = 0,
    G360_IMAGE_SCREEN = 1,
    G360_IMAGE_THUMB = 2,
    G360_IMAGE_FIRST = 3,      /* the "Raw Compress"/SRW image, only for RAW qualities */
    G360_IMAGE_COUNT = 4
} g360_image_kind;

/* Per-image status flags. */
#define G360_IMAGE_PRESENT     0x01u  /* the set listed a nonzero address and size */
#define G360_IMAGE_COPIED      0x02u  /* the bytes were copied into data */
#define G360_IMAGE_UNMAPPED    0x04u  /* the range was not readable; nothing was read */
#define G360_IMAGE_TOO_LARGE   0x08u  /* larger than G360_MAX_IMAGE_BYTES; not copied */
#define G360_IMAGE_NO_MEMORY   0x10u  /* the copy allocation failed */
#define G360_IMAGE_JPEG_OK     0x20u  /* starts FF D8 and ends FF D9 */
#define G360_IMAGE_RELEASED    0x40u  /* given back to the camera (call returned 0) */
#define G360_IMAGE_RELEASE_ERR 0x80u  /* the give-back call returned nonzero */
#define G360_IMAGE_RAW_VALUES_OK 0x100u /* RAW: every sample fits the bit depth */
#define G360_IMAGE_RAW_CHANGED  0x200u  /* RAW: the source changed during the copy */
#define G360_IMAGE_NO_PAUSE     0x400u  /* frame: no usable orientation expansion for this still */
#define G360_IMAGE_SKIPPED      0x800u  /* JPEG copy deliberately omitted; buffer still returned */

#define G360_MAX_IMAGE_BYTES ((size_t)96u * 1024u * 1024u)

/*
 * How much of the camera's maker-note buffer g360_capture copies (the first
 * size of these that the process can read: 64, 32 or 24 KiB). The buffer
 * holds the RTOS's image-processing records that the stock app puts in the
 * JPEG's MakerNote; the IQ block alone is 0x5314 bytes (AWB, AE and colour
 * records; see reverse-engineering/dng). Bytes past the buffer's end belong
 * to whatever follows it in the camera's memory.
 */
#define G360_MAKER_NOTE_BYTES ((size_t)0x10000u)

/* Inline stCAPTURE_EXIFDATA in the image set, independent of the IQ buffer.
 * Stock libmm-photo copies these 0x2d8 bytes from image-set +8. */
#define G360_CAPTURE_EXIF_BYTES ((size_t)0x2d8u)

/* RAW images one still can copy (g360_capture_options.raw). */
#define G360_MAX_RAW 2u

typedef struct g360_image {
    unsigned char *data;       /* owned copy (malloc), or NULL */
    size_t size;               /* bytes in data */
    uint32_t width;
    uint32_t height;
    uint32_t stride_x;         /* the descriptor's words, unit unproven */
    uint32_t stride_y;
    uint32_t camera_address;   /* the library's virtual address (diagnostic) */
    uint32_t flags;            /* G360_IMAGE_* */
} g360_image;

/*
 * The still's full RAW frame (g360_capture_options.frame), copied while the
 * camera waits for the gyro reply. image: one 16-bit little-endian word per
 * sample, image.stride_x bytes per row, image.stride_y = the layout's bit
 * depth; flags as for RAW images, plus G360_IMAGE_NO_PAUSE.
 */
typedef struct g360_frame {
    g360_image image;
    uint32_t physical;         /* where it was read from */
    uint32_t hold_us;          /* how long the library held the camera in the callback */
    uint32_t mode;             /* the expansion descriptor's word +0x3c0 (3: orientation) */
} g360_frame;

typedef struct g360_still {
    uint32_t sequence;         /* 1 for the session's first still */
    uint32_t set_type;         /* 1 plain, 2/3 RAW configurations */
    uint32_t maker_note_address;
    uint32_t maker_note_flags; /* G360_IMAGE_RELEASED / _RELEASE_ERR */
    uint32_t notify_mask;      /* bit n set: still notification id n arrived */
    uint64_t set_after_ns;     /* still call to image set */
    uint64_t ready_after_ns;   /* still call to capture-ready (0 if none) */
    g360_image images[G360_IMAGE_COUNT];
    /*
     * The still's RAW images (g360_capture_options.raw), by default lens 1
     * and lens 2: one 16-bit little-endian word per sample, raw[k].stride_x
     * bytes per row (width = raw[k].width samples), raw[k].stride_y = the
     * layout's bit depth. See g360_raw_layout. Copied from the first image
     * set of a still only.
     */
    g360_image raw[G360_MAX_RAW];
    uint32_t raw_physical[G360_MAX_RAW]; /* where each RAW image was read from */
    g360_frame frame;          /* g360_capture only; see g360_shot.frame */
    /*
     * The camera's maker-note buffer at maker_note_address, copied before it
     * is given back (G360_MAKER_NOTE_BYTES or less; flags PRESENT, COPIED,
     * UNMAPPED, NO_MEMORY). Appended last: existing field offsets are
     * unchanged, but callers must be rebuilt for the larger struct.
     */
    g360_image maker_note;
    /* Request-associated stock Exif record, copied in the callback before
     * its containing image set expires. Little-endian: exposure numerator
     * and denominator at +0/+4, ISO at +0x70, exposure program at +0x6c,
     * white balance at +0x84. Zero size means unavailable. Rebuild callers. */
    unsigned char capture_exif[G360_CAPTURE_EXIF_BYTES];
    size_t capture_exif_size;
} g360_still;

/*
 * Where a RAW image lies in the capture memory. The defaults are the two
 * per-lens images every normal still writes before development: the PP
 * core's RAW after lens-shading, crosstalk and defect correction, 2896x2896
 * at stride 5824 (2 bytes per pixel), lens 1 at 0xab134000, lens 2 at
 * 0xad134000 (RTOS log; output/decompile/rea/pass-raw-keep-trace-20261104).
 * The RTOS does not hand them over. The stitch step does not write them,
 * but the live view restarted after the still does, from about 100 ms after
 * the image set; with the quick-view time HOLDZOOM (G360_QUICKVIEW_HOLDZOOM)
 * a single still should not restart it (static only). Their bit depth is not
 * proven: the default 16 accepts every value. The full side-by-side frame
 * (0xa6000000, 5824x2896, stride 11648, 12-bit) is overwritten by the
 * stitched YUV picture before the image set arrives (trial R1). Zero fields
 * mean the defaults for that index.
 */
typedef struct g360_raw_layout {
    uint32_t physical;         /* lens 1 0xab134000, lens 2 0xad134000 */
    uint32_t width;            /* samples per row: 2896 */
    uint32_t height;           /* rows: 2896 */
    uint32_t stride;           /* bytes per row: 5824 */
    uint32_t bits;             /* significant bits per sample: 16 (unproven) */
} g360_raw_layout;

typedef struct g360_capture_options {
    uint32_t set_timeout_ms;   /* wait for the first image set; 0 means 15000 */
    uint32_t ready_timeout_ms; /* wait for capture-ready after it; 0 means 10000 */
    /*
     * Image sets expected from one still; 0 means 1. A bracket sends one set
     * per frame. Sets beyond G360_MAX_SETS (or beyond this number, for
     * g360_capture_shot's copies) are given back without being copied.
     */
    uint32_t max_sets;
    /*
     * After capture-ready, keep waiting this long for further sets (each new
     * set restarts the wait); 0 means none. Used when the number of sets is
     * not known in advance.
     */
    uint32_t extra_set_wait_ms;
    /*
     * The mm capture type sent as the still request's third word
     * (G360_CAPTURE_*); 0 means G360_CAPTURE_LOCK3A, the stock app's
     * request when 3A is not yet locked, which survived on the camera. The
     * request's capture type also sets the camera's drive mode (step 5b):
     * a bracket is requested with its own type, not with
     * g360_set_drive_mode.
     */
    uint32_t capture_type;
    /*
     * The request's words 11-14 (bracket parameters), sent as given; zero
     * by default. libcapture checks them per capture type: an exposure
     * bracket (G360_CAPTURE_AEB) takes the order 0x18000N and the step
     * 0x19000N in words 11 and 12; a white-balance bracket
     * (G360_CAPTURE_WBB) requires word 13 = 0x1a0000..0x1a0005 (AB3, AB2,
     * AB1, MG3, MG2, MG1) and word 14 = 1 (AB) or 2 (MG), else the request
     * is refused locally (-3).
     */
    uint32_t bracket_words[4];
    /*
     * RAW images to copy into g360_still.raw[] (0 none, up to G360_MAX_RAW;
     * see g360_raw_layout): after the image set arrives, before the JPEGs
     * are copied and the buffers given back. The still fails with
     * G360_E_IMAGE unless each is copied, fits its bit depth and did not
     * change during the copy.
     */
    uint32_t raw;
    g360_raw_layout raw_layout[G360_MAX_RAW];
    /*
     * Nonzero: copy the still's full RAW frame during the camera's gyro
     * hand-off (output/decompile/rea/pass-expansion-pause-20261104). After
     * writing the frame, the RTOS publishes the still-expansion topic and
     * waits, without a timeout, for the gyro reply that the capture
     * library's Beauty object sends from this process; the next stages,
     * which reuse the frame's memory later, wait too. With
     * g360_open_options.frame_pause the library's callback runs first and
     * copies the frame before returning. The buffer is allocated before the
     * still request. Requires a session opened with frame_pause; the still
     * fails with G360_E_IMAGE unless the copy passed its range, sample
     * value and source spot checks. Only a readable 0x3c8-byte expansion
     * descriptor in orientation mode 3 can start the copy; other requests
     * leave it armed. Invalid/oversize/overflowing layouts fail with
     * G360_E_ARGUMENT before any still is sent (96 MiB limit).
     * frame_layout zero fields mean: physical 0xa6000000, 5824 x 2896
     * samples, stride 11648 bytes, 16 bits (the RTOS logs 12-bit samples;
     * 16 accepts any value).
     */
    uint32_t frame;
    g360_raw_layout frame_layout;
    /* Experiment: omit the three JPEG copies while still returning their
     * buffers and waiting for capture-ready. Requires frame != 0, whose
     * validation still determines success. Does not disable RTOS encoding.
     * Zero preserves the normal copy/validate behavior. Rebuild callers:
     * this appends a field to the options structure (no stable binary ABI).
     */
    uint32_t skip_jpeg_copy;
} g360_capture_options;

/* mm capture types (app get_mm_capture_type_str; converted by libcapture's
   FUN_00042e18 into RTOS still commands 0x10100xx). */
#define G360_CAPTURE_SINGLE    0x01u  /* plain still, command 0x1010001 */
#define G360_CAPTURE_AEB       0x05u  /* exposure bracket */
#define G360_CAPTURE_WBB       0x06u  /* white-balance bracket */
#define G360_CAPTURE_PWB       0x07u  /* picture-wizard bracket */
#define G360_CAPTURE_LOCK3A    0x15u  /* lock 3A, then capture: command 0x1010016 */
#define G360_CAPTURE_DEPTH_BRK 0x18u

/* One still's image sets (g360_capture_shot). */
#define G360_MAX_SETS 8u

typedef struct g360_shot {
    uint32_t sequence;         /* 1 for the session's first still */
    uint32_t set_count;        /* image sets copied into sets[] */
    uint32_t sets_not_copied;  /* further sets given back without copying */
    uint32_t notify_mask;      /* bit n set: still notification id n arrived */
    uint64_t ready_after_ns;   /* still call to capture-ready (0 if none) */
    g360_still sets[G360_MAX_SETS];
    g360_frame frame;          /* g360_capture_options.frame */
} g360_shot;

/*
 * Sends one still and blocks until its images are copied and the camera's
 * buffers are given back, then until the camera reports capture-ready.
 * options may be NULL.
 *
 * Returns G360_OK when the image set arrived, every requested image was
 * copied and copied JPEGs are valid. With skip_jpeg_copy, skipped JPEGs
 * are not validated; the requested full frame must still validate.
 * G360_E_IMAGE when the set arrived but an image
 * could not be copied or is not a valid JPEG (whatever was copied is still
 * in *still, and the buffers were still given back). G360_E_TIMEOUT when no
 * image set arrived: the still may still be in flight, and the session is
 * latched (no further stills). On any return, *still is filled as far as it
 * got, and the caller must call g360_still_free().
 */
int g360_capture(const g360_capture_options *options, g360_still *still);

/*
 * Like g360_capture, for stills that send several image sets (brackets):
 * copies and gives back each set as it arrives, up to options->max_sets
 * (at most G360_MAX_SETS), and gives back any further set without copying.
 * The still is complete when max_sets sets have arrived and capture-ready
 * came, or when capture-ready came, at least one set arrived, and
 * extra_set_wait_ms passed without another set. Returns G360_E_NO_SET when
 * capture-ready came but no set followed within extra_set_wait_ms (at least
 * 3 s): the camera dropped the still (as it does for a RAW quality without
 * a RAW-compression stage). Image sets that arrive after a still returned
 * are given back at the start of the next still or at g360_close. The caller
 * must call g360_shot_free() on any return.
 */
int g360_capture_shot(const g360_capture_options *options, g360_shot *shot);

/* Frees every set's copies and clears the shot. */
void g360_shot_free(g360_shot *shot);

/*
 * Waits wait_ms while giving back any image set that arrives late (after
 * its still returned). Call it while idle between stills and before exit;
 * returns the number of sets given back, or a negative status.
 */
int g360_service(uint32_t wait_ms);

/* Writes one image's bytes to path (created or truncated, synced). */
int g360_save_image(const g360_image *image, const char *path);

/* Frees the copies in still and clears it. */
void g360_still_free(g360_still *still);

/*
 * Image quality, in the capture library's own value domain (its user
 * attribute 3, wire attribute 0x1103). The RTOS converts these to its native
 * IMAGEQUALITY values (userdata 4); note the Linux order differs from the
 * native one (0x30000 is RAW). From the RTOS order-sheet writer, the native
 * value sets the image-set type: SUPERFINE/FINE/NORMAL -> 1 (JPEG only),
 * RAW_SUPERFINE/RAW_FINE/RAW_NORMAL -> 3 (first image plus JPEGs),
 * RAW/SRAW -> 2. Static evidence only; whether the camera accepts a RAW
 * value is what the camera trial shows (the RTOS logs "SetUserData").
 */
#define G360_QUALITY_RAW           0x30000u  /* native 0x40003 */
#define G360_QUALITY_SUPERFINE     0x30001u  /* native 0x40000, the camera's usual value */
#define G360_QUALITY_FINE          0x30002u  /* native 0x40001 */
#define G360_QUALITY_NORMAL        0x30003u  /* native 0x40002 */
#define G360_QUALITY_RAW_SUPERFINE 0x30004u  /* native 0x40004 */
#define G360_QUALITY_RAW_FINE      0x30005u  /* native 0x40005 */
#define G360_QUALITY_RAW_NORMAL    0x30006u  /* native 0x40006 */
#define G360_QUALITY_SRAW          0x30007u  /* native 0x40007 */

/*
 * Sends one image-quality value (G360_QUALITY_*) through
 * mm_camera_set_usr_attributes. The setting stays in the RTOS after this
 * process exits, so a caller that changes it should set
 * G360_QUALITY_SUPERFINE back when done. Returns G360_E_SEND when the call
 * returns nonzero.
 */
int g360_set_image_quality(uint32_t quality);

/*
 * Drive mode, in the capture library's value domain (its user attribute 8,
 * wire attribute 0x1108). The RTOS maps these to native USERDATA_DRIVE
 * (userdata 10) values (static: RTOS converter 0x80542fb8). From the RTOS
 * order sheet, DRIVE_WBBRK and DRIVE_PWBRK select the bracket recipe, which
 * includes the RAW-compression stage at a RAW quality; DRIVE_AEBRK uses the
 * normal recipe. Static evidence only.
 */
#define G360_DRIVE_SINGLE   0x80000u  /* native 0xa0001, the camera's usual value */
#define G360_DRIVE_CONTIMID 0x80001u  /* native 0xa0003 */
#define G360_DRIVE_CONTILOW 0x80002u  /* native 0xa0004 */
#define G360_DRIVE_BURST    0x80003u  /* native 0xa0005 */
#define G360_DRIVE_AEBRK    0x80004u  /* native 0xa0008, exposure bracket */
#define G360_DRIVE_WBBRK    0x80005u  /* native 0xa0009, white-balance bracket */
#define G360_DRIVE_PWBRK    0x80006u  /* native 0xa000a, picture-wizard bracket */
#define G360_DRIVE_DEPTHBRK 0x80008u  /* native 0xa000b */

/*
 * Sends one drive-mode value (G360_DRIVE_*) through
 * mm_camera_set_usr_attributes. Like the quality, it stays in the RTOS after
 * exit; set G360_DRIVE_SINGLE back when done.
 */
int g360_set_drive_mode(uint32_t drive);

/*
 * Exposure. The library sends the camera's own settings through the capture
 * library (mm_camera_set_usr_attributes / mm_camera_set_var_attributes); the
 * values below are in the capture library's value domain, converted by it
 * and by the RTOS (static: RTOS converters 0x80541f44, 0x80542054,
 * 0x805427d8, 0x80543890, 0x80543930; libcapture converters 0x4408c,
 * 0x4736c). The same native settings were used successfully from the camera
 * shell in September 2026 (hdr/five-shot-bracket.sh), where EXIF confirmed
 * the shutter time.
 *
 * Shutter as TV = 16 * log2(1 / seconds): 0 = 1 s, 32 = 1/4 s, 64 = 1/16 s,
 * 96 = 1/64 s, 128 = 1/250 s, 160 = 1/1000 s, 208 = 1/8000 s; negative for
 * longer than 1 s (-37 = about 5 s). The September tests verified 1/8000 to
 * 1/8 s; from about 1/5 s the images stopped getting brighter.
 */
#define G360_ATTR_DIALMODE      0   /* 1 PROGRAM (usual), 3 -> native 4 (manual-exposure dial) */
#define G360_ATTR_SHOOTINGMODE  1   /* 0x10000 PROGRAM (usual), 0x10003 -> native MANUAL */
#define G360_ATTR_ISO           4   /* 0 AUTO (usual), or 100, 125, 160, 200, ... 5000 */
#define G360_ATTR_WB            5   /* 0x50000 AUTO (usual) */
#define G360_ATTR_ISONR         22  /* 0x160000 OFF ... 0x160002 MID (usual) ... */
#define G360_ATTR_LTNR          23  /* 0 off, 1 on (usual) */
#define G360_VAR_TV             0x101u

/*
 * Quick-view (review) time: capture-library user attribute 30, which
 * libcapture sends as wire attribute 0x2b and the RTOS stores as
 * USERDATA_QUICKVIEWTIME (native 0x33000N for 0x1e000N; static: libcapture
 * 0x4408c, RTOS 0x80052b9c and 0x80543e48). With HOLDZOOM the RTOS does not
 * restart the live view after a single still (static: RTOS 0x80276c04,
 * unless a monitor-out, custom-WB, smart-mode or boot-shot check applies),
 * so the per-lens RAW images are not overwritten. It stays in the RTOS after
 * exit: set the previous value back when done.
 */
#define G360_ATTR_QUICKVIEWTIME 30
#define G360_QUICKVIEW_OFF      0x1e0000u
#define G360_QUICKVIEW_1SEC     0x1e0001u
#define G360_QUICKVIEW_3SEC     0x1e0002u
#define G360_QUICKVIEW_5SEC     0x1e0003u
#define G360_QUICKVIEW_HOLD     0x1e0004u
#define G360_QUICKVIEW_HOLDZOOM 0x1e0005u

/* Sends one quick-view value (G360_QUICKVIEW_*); G360_E_ARGUMENT for others. */
int g360_set_quickview(uint32_t value);

/* Sends one user attribute (G360_ATTR_* or another capture-library id) as given. */
int g360_set_user_attribute(int attribute, uint32_t value);

/* Sends one variable (G360_VAR_TV or another id 0x100..0x12b) as given. */
int g360_set_variable(uint32_t id, int32_t value);

/*
 * Manual exposure as the September scripts set it: dial 3, shooting mode
 * 0x10003 (native MANUAL), the ISO, then the shutter TV. Call before each
 * still if anything else could change the mode.
 */
int g360_set_manual_exposure(int32_t tv, uint32_t iso);

/* Back to the camera's usual automatic exposure: dial 1, shooting mode
   0x10000 (PROGRAM), ISO auto. */
int g360_set_auto_exposure(void);

/* Updates the card free space sent with the following stills. */
void g360_set_free_bytes(uint64_t free_bytes);

/*
 * Movie-mode preview (T20, experimental; output/live-preview-T20/RESULT.md).
 *
 * The stock Wi-Fi preview is a movie recording with no file: the camera
 * sends YUV 4:2:0 frames after RecordMovie(0x1020001 E_CAP_CMD_MV_START)
 * and stops them with RecordMovie(0x1020002). g360_preview_start sends the
 * start with the stock 28-byte layout, waits for the camera's
 * VIDEOSTART_DONE (0x2030001) and then calls the callback for every frame,
 * on the capture library's receiver thread. The callback must return
 * quickly and must not keep the pointers: copy what it needs.
 *
 * While a preview runs g360_capture refuses with G360_E_STATE; stop it
 * first. g360_preview_stop waits for VIDEOSTOP_DONE (0x2030002).
 */
#define G360_PREVIEW_TOPIC_THUMB   0x03030001u
#define G360_PREVIEW_TOPIC_DISPLAY 0x03030002u
#define G360_PREVIEW_TOPIC_MOVIE   0x03030003u
#define G360_PREVIEW_TOPIC_SET     0x04010007u
#define G360_PREVIEW_BLOCK_BYTES   0x88u

typedef struct g360_preview_frame {
    uint32_t topic;          /* one of G360_PREVIEW_TOPIC_* */
    uint32_t buffer_width;   /* block +0x00 */
    uint32_t buffer_height;  /* +0x04 */
    uint32_t width;          /* +0x08 */
    uint32_t height;         /* +0x0c */
    uint32_t yuv_kind[2];    /* +0x38, +0x3c */
    uint32_t address_type;   /* +0x40: 0, 5, 6 or 7 */
    uint32_t y_address;      /* +0x54, the camera word */
    uint32_t c_address;      /* the chroma word for this address type */
    uint32_t side;           /* +0x64: 1 single, 2 left, 3 right */
    uint64_t timestamp;      /* +0x68 */
    /* The planes, only when the whole range (buffer_width x buffer_height
       for Y, half that for C) is mapped readable in this process; else 0. */
    const unsigned char *y;
    const unsigned char *c;
    size_t y_bytes;
    size_t c_bytes;
    /* The block itself (G360_PREVIEW_BLOCK_BYTES), valid during the call. */
    const unsigned char *block;
} g360_preview_frame;

typedef void (*g360_preview_callback)(const g360_preview_frame *frame, void *context);

typedef struct g360_preview_options {
    /* One of the camera's movie sizes (4096x2048, 2880x1440, 2560x1280,
       1920x960, 1440x720, 1920x1080, 1280x720, 1280x640); 0x0: 1280x640. */
    uint32_t width;
    uint32_t height;
    /* Frames per second times ten: 125, 150, 240, 250, 300, 500, 600, 1000
       or 1200; 0: 150. */
    uint32_t fps_x10;
    /* How long to wait for VIDEOSTART_DONE; 0: 3000 ms. */
    uint32_t wait_ms;
    /* Also subscribe the image set 0x04010007 (frames per the stock code). */
    uint32_t with_set;
    g360_preview_callback callback;
    void *context;
} g360_preview_options;

/* G360_OK; G360_E_ARGUMENT for a size or rate the camera refuses;
 * G360_E_STATE when not open or already running; G360_E_LOAD when a symbol
 * is missing; G360_E_SEND when RecordMovie returns nonzero; G360_E_TIMEOUT
 * when VIDEOSTART_DONE does not come (a stop is then sent). */
int g360_preview_start(const g360_preview_options *options);
/* Stops a running preview and waits for VIDEOSTOP_DONE (0: 3000 ms).
 * G360_OK when stopped (or not running); G360_E_TIMEOUT without the ack
 * (the preview counts as stopped anyway). */
int g360_preview_stop(uint32_t wait_ms);
/* 1 while a preview runs. */
int g360_preview_running(void);
/* Frames delivered per topic since g360_open (index: topic & 3 for the
   images, 0 for the set). Diagnostic. */
uint32_t g360_preview_count(uint32_t topic);
/* The camera's sensor frame rate as GetCaptureData(0x1080017) reports it
   (float bits); 0 on failure. Diagnostic. */
uint32_t g360_preview_sensor_rate_bits(void);

/*
 * Closes the log. The capture library stays loaded and registered until
 * process exit, so a closed session cannot be opened again in this process.
 */
void g360_close(void);

/* Appends one line to the session log (for the caller's own milestones). */
void g360_log(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* G360CAP_H */
