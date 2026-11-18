/* delete.c - removes a game from the HDD or a USB drive: the game itself and the files that
   belong only to it (its .cue, split ISO parts, the cover next to it, its game info file,
   the other discs of a multi-disc game). Saves and installed updates in /dev_hdd0/game are
   never touched.

   Safety: only paths inside the game folders the library scans (GAMES, GAMEZ, PS3ISO,
   PS2ISO, PSXISO on /dev_hdd0 or /dev_usb...) can be deleted, and never those folders
   themselves. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "delete.h"
#include "cobra.h"
#include "info.h"
#include "log.h"

#define MAXT 48
#define IS_DIR(m) (((m) & 0170000) == 0040000)

static char targets[MAXT][420];
static int ntargets;
static int files_done;
static void (*progress_fn)(int) = NULL;

static const char *roots[] = { "/GAMES/", "/GAMEZ/", "/PS3ISO/", "/PS2ISO/", "/PSXISO/", NULL };

/* inside a game folder, and not the game folder itself */
static int safe_path(const char *p)
{
    int i;
    if (strncmp(p, "/dev_hdd0/", 10) && strncmp(p, "/dev_usb", 8)) return 0;
    if (strstr(p, "/../") || strstr(p, "/./")) return 0;
    for (i = 0; roots[i]; i++) {
        const char *r = strstr(p, roots[i]);
        if (r && r[strlen(roots[i])] != 0) return 1;      /* something after "/PS2ISO/" */
    }
    return 0;
}

static int exists(const char *p) { sysFSStat st; return sysLv2FsStat(p, &st) == 0; }

static void add(const char *p)
{
    int i;
    if (!p[0] || ntargets >= MAXT || !exists(p) || !safe_path(p)) return;
    for (i = 0; i < ntargets; i++) if (!strcmp(targets[i], p)) return;
    snprintf(targets[ntargets++], 420, "%s", p);
}

/* one disc image and its companions */
static void add_image(const char *path)
{
    char p[460], stem[420];
    static const char *same_stem[] = { ".cue", ".CUE", ".ccd", ".CCD", ".sub", ".SUB", ".JPG", ".jpg", ".PNG", ".png", NULL };
    int i;
    size_t l = strlen(path);
    add(path);
    if (l > 2 && path[l - 2] == '.' && path[l - 1] == '0')            /* split ISO: .iso.0 .iso.1 ... */
        for (i = 1; i < 32; i++) { snprintf(p, sizeof(p), "%.*s.%d", (int) (l - 2), path, i); if (!exists(p)) break; add(p); }
    snprintf(p, sizeof(p), "%s.CONFIG", path); add(p);                  /* PS2 emulator config */
    snprintf(stem, sizeof(stem), "%s", path);
    char *dot = strrchr(stem, '.'), *slash = strrchr(stem, '/');
    if (dot && dot > slash) *dot = 0;
    if (l > 2 && path[l - 2] == '.' && path[l - 1] == '0') { dot = strrchr(stem, '.'); if (dot && dot > slash) *dot = 0; }
    for (i = 0; same_stem[i]; i++) { snprintf(p, sizeof(p), "%s%s", stem, same_stem[i]); add(p); }
    snprintf(p, sizeof(p), "%s.JPG", path); add(p);                    /* cover named after the full file name */
}

/* how much a file or folder holds */
static void measure(const char *p, u64 *bytes, int *files)
{
    sysFSStat st;
    if (sysLv2FsStat(p, &st) != 0) return;
    if (!IS_DIR(st.st_mode)) { *bytes += st.st_size; (*files)++; return; }
    s32 fd; sysFSDirent e; u64 rd;
    char sub[700];
    if (sysLv2FsOpenDir(p, &fd) != 0) return;
    while (sysLv2FsReadDir(fd, &e, &rd) == 0 && rd > 0) {
        if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, "..")) continue;
        snprintf(sub, sizeof(sub), "%s/%s", p, e.d_name);
        measure(sub, bytes, files);
    }
    sysLv2FsCloseDir(fd);
}

