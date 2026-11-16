/* ftp.c - a small FTP server built into PS Dashboard, so the PC cover and game info tool can
   reach the PS3 without any other app. It runs while the dashboard is open, on port 21, with
   no password (like the PS3's usual homebrew FTP servers). If something else is already using
   port 21 (webMAN's or multiMAN's FTP), that one is left to do the job. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>

#include <net/net.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/thread.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "ftp.h"
#include "log.h"

#define FTP_PORT    21
#define MAX_CLIENTS 4
#define BUF_SIZE    (256 * 1024)
#define PATH_LEN    1024

static volatile int status = FTP_OFF, clients = 0;
static sys_ppu_thread_t server_th;
static char ip_text[32] = "";

typedef struct {
    int ctrl, pasv;
    char cwd[PATH_LEN], rnfr[PATH_LEN];
    u64 rest;
    char in[1024];
    int in_len;
} Session;

/* ---------------------------------------------------------------- helpers */
static void say(Session *s, const char *fmt, ...)
{
    char b[600];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int) sizeof(b) - 3) n = sizeof(b) - 3;
    b[n++] = '\r'; b[n++] = '\n';
    send(s->ctrl, b, n, 0);
}

static int send_all(int sock, const char *b, int n)
{
    while (n > 0) {
        int w = send(sock, b, n, 0);
        if (w <= 0) return 0;
        b += w; n -= w;
    }
    return 1;
}

/* one command line from the client; 0 when the connection closed */
static int read_line(Session *s, char *out, int max)
{
    for (;;) {
        int i;
        for (i = 0; i < s->in_len; i++)
            if (s->in[i] == '\n') {
                int l = i;
                if (l && s->in[l - 1] == '\r') l--;
                if (l >= max) l = max - 1;
                memcpy(out, s->in, l); out[l] = 0;
                memmove(s->in, s->in + i + 1, s->in_len - i - 1);
                s->in_len -= i + 1;
                return 1;
            }
        if (s->in_len >= (int) sizeof(s->in) - 1) s->in_len = 0;       /* line far too long: drop it */
        int got = recv(s->ctrl, s->in + s->in_len, sizeof(s->in) - 1 - s->in_len, 0);
        if (got <= 0) return 0;
        s->in_len += got;
    }
}

/* cwd + argument -> clean absolute path ("..", "." and doubled slashes handled) */
static void resolve(const Session *s, const char *arg, char *out)
{
    char tmp[PATH_LEN * 2], *parts[128], *p, *save = NULL;
    int n = 0, i;
    if (!arg || !*arg) snprintf(tmp, sizeof(tmp), "%s", s->cwd);
    else if (arg[0] == '/') snprintf(tmp, sizeof(tmp), "%s", arg);
    else snprintf(tmp, sizeof(tmp), "%s/%s", s->cwd, arg);
    for (p = strtok_r(tmp, "/", &save); p && n < 128; p = strtok_r(NULL, "/", &save)) {
        if (!strcmp(p, ".")) continue;
        if (!strcmp(p, "..")) { if (n) n--; continue; }
        parts[n++] = p;
    }
    out[0] = 0;
    if (!n) { strcpy(out, "/"); return; }
    for (i = 0; i < n; i++) { strncat(out, "/", PATH_LEN - strlen(out) - 1); strncat(out, parts[i], PATH_LEN - strlen(out) - 1); }
}

static int is_dir(const char *p) { sysFSStat st; return sysLv2FsStat(p, &st) == 0 && ((st.st_mode & 0170000) == 0040000); }

