/* g360-remote: the web page's CGI (installed as www/cgi-bin/g360).
 *
 * Sends the request's query string (QUERY_STRING; none is cmd=state) to the
 * running g360-app over its socket and prints the app's one-line JSON reply
 * (remote.h), or {"app":"off"} when no app answers; cmd=stream relays the
 * live preview's MJPEG instead (g360_remote_stream); cmd=scan sends the
 * app's sweep scan file (G360_SCAN_FILE, scan.h) as it is. A program of its own,
 * not g360-app: start.sh refuses to launch while any process named
 * g360-app exists, and install-app.sh stops every one, so the page's
 * polling must not run under that name. */
#include "app_platform.h"
#include "remote.h"

/* Whether QUERY is CMD alone or followed by '&' (the page's cache-buster). */
static int is_command(const char *query, const char *cmd)
{
    size_t n = strlen(cmd);
    return strlen(query) >= n && memcmp(query, cmd, n) == 0 && (query[n] == '\0' || query[n] == '&');
}

/* The scan file, binary, or 404 before the first scanned sweep. */
static int send_scan(void)
{
    static unsigned char buffer[65536];
    int file = open(G360_SCAN_FILE, 0);
    ptrdiff_t n;
    if (file < 0) {
        printf("Status: 404 Not Found\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\n\r\nno scan yet\n");
        return 0;
    }
    printf("Content-Type: application/octet-stream\r\nCache-Control: no-store\r\n\r\n");
    (void)fflush(stdout);
    while ((n = read(file, buffer, sizeof buffer)) > 0) {
        ptrdiff_t at = 0;
        while (at < n) {
            ptrdiff_t w = write(1, buffer + at, (size_t)(n - at));
            if (w <= 0) { (void)close(file); return 0; }
            at += w;
        }
    }
    (void)close(file);
    return 0;
}

int main(void)
{
    static char reply[G360_REMOTE_REPLY_MAX + 1];
    const char *query = getenv("QUERY_STRING");
    if (query == 0 || query[0] == '\0') query = "cmd=state";
    /* The live preview's MJPEG, from the app's own port, so that the page
       needs only this address (busybox httpd passes output that starts
       with "HTTP/" through as it comes). */
    if (is_command(query, "cmd=scan")) return send_scan();
    if (is_command(query, "cmd=stream")) {
        if (g360_remote_stream(G360_PREVIEW_PORT, 1) != 0)
            printf("Status: 503 Service Unavailable\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\n\r\npreview not running\n");
        return 0;
    }
    if (g360_remote_call(G360_REMOTE_SOCKET, query, reply, sizeof reply, 5000) != 0)
        (void)snprintf(reply, sizeof reply, "{\"app\":\"off\"}");
    printf("Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n%s\n", reply);
    return 0;
}
