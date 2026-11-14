#include "remote.h"
#include "settings.h"
#include "storage_platform.h"

#if defined(__APPLE__)
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#else
/* The camera's glibc (ARM32 Linux): no headers, so the socket calls and
   their numbers are declared here. sockaddr_un is a 16-bit family and a
   108-byte path; SOCK_CLOEXEC is O_CLOEXEC (02000000, measured, see
   storage_platform.h); MSG_NOSIGNAL keeps a client that went away from
   killing the app with SIGPIPE. */
#define AF_UNIX 1
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_CLOEXEC 02000000
#define MSG_NOSIGNAL 0x4000
#define POLLIN 1
struct sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};
_Static_assert(sizeof(struct sockaddr_un) == 110, "not Linux's struct sockaddr_un");
/* For the preview relay: an IPv4 address and port, network order. */
struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    uint32_t sin_addr;
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
int connect(int fd, const void *address, unsigned length);
ptrdiff_t send(int fd, const void *data, size_t count, int flags);
int poll(struct pollfd *fds, unsigned long count, int timeout);
#endif

/* ---- Parsing ------------------------------------------------------------ */

/* Compares the LENGTH characters at TEXT with WORD. */
static int same(const char *text, size_t length, const char *word)
{
    return strlen(word) == length && memcmp(text, word, length) == 0;
}

/* A whole number of at most 5 digits, no sign and no leading zero. */
static int whole(const char *text, size_t length, uint32_t *out)
{
    uint32_t n = 0;
    size_t k;
    if (length == 0 || length > 5 || (length > 1 && text[0] == '0')) return -1;
    for (k = 0; k < length; ++k) {
        if (text[k] < '0' || text[k] > '9') return -1;
        n = n * 10u + (uint32_t)(text[k] - '0');
    }
    *out = n;
    return 0;
}

/* One setting's name and value into REQUEST. Wi-Fi is not one: the page
   shows it but doesn't change it (a change would cut the page off). */
static int setting(const char *key, size_t key_length, const char *value, size_t length, g360_remote_request *r)
{
    uint32_t n;
    if (same(key, key_length, "mode")) {
        r->setting = G360_REMOTE_MODE;
        if (same(value, length, "single")) r->value = G360_MODE_SINGLE;
        else if (same(value, length, "hdr")) r->value = G360_MODE_HDR;
        else return -1;
    } else if (same(key, key_length, "output")) {
        r->setting = G360_REMOTE_OUTPUT;
        if (same(value, length, "raw")) r->value = G360_OUTPUT_RAW;
        else if (same(value, length, "jpeg")) r->value = G360_OUTPUT_JPEG;
        else if (same(value, length, "raw+jpeg") || same(value, length, "raw%2Bjpeg") ||
                 same(value, length, "raw%2bjpeg")) r->value = G360_OUTPUT_BOTH;
        else return -1;
    } else if (same(key, key_length, "raw")) {
        /* Compatibility for older clients: the old ON choice now saves both. */
        r->setting = G360_REMOTE_OUTPUT;
        if (same(value, length, "on")) r->value = G360_OUTPUT_BOTH;
        else if (same(value, length, "off")) r->value = G360_OUTPUT_JPEG;
        else return -1;
    } else if (same(key, key_length, "sounds")) {
        r->setting = G360_REMOTE_SOUNDS;
        if (same(value, length, "on")) r->value = 1;
        else if (same(value, length, "off")) r->value = 0;
        else return -1;
    } else if (same(key, key_length, "save")) {
        r->setting = G360_REMOTE_SAVE;
        if (same(value, length, "flash")) r->value = G360_SAVE_FLASH;
        else if (same(value, length, "card")) r->value = G360_SAVE_CARD;
        else return -1;
    } else if (same(key, key_length, "delay")) {
        r->setting = G360_REMOTE_DELAY;
        if (whole(value, length, &n) != 0 || (n != 0 && n != 2 && n != 5 && n != 10)) return -1;
        r->value = n;
    } else if (same(key, key_length, "shutter")) {
        r->setting = G360_REMOTE_SHUTTER;
        if (same(value, length, "auto")) r->value = 0;
        else {
            static const uint32_t tvs[G360_SHUTTER_COUNT] = G360_SHUTTER_TVS;
            unsigned k;
            if (whole(value, length, &n) != 0) return -1;
            for (k = 0; k < G360_SHUTTER_COUNT && tvs[k] != n; ++k) {}
            if (k == G360_SHUTTER_COUNT) return -1;
            r->value = n;
        }
    } else if (same(key, key_length, "iso")) {
        r->setting = G360_REMOTE_ISO;
        if (whole(value, length, &n) == 0 && (n == 100 || n == 200 || n == 400 || n == 800 || n == 1600)) r->value = n;
        else return -1;
    } else if (same(key, key_length, "stop")) {
        r->setting = G360_REMOTE_STOP;
        if (same(value, length, "auto")) r->value = 1;
        else if (same(value, length, "full")) r->value = 0;
        else return -1;
    } else {
        return -1;
    }
    return 0;
}

