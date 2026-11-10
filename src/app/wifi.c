#include "wifi.h"
#include "storage_platform.h"

#if defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#else
int fork(void);
int setsid(void);
int waitpid(int pid, int *status, int options);
int dup2(int file, int target);
int chdir(const char *path);
int pipe(int fds[2]);
int execv(const char *path, char *const argv[]);
void _exit(int status) __attribute__((noreturn));
#endif

int g360_wifi_file_present(void)
{
    int fd = open(G360_WIFI_FILE, ST_RDONLY | ST_CLOEXEC);
    if (fd < 0) return 0;
    (void)close(fd);
    return 1;
}

int g360_wifi_apply(const char *mode)
{
    char *argv[5];
    int fd, pid, status = 0;
    if (mode == 0) return -1;
    fd = open(G360_WIFI_SCRIPT, ST_RDONLY | ST_CLOEXEC);
    if (fd < 0) return -1;
    (void)close(fd);
    argv[0] = "sh"; argv[1] = G360_WIFI_SCRIPT; argv[2] = "apply"; argv[3] = (char *)mode; argv[4] = 0;
    /* Two forks: the script is the grandchild, adopted by init, so the app
       neither waits for it nor leaves a zombie. Only calls that are safe
       after fork() in a threaded process are made before execv. */
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int file;
        (void)setsid();
        if (fork() != 0) _exit(0);
        file = open("/dev/null", ST_RDWR);
        if (file >= 0) { (void)dup2(file, 0); (void)dup2(file, 1); (void)dup2(file, 2); }
        for (file = 3; file < 1024; ++file) (void)close(file);
        (void)chdir("/");
        (void)execv("/bin/sh", argv);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return (status & 0x7f) == 0 && ((status >> 8) & 0xff) == 0 ? 0 : -1;
}

/* Counts the lines that start with "assoclist ": one per station. */
static int count_stations(const char *text)
{
    static const char tag[] = "assoclist ";
    int n = 0;
    const char *line = text;
    while (*line != '\0') {
        size_t k = 0;
        while (tag[k] != '\0' && line[k] == tag[k]) ++k;
        if (tag[k] == '\0') ++n;
        while (*line != '\0' && *line != '\n') ++line;
        if (*line == '\n') ++line;
    }
    return n;
}

int g360_wifi_ap_clients(const char *wl)
{
    char text[2048], *argv[3];
    int fds[2], pid, status = 0;
    size_t used = 0;
    if (wl == 0) return -1;
    if (pipe(fds) != 0) return -1;
    argv[0] = "wl"; argv[1] = "assoclist"; argv[2] = 0;
    pid = fork();
    if (pid < 0) { (void)close(fds[0]); (void)close(fds[1]); return -1; }
    if (pid == 0) {
        int file = open("/dev/null", ST_RDWR);
        if (file >= 0) { (void)dup2(file, 0); (void)dup2(file, 2); }
        (void)dup2(fds[1], 1);
        for (file = 3; file < 1024; ++file) (void)close(file);
        (void)execv(wl, argv);
        _exit(127);
    }
    (void)close(fds[1]);
    for (;;) {
        ptrdiff_t got;
        char rest[256];
        /* Past the buffer the rest is read and dropped, so the tool never blocks on a full pipe. */
        if (used + 1u >= sizeof text) got = read(fds[0], rest, sizeof rest);
        else got = read(fds[0], text + used, sizeof text - 1u - used);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        if (used + 1u < sizeof text) used += (size_t)got;
    }
    text[used] = '\0';
    (void)close(fds[0]);
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if ((status & 0x7f) != 0 || ((status >> 8) & 0xff) != 0) return -1;
    return count_stations(text);
}
