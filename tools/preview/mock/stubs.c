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
    g->plat = p; g->format = fmt; strcpy(g->where, "HDD");
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
    { static const char *ps2[] = { "Burnout 3", "Def Jam Fight for NY", "GTA San Andreas", "Jak and Daxter", "Kingdom Hearts", "Okami", "Ratchet and Clank", "Shadow of the Colossus", "SSX Tricky", "TimeSplitters 2", NULL };
      for (i = 0; ps2[i]; i++) { snprintf(path, sizeof(path), "/dev_hdd0/PS2ISO/%s.iso", ps2[i]); snprintf(cov, sizeof(cov), (i % 4) ? "cover_ps2_%d" : "", i + 2); add(PLAT_PS2, ps2[i], "", path, FMT_ISO, cov); } }
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
int launch_path(const char *p) { return 1; }
int unmount_disc(void) { return 1; }
const Texture *backdrop_get(const char *p) { return NULL; }
Game *library_find(const char *path) { int p, i; for (p = 0; p < PLAT_COUNT; p++) for (i = 0; i < library[p].count; i++) if (!strcmp(library[p].games[i].path, path)) return &library[p].games[i]; return NULL; }
#include "../../../source/lists.h"
#include "../../../source/sysinfo.h"
#include "../../../source/sound.h"
void lists_load(void) { if (getenv("PREVIEW_FAVS")) { library[0].games[2].fav = library[0].games[5].fav = library[1].games[3].fav = 1; library[0].games[3].ndiscs = 2; strcpy(library[0].games[3].discs[0], "/dev_hdd0/PSXISO/A Bugs Life Disc 2.bin"); } }
void fav_toggle(Game *g) { g->fav = !g->fav; }
int fav_list(Game **o, int m) { int p, i, n = 0; for (p = 0; p < PLAT_COUNT; p++) for (i = 0; i < library[p].count && n < m; i++) if (library[p].games[i].fav) o[n++] = &library[p].games[i]; return n; }
void recent_add(const Game *g) {}
int recent_list(Game **o, int m) { if (!getenv("PREVIEW_FAVS")) return 0; o[0] = &library[2].games[0]; o[1] = &library[0].games[8]; o[2] = &library[1].games[1]; o[3] = &library[0].games[12]; return 4; }
void sysinfo_poll(void) {}
const char *sysinfo_text(void) { return "214.6 GB free   \xB7   CPU 61\xB0" "C  RSX 54\xB0" "C"; }
void sound_enable(int on) {} void sound_play(int w) {} void sound_shutdown(void) {}
void log_reset(void) {}
void log_step(const char *fmt, ...) {}
#include "../../../source/disc.h"
/* PREVIEW_DISC=1/2/3 pretends a PS1/PS2/PS3 disc goes in on frame 3 */
int launch_disc(void) { return 1; }
int install_disc(const char *t) { return 1; }
const char *disc_name(int type) { return type == DISC_PS1 ? "PlayStation disc" : type == DISC_PS2 ? "PlayStation 2 disc" : "PlayStation 3 disc"; }
int disc_poll(DiscInfo *d)
{
    static int f = 0; const char *e = getenv("PREVIEW_DISC");
    if (!e || ++f != 3) return 0;
    memset(d, 0, sizeof(*d)); d->type = atoi(e);
    if (d->type == DISC_PS3) { strcpy(d->title, "Gran Turismo 5"); strcpy(d->id, "BCES00569"); }
    else strcpy(d->id, d->type == DISC_PS1 ? "SCES-00311" : "SLES-50330");
    return 1;
}
#include "../../../source/settings.h"
#include "../../../source/gfx.h"
const Settings settings_defaults = { 0, 2, 2, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, "" };
Settings settings = { 0, 2, 2, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, "" };
void settings_apply(void) { gfx_theme_set(settings.theme, settings.brightness, settings.wave); settings.font = gfx_font_set(settings.font); }
void settings_load(void) { const char *t = getenv("PREVIEW_THEME"); if (t) settings.theme = atoi(t); t = getenv("PREVIEW_SIZE"); if (t) settings.cover_size = atoi(t); t = getenv("PREVIEW_VIEW"); if (t) settings.view = atoi(t); }
void settings_save(void) {}
void covers_reset(void) { ntex = 0; }
#include "../../../source/info.h"
/* sample info for the first PS1 game so the Game info page can be previewed */
const GameInfo *info_get(const Game *g)
{
    static GameInfo gi; memset(&gi, 0, sizeof(gi));
    if (getenv("PREVIEW_NOINFO")) return &gi;
    gi.found = 1; strcpy(gi.id, "SLES-12345"); strcpy(gi.developer, "Sample Developer Studios"); strcpy(gi.publisher, "Sample Publisher");
    strcpy(gi.release, "2000-11-24"); strcpy(gi.genre, "Racing; Action"); strcpy(gi.players, "1-2"); strcpy(gi.source, "LaunchBox Games Database");
    for (int i = 0; i < 9; i++) strcat(gi.synopsis, "This is placeholder preview text standing in for a real synopsis, long enough to wrap over several lines. ");
    return &gi;
}
void info_forget(void) {}
#include "../../../source/rip.h"
static int rip_state = 0; static float rip_p = 0;
int rip_begin(int t, const char *n, const char *id) { rip_state = 1; rip_p = 0.37f; return 1; }
int rip_step(void) { return 1; }
void rip_cancel(void) { rip_state = 3; }
int rip_active(void) { return rip_state == 1; }
int rip_done(void) { return rip_state == 2; }
int rip_failed(void) { return rip_state == 3; }
float rip_progress(void) { return rip_p; }
u64 rip_bytes_done(void) { return 271ULL << 20; }
u64 rip_bytes_total(void) { return 734ULL << 20; }
const char *rip_message(void) { return "Medievil [SCES-00311]"; }
const char *rip_output(void) { return "/dev_hdd0/PSXISO/Medievil [SCES-00311]/Medievil [SCES-00311].bin"; }
int disc_image_mounted(void) { return 0; }
void disc_read_id(DiscInfo *d) {}
#include "../../../source/direct.h"
int ps1_cards(char names[][64], int max) { strcpy(names[0], "Main_Card.VM1"); strcpy(names[1], "Second.VM1"); return 2; }
const char *ps1_card_in_use(void) { return settings.ps1_card[0] ? settings.ps1_card : "Main_Card.VM1"; }
int direct_start(const Game *g, const char *p) { return 0; }
int mount_only(const char *p) { return 1; }
