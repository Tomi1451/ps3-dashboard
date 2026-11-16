/* cobra.h - PS Dashboard's own game loader. Talks straight to Cobra (the part of the
   custom firmware / PS3HEN that can mount game images), so no other app is needed. */
#ifndef COBRA_H
#define COBRA_H

#include "library.h"

int  cobra_present(void);                 /* 1 if Cobra answers */
int  cobra_is_hen(void);                  /* 1 on PS3HEN */
const char *cobra_describe(void);         /* e.g. "Cobra 8.4 (CFW)" or "Not found" */

/* mount a game image or folder so it shows up as the disc in the drive.
   On failure returns 0 and puts a short reason in why. */
int  game_mount(const Game *g, const char *path, char *why, int why_len);
int  game_unmount(void);                  /* back to the real disc (or no disc) */

void disc_eject(void);                    /* opens the drive tray / ejects the disc */
void power_restart(void);
void power_off(void);

#endif
