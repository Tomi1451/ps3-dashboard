/* direct.h - start games straight from the dashboard, without going back to the XMB */
#ifndef DIRECT_H
#define DIRECT_H

#include "library.h"

int  ps1_cards(char names[][64], int max);      /* .VM1 memory cards in /dev_hdd0/savedata/vmc */
const char *ps1_card_in_use(void);              /* the one direct start will use ("" = none found) */
int  direct_start(const Game *g, const char *path, char *why, int why_len);  /* only returns if it couldn't start */
int  direct_start_ps3_disc(const char *id, char *why, int why_len);         /* the PS3 disc in the drive */

#endif
