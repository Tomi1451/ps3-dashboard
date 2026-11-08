/* PC stand-ins for the PS3-only modules: sample library, covers, launch, log */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../../source/library.h"
#include "../../../source/covers.h"
#include "../../../source/launch.h"
#include "../../../source/log.h"

GameList library[PLAT_COUNT];
const char *plat_name(int p)  { return p == PLAT_PS1 ? "PlayStation" : p == PLAT_PS2 ? "PlayStation 2" : "PlayStation 3"; }
const char *plat_short(int p) { return p == PLAT_PS1 ? "PS1" : p == PLAT_PS2 ? "PS2" : "PS3"; }
const char *format_name(int f) { return f == FMT_FOLDER ? "Folder (JB)" : f == FMT_ISO ? "ISO" : f == FMT_BINCUE ? "BIN + CUE" : "PS2 Classic"; }

static void add(int p, const char *t, const char *id, const char *path, int fmt, const char *cover)
{
    GameList *l = &library[p];
    if (l->count == l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->games = realloc(l->games, l->cap * sizeof(Game)); }
    Game *g = &l->games[l->count++]; memset(g, 0, sizeof(*g));
    g->plat = p; g->format = fmt;
    strcpy(g->title, t); strcpy(g->id, id); strcpy(g->region, strstr(path, "PAL") ? "PAL" : (id[0] ? "PAL" : "")); strcpy(g->path, path); strcpy(g->cover, cover ? cover : "");
}
void library_scan(void)
{
    static const char *ps1[] = { "007 Racing", "007 The World Is Not Enough", "007 Tomorrow Never Dies", "A Bugs Life",
        "Bomberman World", "Cardinal Syn", "Chicken Run", "Circuit Breakers", "Crash Bandicoot", "Crash Bandicoot 2 Cortex Strikes Back",
        "Crash Bandicoot 3 Warped", "Crash Bash", "Crash Team Racing", "DEADBALLZONE", "Driver", "DRIVER2", "DRIVER2",
        "Everybodys Golf 2", "FIFA 99", "Future Cop LAPD", "Gran Turismo", "Harry Potter And The Chamber Of Secrets",
        "Harry Potter And The Philosophers Stone", "Hercules", "Kula World", "KURUSHI", "MEDIEVIL", NULL };
    char path[400], cov[400]; int i;
    for (i = 0; ps1[i]; i++) {
        snprintf(path, sizeof(path), "/dev_hdd0/PSXISO/%s/%s PAL.BIN", ps1[i], ps1[i]);
        snprintf(cov, sizeof(cov), (i % 3) ? "cover_ps1_%d" : "", i);
        add(PLAT_PS1, ps1[i], "", path, FMT_BINCUE, cov);
    }
    add(PLAT_PS2, "BMX XXX", "", "/dev_hdd0/PS2ISO/BMX XXX.ISO", FMT_ISO, "");
    add(PLAT_PS2, "WARRIORS", "", "/dev_hdd0/PS2ISO/WARRIORS.ISO", FMT_ISO, "cover_ps2_1");
    add(PLAT_PS3, "Uncharted 2: Among Thieves", "BCES00757", "/dev_hdd0/GAMES/Uncharted 2", FMT_FOLDER, "cover_ps3_0");
    add(PLAT_PS3, "Some PS3 Game", "BLES00932", "/dev_hdd0/PS3ISO/Some PS3 Game.iso", FMT_ISO, "");
}
/* covers: "cover_<plat>_<n>" names become fake textures the renderer draws as tinted images */
static Texture texs[256]; static int ntex = 0;
void covers_init(void) {}
void covers_frame(void) {}
const Texture *covers_get(const Game *g)
{
    if (!g->cover[0]) return NULL;
    int i;
    for (i = 0; i < ntex; i++) if (texs[i].offset == (u32) (100000 + (g - library[g->plat].games) + g->plat * 1000)) return &texs[i];
    Texture *t = &texs[ntex++];
    t->offset = 100000 + (g - library[g->plat].games) + g->plat * 1000;
    if (g->plat == PLAT_PS1) { t->w = t->h = 500; } else if (g->plat == PLAT_PS2) { t->w = 512; t->h = 736; } else { t->w = 260; t->h = 300; }
    t->tex_w = (t->w + 15) & ~15; t->tex_h = t->h;
    return t;
}
int launch_game(const Game *g) { return 1; }
void log_reset(void) {}
void log_step(const char *fmt, ...) {}
