/* launch.h - asks webMAN MOD (running in the background) to mount and start a game */
#ifndef LAUNCH_H
#define LAUNCH_H

#include "library.h"

/* returns 1 if webMAN accepted the request (the dashboard should then exit), 0 if webMAN is not reachable */
int launch_game(const Game *g);
int launch_path(const char *path);       /* same, for one disc of a multi-disc game */
int launch_disc(void);                    /* start the disc in the drive */
int unmount_disc(void);
int install_disc(const char *target);     /* copy the PS3 disc in the drive to <target> on the HDD */

#endif
