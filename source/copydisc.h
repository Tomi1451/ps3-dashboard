/* copydisc.h - install the PS3 disc in the drive to /dev_hdd0/GAMES, inside the dashboard */
#ifndef COPYDISC_H
#define COPYDISC_H

#include <ppu-types.h>

int   cd_begin(const char *title, const char *id);   /* 0 = failed, see cd_message() */
int   cd_step(void);
void  cd_cancel(void);
int   cd_active(void);
int   cd_done(void);
int   cd_failed(void);
float cd_progress(void);
u64   cd_bytes_done(void);
u64   cd_bytes_total(void);
const char *cd_message(void);
const char *cd_output(void);

#endif