int g360_remote_parse(const char *query, g360_remote_request *r)
{
    /* The fields seen, by name: a name given twice is refused. */
    const char *cmd = 0, *set_key = 0, *set_value = 0, *sweep = 0, *every = 0;
    size_t cmd_length = 0, set_key_length = 0, set_value_length = 0, sweep_length = 0, every_length = 0;
    const char *p = query;
    memset(r, 0, sizeof *r);
    r->command = G360_REMOTE_BAD;
    if (query == 0 || strlen(query) >= G360_REMOTE_REQUEST_MAX) return -1;
    while (*p != '\0') {
        const char *key = p, *equals = 0, *end = p;
        size_t key_length, value_length;
        while (*end != '\0' && *end != '&') {
            if (*end == '=' && equals == 0) equals = end;
            ++end;
        }
        if (equals == 0) return -1;
        key_length = (size_t)(equals - key);
        value_length = (size_t)(end - equals - 1);
        if (same(key, key_length, "cmd")) {
            if (cmd) return -1;
            cmd = equals + 1; cmd_length = value_length;
        } else if (same(key, key_length, "sweep")) {
            if (sweep) return -1;
            sweep = equals + 1; sweep_length = value_length;
        } else if (same(key, key_length, "every")) {
            if (every) return -1;
            every = equals + 1; every_length = value_length;
        } else if (same(key, key_length, "_")) {
            /* the page's cache-buster */
        } else {
            if (set_key) return -1;
            set_key = key; set_key_length = key_length;
            set_value = equals + 1; set_value_length = value_length;
        }
        p = *end == '&' ? end + 1 : end;
    }
    if (cmd == 0) return -1;
    if (same(cmd, cmd_length, "state") || same(cmd, cmd_length, "cancel")) {
        if (set_key || sweep || every) return -1;
        r->command = cmd[0] == 's' ? G360_REMOTE_STATE : G360_REMOTE_CANCEL;
    } else if (same(cmd, cmd_length, "set")) {
        if (set_key == 0 || sweep || every) return -1;
        if (setting(set_key, set_key_length, set_value, set_value_length, r) != 0) { r->setting = 0; r->value = 0; return -1; }
        r->command = G360_REMOTE_SET;
    } else if (same(cmd, cmd_length, "capture")) {
        if (every) return -1;
        if (sweep) {
            if (same(sweep, sweep_length, "1")) r->sweep = 1;
            else if (!same(sweep, sweep_length, "0")) return -1;
        }
        /* nd=left|right|both: an ND sweep, so sweep=1 too. */
        if (set_key) {
            if (!same(set_key, set_key_length, "nd") || !r->sweep) { r->sweep = 0; return -1; }
            if (same(set_value, set_value_length, "left")) r->nd = G360_REMOTE_ND_LEFT;
            else if (same(set_value, set_value_length, "right")) r->nd = G360_REMOTE_ND_RIGHT;
            else if (same(set_value, set_value_length, "both")) r->nd = G360_REMOTE_ND_LEFT | G360_REMOTE_ND_RIGHT;
            else { r->sweep = 0; return -1; }
        }
        r->command = G360_REMOTE_CAPTURE;
    } else if (same(cmd, cmd_length, "auto")) {
        uint32_t n;
        if (set_key || sweep || every == 0) return -1;
        if (whole(every, every_length, &n) != 0 || n > G360_REMOTE_AUTO_MAX) return -1;
        r->value = n;
        r->command = G360_REMOTE_AUTO;
    } else if (same(cmd, cmd_length, "preview")) {
        if (sweep || every || set_key == 0 || !same(set_key, set_key_length, "on")) return -1;
        if (same(set_value, set_value_length, "1")) r->value = 1;
        else if (!same(set_value, set_value_length, "0")) return -1;
        r->command = G360_REMOTE_PREVIEW;
    } else {
        return -1;
    }
    return 0;
}

/* ---- Auto photos ---------------------------------------------------------- */

int g360_remote_auto_due(uint64_t now, uint64_t target, uint64_t lead)
{
    return now + lead >= target;
}

uint64_t g360_remote_auto_next(uint64_t now, uint64_t lead, uint64_t every)
{
    return now + lead + every;
}

/* ---- The socket --------------------------------------------------------- */

static int address(const char *path, struct sockaddr_un *a)
{
    size_t length = strlen(path);
    memset(a, 0, sizeof *a);
    if (length == 0 || length >= sizeof a->sun_path) return -1;
    a->sun_family = AF_UNIX;
    memcpy(a->sun_path, path, length);
    return 0;
}

#if defined(__APPLE__)
/* The host has no SOCK_CLOEXEC or accept4: the flags are set after. */
static int new_socket(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0), one = 1;
    if (fd < 0) return -1;
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    return fd;
}
static int accept_one(int listener)
{
    int fd = accept(listener, 0, 0), one = 1;
    if (fd < 0) return -1;
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    return fd;
}
#define SEND_FLAGS 0
#else
static int new_socket(void) { return socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0); }
/* Close-on-exec from the start: the app starts sound players and the Wi-Fi
   script from other threads, and they must not keep a connection open. */
