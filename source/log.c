#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <lv2/sysfs.h>
#include <sys/file.h>
#include "log.h"

#define LOG_PATH "/dev_hdd0/tmp/ps_dashboard.log"

static void write_line(const char *s, int trunc)
{
    s32 fd; u64 done;
    s32 flags = SYS_O_WRONLY | SYS_O_CREAT | (trunc ? SYS_O_TRUNC : SYS_O_APPEND);
    if (sysLv2FsOpen(LOG_PATH, flags, &fd, 0666, NULL, 0) != 0) return;
    sysLv2FsWrite(fd, s, strlen(s), &done);
    sysLv2FsFsync(fd);              /* make sure it is on disk even if we freeze next */
    sysLv2FsClose(fd);
}

void log_reset(void) { write_line("PS Dashboard log\n", 1); }

void log_step(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    write_line(buf, 0);
}
