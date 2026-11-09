/* sysinfo.h - free HDD space and CPU / RSX temperatures for the top bar */
#ifndef SYSINFO_H
#define SYSINFO_H

void        sysinfo_poll(void);     /* call every frame; reads the console every few seconds */
const char *sysinfo_text(void);     /* e.g. "214.6 GB free  ·  CPU 61°C  RSX 54°C" */

#endif
