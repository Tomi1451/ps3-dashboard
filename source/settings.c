/* settings.c - tiny "key=value" text file in /dev_hdd0/game/PSDB00001/USRDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lv2/sysfs.h>
#include <sys/file.h>

#include "settings.h"
#include "gfx.h"
#include "log.h"

#define SETTINGS_FILE "/dev_hdd0/game/PSDB00001/USRDIR/settings.txt"

Settings settings = { 0, 2, 1 };

void settings_apply(void)
{
    gfx_theme_set(settings.theme, settings.brightness, settings.wave);
}

static int val(const char *buf, const char *key, int def)
{
    const char *p = strstr(buf, key);
    return p ? atoi(p + strlen(key)) : def;
}

void settings_load(void)
{
    char buf[256];
    s32 fd; u64 got = 0;
    if (sysLv2FsOpen(SETTINGS_FILE, SYS_O_RDONLY, &fd, 0, NULL, 0) == 0) {
        sysLv2FsRead(fd, buf, sizeof(buf) - 1, &got);
        sysLv2FsClose(fd);
        buf[got] = 0;
        settings.theme      = val(buf, "theme=", 0);
        settings.brightness = val(buf, "brightness=", 2);
        settings.wave       = val(buf, "wave=", 1);
        if (settings.theme < 0 || settings.theme >= gfx_theme_count()) settings.theme = 0;
        if (settings.brightness < 0 || settings.brightness > 4) settings.brightness = 2;
        settings.wave = settings.wave ? 1 : 0;
        log_step("settings: theme %d, brightness %d, wave %d", settings.theme, settings.brightness, settings.wave);
    }
    settings_apply();
}

void settings_save(void)
{
    char buf[128];
    s32 fd; u64 wrote = 0;
    int n = snprintf(buf, sizeof(buf), "theme=%d\nbrightness=%d\nwave=%d\n", settings.theme, settings.brightness, settings.wave);
    if (sysLv2FsOpen(SETTINGS_FILE, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666, NULL, 0) != 0) {
        log_step("settings: could not save to " SETTINGS_FILE);
        return;
    }
    sysLv2FsWrite(fd, buf, n, &wrote);
    sysLv2FsClose(fd);
    sysLv2FsChmod(SETTINGS_FILE, 0666);
}
