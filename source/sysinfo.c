/* sysinfo.c - free space via syscall 840 and temperatures via syscall 383 (as webMAN does),
   firmware from /dev_flash/vsh/etc/version.txt. Anything the console won't tell us is left out. */
#include <stdio.h>
#include <string.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "sysinfo.h"
#include "launch.h"
#include "log.h"

static SysInfo info;
static int frame = 0, temps_ok = 1, fw_done = 0;

static int disk_free(const char *path, u64 *total, u64 *avail)
{
    lv2syscall3(840, (u64)(u32) path, (u64)(u32) total, (u64)(u32) avail);
    return_to_user_prog(int);
}

static int get_temp(u32 sensor, u32 *t)
{
    lv2syscall2(383, sensor, (u64)(u32) t);
    return_to_user_prog(int);
}

static void read_firmware(void)
{
    char buf[128]; s32 fd; u64 got = 0;
    fw_done = 1;
    if (sysLv2FsOpen("/dev_flash/vsh/etc/version.txt", SYS_O_RDONLY, &fd, 0, NULL, 0) != 0) return;
    sysLv2FsRead(fd, buf, sizeof(buf) - 1, &got);
    sysLv2FsClose(fd);
    buf[got] = 0;
    char *p = strstr(buf, "release:");           /* "release:04.9100:" */
    if (!p) return;
    p += 8;
    int maj = (p[0] - '0') * 10 + (p[1] - '0'), mn = (p[3] - '0') * 10 + (p[4] - '0');
    snprintf(info.firmware, sizeof(info.firmware), "%d.%02d", maj, mn);
}

void sysinfo_poll(void)
{
    if (frame++ % 180) return;                  /* every ~3 seconds */
    u64 total = 0, avail = 0;
    u32 t;
    int u;
    sysFSStat st;
    char p[24];

    if (!fw_done) read_firmware();
    info.have_space = (disk_free("/dev_hdd0", &total, &avail) == 0 && total);
    if (info.have_space) { info.free_gb = avail / 1073741824.0; info.total_gb = total / 1073741824.0; }
    if (temps_ok) {
        if (get_temp(0, &t) == 0) info.cpu_c = t >> 24; else temps_ok = 0;
        if (temps_ok && get_temp(1, &t) == 0) info.rsx_c = t >> 24;
        if (!temps_ok) log_step("sysinfo: temperature not readable");
    }
    info.have_temp = temps_ok && info.cpu_c > 0 && info.cpu_c < 120;
    info.usb_drives = 0;
    for (u = 0; u < 8; u++) { snprintf(p, sizeof(p), "/dev_usb00%d", u); if (sysLv2FsStat(p, &st) == 0) info.usb_drives++; }
    info.webman = webman_alive();
}

const SysInfo *sysinfo_get(void) { return &info; }
