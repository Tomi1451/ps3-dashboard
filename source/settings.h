/* settings.h - user settings, kept in the app's own folder on the HDD */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int theme;        /* background colour (gfx theme index) */
    int brightness;   /* 0..4, 2 = normal */
    int wave;         /* 1 = animated wave on */
} Settings;

extern Settings settings;

void settings_load(void);   /* also applies them */
void settings_save(void);
void settings_apply(void);

#endif
