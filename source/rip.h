/* rip.h - makes an ISO / BIN+CUE from the PS1 or PS2 disc in the drive */
#ifndef RIP_H
#define RIP_H

#include <ppu-types.h>

enum { RIP_PS1 = 1, RIP_PS2 = 2 };

int   rip_begin(int type, const char *name, const char *id);   /* 0 = failed, see rip_message() */
int   rip_step(void);          /* call every frame while active: reads and writes a few sectors */
void  rip_cancel(void);        /* stops and deletes the unfinished file */
int   rip_active(void);
int   rip_done(void);
int   rip_failed(void);
float rip_progress(void);      /* 0..1 */
u64   rip_bytes_done(void);
u64   rip_bytes_total(void);
const char *rip_message(void); /* game name while working, reason when failed */
const char *rip_output(void);  /* file written */

#endif
