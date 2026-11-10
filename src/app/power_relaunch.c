/* Moving the power-off sequence off the card; see power.h. */
#include "power.h"

int g360_power_relaunch(void)
{
    static char *const argv[] = {G360_POWER_COPY, "--power-off", 0};
    char buffer[16384];
    int in = open(G360_POWER_SELF, 0), out;
    ptrdiff_t n = 0;
    if (in < 0) return -1;
    out = open(G360_POWER_COPY, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC, 0700);
    if (out < 0) { (void)close(in); return -1; }
    while ((n = read(in, buffer, sizeof buffer)) > 0) {
        if (write(out, buffer, (size_t)n) != n) { n = -1; break; }
    }
    (void)close(in);
    if (close(out) != 0 || n < 0) return -1;
    (void)execv(G360_POWER_COPY, argv);
    return -1;
}

int g360_power_detach(void)
{
    int file, log = open(G360_POWER_LOG, CAPTURE_O_WRONLY | CAPTURE_O_CREAT | CAPTURE_O_TRUNC, 0644);
    int null = open("/dev/null", 0);
    if (null >= 0) (void)dup2(null, 0);
    if (log >= 0) { (void)dup2(log, 1); (void)dup2(log, 2); }
    for (file = 3; file < 1024; ++file) (void)close(file);
    (void)chdir("/");
    return log >= 0 ? 0 : -1;
}