/* ---------------------------------------------------------------- passive data connection */
static int open_pasv(Session *s)
{
    struct sockaddr_in a, me;
    socklen_t l = sizeof(a);
    if (s->pasv >= 0) { close(s->pasv); s->pasv = -1; }
    s->pasv = socket(AF_INET, SOCK_STREAM, 0);
    if (s->pasv < 0) return 0;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = 0;
    if (bind(s->pasv, (struct sockaddr *) &a, sizeof(a)) < 0 || listen(s->pasv, 1) < 0) { close(s->pasv); s->pasv = -1; return 0; }
    getsockname(s->pasv, (struct sockaddr *) &a, &l);
    l = sizeof(me);
    getsockname(s->ctrl, (struct sockaddr *) &me, &l);        /* the address the PC reached us on */
    u32 ip = ntohl(me.sin_addr.s_addr);
    u16 port = ntohs(a.sin_port);
    say(s, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, port >> 8, port & 255);
    return 1;
}

static int data_accept(Session *s)
{
    if (s->pasv < 0) { say(s, "425 Use PASV first"); return -1; }
    int d = accept(s->pasv, NULL, NULL);
    close(s->pasv); s->pasv = -1;
    if (d < 0) say(s, "425 Can't open data connection");
    return d;
}

/* ---------------------------------------------------------------- listing */
static void list_dir(Session *s, const char *path, int names_only)
{
    static const char *mon[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    s32 fd; sysFSDirent e; u64 rd;
    char full[PATH_LEN + 300], line[PATH_LEN + 120];
    if (sysLv2FsOpenDir(path, &fd) != 0) { say(s, "550 Can't open %s", path); if (s->pasv >= 0) { close(s->pasv); s->pasv = -1; } return; }
    say(s, "150 Here comes the listing");
    int d = data_accept(s);
    if (d < 0) { sysLv2FsCloseDir(fd); return; }
    while (sysLv2FsReadDir(fd, &e, &rd) == 0 && rd > 0) {
        if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, "..")) continue;
        if (names_only) { snprintf(line, sizeof(line), "%s\r\n", e.d_name); }
        else {
            sysFSStat st;
            int dir = 0; u64 size = 0; time_t mt = 0;
            snprintf(full, sizeof(full), "%s%s%s", path, strcmp(path, "/") ? "/" : "", e.d_name);
            if (sysLv2FsStat(full, &st) == 0) { dir = (st.st_mode & 0170000) == 0040000; size = st.st_size; mt = st.st_mtime; }
            struct tm *t = gmtime(&mt);
            snprintf(line, sizeof(line), "%s 1 root root %llu %s %2d %4d %s\r\n", dir ? "drwxrwxrwx" : "-rw-rw-rw-",
                     (unsigned long long) size, mon[t ? t->tm_mon % 12 : 0], t ? t->tm_mday : 1, t ? t->tm_year + 1900 : 2026, e.d_name);
        }
        if (!send_all(d, line, strlen(line))) break;
    }
    sysLv2FsCloseDir(fd);
    close(d);
    say(s, "226 Done");
}

/* ---------------------------------------------------------------- files */
static void send_file(Session *s, const char *path)
{
    s32 fd; u64 got = 0, pos = 0;
    if (sysLv2FsOpen(path, SYS_O_RDONLY, &fd, 0, NULL, 0) != 0) { say(s, "550 Can't open %s", path); s->rest = 0; return; }
    if (s->rest) sysLv2FsLSeek64(fd, s->rest, 0, &pos);
    s->rest = 0;
    say(s, "150 Sending");
    int d = data_accept(s);
    if (d < 0) { sysLv2FsClose(fd); return; }
    char *buf = memalign(128, BUF_SIZE);
    int ok = buf != NULL;
    while (ok && sysLv2FsRead(fd, buf, BUF_SIZE, &got) == 0 && got > 0) ok = send_all(d, buf, (int) got);
    free(buf);
    sysLv2FsClose(fd);
    close(d);
    say(s, ok ? "226 Done" : "426 Transfer aborted");
}

