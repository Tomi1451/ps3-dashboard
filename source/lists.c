/* lists.c - favourites.txt and recent.txt in the app folder, one game path per line */
#include <stdio.h>
#include <string.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "lists.h"
#include "log.h"

#define DIR_ "/dev_hdd0/game/PSDB00001/USRDIR/"
#define FAV_FILE    DIR_ "favourites.txt"
#define RECENT_FILE DIR_ "recent.txt"

static char recent[RECENT_MAX][400];
static int  nrecent = 0;

/* calls fn for each line of a text file */
static void read_lines(const char *path, void (*fn)(const char *line))
{
    static char buf[64 * 1024];
    s32 fd; u64 got = 0;
    if (sysLv2FsOpen(path, SYS_O_RDONLY, &fd, 0, NULL, 0) != 0) return;
    sysLv2FsRead(fd, buf, sizeof(buf) - 1, &got);
    sysLv2FsClose(fd);
    buf[got] = 0;
    char *p = buf;
    while (*p) {
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        size_t n = strlen(p);
        if (n && p[n - 1] == '\r') p[n - 1] = 0;
        if (*p) fn(p);
        if (!e) break;
        p = e + 1;
    }
}

static void write_file(const char *path, const char *data, size_t n)
{
    s32 fd; u64 w = 0;
    if (sysLv2FsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666, NULL, 0) != 0) { log_step("could not save %s", path); return; }
    sysLv2FsWrite(fd, data, n, &w);
    sysLv2FsClose(fd);
    sysLv2FsChmod(path, 0666);
}

static void on_fav(const char *line)    { Game *g = library_find(line); if (g) g->fav = 1; }
static void on_recent(const char *line) { if (nrecent < RECENT_MAX) { strncpy(recent[nrecent], line, 399); recent[nrecent][399] = 0; nrecent++; } }

void lists_load(void)
{
    nrecent = 0;
    read_lines(FAV_FILE, on_fav);
    read_lines(RECENT_FILE, on_recent);
}

static void save_favs(void)
{
    static char out[64 * 1024];
    size_t n = 0;
    int p, i;
    for (p = 0; p < PLAT_COUNT; p++)
        for (i = 0; i < library[p].count; i++)
            if (library[p].games[i].fav && n + 402 < sizeof(out))
                n += snprintf(out + n, sizeof(out) - n, "%s\n", library[p].games[i].path);
    write_file(FAV_FILE, out, n);
}

void fav_toggle(Game *g)
{
    g->fav = !g->fav;
    save_favs();
}

int fav_list(Game **out, int max)
{
    int p, i, n = 0;
    for (p = 0; p < PLAT_COUNT; p++)
        for (i = 0; i < library[p].count && n < max; i++)
            if (library[p].games[i].fav) out[n++] = &library[p].games[i];
    return n;
}

void recent_add(const Game *g)
{
    static char out[RECENT_MAX * 402];
    int i, j;
    size_t n = 0;
    for (i = 0; i < nrecent; i++) if (!strcmp(recent[i], g->path)) break;
    if (i == nrecent && nrecent < RECENT_MAX) nrecent++;
    if (i >= RECENT_MAX) i = RECENT_MAX - 1;
    for (j = i; j > 0; j--) strcpy(recent[j], recent[j - 1]);     /* move to the front */
    strncpy(recent[0], g->path, 399); recent[0][399] = 0;
    for (i = 0; i < nrecent; i++) n += snprintf(out + n, sizeof(out) - n, "%s\n", recent[i]);
    write_file(RECENT_FILE, out, n);
}

int recent_list(Game **out, int max)
{
    int i, n = 0;
    for (i = 0; i < nrecent && n < max; i++) {
        Game *g = library_find(recent[i]);
        if (g) out[n++] = g;
    }
    return n;
}
