/* g360-remote: the web page's CGI (installed as www/cgi-bin/g360).
 *
 * Sends the request's query string (QUERY_STRING; none is cmd=state) to the
 * running g360-app over its socket and prints the app's one-line JSON reply
 * (remote.h), or {"app":"off"} when no app answers; cmd=stream relays the
 * live preview's MJPEG instead (g360_remote_stream). A program of its own,
 * not g360-app: start.sh refuses to launch while any process named
 * g360-app exists, and install-app.sh stops every one, so the page's
 * polling must not run under that name. */
#include "app_platform.h"
#include "remote.h"

int main(void)
{
    static char reply[G360_REMOTE_REPLY_MAX + 1];
    const char *query = getenv("QUERY_STRING");
    if (query == 0 || query[0] == '\0') query = "cmd=state";
    /* The live preview's MJPEG, from the app's own port, so that the page
       needs only this address (busybox httpd passes output that starts
       with "HTTP/" through as it comes). */
    if (strlen(query) >= 10u && memcmp(query, "cmd=stream", 10) == 0 && (query[10] == '\0' || query[10] == '&')) {
        if (g360_remote_stream(G360_PREVIEW_PORT, 1) != 0)
            printf("Status: 503 Service Unavailable\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\n\r\npreview not running\n");
        return 0;
    }
    if (g360_remote_call(G360_REMOTE_SOCKET, query, reply, sizeof reply, 5000) != 0)
        (void)snprintf(reply, sizeof reply, "{\"app\":\"off\"}");
    printf("Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n%s\n", reply);
    return 0;
}
