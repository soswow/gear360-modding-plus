/* The live preview (T20). See preview.h. */
#include "app_platform.h"
#include <stdatomic.h> /* the compiler's own header: no C library needed */
#include "preview.h"

#if defined(__APPLE__)
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#else
/* The camera's glibc (ARM32 Linux), declared by hand as in remote.c. */
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_CLOEXEC 02000000
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define MSG_NOSIGNAL 0x4000
#define POLLIN 1
#define POLLOUT 4
struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port; /* network order */
    uint32_t sin_addr;       /* network order; 0 is any address */
    unsigned char sin_zero[8];
};
_Static_assert(sizeof(struct sockaddr_in) == 16, "not Linux's struct sockaddr_in");
struct pollfd {
    int fd;
    short events, revents;
};
int socket(int domain, int type, int protocol);
int bind(int fd, const void *address, unsigned length);
int listen(int fd, int backlog);
int accept4(int fd, void *address, unsigned *length, int flags);
int setsockopt(int fd, int level, int name, const void *value, unsigned length);
ptrdiff_t send(int fd, const void *data, size_t count, int flags);
int poll(struct pollfd *fds, unsigned long count, int timeout);
char *strstr(const char *text, const char *word);
#endif

#define BOUNDARY "g360frame"
#define FRAME_PERIOD_MS (1000u / G360_PREVIEW_FPS)
/* The half-size copy: every other Y row and every chroma row, WIDTH bytes
   each, for frames up to 1920 wide (the module asks for 1280x640). */
#define COPY_MAX_WIDTH 1920u
#define COPY_MAX_ROWS 540u

typedef _Atomic uint32_t shared_u32;
typedef void *(*tj_init_fn)(void);
typedef int (*tj_compress2_fn)(void *handle, unsigned char *source, int width, int pitch, int height,
                               int pixel_format, unsigned char **jpeg, unsigned long *jpeg_size,
                               int subsampling, int quality, int flags);
typedef void (*tj_free_fn)(unsigned char *buffer);

/* The frame hand-over between the encoder (asks) and the capture
   library's thread (copies): */
enum { COPY_IDLE, COPY_ASKED, COPY_COPYING, COPY_DONE };
static shared_u32 copy_state, wanted, running, capturing, viewers, encoded, ending;
static shared_u32 jpeg_seq;              /* frames published; slot jpeg_seq & 1 is the latest */
static uint64_t wanted_since, last_left; /* ms; written under the atomics' ordering */
static unsigned char copy_y[COPY_MAX_WIDTH * COPY_MAX_ROWS], copy_c[COPY_MAX_WIDTH * COPY_MAX_ROWS];
static uint32_t copy_width, copy_rows;
static unsigned char jpeg_slot[2][G360_PREVIEW_JPEG_MAX];
static size_t jpeg_size[2];
static int listener = -1;

