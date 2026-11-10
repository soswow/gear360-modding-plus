/*
 * capture-shot: the demonstration program for the capture library.
 *
 * It takes one still through the capture library's own API and writes what
 * came back: the sensor samples as a raw file, and optionally the camera's
 * JPEG of the same still.  The library host backend cannot deliver the
 * samples, so with that backend the program refuses to run the still
 * instead of pretending it can - see the note at the end of this header.
 *
 * This is the program the goal asks for - a small command-line program that
 * demonstrates the API - and it is deliberately thin: everything it does is
 * done through the capture session, so that a network or service caller can
 * be written around the same API without becoming the capture engine.
 *
 * Usage:
 *   capture-shot [options]
 *     --raw <path>        write the sensor samples here (default: shot.raw)
 *     --jpeg <path>       write the camera's JPEG of the same still here
 *     --iso <n>           the ISO for the shot; 0 leaves it alone
 *     --shutter <n>       the shutter value for the shot; 0 leaves it alone
 *     --timeout <ms>      how long to wait for the still (default 15000)
 *     --library <path>    the capture library to host
 *                         (default /usr/lib/libcapture-fw-prod.so)
 *     --remaining <n>     the remaining-shot count in the still request
 *     --free <bytes>      the free space in the still request
 *     --command <word>    the still command to send: 0x01010016 is the
 *                         device-shutter still, 0x01010001 the plain one and
 *                         0x01010005 the AE bracket
 *     --capture-mode <n>  ask the library for a capture mode, 0 to 4, before
 *                         the still.  This is the library's own switch and not
 *                         the camera's raw switch; the link between them is
 *                         not established, so it is an experiment and not a
 *                         setting.  It is not asked for at all by default.  It
 *                         belongs to the still path, so it is not reached
 *                         while the backend cannot deliver the sensor
 *                         samples.
 *     --load-only          load the library, resolve its entry points and
 *                         stop. It skips explicit client creation and still submission;
 *                         loading may run library/dependency constructors.
 *     --probe              host the library and create its client, and stop.
 *                         The client's creation is the library's power-on
 *                         call, so this may touch the device.
 *     --probe-local-bootstrap  explicitly attempt local adapter and orientation
 *                         setup before --probe; requires --probe.
 *     --probe-hold-ms <n>  keep the probe process alive for 1..30000 ms
 *                         after client creation; requires --probe
 *     --verbose           print what the session reports
 *
 * **The library host cannot deliver the sensor samples.**  This
 * implementation has no verified way to ask the camera to keep them: the
 * camera's raw switch is among the candidates for what turns the raw
 * configuration on, but nothing here establishes that SSIF_RAW alone
 * controls this sheet's configuration, and the library's interface names no
 * call that has been verified to write it.  The backend refuses the ask
 * rather than sending a guessed id, so this program refuses to open the
 * library at all for a still and says so.  It does not enable the switch by
 * a guess, and it does not take the still and hand the set's first image
 * out as the sensor samples while pretending the ask succeeded: that
 * mapping is the backend's own experimental one, not a proven sensor
 * output.
 */
#include "capture_libc.h"
#include "capture_session.h"
#include "capture_session_library.h"

/*
 * The status the program exits with when the backend does not support what
 * the program needs.  It is not 1: 1 is a failed run, and "this backend
 * cannot deliver the sensor samples" is not a failure of the run - it is
 * the run's answer, and it is reported as its own status so that a caller
 * can tell it apart from a run that broke.
 */
#define CAPTURE_SHOT_UNSUPPORTED 3

/*
 * The program's own log.
 *
 * A run that takes the camera down takes the console output with it, and
 * then there is no record of how far the program got.  The log is written
 * to a file and flushed to the disk at every line, so what survives the
 * reboot is the last thing the program did.
 */
#define CAPTURE_SHOT_LOG_CAPACITY 4096
static char log_line[CAPTURE_SHOT_LOG_CAPACITY];
static size_t log_length;
static int log_fd = -1;