static void receive_file(Session *s, const char *path, int append)
{
    s32 fd; u64 wrote = 0, pos = 0;
    int flags = SYS_O_WRONLY | SYS_O_CREAT | (append || s->rest ? 0 : SYS_O_TRUNC);
    if (sysLv2FsOpen(path, flags, &fd, 0666, NULL, 0) != 0) { say(s, "550 Can't create %s", path); s->rest = 0; return; }
    if (append) sysLv2FsLSeek64(fd, 0, 2, &pos);
    else if (s->rest) sysLv2FsLSeek64(fd, s->rest, 0, &pos);
    s->rest = 0;
    say(s, "150 Ready");
    int d = data_accept(s);
    if (d < 0) { sysLv2FsClose(fd); return; }
    char *buf = memalign(128, BUF_SIZE);
    int ok = buf != NULL, got;
    while (ok && (got = recv(d, buf, BUF_SIZE, 0)) > 0)
        ok = sysLv2FsWrite(fd, buf, got, &wrote) == 0 && (int) wrote == got;
    free(buf);
    sysLv2FsClose(fd);
    sysLv2FsChmod(path, 0666);
    close(d);
    say(s, ok ? "226 Done" : "451 Couldn't write the file");
}

/* ---------------------------------------------------------------- one client */
static void client_thread(void *arg)
{
    Session *s = (Session *) arg;
    char line[1100], path[PATH_LEN];
    say(s, "220 PS Dashboard FTP ready");
    while (read_line(s, line, sizeof(line))) {
        char *a = strchr(line, ' ');
        if (a) *a++ = 0; else a = "";
        char *c = line;
        for (char *q = c; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;

        if (!strcmp(c, "USER")) say(s, "331 Any password is fine");
        else if (!strcmp(c, "PASS")) say(s, "230 Logged in");
        else if (!strcmp(c, "SYST")) say(s, "215 UNIX Type: L8");
        else if (!strcmp(c, "FEAT")) { say(s, "211-Features:"); say(s, " SIZE"); say(s, " REST STREAM"); say(s, " UTF8"); say(s, "211 End"); }
        else if (!strcmp(c, "OPTS")) say(s, "200 OK");
        else if (!strcmp(c, "NOOP")) say(s, "200 OK");
        else if (!strcmp(c, "TYPE") || !strcmp(c, "MODE") || !strcmp(c, "STRU")) say(s, "200 OK");
        else if (!strcmp(c, "PWD") || !strcmp(c, "XPWD")) say(s, "257 \"%s\"", s->cwd);
        else if (!strcmp(c, "CWD") || !strcmp(c, "XCWD")) {
            resolve(s, a, path);
            if (!strcmp(path, "/") || is_dir(path)) { snprintf(s->cwd, sizeof(s->cwd), "%s", path); say(s, "250 OK"); }
            else say(s, "550 No such folder");
        }
        else if (!strcmp(c, "CDUP")) { resolve(s, "..", path); snprintf(s->cwd, sizeof(s->cwd), "%s", path); say(s, "250 OK"); }
        else if (!strcmp(c, "PASV")) { if (!open_pasv(s)) say(s, "425 Can't open passive port"); }
        else if (!strcmp(c, "LIST") || !strcmp(c, "NLST")) {
            if (*a == '-') { a = strchr(a, ' '); a = a ? a + 1 : ""; }   /* "LIST -a" style options */
            resolve(s, a, path);
            list_dir(s, path, c[0] == 'N');
        }
        else if (!strcmp(c, "RETR")) { resolve(s, a, path); send_file(s, path); }
        else if (!strcmp(c, "STOR")) { resolve(s, a, path); receive_file(s, path, 0); }
        else if (!strcmp(c, "APPE")) { resolve(s, a, path); receive_file(s, path, 1); }
        else if (!strcmp(c, "REST")) { s->rest = strtoull(a, NULL, 10); say(s, "350 Restarting at %llu", (unsigned long long) s->rest); }
        else if (!strcmp(c, "SIZE")) {
            sysFSStat st; resolve(s, a, path);
            if (sysLv2FsStat(path, &st) == 0) say(s, "213 %llu", (unsigned long long) st.st_size); else say(s, "550 No such file");
        }
        else if (!strcmp(c, "MKD") || !strcmp(c, "XMKD")) {
            resolve(s, a, path);
            if (sysLv2FsMkdir(path, 0777) == 0 || is_dir(path)) say(s, "257 \"%s\" created", path); else say(s, "550 Can't create it");
        }
        else if (!strcmp(c, "RMD") || !strcmp(c, "XRMD")) { resolve(s, a, path); say(s, sysLv2FsRmdir(path) == 0 ? "250 OK" : "550 Can't remove it"); }
        else if (!strcmp(c, "DELE")) { resolve(s, a, path); say(s, sysLv2FsUnlink(path) == 0 ? "250 OK" : "550 Can't delete it"); }
        else if (!strcmp(c, "RNFR")) { resolve(s, a, s->rnfr); say(s, "350 Ready for RNTO"); }
        else if (!strcmp(c, "RNTO")) { resolve(s, a, path); say(s, s->rnfr[0] && sysLv2FsRename(s->rnfr, path) == 0 ? "250 OK" : "550 Can't rename it"); s->rnfr[0] = 0; }
        else if (!strcmp(c, "QUIT")) { say(s, "221 Bye"); break; }
        else say(s, "502 Not supported");
    }
    if (s->pasv >= 0) close(s->pasv);
    close(s->ctrl);
    free(s);
    clients--;
    sysThreadExit(0);
}

/* ---------------------------------------------------------------- the server */
static void server_thread(void *arg)
{
    struct sockaddr_in a;
    int one = 1, srv;
    (void) arg;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { status = FTP_FAILED; log_step("ftp: no socket"); sysThreadExit(0); }
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(FTP_PORT);
    if (bind(srv, (struct sockaddr *) &a, sizeof(a)) < 0 || listen(srv, 4) < 0) {
        status = FTP_PORT_BUSY;
        log_step("ftp: port %d already in use (another FTP server is running) - leaving it to that one", FTP_PORT);
        close(srv);
        sysThreadExit(0);
    }
    status = FTP_RUNNING;
    log_step("ftp: running on port %d", FTP_PORT);
    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) { usleep(100000); continue; }
        if (clients >= MAX_CLIENTS) { const char *busy = "421 Too many connections\r\n"; send(c, busy, strlen(busy), 0); close(c); continue; }
        Session *s = calloc(1, sizeof(Session));
        if (!s) { close(c); continue; }
        s->ctrl = c; s->pasv = -1; strcpy(s->cwd, "/");
        sys_ppu_thread_t th;
        clients++;
        if (sysThreadCreate(&th, client_thread, s, 1200, 0x8000, 0, "ftp client") != 0) { clients--; close(c); free(s); }
    }
}

void ftp_start(void)
{
    if (status != FTP_OFF) return;
    netInitialize();
    status = FTP_STARTING;
    if (sysThreadCreate(&server_th, server_thread, NULL, 1200, 0x4000, 0, "ftp server") != 0) { status = FTP_FAILED; log_step("ftp: thread not started"); }
}

int ftp_status(void) { return status; }

/* the PS3's address on the home network (asks the network stack which address it would use
   to reach the internet - nothing is actually sent) */
const char *ftp_ip(void)
{
    struct sockaddr_in a, me;
    socklen_t l = sizeof(me);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    ip_text[0] = 0;
    if (s < 0) return ip_text;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(53); a.sin_addr.s_addr = htonl(0x08080808);
    if (connect(s, (struct sockaddr *) &a, sizeof(a)) == 0 && getsockname(s, (struct sockaddr *) &me, &l) == 0 && me.sin_addr.s_addr)
        snprintf(ip_text, sizeof(ip_text), "%s", inet_ntoa(me.sin_addr));
    close(s);
    return ip_text;
}
