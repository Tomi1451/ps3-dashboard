/* lists.h - favourites and recently played, kept as plain path lists on the HDD */
#ifndef LISTS_H
#define LISTS_H

#include "library.h"

#define RECENT_MAX 12

void  lists_load(void);                 /* after library_scan: marks favourites */
void  fav_toggle(Game *g);
int   fav_list(Game **out, int max);    /* favourites still in the library, A-Z order of the library */
void  recent_add(const Game *g);        /* saves straight away (called just before a game starts) */
int   recent_list(Game **out, int max); /* most recent first, only games still in the library */

#endif
