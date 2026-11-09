/* sysinfo.c - free space via sysFsGetFreeSize, temperatures via lv2 syscall 383
   (the one webMAN uses). Anything the console won't tell us is just left out. */
#include <stdio.h>
#include <string.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>

#include "sysinfo.h"
#include "log.h"

static char text[96];
static int  frame = 0, temps_ok = 1;

static int disk_free(const char *path, u64 *total, u64 *avail)   /* syscall 840, as webMAN does */
{
    lv2syscall3(840, (u64)(u32) path, (u64)(u32) total, (u64)(u32) avail);
    return_to_user_prog(int);
}

static int get_temp(u32 sensor, u32 *t)
{
    lv2syscall2(383, sensor, (u64)(u32) t);
    return_to_user_prog(int);
}

void sysinfo_poll(void)
{
    if (frame++ % 300) return;                  /* every ~5 seconds */
    u64 total = 0, avail = 0, cpu = 0, rsx = 0;
    u32 t;
    int n = 0;
    text[0] = 0;
    if (disk_free("/dev_hdd0", &total, &avail) == 0 && total) {
        double gb = (double) avail / (1024.0 * 1024.0 * 1024.0);
        n += snprintf(text + n, sizeof(text) - n, "%.1f GB free", gb);
    }
    if (temps_ok) {
        if (get_temp(0, &t) == 0) cpu = t >> 24; else temps_ok = 0;
        if (temps_ok && get_temp(1, &t) == 0) rsx = t >> 24;
        if (!temps_ok) log_step("sysinfo: temperature not readable, hiding it");
    }
    if (temps_ok && cpu > 0 && cpu < 120)
        n += snprintf(text + n, sizeof(text) - n, "%sCPU %d\xB0" "C  RSX %d\xB0" "C", n ? "   \xB7   " : "", (int) cpu, (int) rsx);
}

const char *sysinfo_text(void) { return text; }
