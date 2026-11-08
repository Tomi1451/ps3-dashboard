/* library.h - finds games on the internal HDD */
#ifndef LIBRARY_H
#define LIBRARY_H

#include <ppu-types.h>
#include <stddef.h>

enum { PLAT_PS1 = 0, PLAT_PS2 = 1, PLAT_PS3 = 2, PLAT_COUNT = 3 };
enum { FMT_FOLDER = 0, FMT_ISO = 1, FMT_BINCUE = 2, FMT_CLASSIC = 3 };

typedef struct {
    char title[96];
    char id[16];          /* PS3 TITLE_ID when known */
    char path[400];       /* what webMAN mounts */
    char cover[400];      /* best cover image found ("" = none) */
    char region[12];      /* "PAL", "NTSC-U", "NTSC-J" or "" */
    u8   plat;
    u8   format;
} Game;

typedef struct {
    Game *games;
    int   count;
    int   cap;
} GameList;

extern GameList library[PLAT_COUNT];

void        library_scan(void);
const char *plat_name(int plat);        /* "PlayStation", "PlayStation 2", ... */
const char *plat_short(int plat);       /* "PS1", "PS2", "PS3" */
const char *format_name(int fmt);
void        read_sfo(const char *path, char *title, size_t tn, char *id, size_t in);   /* PARAM.SFO TITLE / TITLE_ID */

#endif