static void log_flush(void)
{
    if (log_fd < 0 || log_length == 0u) {
        return;
    }
    (void)write(log_fd, log_line, log_length);
    fsync(log_fd);
    log_length = 0u;
}

static void log_write(const char *text)
{
    size_t index;

    for (index = 0; text[index] != 0; ++index) {
        if (log_length + 2u >= CAPTURE_SHOT_LOG_CAPACITY) {
            log_flush();
        }
        log_line[log_length++] = text[index];
        if (text[index] == '\n') {
            log_flush();
        }
    }
}

static void log_open(const char *path)
{
    if (path == 0 || path[0] == 0) {
        return;
    }
    /* The flags are the ones this platform really has: capture_libc.h gives
     * the host its own and the camera the target's. */
    log_fd = open(path, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC,
                  0644);
}

static int verbose;

/* Diagnostic only: successful returns do not certify service readiness.
 * Keep the loader reference for the process lifetime, including on failure. */
static int probe_local_bootstrap(const char *library)
{
    typedef void *(*get_instance_fn)(void);
    typedef int (*initialize_fn)(void *);
    typedef char pointer_sizes_match[(sizeof(void *) == sizeof(get_instance_fn) &&
                                      sizeof(void *) == sizeof(initialize_fn)) ? 1 : -1];
    get_instance_fn instance_fn, beauty_fn;
    initialize_fn initialize;
    void *handle, *symbol, *instance, *beauty;
    int result;
    char message[160];
    (void)sizeof(pointer_sizes_match);

    log_write("capture-shot: local bootstrap dlopen entering\n");
    handle = dlopen(library, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    if (handle == 0) {
        log_write("capture-shot: local bootstrap dlopen failed\n");
        return -1;
    }
    log_write("capture-shot: local bootstrap resolving symbols\n");
    symbol = dlsym(handle, "_ZN11CMCBAdapter8InstanceEv");
    memcpy(&instance_fn, &symbol, sizeof(instance_fn));
    symbol = dlsym(handle, "_ZN11CMCBAdapter10InitializeEv");
    memcpy(&initialize, &symbol, sizeof(initialize));
    symbol = dlsym(handle, "_ZN19CBeautyFaceIf_Linux11GetInstanceEv");
    memcpy(&beauty_fn, &symbol, sizeof(beauty_fn));
    if (instance_fn == 0 || initialize == 0 || beauty_fn == 0) {
        log_write("capture-shot: local bootstrap missing required symbol\n");
        return -1;
    }
    log_write("capture-shot: local bootstrap Instance entering\n");
    instance = instance_fn();
    log_write(instance != 0 ? "capture-shot: local bootstrap Instance nonnull\n" :
                             "capture-shot: local bootstrap Instance null\n");
    if (instance == 0) return -1;
    log_write("capture-shot: local bootstrap Initialize entering\n");
    result = initialize(instance);
    snprintf(message, sizeof(message), "capture-shot: local bootstrap Initialize returned %d\n", result);
    log_write(message);
    if (result != 0) return -1;
    log_write("capture-shot: local bootstrap BeautyGetInstance entering\n");
    beauty = beauty_fn();
    log_write(beauty != 0 ? "capture-shot: local bootstrap BeautyGetInstance nonnull\n" :
                           "capture-shot: local bootstrap BeautyGetInstance null\n");
    if (beauty == 0) return -1;
    log_write("capture-shot: local bootstrap calls returned; readiness is unknown\n");
    return 0;
}

/* Opening this library may run loader or dependency constructors. */
#define BRIDGE_LIBRARY "/usr/lib/libmulticore-bridge.so"
#define NOTIFY_CATEGORY 0x10u
#define NOTIFY_SETTLE_MS 5000u
#define NOTIFY_POLL_MS 50u

typedef void (*bridge_send_fn)(unsigned short category,
                               unsigned short attribute,
                               unsigned short length,
                               unsigned char *body);
typedef unsigned char (*bridge_alive_fn)(void);

static const char *notify_change(unsigned char before, unsigned char after)
{
    if (before == 0 && after != 0) return "rise; incoming category unattributed";
    if (before != 0 && after == 0) return "fall; cause unattributed";
    return "change; cause unattributed";
}

/* This untagged latch is an incoming-category observation, not an ACK. */
static int probe_notify_alive(void)
{
    bridge_send_fn send_fn;
    bridge_alive_fn alive_fn;
    void *handle, *symbol;
    unsigned char before, after;
    capture_timespec start, now, requested, remaining;
    long seconds, nanoseconds;
    unsigned long elapsed;
    char message[192];

    log_write("capture-shot: notify dlopen entering\n");
    handle = dlopen(BRIDGE_LIBRARY, CAPTURE_RTLD_NOW | CAPTURE_RTLD_GLOBAL);
    if (handle == 0) {
        log_write("capture-shot: notify dlopen failed\n");
        fprintf(stderr, "capture-shot: notify dlopen failed\n");
        return -1;
    }
    symbol = dlsym(handle, "Mcb_send");
    memcpy(&send_fn, &symbol, sizeof(send_fn));
    symbol = dlsym(handle, "_ZN16CMulticoreBridge18IsDestinationAliveEv");
    memcpy(&alive_fn, &symbol, sizeof(alive_fn));
    if (send_fn == 0 || alive_fn == 0) {
        log_write("capture-shot: notify missing required symbol\n");
        fprintf(stderr, "capture-shot: notify missing required symbol\n");
        return -1;
    }
    before = alive_fn();
    if (before != 0) {
        log_write("capture-shot: notify unusable nonzero baseline; no send\n");
        fprintf(stderr, "capture-shot: notify unusable nonzero baseline; no send\n");
        return 1;
    }
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &start) != 0) {
        log_write("capture-shot: notify monotonic clock unavailable; no send\n");
        fprintf(stderr, "capture-shot: notify monotonic clock unavailable; no send\n");
        return -1;
    }
    log_write("capture-shot: notify sending category 16 attribute 0 length 0\n");
    send_fn(NOTIFY_CATEGORY, 0, 0, 0);
    for (;;) {
        if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &now) != 0) {
            log_write("capture-shot: notify monotonic clock failed after send\n");
            fprintf(stderr, "capture-shot: notify monotonic clock failed after send\n");
            return -1;
        }
        seconds = now.tv_sec - start.tv_sec;
        nanoseconds = now.tv_nsec - start.tv_nsec;
        if (nanoseconds < 0) { --seconds; nanoseconds += 1000000000L; }
        if (seconds < 0) {
            log_write("capture-shot: notify monotonic clock moved backwards\n");
            fprintf(stderr, "capture-shot: notify monotonic clock moved backwards\n");
            return -1;
        }
        /* Cap before multiplying, so a long stall cannot overflow. */
        elapsed = seconds >= 5 ? NOTIFY_SETTLE_MS :
                  (unsigned long)seconds * 1000u + (unsigned long)nanoseconds / 1000000u;
        after = alive_fn();
        if (after != before || elapsed >= NOTIFY_SETTLE_MS) break;
        requested.tv_sec = 0;
        requested.tv_nsec = (long)NOTIFY_POLL_MS * 1000000L;
        if (nanosleep(&requested, &remaining) != 0) {
            log_write("capture-shot: notify sleep interrupted or failed; observation incomplete\n");
            fprintf(stderr, "capture-shot: notify sleep interrupted or failed; observation incomplete\n");
            return -1;
        }
    }
    snprintf(message, sizeof(message),
             "capture-shot: notify untagged latch before %u after %u elapsed %lu ms%s\n",
             (unsigned)before, (unsigned)after, elapsed,
             seconds >= 5 ? " or more" : "");
    log_write(message);
    fprintf(stderr, "%s", message);
    if (after == before) {
        log_write("capture-shot: notify no latch change; response unestablished\n");
        return 1;
    }
    snprintf(message, sizeof(message), "capture-shot: notify latch %s\n",
             notify_change(before, after));
    log_write(message);
    fprintf(stderr, "%s", message);
    return 0;
}

