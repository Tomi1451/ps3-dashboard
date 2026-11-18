/* delete.h - delete a game (and only its own files) from the HDD or a USB drive */
#ifndef DELETE_H
#define DELETE_H

#include <ppu-types.h>
#include "library.h"

int del_plan(const Game *g, u64 *bytes, int *files);   /* works out what will go; returns how many items */
int del_run(const Game *g, void (*progress)(int files_done));   /* 1 = everything deleted */

#endif
