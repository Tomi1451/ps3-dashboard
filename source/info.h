/* info.h - game details (developer, synopsis, ...) written to the HDD by the PC Game Info tool */
#ifndef INFO_H
#define INFO_H

#include "library.h"

#define INFO_DIR "/dev_hdd0/game/PSDB00001/USRDIR/info"

typedef struct {
    int  found;              /* 1 = an info file exists for this game */
    char id[24];
    char developer[96];
    char publisher[96];
    char release[32];        /* as given, e.g. 1999-11-05 */
    char genre[96];
    char players[16];
    char source[64];
    char synopsis[2400];
} GameInfo;

/* details for g (read from the HDD the first time, then cached for the last game asked) */
const GameInfo *info_get(const Game *g);
void info_forget(void);

#endif