static int parse_probe_hold_ms(const char *text, uint32_t *milliseconds){
    uint32_t value = 0u;
    size_t index;

    if (text == 0 || text[0] == '\0') {
        return -1;
    }
    for (index = 0u; text[index] != '\0'; ++index) {
        uint32_t digit;
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        digit = (uint32_t)(text[index] - '0');
        if (value > 30000u / 10u ||
            (value == 30000u / 10u && digit > 30000u % 10u)) {
            return -1;
        }
        value = value * 10u + digit;
    }
    if (value == 0u) {
        return -1;
    }
    *milliseconds = value;
    return 0;
}

static int hold_probe(uint32_t milliseconds)
{
    capture_timespec requested;
    capture_timespec remaining;

    requested.tv_sec = (long)(milliseconds / 1000u);
    requested.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    return nanosleep(&requested, &remaining);
}

static int write_file(const char *path, const void *data, size_t size)
{
    capture_file *file;

    if (path == 0 || data == 0) {
        return 0;
    }
    file = fopen(path, "wb");
    if (file == 0) {
        fprintf(stderr, "capture-shot: cannot write %s\n", path);
        return -1;
    }
    if (size != 0u && fwrite(data, 1u, size, file) != size) {
        fprintf(stderr, "capture-shot: short write on %s\n", path);
        fclose(file);
        return -1;
    }
    fclose(file);
    if (verbose) {
        fprintf(stderr, "capture-shot: wrote %lu bytes to %s\n",
                (unsigned long)size, path);
    }
    return 0;
}