int del_plan(const Game *g, u64 *bytes, int *files)
{
    char p[460];
    int i;
    ntargets = 0; *bytes = 0; *files = 0;
    if (g->format == FMT_FOLDER && g->plat == PLAT_PS3) add(g->path);
    else {
        add_image(g->path);
        for (i = 0; i < 3 && i < g->ndiscs - 1; i++) if (g->discs[i][0]) add_image(g->discs[i]);
    }
    /* its info file from the PC tool, and an uploaded cover */
    {
        const char *base = strrchr(g->path, '/'); base = base ? base + 1 : g->path;
        snprintf(p, sizeof(p), INFO_DIR "/%s/%s.txt", plat_short(g->plat), base);
        if (exists(p) && ntargets < MAXT) snprintf(targets[ntargets++], 420, "%s", p);
        if (g->id[0]) {
            snprintf(p, sizeof(p), "/dev_hdd0/game/PSDB00001/USRDIR/covers/%s.JPG", g->id);
            if (exists(p) && ntargets < MAXT) snprintf(targets[ntargets++], 420, "%s", p);
        }
    }
    for (i = 0; i < ntargets; i++) measure(targets[i], bytes, files);
    return ntargets;
}

static int remove_tree(const char *p)
{
    sysFSStat st;
    if (sysLv2FsStat(p, &st) != 0) return 1;
    if (!IS_DIR(st.st_mode)) {
        int ok = sysLv2FsUnlink(p) == 0;
        if (!ok) { sysLv2FsChmod(p, 0777); ok = sysLv2FsUnlink(p) == 0; }
        if (!ok) log_step("delete: couldn't delete %s", p);
        files_done++;
        if (progress_fn && files_done % 25 == 0) progress_fn(files_done);
        return ok;
    }
    s32 fd; sysFSDirent e; u64 rd;
    char sub[700];
    int ok = 1;
    if (sysLv2FsOpenDir(p, &fd) == 0) {
        while (sysLv2FsReadDir(fd, &e, &rd) == 0 && rd > 0) {
            if (!strcmp(e.d_name, ".") || !strcmp(e.d_name, "..")) continue;
            snprintf(sub, sizeof(sub), "%s/%s", p, e.d_name);
            ok &= remove_tree(sub);
        }
        sysLv2FsCloseDir(fd);
    }
    if (sysLv2FsRmdir(p) != 0) { log_step("delete: couldn't remove folder %s", p); ok = 0; }
    return ok;
}

/* folder the game sat in on its own (PSXISO/Name/Name.bin): remove it if it's empty now */
static void remove_if_empty(const char *file)
{
    char dir[420];
    s32 fd; sysFSDirent e; u64 rd;
    int any = 0;
    snprintf(dir, sizeof(dir), "%s", file);
    char *s = strrchr(dir, '/');
    if (!s) return;
    *s = 0;
    if (!safe_path(dir)) return;                       /* never the PS2ISO / PSXISO folder itself */
    {
        size_t l = strlen(dir);
        int i;
        for (i = 0; roots[i]; i++) { size_t rl = strlen(roots[i]) - 1; if (l >= rl && !strcmp(dir + l - rl, roots[i]) ) return; }
    }
    if (sysLv2FsOpenDir(dir, &fd) != 0) return;
    while (sysLv2FsReadDir(fd, &e, &rd) == 0 && rd > 0)
        if (strcmp(e.d_name, ".") && strcmp(e.d_name, "..")) { any = 1; break; }
    sysLv2FsCloseDir(fd);
    if (!any && sysLv2FsRmdir(dir) == 0) log_step("delete: removed empty folder %s", dir);
}

int del_run(const Game *g, void (*progress)(int))
{
    int i, ok = 1;
    progress_fn = progress;
    files_done = 0;
    game_unmount();                                    /* it may still be in the drive */
    log_step("delete: %s (%d item%s)", g->title, ntargets, ntargets == 1 ? "" : "s");
    for (i = 0; i < ntargets; i++) {
        log_step("delete: %s", targets[i]);
        ok &= remove_tree(targets[i]);
    }
    if (!(g->format == FMT_FOLDER && g->plat == PLAT_PS3)) {
        remove_if_empty(g->path);
        for (i = 0; i < 3 && i < g->ndiscs - 1; i++) if (g->discs[i][0]) remove_if_empty(g->discs[i]);
    }
    log_step("delete: %s", ok ? "done" : "finished with errors");
    return ok;
}