static uint64_t now_ms(void)
{
    capture_timespec t;
    if (clock_gettime(CAPTURE_CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static void pause_ms(unsigned ms)
{
    capture_timespec t, left;
    t.tv_sec = (long)(ms / 1000u);
    t.tv_nsec = (long)(ms % 1000u) * 1000000L;
    (void)nanosleep(&t, &left);
}

/* ---- State ---------------------------------------------------------------- */

void g360_app_preview_want(int on)
{
    if (on && !atomic_load(&wanted)) {
        wanted_since = now_ms();
        printf("APP PREVIEW on\n");
    } else if (!on && atomic_load(&wanted)) {
        printf("APP PREVIEW off\n");
    }
    (void)fflush(stdout);
    atomic_store(&wanted, on ? 1u : 0u);
}

int g360_app_preview_wanted(void) { return atomic_load(&wanted) != 0; }

void g360_app_preview_running(int is_running, int is_capturing)
{
    atomic_store(&running, is_running ? 1u : 0u);
    atomic_store(&capturing, is_capturing ? 1u : 0u);
}

int g360_app_preview_state(void)
{
    if (!atomic_load(&wanted)) return G360_PREVIEW_OFF;
    if (atomic_load(&running)) return G360_PREVIEW_ON;
    return atomic_load(&capturing) ? G360_PREVIEW_PAUSED : G360_PREVIEW_STARTING;
}

uint32_t g360_app_preview_viewers(void) { return atomic_load(&viewers); }
uint32_t g360_app_preview_encoded(void) { return atomic_load(&encoded); }

/* ---- Frames --------------------------------------------------------------- */

/* On the capture library's thread: copies the frame when the encoder asked
   for one. Reading the camera's frame memory is slow (about 60 MB/s), so
   only every other Y row is read. */
void g360_app_preview_frame(const g360_preview_frame *frame, void *context)
{
    uint32_t expected = COPY_ASKED, row, rows;
    (void)context;
    if (frame->y == 0 || frame->c == 0 || frame->width < 2u || frame->height < 2u ||
        frame->width > COPY_MAX_WIDTH || frame->height / 2u > COPY_MAX_ROWS)
        return;
    if (!atomic_compare_exchange_strong(&copy_state, &expected, COPY_COPYING)) return;
    rows = frame->height / 2u;
    for (row = 0; row < rows; ++row) {
        memcpy(copy_y + (size_t)row * frame->width, frame->y + (size_t)(2u * row) * frame->buffer_width,
               frame->width);
        memcpy(copy_c + (size_t)row * frame->width, frame->c + (size_t)row * frame->buffer_width,
               frame->width);
    }
    copy_width = frame->width & ~1u;
    copy_rows = rows;
    atomic_store(&copy_state, COPY_DONE);
}

static unsigned char clamp(int v) { return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); }

void g360_app_preview_half_rgb(const unsigned char *y, const unsigned char *c, uint32_t width,
                               uint32_t rows, unsigned char *rgb)
{
    uint32_t row, col;
    for (row = 0; row < rows; ++row) {
        const unsigned char *yr = y + (size_t)row * width, *cr = c + (size_t)row * width;
        for (col = 0; col < width / 2u; ++col) {
            int yy = yr[2u * col], cb = cr[2u * col] - 128, cv = cr[2u * col + 1u] - 128;
            /* BT.601 full range, 16-bit fixed point. */
            rgb[0] = clamp(yy + ((91881 * cv) >> 16));
            rgb[1] = clamp(yy - ((22554 * cb + 46802 * cv) >> 16));
            rgb[2] = clamp(yy + ((116130 * cb) >> 16));
            rgb += 3;
        }
    }
}

/* Asks the capture library's thread for a copy; 1 when one arrived. */
static int take_copy(void)
{
    uint64_t start = now_ms();
    uint32_t expected = COPY_IDLE;
    (void)atomic_compare_exchange_strong(&copy_state, &expected, COPY_ASKED);
    while (atomic_load(&copy_state) != COPY_DONE) {
        expected = COPY_ASKED;
        if (now_ms() - start > 500u && atomic_compare_exchange_strong(&copy_state, &expected, COPY_IDLE)) return 0;
        if (atomic_load(&ending)) return 0;
        pause_ms(5);
    }
    return 1;
}

/* ---- The encoder ---------------------------------------------------------- */

static void *encoder_main(void *unused)
{
    static unsigned char rgb[(COPY_MAX_WIDTH / 2u) * COPY_MAX_ROWS * 3u];
    void *library = 0, *handle = 0, *symbol;
    tj_init_fn init_fn = 0;
    tj_compress2_fn compress_fn = 0;
    tj_free_fn free_fn = 0;
    int failed = 0;
    (void)unused;
    while (!atomic_load(&ending)) {
        uint64_t started = now_ms();
        uint32_t seq, slot;
        unsigned char *jpeg = 0;
        unsigned long size = 0;
        int rc;
        /* Nobody watching for G360_PREVIEW_IDLE_MS: stop. */
        if (atomic_load(&wanted) && !atomic_load(&viewers)) {
            uint64_t since = last_left > wanted_since ? last_left : wanted_since;
            if (started - since > G360_PREVIEW_IDLE_MS) {
                printf("APP PREVIEW stopped (no viewer for %u s)\n", G360_PREVIEW_IDLE_MS / 1000u);
                g360_app_preview_want(0);
            }
        }
        if (!atomic_load(&running) || !atomic_load(&viewers) || failed) { pause_ms(50); continue; }
        if (handle == 0) {
            library = dlopen(G360_TURBOJPEG_LIBRARY, CAPTURE_RTLD_NOW);
            if (library != 0) {
                symbol = dlsym(library, "tjInitCompress"); memcpy(&init_fn, &symbol, sizeof symbol);
                symbol = dlsym(library, "tjCompress2"); memcpy(&compress_fn, &symbol, sizeof symbol);
                symbol = dlsym(library, "tjFree"); memcpy(&free_fn, &symbol, sizeof symbol);
            }
            if (init_fn && compress_fn && free_fn) handle = init_fn();
            if (handle == 0) {
                printf("APP PREVIEW encoder unavailable (%s)\n", G360_TURBOJPEG_LIBRARY); (void)fflush(stdout);
                failed = 1;
                continue;
            }
        }
        if (!take_copy()) continue;
        g360_app_preview_half_rgb(copy_y, copy_c, copy_width, copy_rows, rgb);
        rc = compress_fn(handle, rgb, (int)(copy_width / 2u), (int)(copy_width / 2u * 3u), (int)copy_rows, 0 /* RGB */,
                         &jpeg, &size, 2 /* 4:2:0 */, G360_PREVIEW_QUALITY, 0);
        atomic_store(&copy_state, COPY_IDLE);
        if (rc == 0 && jpeg != 0 && size > 0 && size <= G360_PREVIEW_JPEG_MAX) {
            /* The other slot than the latest, then announce it: a viewer
               copying the latest one is not disturbed. */
            seq = atomic_load(&jpeg_seq);
            slot = (seq + 1u) & 1u;
            memcpy(jpeg_slot[slot], jpeg, size);
            jpeg_size[slot] = size;
            atomic_store(&jpeg_seq, seq + 1u);
            atomic_fetch_add(&encoded, 1u);
        }
        if (jpeg != 0) free_fn(jpeg);
        if (now_ms() - started < FRAME_PERIOD_MS) pause_ms((unsigned)(FRAME_PERIOD_MS - (now_ms() - started)));
    }
    return 0;
}

/* Copies the latest JPEG into OUT; returns its size and sets *SEQ, or 0
   when there is none (or none newer than SINCE). */
static size_t latest_jpeg(unsigned char *out, uint32_t since, uint32_t *seq)
{
    for (;;) {
        uint32_t s = atomic_load(&jpeg_seq), after;
        size_t size;
        if (s == 0 || s == since) return 0;
        size = jpeg_size[s & 1u];
        memcpy(out, jpeg_slot[s & 1u], size);
        after = atomic_load(&jpeg_seq);
        /* Two newer frames mean the slot was written during the copy. */
        if (after - s < 2u) { *seq = s; return size; }
    }
}

/* ---- The viewers ---------------------------------------------------------- */

int g360_app_preview_part_header(char *out, size_t size, size_t jpeg_bytes)
{
    return snprintf(out, size, "--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %lu\r\n\r\n",
                    (unsigned long)jpeg_bytes);
}

/* Sends all of DATA within 5 s; 0 or -1. */
static int send_all(int fd, const void *data, size_t count)
{
    const unsigned char *p = data;
    while (count > 0) {
        struct pollfd w;
        ptrdiff_t n;
        w.fd = fd; w.events = POLLOUT; w.revents = 0;
        if (poll(&w, 1, 5000) <= 0) return -1;
        n = send(fd, p, count, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        p += n; count -= (size_t)n;
    }
    return 0;
}

static void *viewer_main(void *argument)
{
    static const char header[] =
        "HTTP/1.0 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=" BOUNDARY "\r\n"
        "Cache-Control: no-cache, no-store\r\nPragma: no-cache\r\nAccess-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n";
    int fd = (int)(intptr_t)argument;
    unsigned char *jpeg = malloc(G360_PREVIEW_JPEG_MAX);
    char request[512], part[128];
    size_t have = 0;
    uint32_t seq = 0;
    uint64_t start = now_ms();
    /* The request: read up to its blank line (1 s), its content ignored. */
    while (jpeg != 0 && have < sizeof request - 1u && now_ms() - start < 1000u) {
        struct pollfd r;
        ptrdiff_t n;
        r.fd = fd; r.events = POLLIN; r.revents = 0;
        if (poll(&r, 1, 100) <= 0) continue;
        n = read(fd, request + have, sizeof request - 1u - have);
        if (n <= 0) break;
        have += (size_t)n;
        request[have] = '\0';
        if (strstr(request, "\r\n\r\n") || strstr(request, "\n\n")) break;
    }
    if (jpeg != 0 && have >= 4u && memcmp(request, "GET ", 4) == 0 && send_all(fd, header, sizeof header - 1u) == 0) {
        while (!atomic_load(&ending) && atomic_load(&wanted)) {
            size_t size = latest_jpeg(jpeg, seq, &seq);
            int length;
            if (size == 0) { pause_ms(20); continue; }
            length = g360_app_preview_part_header(part, sizeof part, size);
            if (send_all(fd, part, (size_t)length) != 0 || send_all(fd, jpeg, size) != 0 ||
                send_all(fd, "\r\n", 2u) != 0)
                break;
        }
    }
    free(jpeg);
    (void)close(fd);
    last_left = now_ms();
    atomic_fetch_sub(&viewers, 1u);
    printf("APP PREVIEW viewer left (%lu watching)\n", (unsigned long)atomic_load(&viewers)); (void)fflush(stdout);
    return 0;
}

static void *listener_main(void *unused)
{
    (void)unused;
    while (!atomic_load(&ending)) {
        struct pollfd r;
        pthread_t thread;
        int fd;
        r.fd = listener; r.events = POLLIN; r.revents = 0;
        if (poll(&r, 1, 200) <= 0) continue;
#if defined(__APPLE__)
        fd = accept(listener, 0, 0);
        if (fd >= 0) { int one = 1; (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); }
#else
        fd = accept4(listener, 0, 0, SOCK_CLOEXEC);
#endif
        if (fd < 0) continue;
        /* Busy: a short refusal, so the page can say so. */
        if (atomic_fetch_add(&viewers, 1u) >= G360_PREVIEW_VIEWERS || !atomic_load(&wanted)) {
            static const char busy[] = "HTTP/1.0 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            char request[512];
            struct pollfd q;
            /* Read the request first: closing over unread bytes resets the
               connection, which can lose the refusal. */
            q.fd = fd; q.events = POLLIN; q.revents = 0;
            if (poll(&q, 1, 200) > 0) (void)read(fd, request, sizeof request);
            (void)send_all(fd, busy, sizeof busy - 1u);
            (void)close(fd);
            atomic_fetch_sub(&viewers, 1u);
            continue;
        }
        printf("APP PREVIEW viewer joined (%lu watching)\n", (unsigned long)atomic_load(&viewers)); (void)fflush(stdout);
        if (pthread_create(&thread, 0, viewer_main, (void *)(intptr_t)fd) != 0) {
            (void)close(fd);
            atomic_fetch_sub(&viewers, 1u);
            continue;
        }
        (void)pthread_detach(thread);
    }
    return 0;
}

int g360_app_preview_init(int port)
{
    struct sockaddr_in a;
    pthread_t thread;
    int one = 1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = (unsigned short)(((port & 0xff) << 8) | ((port >> 8) & 0xff)); /* network order */
#if defined(__APPLE__)
    listener = socket(AF_INET, SOCK_STREAM, 0);
    a.sin_addr.s_addr = 0;
#else
    listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    a.sin_addr = 0;
#endif
    if (listener < 0) return -1;
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (bind(listener, (const void *)&a, (unsigned)sizeof a) != 0 || listen(listener, 4) != 0) {
        (void)close(listener);
        listener = -1;
        return -1;
    }
    if (pthread_create(&thread, 0, listener_main, 0) != 0) return -1;
    (void)pthread_detach(thread);
    if (pthread_create(&thread, 0, encoder_main, 0) != 0) return -1;
    (void)pthread_detach(thread);
    return 0;
}

void g360_app_preview_shutdown(void)
{
    atomic_store(&ending, 1u);
    atomic_store(&wanted, 0u);
}