static const char *status_text(capture_session_status status)
{
    switch (status) {
    case CAPTURE_SESSION_OK: return "ok";
    case CAPTURE_SESSION_INVALID_ARGUMENT: return "invalid argument";
    case CAPTURE_SESSION_BACKEND_ERROR: return "the camera refused the request";
    case CAPTURE_SESSION_BAD_STATE: return "the session is in the wrong state";
    case CAPTURE_SESSION_REFUSED: return "the request was refused";
    case CAPTURE_SESSION_MALFORMED_COMPLETION: return "the completion was malformed";
    case CAPTURE_SESSION_UNASSOCIATED_COMPLETION: return "the completion was not ours";
    case CAPTURE_SESSION_NO_CAPACITY: return "no capacity for another capture";
    case CAPTURE_SESSION_TIMEOUT: return "the capture did not complete in time";
    case CAPTURE_SESSION_CLOCK_ERROR: return "the clock is not available";
    }
    return "unknown";
}

int main(int argc, char **argv)
{
    const char *raw_path = "shot.raw";
    /* The log is on the camera's own filesystem, not in /tmp: /tmp is
     * lost when the camera reboots, and a run that takes the camera down
     * is exactly the run whose record is wanted. */
    const char *log_path = "/opt/capture-host.log";
    int probe_only = 0;
    int probe_local = 0;
    int probe_notify = 0;
    int load_only = 0;
    uint32_t probe_hold_ms = 0u;
    const char *jpeg_path = 0;
    const char *library = "/usr/lib/libcapture-fw-prod.so";
    uint32_t iso = 0u;
    uint32_t shutter = 0u;
    uint32_t timeout_ms = 15000u;
    uint32_t remaining = 0u;
    uint64_t free_space = 0u;
    /*
     * The library's own capture mode, asked for before the still.
     *
     * The default is -1, which means "do not ask".  The mode is not the
     * camera's raw switch: the link between the capture mode and the raw
     * configuration is not established, so the option is there for a run
     * that wants to try one and observe what the still's set does.
     */
    int capture_mode = -1;
    /*
     * The still command the session sends.  Zero would select the
     * session's plain still, so the default is the device-shutter command
     * and a caller can name another one - the AE bracket 0x01010005, say.
     */
    uint32_t command = CAPTURE_LIBRARY_STILL_COMMAND;
    int i;

    for (i = 1; i < argc; ++i) {
        const char *name = argv[i];
        const char *value = (i + 1 < argc) ? argv[i + 1] : 0;
        if (strcmp(name, "--raw") == 0 && value != 0) {
            raw_path = value;
            ++i;
        } else if (strcmp(name, "--jpeg") == 0 && value != 0) {
            jpeg_path = value;
            ++i;
        } else if (strcmp(name, "--library") == 0 && value != 0) {
            library = value;
            ++i;
        } else if (strcmp(name, "--iso") == 0 && value != 0) {
            iso = (uint32_t)strtoul(value, 0, 0);
            ++i;
        } else if (strcmp(name, "--shutter") == 0 && value != 0) {
            shutter = (uint32_t)strtoul(value, 0, 0);
            ++i;
        } else if (strcmp(name, "--command") == 0 && value != 0) {
            command = (uint32_t)strtoul(value, 0, 0);
            ++i;
        } else if (strcmp(name, "--capture-mode") == 0 && value != 0) {
            capture_mode = (int)strtoul(value, 0, 0);
            ++i;
        } else if (strcmp(name, "--timeout") == 0 && value != 0) {
            timeout_ms = (uint32_t)strtoul(value, 0, 0);
            ++i;
        } else if (strcmp(name, "--remaining") == 0 && value != 0) {
            remaining = (uint32_t)strtoul(value, 0, 0);
            ++i;
        } else if (strcmp(name, "--free") == 0 && value != 0) {
            free_space = strtoull(value, 0, 0);
            ++i;
        } else if (strcmp(name, "--log") == 0 && value != 0) {
            log_path = value;
            ++i;
        } else if (strcmp(name, "--probe") == 0) {
            probe_only = 1;
        } else if (strcmp(name, "--probe-local-bootstrap") == 0) {
            probe_local = 1;
        } else if (strcmp(name, "--probe-notify") == 0) {
            probe_notify = 1;
        } else if (strcmp(name, "--probe-hold-ms") == 0 && value != 0) {
            if (parse_probe_hold_ms(value, &probe_hold_ms) != 0) {
                fprintf(stderr, "capture-shot: --probe-hold-ms must be a decimal value from 1 to 30000\n");
                return 2;
            }
            ++i;
        } else if (strcmp(name, "--load-only") == 0) {
            load_only = 1;
        } else if (strcmp(name, "--verbose") == 0) {
            verbose = 1;
        } else {
            fprintf(stderr, "capture-shot: unknown option %s\n", name);
            return 2;
        }
    }

    if ((probe_only && load_only) || ((probe_hold_ms != 0u || probe_local || probe_notify) && !probe_only)) {
        fprintf(stderr, "capture-shot: --probe and --load-only are mutually exclusive; probe options require --probe\n");
        return 2;
    }

    {
        capture_library_host *host = 0;
        capture_session_backend backend;
        capture_session_config config;
        capture_session session;
        capture_session_image image;
        capture_session_status status;
        int failed;

        log_open(log_path);
        log_write("capture-shot: start\n");

        if (load_only) {
            /*
             * The lightest contact with the camera's library: load it and
             * resolve its entry points. It skips explicit client creation
             * and still submission; constructors have separate effects.
             *
             * Two things are said here because the opposite is not true:
             * loading a shared object may run the constructors of the
             * library and of the libraries it depends on, and this
             * component does not know what those constructors do; and the
             * close does not unload the shared object, so the library stays
             * in the process until the process exits.
             */
            if (capture_library_host_load(&host, library, verbose) != 0) {
                fprintf(stderr, "capture-shot: cannot load the capture "
                                "library %s\n", library);
                log_write("capture-shot: the capture library could not be "
                          "loaded\n");
                return 1;
            }
            log_write("capture-shot: the capture library was loaded and its "
                      "entry points resolved; it stays loaded until the "
                      "process exits\n");
            fprintf(stderr, "capture-shot: the capture library was loaded and "
                            "its entry points resolved; loading it may have "
                            "run the library's own constructors and those of "
                            "the libraries it depends on, and it stays loaded "
                            "until the process exits\n");
            capture_library_host_close(host);
            return 0;
        }

        /*
         * The capability this program needs and this backend does not have.
         *
         * The program's purpose is to hand the caller the sensor samples of
         * the still, and the session's contract is that the samples are the
         * ones the camera kept for it: the capture has to ask the camera to
         * keep them.  This backend has no verified way to make that ask, so
         * it refuses it rather than sending a guessed id.
         *
         * That is checked **here**, before the library is opened, so that a
         * run does not load the library, create a client and initialise the
         * capture module for a capture that cannot deliver its samples.
         *
         * It is not worked around either.  The program does not enable the
         * camera's raw switch by a guess, and it does not take the still and
         * hand the set's first image out as the sensor samples while
         * pretending the ask succeeded: that mapping is the backend's own
         * experimental one, and the image is the firmware's "Raw Compress"
         * candidate, whose native name is not a provenance.
         */
        if (!probe_only && !capture_library_host_supports_sensor_retention()) {
            fprintf(stderr,
                    "capture-shot: this backend has no verified way to ask the "
                    "camera to keep the sensor samples of a still, so it "
                    "cannot deliver them; the library was not opened\n");
            log_write("capture-shot: the backend has no verified way to ask "
                      "the camera to keep the sensor samples; the library was "
                      "not opened\n");
            return CAPTURE_SHOT_UNSUPPORTED;
        }

        log_write("capture-shot: before library host open\n");
        if (probe_local && probe_local_bootstrap(library) != 0) {
            log_write("capture-shot: local bootstrap failed; process exit does not establish remote cleanup\n");
            fprintf(stderr, "capture-shot: local bootstrap failed; client creation was not attempted; process exit does not establish remote cleanup\n");
            return 1;
        }
        if (capture_library_host_open(&host, library, verbose) != 0) {
            const char *reason = capture_library_host_last_error();

            fprintf(stderr, "capture-shot: cannot host the capture library %s: %s\n",
                    library, (reason != 0 && reason[0] != '\0')
                                ? reason
                                : "the library could not be hosted");
            log_write("capture-shot: the capture library could not be hosted\n");
            return 1;
        }
        log_write("capture-shot: the capture library is hosted and its client "
                  "is created; the client slot does not establish that the "
                  "capture module was initialised\n");
        log_write("capture-shot: after library host open\n");

        if (probe_only) {
            /*
             * The probe stops here: the library is hosted and its client is
             * created, and no still is asked for.  The client's creation is
             * the capture module's power-on, so this may touch the device,
             * and the probe says so.
             */
            log_write("capture-shot: client created; readiness unverified\n");
            fprintf(stderr, "capture-shot: client created; readiness unverified; no still requested\n");
            if (probe_notify) {
                /* One fixed diagnostic send; the latch is untagged. */
                if (probe_notify_alive() != 0) {
                    log_write("capture-shot: notify observation incomplete or unchanged\n");
                    fprintf(stderr, "capture-shot: notify observation incomplete or unchanged\n");
                }
            }
            log_write("capture-shot: before probe hold\n");
            if (probe_hold_ms != 0u && hold_probe(probe_hold_ms) != 0) {
                log_write("capture-shot: probe hold failed\n");
                fprintf(stderr, "capture-shot: probe hold failed\n");
                log_write("capture-shot: before library host close\n");
                capture_library_host_close(host);
                log_write("capture-shot: after library host close\n");
                return 1;
            }
            log_write("capture-shot: after probe hold\n");
            log_write("capture-shot: before library host close\n");
            capture_library_host_close(host);
            log_write("capture-shot: after library host close\n");
            return 0;
        }

        if (capture_mode >= 0) {
            /*
             * The library's own capture mode, asked for before the still.
             *
             * The mode is not the camera's raw switch, and the link between
             * them is not established, so this is an experiment and not a
             * setting: the run says which mode it asked for, and the still's
             * set says what came back.
             */
            if (capture_library_host_set_capture_mode(host,
                                                       (uint32_t)capture_mode) != 0) {
                fprintf(stderr, "capture-shot: the capture mode %d was refused: %s\n",
                        capture_mode, capture_library_host_error(host));
                log_write("capture-shot: the capture mode was refused\n");
                capture_library_host_close(host);
                return 1;
            }
            log_write("capture-shot: the capture mode was asked for\n");
        }

        capture_library_host_backend(host, &backend);

        capture_session_init(&session);
        memset(&config, 0, sizeof(config));
        config.backend = &backend;
        config.still_command = command;
        config.remaining_shots = remaining;
        config.free_space_bytes = free_space;
        config.on_image = 0;

        log_write("capture-shot: opening the session\n");
        status = capture_session_open(&session, &config);
        if (status != CAPTURE_SESSION_OK) {
            fprintf(stderr, "capture-shot: cannot open the session: %s\n",
                    status_text(status));
            capture_library_host_close(host);
            return 1;
        }

        if (iso != 0u || shutter != 0u) {
            status = capture_session_set_exposure(&session, iso, shutter);
            if (status != CAPTURE_SESSION_OK) {
                fprintf(stderr, "capture-shot: cannot set the exposure: %s\n",
                        status_text(status));
                capture_session_close(&session);
                capture_library_host_close(host);
                return 1;
            }
        }

        /*
         * The sensor data has to be kept by the capture for us to read it.
         *
         * The library host cannot make that ask, so the run above has
         * already refused this program before the library was opened.  The
         * ask is made here anyway, because it is the real one - it is the
         * session's own switch, and a backend that can make it must make
         * it - and a refusal here is reported as a refusal, not as a
         * missing image.
         */
        status = capture_session_set_sensor_retention(&session, 1);
        if (status != CAPTURE_SESSION_OK) {
            fprintf(stderr, "capture-shot: cannot ask for the sensor data: %s\n",
                    status_text(status));
            capture_session_close(&session);
            capture_library_host_close(host);
            return 1;
        }

        memset(&image, 0, sizeof(image));
        log_write("capture-shot: asking for a still\n");
        status = capture_session_capture(&session, timeout_ms, &image);
        log_write("capture-shot: the capture returned\n");
        if (status != CAPTURE_SESSION_OK) {
            fprintf(stderr, "capture-shot: the capture did not complete: %s\n",
                    status_text(status));
            capture_session_close(&session);
            capture_library_host_close(host);
            return 1;
        }

        if (image.sensor_data == 0 || image.sensor_size_bytes == 0u) {
            fprintf(stderr, "capture-shot: the still completed with no sensor data\n");
            failed = 1;
        } else {
            if (verbose) {
                fprintf(stderr,
                        "capture-shot: %lu bytes of sensor data, %ux%u, "
                        "stride %u, %u bits a sample\n",
                        (unsigned long)image.sensor_size_bytes, image.width,
                        image.height, image.stride_bytes,
                        image.bits_per_sample);
            }
            if (write_file(raw_path, image.sensor_data, image.sensor_size_bytes) != 0) {
                failed = 1;
            } else {
                failed = 0;
            }
        }
        if (!failed && jpeg_path != 0) {
            if (image.jpeg_data == 0 || image.jpeg_size_bytes == 0u) {
                /*
                 * The encoded set's main image is the YCbCr image, not the
                 * camera's JPEG: the stock application encodes that itself.
                 * So the JPEG is not on this route, and saying so is more
                 * useful than "no JPEG".
                 */
                fprintf(stderr,
                        "capture-shot: the still completed with no JPEG: the "
                        "encoded set's main image is the YCbCr image, and the "
                        "camera's JPEG is not on this route\n");
                failed = 1;
            } else if (write_file(jpeg_path, image.jpeg_data,
                                  image.jpeg_size_bytes) != 0) {
                failed = 1;
            }
        }

        log_write("capture-shot: done\n");
        (void)capture_session_release_image(&session);
        capture_session_close(&session);
        capture_library_host_close(host);
        return failed;
    }
}
