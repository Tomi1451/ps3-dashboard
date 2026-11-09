/* launch.c - talks to webMAN MOD on 127.0.0.1:80, like webMAN's own pkglaunch tool */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

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

/* sends one request to webMAN; webMAN runs ";"-chained commands one after another, in the background */
static int webman_send(const char *cmds)
{
    char req[1400];
    struct sockaddr_in sin;
    int s;

    if (!net_ready) { int r = netInitialize(); log_step("netInitialize = %d", r); net_ready = 1; }
    snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\n\r\n", cmds);

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(0x7F000001);   /* 127.0.0.1 */
    sin.sin_port = htons(80);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { log_step("socket() failed, errno %d", errno); return 0; }
    if (connect(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) { log_step("connect to 127.0.0.1:80 failed, errno %d (webMAN not running?)", errno); close(s); return 0; }
    log_step("sent: %s", req);
    send(s, req, strlen(req), 0);

    /* webMAN answers only after the whole chain has run (after we exit), so don't wait long */
    {
        struct timeval tv = { 1, 0 };
        char buf[512];
        int got, total = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        while ((got = recv(s, buf, sizeof(buf), 0)) > 0) total += got;
        log_step("webMAN replied %d bytes", total);
    }
    close(s);
    return 1;
}

/* Chain run by webMAN after we exit:
   /wait.ps3?xmb   - wait until the XMB has fully reloaded
   /wait.ps3?2     - 2 more seconds to settle
   /play.ps3<path> - mount the game and start it from the disc icon
   A lone /play.ps3 sent while we are still running only mounts: webMAN checks for the XMB once,
   right as we close and the XMB is still loading, so it skips the start. */
int launch_game(const Game *g) { return launch_path(g->path); }

int launch_path(const char *path)
{
    char enc[1024], cmds[1200];
    url_encode(enc, sizeof(enc), path);
    snprintf(cmds, sizeof(cmds), "/wait.ps3?xmb;/wait.ps3?2;/play.ps3%s", enc);
    return webman_send(cmds);
}

/* real disc in the drive: drop any mounted game image so the XMB sees the disc, then start it */
int launch_disc(void)
{
    return webman_send("/mount.ps3/unmount;/wait.ps3?xmb;/wait.ps3?3;/play.ps3");
}

/* PS3 disc -> folder game on the HDD, copied by webMAN (it decrypts the disc as it copies and
   shows its own progress / "copy finished" notifications on the XMB) */
int install_disc(const char *target)
{
    char enc[600], cmds[800];
    url_encode(enc, sizeof(enc), target);
    snprintf(cmds, sizeof(cmds), "/mount.ps3/unmount;/wait.ps3?xmb;/wait.ps3?3;/copy.ps3/dev_bdvd&to=%s", enc);
    return webman_send(cmds);
}

/* drop any mounted game image so the real disc in the drive is what other apps see */
int unmount_disc(void)
{
    return webman_send("/mount.ps3/unmount");
}

/* multiMAN, started the way the XMB starts it (webMAN puts it on the app_home icon and
   presses it), after we exit and the XMB has loaded. Unmounts any game image first so
   multiMAN sees the real disc in the drive. */
int open_multiman(void)
{
    return webman_send("/mount.ps3/unmount;/wait.ps3?xmb;/wait.ps3?3;/play.ps3?multiman");
}
