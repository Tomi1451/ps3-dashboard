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

const Settings settings_defaults = { 0, 2, 2, 0, 1, 1, 0, 0, 0, 1 };
Settings settings = { 0, 2, 2, 0, 1, 1, 0, 0, 0, 1 };

static const struct { const char *key; int *v; int lo, hi; } keys[] = {
    { "theme=",      &settings.theme,      0, 99 },
    { "brightness=", &settings.brightness, 0, 4 },
    { "wave=",       &settings.wave,       0, 2 },
    { "font=",       &settings.font,       0, 99 },
    { "cover_size=", &settings.cover_size, 0, 2 },
    { "titles=",     &settings.titles,     0, 1 },
    { "sort=",       &settings.sort,       0, 1 },
    { "open_on=",    &settings.open_on,    0, 3 },
    { "last_plat=",  &settings.last_plat,  0, 2 },
    { "disc_popup=", &settings.disc_popup, 0, 1 },
};
#define NKEYS ((int) (sizeof(keys) / sizeof(keys[0])))

void settings_apply(void)
{
    gfx_theme_set(settings.theme, settings.brightness, settings.wave);
    settings.font = gfx_font_set(settings.font);
}

void settings_load(void)
{
    char buf[512];
    s32 fd; u64 got = 0;
    int i;
    if (sysLv2FsOpen(SETTINGS_FILE, SYS_O_RDONLY, &fd, 0, NULL, 0) == 0) {
        sysLv2FsRead(fd, buf, sizeof(buf) - 1, &got);
        sysLv2FsClose(fd);
        buf[got] = 0;
        for (i = 0; i < NKEYS; i++) {
            const char *p = strstr(buf, keys[i].key);
            if (!p) continue;
            int v = atoi(p + strlen(keys[i].key));
            if (v >= keys[i].lo && v <= keys[i].hi) *keys[i].v = v;
        }
        log_step("settings loaded");
    }
    if (settings.theme >= gfx_theme_count()) settings.theme = 0;
    if (settings.font >= gfx_font_count()) settings.font = 0;
    gfx_font_choice = settings.font;
}

void settings_save(void)
{
    char buf[512];
    s32 fd; u64 wrote = 0;
    int i, n = 0;
    for (i = 0; i < NKEYS; i++) n += snprintf(buf + n, sizeof(buf) - n, "%s%d\n", keys[i].key, *keys[i].v);
    if (sysLv2FsOpen(SETTINGS_FILE, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, 0666, NULL, 0) != 0) {
        log_step("settings: could not save to " SETTINGS_FILE);
        return;
    }
    sysLv2FsWrite(fd, buf, n, &wrote);
    sysLv2FsClose(fd);
    sysLv2FsChmod(SETTINGS_FILE, 0666);
}
