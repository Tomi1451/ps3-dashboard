/* launch.c - talks to webMAN MOD on 127.0.0.1:80, like webMAN's own pkglaunch tool */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <net/net.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include "launch.h"
#include "log.h"

static int net_ready = 0;

static void url_encode(char *dst, size_t n, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (; *src && j + 4 < n; src++) {
        unsigned char c = (unsigned char) *src;
        if (c == ' ' || c == '"' || c == '#' || c == '%' || c == '&' || c == '\'' || c == '+' || c == '?' || c >= 0x80) {
            dst[j++] = '%'; dst[j++] = hex[c >> 4]; dst[j++] = hex[c & 15];
        } else dst[j++] = c;
    }
    dst[j] = 0;
}

int launch_game(const Game *g)
{
    char enc[1024], req[1200];
    struct sockaddr_in sin;
    int s;

    if (!net_ready) { int r = netInitialize(); log_step("netInitialize = %d", r); net_ready = 1; }

    url_encode(enc, sizeof(enc), g->path);
    /* /mount_ps3 = mount the game and start it once we exit */
    snprintf(req, sizeof(req), "GET /mount_ps3%s HTTP/1.0\r\n\r\n", enc);

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(0x7F000001);   /* 127.0.0.1 */
    sin.sin_port = htons(80);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { log_step("socket() failed, errno %d", errno); return 0; }
    if (connect(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) { log_step("connect to 127.0.0.1:80 failed, errno %d (webMAN not running?)", errno); close(s); return 0; }
    log_step("sent: %s", req);
    send(s, req, strlen(req), 0);
    usleep(300 * 1000);                         /* give webMAN a moment to take the request */
    close(s);
    return 1;
}
