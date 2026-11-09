/* info.c - reads INFO_DIR/<PS1|PS2|PS3>/<game file or folder name>.txt
   Plain "key=value" lines, Latin-1, written by GET_GAME_INFO.bat on the PC.
   Nothing is made up here: a field that isn't in the file stays empty. */
#include <stdio.h>
#include <string.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "info.h"
#include "log.h"

static GameInfo cur;
static const Game *cur_game = NULL;
static char cur_path[400];

static void take(char *dst, size_t n, const char *val, size_t len)
{
    if (len >= n) len = n - 1;
    memcpy(dst, val, len); dst[len] = 0;
}

void info_forget(void) { cur_game = NULL; cur_path[0] = 0; }

const GameInfo *info_get(const Game *g)
{
    static char buf[4096];
    char path[600];
    const char *base;
    s32 fd; u64 got = 0;

    if (g == cur_game && !strcmp(cur_path, g->path)) return &cur;
    cur_game = g; strncpy(cur_path, g->path, sizeof(cur_path) - 1); cur_path[sizeof(cur_path) - 1] = 0;
    memset(&cur, 0, sizeof(cur));

    base = strrchr(g->path, '/'); base = base ? base + 1 : g->path;
    snprintf(path, sizeof(path), INFO_DIR "/%s/%s.txt", plat_short(g->plat), base);
    if (sysLv2FsOpen(path, SYS_O_RDONLY, &fd, 0, NULL, 0) != 0) return &cur;
    sysLv2FsRead(fd, buf, sizeof(buf) - 1, &got);
    sysLv2FsClose(fd);
    buf[got] = 0;
    cur.found = 1;

    char *p = buf;
    while (*p) {
        char *eol = strchr(p, '\n'); if (!eol) eol = p + strlen(p);
        char *eq = memchr(p, '=', eol - p);
        if (eq) {
            size_t kl = eq - p, vl = eol - eq - 1;
            const char *v = eq + 1;
            if (vl && v[vl - 1] == '\r') vl--;
#define K(name, field) else if (kl == sizeof(name) - 1 && !memcmp(p, name, kl)) take(cur.field, sizeof(cur.field), v, vl)
            if (0) ;
            K("id", id); K("developer", developer); K("publisher", publisher); K("release", release);
            K("genre", genre); K("players", players); K("source", source); K("synopsis", synopsis);
#undef K
        }
        p = *eol ? eol + 1 : eol;
    }
    return &cur;
}