static int accept_one(int listener) { return accept4(listener, 0, 0, SOCK_CLOEXEC); }
#define SEND_FLAGS MSG_NOSIGNAL
#endif

int g360_remote_listen(const char *path)
{
    struct sockaddr_un a;
    int fd;
    if (path == 0 || address(path, &a) != 0) return -1;
    (void)unlink(path);
    fd = new_socket();
    if (fd < 0) return -1;
    if (bind(fd, (const void *)&a, (unsigned)sizeof a) != 0 || listen(fd, 4) != 0) { (void)close(fd); return -1; }
    return fd;
}

/* Sends all of DATA, or gives up. */
static int send_all(int fd, const char *data, size_t length)
{
    while (length > 0) {
        ptrdiff_t n = send(fd, data, length, SEND_FLAGS);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        data += n; length -= (size_t)n;
    }
    return 0;
}

/* Reads one line (without its newline) into LINE within TIMEOUT_MS. A line
   that doesn't fit, or none in time, is -1. */
static int read_line(int fd, char *line, size_t size, int timeout_ms)
{
    size_t used = 0;
    int waited = 0;
    while (waited <= timeout_ms) {
        struct pollfd p;
        ptrdiff_t n;
        p.fd = fd; p.events = POLLIN; p.revents = 0;
        n = poll(&p, 1, 100);
        if (n < 0 && errno != EINTR) return -1;
        if (n <= 0) { waited += 100; continue; }
        if (used + 1 >= size) return -1;
        n = read(fd, line + used, size - 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        while (n-- > 0) {
            if (line[used] == '\n') { line[used] = '\0'; return 0; }
            ++used;
        }
    }
    return -1;
}

int g360_remote_serve_one(int listener, g360_remote_handler handler, void *context)
{
    static char request[G360_REMOTE_REQUEST_MAX], reply[G360_REMOTE_REPLY_MAX + 1];
    int fd = accept_one(listener);
    size_t length;
    if (fd < 0) return -1;
    if (read_line(fd, request, sizeof request, 1000) == 0) {
        reply[0] = '\0';
        handler(context, request, reply, G360_REMOTE_REPLY_MAX);
        length = strlen(reply);
        reply[length++] = '\n';
        (void)send_all(fd, reply, length);
    }
    (void)close(fd);
    return 0;
}

int g360_remote_call(const char *path, const char *request, char *reply, size_t size, int timeout_ms)
{
    struct sockaddr_un a;
    int fd, status;
    size_t length;
    if (path == 0 || request == 0 || reply == 0 || size == 0 || address(path, &a) != 0) return -1;
    reply[0] = '\0';
    length = strlen(request);
    if (length >= G360_REMOTE_REQUEST_MAX || strchr(request, '\n') != 0) return -1;
    fd = new_socket();
    if (fd < 0) return -1;
    if (connect(fd, (const void *)&a, (unsigned)sizeof a) != 0 || send_all(fd, request, length) != 0 || send_all(fd, "\n", 1) != 0) {
        (void)close(fd);
        return -1;
    }
    status = read_line(fd, reply, size, timeout_ms);
    (void)close(fd);
    if (status != 0) reply[0] = '\0';
    return status;
}

/* ---- The preview relay ---------------------------------------------------- */

int g360_remote_stream(int port, int out)
{
    static const char request[] = "GET /preview.mjpg HTTP/1.0\r\n\r\n";
    static char buffer[16384];
    struct sockaddr_in a;
    int fd, relayed = 0;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = (unsigned short)(((port & 0xff) << 8) | ((port >> 8) & 0xff));
#if defined(__APPLE__)
    a.sin_addr.s_addr = 0x0100007fu; /* 127.0.0.1 */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) { int one = 1; (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); }
#else
    a.sin_addr = 0x0100007fu; /* 127.0.0.1 */
    fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
#endif
    if (fd < 0) return -1;
    if (connect(fd, (const void *)&a, (unsigned)sizeof a) != 0 || send_all(fd, request, sizeof request - 1u) != 0) {
        (void)close(fd);
        return -1;
    }
    for (;;) {
        struct pollfd p;
        ptrdiff_t n;
        size_t done = 0;
        p.fd = fd; p.events = POLLIN; p.revents = 0;
        /* No byte for 10 s: the app is gone or stuck. */
        if (poll(&p, 1, 10000) <= 0) break;
        n = read(fd, buffer, sizeof buffer);
        if (n <= 0) break;
        while (done < (size_t)n) {
            ptrdiff_t w = write(out, buffer + done, (size_t)n - done);
            if (w <= 0) { (void)close(fd); return 0; } /* the viewer left */
            done += (size_t)w;
        }
        relayed = 1;
    }
    (void)close(fd);
    return relayed ? 0 : -1;
}
