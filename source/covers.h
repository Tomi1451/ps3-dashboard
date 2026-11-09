/* covers.h - loads cover art into a small cache of GPU textures */
#ifndef COVERS_H
#define COVERS_H

#include "gfx.h"
#include "library.h"

void           covers_init(void);
void           covers_reset(void);
void           covers_frame(void);               /* call once per frame (limits loading per frame) */
const Texture *covers_get(const Game *g);        /* NULL while loading or if there is no cover */
const Texture *backdrop_get(const char *png_path);  /* a game background picture, NULL if none */

#endif
