/* settings.h - user settings, kept in the app's own folder on the HDD */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int theme;        /* background colour (gfx theme index) */
    int brightness;   /* 0..4, 2 = normal */
    int wave;         /* background effects: 0 off, 1 wave, 2 wave + floating circles */
    int font;         /* gfx font index */
    int cover_size;   /* 0 small (8 a row), 1 medium (7), 2 large (5) */
    int titles;       /* 1 = names under the covers */
    int sort;         /* 0 A-Z, 1 Z-A */
    int open_on;      /* 0 last console used, 1 PS1, 2 PS2, 3 PS3 */
    int last_plat;    /* remembered for open_on = 0 */
    int disc_popup;   /* 1 = ask what to do when a disc goes in */
    int view;         /* 0 grid, 1 list, 2 large artwork */
    int sounds;       /* 1 = navigation sounds */
    int dim;          /* dim when idle: 0 off, 1 5 min, 2 15 min, 3 30 min */
    int direct_ps1;   /* 1 = start PS1 games straight from the dashboard (no XMB in between) */
    char ps1_card[64];/* memory card for slot 1 (file in /dev_hdd0/savedata/vmc), "" = automatic */
} Settings;

extern Settings settings;
extern const Settings settings_defaults;

void settings_load(void);    /* reads the file only (call before gfx_init) */
void settings_save(void);
void settings_apply(void);   /* colours + font */

#endif
