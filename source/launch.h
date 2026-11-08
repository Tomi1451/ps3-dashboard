/* launch.h - asks webMAN MOD (running in the background) to mount and start a game */
#ifndef LAUNCH_H
#define LAUNCH_H

#include "library.h"

/* returns 1 if webMAN accepted the request (the dashboard should then exit), 0 if webMAN is not reachable */
int launch_game(const Game *g);

#endif
