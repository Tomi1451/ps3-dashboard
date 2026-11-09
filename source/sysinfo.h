/* sysinfo.h - console details for the System section of Settings */
#ifndef SYSINFO_H
#define SYSINFO_H

typedef struct {
    int    have_space;  double free_gb, total_gb;
    int    have_temp;   int cpu_c, rsx_c;
    char   firmware[16];         /* "4.91" or "" */
    int    usb_drives;
    int    webman;               /* 1 running, 0 not answering */
} SysInfo;

void           sysinfo_poll(void);        /* call every frame while the info is on screen: refreshes every few seconds */
const SysInfo *sysinfo_get(void);

#endif
