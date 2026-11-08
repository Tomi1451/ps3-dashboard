/* library.c - finds games on the internal HDD in the standard webMAN folders */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <lv2/sysfs.h>
#include <sys/file.h>

#include "library.h"
#include "log.h"

#define MM_COVERS "/dev_hdd0/game/BLES80608/USRDIR/covers"
#define S_ISDIR_PS3(m) (((m) & 0170000) == 0040000)

GameList library[PLAT_COUNT];

const char *plat_name(int p)  { return p == PLAT_PS1 ? "PlayStation" : p == PLAT_PS2 ? "PlayStation 2" : "PlayStation 3"; }
const char *plat_short(int p) { return p == PLAT_PS1 ? "PS1" : p == PLAT_PS2 ? "PS2" : "PS3"; }
const char *format_name(int f)
{
    switch (f) {
        case FMT_FOLDER: return "Folder (JB)";
        case FMT_ISO:    return "ISO";
        case FMT_BINCUE: return "BIN + CUE";
        default:         return "PS2 Classic";
    }
}

/* ---------------------------------------------------------------- helpers */
static int file_exists(const char *path)
{
    sysFSStat st;
    return sysLv2FsStat(path, &st) == 0;
}

static int is_dir(const char *path)
{
    sysFSStat st;
    return sysLv2FsStat(path, &st) == 0 && S_ISDIR_PS3(st.st_mode);
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

static Game *add_game(int plat)
{
    GameList *l = &library[plat];
    if (l->count == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 64;
        Game *n = realloc(l->games, ncap * sizeof(Game));
        if (!n) return NULL;
        l->games = n; l->cap = ncap;
    }
    Game *g = &l->games[l->count++];
    memset(g, 0, sizeof(*g));
    g->plat = plat;
    return g;
}

/* "Crash Bandicoot PAL" / "Game [SLUS-20718]" / "My_Game (Europe)" -> tidy title */
static void tidy_title(char *dst, const char *src, size_t n)
{
    char tmp[128];
    size_t i, j = 0;
    int depth = 0;
    for (i = 0; src[i] && j < sizeof(tmp) - 1; i++) {
        char c = src[i];
        if (c == '[' || c == '(') { depth++; continue; }
        if (c == ']' || c == ')') { if (depth) depth--; continue; }
        if (depth) continue;
        if (c == '_') c = ' ';
        if (c == ' ' && j > 0 && tmp[j - 1] == ' ') continue;   /* collapse spaces */
        tmp[j++] = c;
    }
    tmp[j] = 0;
    while (j > 0 && tmp[j - 1] == ' ') tmp[--j] = 0;
    /* drop a trailing region word */
    if (j > 4 && (ends_with(tmp, " PAL") || ends_with(tmp, " NTSC"))) {
        tmp[j - (ends_with(tmp, " PAL") ? 4 : 5)] = 0;
    }
    strncpy(dst, tmp[0] == ' ' ? tmp + 1 : tmp, n - 1);
    dst[n - 1] = 0;
}

/* region from a game ID (SLES/BLUS/...) or from words in the file name */
static void detect_region(Game *g, const char *name)
{
    const char *id = g->id;
    char up[300]; size_t i;
    if (id[0]) {
        char r = id[2];                     /* 3rd letter: E = Europe, U = USA, J/P = Japan */
        if (id[0] == 'B' || id[0] == 'N') r = id[2];
        if (r == 'E') { strcpy(g->region, "PAL"); return; }
        if (r == 'U') { strcpy(g->region, "NTSC-U"); return; }
        if (r == 'J' || r == 'P') { strcpy(g->region, "NTSC-J"); return; }
    }
    for (i = 0; name[i] && i < sizeof(up) - 1; i++) up[i] = toupper((unsigned char) name[i]);
    up[i] = 0;
    if (strstr(up, "PAL") || strstr(up, "EUROPE") || strstr(up, "(E)") || strstr(up, "SLES") || strstr(up, "SCES")) strcpy(g->region, "PAL");
    else if (strstr(up, "NTSC-J") || strstr(up, "JAPAN") || strstr(up, "(J)") || strstr(up, "SLPS") || strstr(up, "SCPS")) strcpy(g->region, "NTSC-J");
    else if (strstr(up, "NTSC") || strstr(up, "USA") || strstr(up, "(U)") || strstr(up, "SLUS") || strstr(up, "SCUS")) strcpy(g->region, "NTSC-U");
}

/* look for "<base>.JPG/.jpg/.PNG/.png" */
static int find_image(char *out, size_t n, const char *base)
{
    static const char *ext[] = { ".JPG", ".jpg", ".PNG", ".png", ".jpeg", NULL };
    int i;
    for (i = 0; ext[i]; i++) {
        snprintf(out, n, "%s%s", base, ext[i]);
        if (file_exists(out)) return 1;
    }
    out[0] = 0;
    return 0;
}

/* tiny PARAM.SFO reader: TITLE and TITLE_ID */
void read_sfo(const char *path, char *title, size_t tn, char *id, size_t in)
{
    s32 fd; u64 got = 0;
    static u8 buf[8192];
    if (sysLv2FsOpen(path, SYS_O_RDONLY, &fd, 0, NULL, 0) != 0) return;
    sysLv2FsRead(fd, buf, sizeof(buf), &got);
    sysLv2FsClose(fd);
    if (got < 20 || memcmp(buf, "\0PSF", 4) != 0) return;

    u32 kt = buf[8] | buf[9] << 8 | buf[10] << 16 | buf[11] << 24;
    u32 dt = buf[12] | buf[13] << 8 | buf[14] << 16 | buf[15] << 24;
    u32 cnt = buf[16] | buf[17] << 8 | buf[18] << 16 | buf[19] << 24;
    u32 k;
    for (k = 0; k < cnt && 20 + k * 16 + 16 <= got; k++) {
        u8 *e = buf + 20 + k * 16;
        u32 ko = e[0] | e[1] << 8;
        u32 len = e[4] | e[5] << 8 | e[6] << 16 | e[7] << 24;
        u32 doff = e[12] | e[13] << 8 | e[14] << 16 | e[15] << 24;
        if (kt + ko >= got || dt + doff + len > got) continue;
        const char *key = (const char *) buf + kt + ko;
        const char *val = (const char *) buf + dt + doff;
        if (!strcmp(key, "TITLE") && title) { size_t m = len < tn ? len : tn - 1; memcpy(title, val, m); title[m] = 0; }
        if (!strcmp(key, "TITLE_ID") && id)  { size_t m = len < in ? len : in - 1; memcpy(id, val, m); id[m] = 0; }
    }
}

/* ---------------------------------------------------------------- scanners */
typedef void (*entry_fn)(const char *dir, const char *name, int isdir, void *ctx);

typedef struct { int plat; int depth; } IsoCtx_any;
static void list_dir(const char *dir, entry_fn fn, void *ctx)
{
    s32 fd;
    sysFSDirent ent;
    u64 read;
    char full[512];
    int n = 0;
    if (sysLv2FsOpenDir(dir, &fd) != 0) { if (!ctx || ((IsoCtx_any *) ctx)->depth == 0) log_step("scan %s: folder not found", dir); return; }
    while (sysLv2FsReadDir(fd, &ent, &read) == 0 && read > 0) {
        if (ent.d_name[0] == '.') continue;
        n++;
        snprintf(full, sizeof(full), "%s/%s", dir, ent.d_name);
        fn(dir, ent.d_name, is_dir(full), ctx);
    }
    sysLv2FsCloseDir(fd);
    if (!ctx || ((IsoCtx_any *) ctx)->depth == 0) log_step("scan %s: %d entries", dir, n);
}

/* PS3 folder games: /dev_hdd0/GAMES/<name>/PS3_GAME */
static void on_ps3_folder(const char *dir, const char *name, int isdir, void *ctx)
{
    char sfo[512], icon[512], base[512];
    if (!isdir) return;
    snprintf(sfo, sizeof(sfo), "%s/%s/PS3_GAME/PARAM.SFO", dir, name);
    if (!file_exists(sfo)) return;
    Game *g = add_game(PLAT_PS3);
    if (!g) return;
    g->format = FMT_FOLDER;
    snprintf(g->path, sizeof(g->path), "%s/%s", dir, name);
    read_sfo(sfo, g->title, sizeof(g->title), g->id, sizeof(g->id));
    if (!g->title[0]) tidy_title(g->title, name, sizeof(g->title));
    detect_region(g, name);
    if (g->id[0]) { snprintf(base, sizeof(base), MM_COVERS "/%s", g->id); find_image(g->cover, sizeof(g->cover), base); }
    if (!g->cover[0]) {
        snprintf(icon, sizeof(icon), "%s/%s/PS3_GAME/ICON0.PNG", dir, name);
        if (file_exists(icon)) strncpy(g->cover, icon, sizeof(g->cover) - 1);
    }
}

typedef struct { int plat; int depth; } IsoCtx;

static void on_iso(const char *dir, const char *name, int isdir, void *vctx)
{
    IsoCtx *ctx = (IsoCtx *) vctx;
    char base[512], stem[256];
    int fmt = -1;

    if (isdir) {   /* games often live in their own folder (PS2ISO/Game/Game.iso, PSXISO/Game/Game.bin) */
        if (ctx->depth == 0) {
            IsoCtx sub = { ctx->plat, 1 };
            char sd[512];
            snprintf(sd, sizeof(sd), "%s/%s", dir, name);
            list_dir(sd, on_iso, &sub);
        }
        return;
    }
    if (ends_with(name, ".iso") || ends_with(name, ".iso.0")) fmt = FMT_ISO;
    else if (ctx->plat == PLAT_PS2 && ends_with(name, ".bin.enc")) fmt = FMT_CLASSIC;
    else if (ctx->plat != PLAT_PS3 && (ends_with(name, ".bin") || ends_with(name, ".img") || ends_with(name, ".mdf"))) fmt = FMT_BINCUE;   /* webMAN mounts these for PS1 and PS2 (CD-based PS2 games) */
    if (fmt < 0) return;

    strncpy(stem, name, sizeof(stem) - 1); stem[sizeof(stem) - 1] = 0;
    char *dot = NULL;
    if (ends_with(stem, ".iso.0")) dot = stem + strlen(stem) - 6;
    else if (ends_with(stem, ".bin.enc")) dot = stem + strlen(stem) - 8;
    else dot = strrchr(stem, '.');
    if (dot) *dot = 0;

    Game *g = add_game(ctx->plat);
    if (!g) return;
    g->format = fmt;
    snprintf(g->path, sizeof(g->path), "%s/%s", dir, name);
    tidy_title(g->title, stem, sizeof(g->title));
    detect_region(g, g->path);
    snprintf(base, sizeof(base), "%s/%s", dir, stem);            /* cover next to the game (Cover Art Tool) */
    find_image(g->cover, sizeof(g->cover), base);
}

static int cmp_title(const void *a, const void *b)
{
    return strcasecmp(((const Game *) a)->title, ((const Game *) b)->title);
}

void library_scan(void)
{
    int p;
    IsoCtx c1 = { PLAT_PS1, 0 }, c2 = { PLAT_PS2, 0 }, c3 = { PLAT_PS3, 0 };
    for (p = 0; p < PLAT_COUNT; p++) library[p].count = 0;

    list_dir("/dev_hdd0/GAMES", on_ps3_folder, NULL);
    list_dir("/dev_hdd0/GAMEZ", on_ps3_folder, NULL);
    list_dir("/dev_hdd0/PS3ISO", on_iso, &c3);
    list_dir("/dev_hdd0/PS2ISO", on_iso, &c2);
    list_dir("/dev_hdd0/PSXISO", on_iso, &c1);

    for (p = 0; p < PLAT_COUNT; p++)
        if (library[p].count > 1) qsort(library[p].games, library[p].count, sizeof(Game), cmp_title);
}
