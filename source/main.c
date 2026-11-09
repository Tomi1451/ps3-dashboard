/*
 * PS Dashboard - a PlayStation library for the PS3
 * Stage 1: PS1 / PS2 / PS3 cover grids, game info panel, launching through webMAN MOD.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <io/pad.h>
#include <tiny3d.h>
#include <libfont.h>

#include "gfx.h"
#include "library.h"
#include "covers.h"
#include "launch.h"
#include "log.h"
#include "disc.h"
#include "settings.h"

#define APP_VERSION "v0.15"

/* ---------------------------------------------------------------- input */
enum {
    B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_CROSS = 16, B_CIRCLE = 32,
    B_TRIANGLE = 64, B_SQUARE = 128, B_L1 = 256, B_R1 = 512, B_START = 1024, B_SELECT = 2048
};

static u32 pad_held = 0, pad_pressed = 0;

static void read_pads(void)
{
    static u32 prev = 0;
    static int repeat_timer = 0;
    padInfo info;
    padData d;
    u32 now = 0;
    int i;

    ioPadGetInfo(&info);
    for (i = 0; i < MAX_PADS; i++) {
        if (!info.status[i]) continue;
        ioPadGetData(i, &d);
        if (d.len == 0) { now = prev; break; }   /* no change reported this frame */
        if (d.BTN_UP)       now |= B_UP;
        if (d.BTN_DOWN)     now |= B_DOWN;
        if (d.BTN_LEFT)     now |= B_LEFT;
        if (d.BTN_RIGHT)    now |= B_RIGHT;
        if (d.BTN_CROSS)    now |= B_CROSS;
        if (d.BTN_CIRCLE)   now |= B_CIRCLE;
        if (d.BTN_TRIANGLE) now |= B_TRIANGLE;
        if (d.BTN_SQUARE)   now |= B_SQUARE;
        if (d.BTN_L1)       now |= B_L1;
        if (d.BTN_R1)       now |= B_R1;
        if (d.BTN_START)    now |= B_START;
        if (d.BTN_SELECT)   now |= B_SELECT;
        break;
    }
    pad_pressed = now & ~prev;

    /* D-pad auto-repeat when held */
    u32 dirs = now & (B_UP | B_DOWN | B_LEFT | B_RIGHT);
    if (dirs && (dirs == (prev & (B_UP | B_DOWN | B_LEFT | B_RIGHT)))) {
        if (++repeat_timer > 18 && (repeat_timer % 5) == 0) pad_pressed |= dirs;
    } else repeat_timer = 0;

    pad_held = now;
    prev = now;
}

/* ---------------------------------------------------------------- button glyphs */
static void circle_fill(float cx, float cy, float r, u32 c)
{
    int i;
    tiny3d_SetPolygon(TINY3D_QUADS);
    for (i = 0; i < 24; i++) {
        float a0 = i * 6.2831853f / 24.0f, a1 = (i + 1) * 6.2831853f / 24.0f;
        tiny3d_VertexPos(cx, cy, 1000);                               tiny3d_VertexColor(c);
        tiny3d_VertexPos(cx + r * cosf(a0), cy + r * sinf(a0), 1000); tiny3d_VertexColor(c);
        tiny3d_VertexPos(cx + r * cosf(a1), cy + r * sinf(a1), 1000); tiny3d_VertexColor(c);
        tiny3d_VertexPos(cx, cy, 1000);                               tiny3d_VertexColor(c);
    }
    tiny3d_End();
}

static void ring(float cx, float cy, float r, u32 c)
{
    int i;
    const float t = 2.0f;
    tiny3d_SetPolygon(TINY3D_QUADS);
    for (i = 0; i < 24; i++) {
        float a0 = i * 6.2831853f / 24.0f, a1 = (i + 1) * 6.2831853f / 24.0f;
        tiny3d_VertexPos(cx + r * cosf(a0), cy + r * sinf(a0), 1000);             tiny3d_VertexColor(c);
        tiny3d_VertexPos(cx + r * cosf(a1), cy + r * sinf(a1), 1000);             tiny3d_VertexColor(c);
        tiny3d_VertexPos(cx + (r - t) * cosf(a1), cy + (r - t) * sinf(a1), 1000); tiny3d_VertexColor(c);
        tiny3d_VertexPos(cx + (r - t) * cosf(a0), cy + (r - t) * sinf(a0), 1000); tiny3d_VertexColor(c);
    }
    tiny3d_End();
}

static void thick_line(float x0, float y0, float x1, float y1, float t, u32 c)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.01f) return;
    float nx = -dy / l * t / 2, ny = dx / l * t / 2;
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x0 + nx, y0 + ny, 1000); tiny3d_VertexColor(c);
    tiny3d_VertexPos(x1 + nx, y1 + ny, 1000); tiny3d_VertexColor(c);
    tiny3d_VertexPos(x1 - nx, y1 - ny, 1000); tiny3d_VertexColor(c);
    tiny3d_VertexPos(x0 - nx, y0 - ny, 1000); tiny3d_VertexColor(c);
    tiny3d_End();
}

/* kind: 'x' cross, 'o' circle, 't' triangle, 's' square */
static void button_glyph(char kind, float cx, float cy)
{
    const float r = 17.0f;
    circle_fill(cx, cy, r, 0x00000070);
    ring(cx, cy, r, 0xffffff60);
    switch (kind) {
        case 'x': thick_line(cx - 7, cy - 7, cx + 7, cy + 7, 3, 0x8fb4ffff); thick_line(cx + 7, cy - 7, cx - 7, cy + 7, 3, 0x8fb4ffff); break;
        case 'o': ring(cx, cy, 8.0f, 0xff7a86ff); ring(cx, cy, 6.6f, 0xff7a86ff); break;
        case 't': thick_line(cx, cy - 8, cx + 8, cy + 6, 2.6f, 0x4fdcb0ff); thick_line(cx + 8, cy + 6, cx - 8, cy + 6, 2.6f, 0x4fdcb0ff);
                  thick_line(cx - 8, cy + 6, cx, cy - 8, 2.6f, 0x4fdcb0ff); break;
        default:  gfx_frame(cx - 6, cy - 6, 12, 12, 2.6f, 0xf39ad6ff); break;
    }
}

static float hint(float x, float y, char kind, const char *label)
{
    button_glyph(kind, x + 17, y + 17);
    return gfx_text(x + 44, y + 4, 24, 0xffffffd0, label) + 40;
}

static float hint_pill(float x, float y, const char *pill, const char *label)
{
    float w = gfx_text_w(17, pill) + 18;
    gfx_rect(x, y + 4, w, 28, 0x00000060);
    gfx_frame(x, y + 4, w, 28, 1, 0xffffff60);
    gfx_text(x + 9, y + 10, 17, 0xffffffff, pill);
    return gfx_text(x + w + 12, y + 4, 24, 0xffffffd0, label) + 40;
}

/* ---------------------------------------------------------------- layout */
#define MARGIN_X   96.0f
#define GRID_Y     330.0f
/* cover size setting: small / medium / large */
static const int   size_cols[3]  = { 8, 7, 5 };
static const float size_cover[3] = { 178.0f, 204.0f, 250.0f };
static const float size_row[3]   = { 266.0f, 292.0f, 330.0f };
#define COLS       (size_cols[settings.cover_size])
#define CELL_W     ((SCREEN_W - 2 * MARGIN_X) / COLS)
#define COVER_H    (size_cover[settings.cover_size])
#define ROW_H      (size_row[settings.cover_size])
#define VIS_ROWS   3
#define FOOTER_Y   948.0f

static const float plat_aspect[PLAT_COUNT] = { 1.0f, 0.70f, 0.86f };   /* PS1 jewel, PS2 DVD, PS3 Blu-ray */
static const u32   plat_hue[PLAT_COUNT][2] = { { 0x545862ff, 0x1c1d22ff }, { 0x1d3fb8ff, 0x070d33ff }, { 0x46566eff, 0x10161fff } };

static int plat = PLAT_PS1;
static int show_settings = 0, set_row = 0, set_top = 0;   /* Settings window (SELECT) */
static int sel[PLAT_COUNT];
static int top_row[PLAT_COUNT];
static int mode = 0;            /* 0 grid, 1 info panel, 2 webMAN error, 3 disc inserted */
static int prev_mode = 0, disc_choice = 0;
static DiscInfo disc;
static float anim = 0.0f;       /* selection pulse */

static void draw_placeholder(const Game *g, float x, float y, float w, float h)
{
    gfx_rect_v(x, y, w, h, plat_hue[g->plat][0], plat_hue[g->plat][1]);
    gfx_text_wrap(x + 10, y + h * 0.45f, w > 160 ? 22 : 18, 0xffffffff, g->title, w - 20, 3);
}

/* draws a cover (or placeholder) sitting on its shelf line at the bottom of the box */
static void draw_cover(const Game *g, float bx, float by, float bw, float bh, int selected)
{
    const Texture *t = covers_get(g);
    float x = bx, y = by, w = bw, h = bh;

    if (t) {   /* fit the real image, no stretching */
        float s = fminf(bw / t->w, bh / t->h);
        w = t->w * s; h = t->h * s;
        x = bx + (bw - w) / 2; y = by + (bh - h);
    }
    if (selected) {   /* lift + glow */
        float k = 0.06f;
        x -= w * k / 2; y -= h * k; w *= 1 + k; h *= 1 + k;
        float p = 0.5f + 0.5f * sinf(anim);
        int i;
        for (i = 6; i >= 1; i--)
            gfx_frame(x, y, w, h, i * 5.0f, gfx_accent((int) ((10 + 10 * p) * (7 - i) / 3)));
    }
    gfx_rect(x + 6, y + 10, w, h, 0x00000070);      /* drop shadow */
    if (t) gfx_image(t, x, y, w, h, 0xffffffff);
    else   draw_placeholder(g, x, y, w, h);
    if (selected) gfx_frame(x, y, w, h, 3, 0xffffffff);
    else          gfx_frame(x, y, w, h, 1, 0xffffff26);
}

static void draw_nav(void)
{
    static const char *labels[PLAT_COUNT] = { "PS1", "PS2", "PS3" };
    float x = MARGIN_X;
    int p, total = 0;
    for (p = 0; p < PLAT_COUNT; p++) {
        int on = (p == plat);
        float size = on ? 46 : 32;
        float w = gfx_text_w(size, labels[p]);
        gfx_text(x, on ? 52 : 64, size, on ? 0xffffffff : 0xffffff90, labels[p]);
        if (on) gfx_rect(x, 112, w, 4, gfx_accent(0xff));
        x += w + 56;
        total += library[p].count;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%d games in your library", total);
    gfx_text(SCREEN_W - MARGIN_X - gfx_text_w(22, buf), 70, 22, 0xffffffa0, buf);
}

static void draw_grid(void)
{
    GameList *l = &library[plat];
    char buf[160];
    int i;

    snprintf(buf, sizeof(buf), "%s", plat_name(plat));
    float x = gfx_text(MARGIN_X, 168, 52, 0xffffffff, buf);
    snprintf(buf, sizeof(buf), "  \xB7  %d game%s", l->count, l->count == 1 ? "" : "s");
    gfx_text(x, 184, 34, 0xffffff90, buf);

    if (l->count == 0) {
        static const char *where[PLAT_COUNT] = { "/dev_hdd0/PSXISO", "/dev_hdd0/PS2ISO", "/dev_hdd0/GAMES or /dev_hdd0/PS3ISO" };
        gfx_text_center(SCREEN_W / 2, 520, 34, 0xffffffe0, "No games found here yet");
        snprintf(buf, sizeof(buf), "Put your %s games in %s", plat_short(plat), where[plat]);
        gfx_text_center(SCREEN_W / 2, 572, 24, 0xffffffa0, buf);
        return;
    }

    /* keep the selected row visible */
    int srow = sel[plat] / COLS;
    if (srow < top_row[plat]) top_row[plat] = srow;
    if (srow > top_row[plat] + 1) top_row[plat] = srow - 1;

    const Game *cur = &l->games[sel[plat]];
    {
        char pos[48];
        snprintf(pos, sizeof(pos), "%d / %d   \xB7   %s", sel[plat] + 1, l->count, settings.sort ? "Z-A" : "A-Z");
        gfx_text(SCREEN_W - MARGIN_X - gfx_text_w(22, pos), 196, 22, 0xffffff80, pos);
    }

    float bh = COVER_H, bw = COVER_H * plat_aspect[plat];
    int first = top_row[plat] * COLS, last = first + COLS * VIS_ROWS;
    if (last > l->count) last = l->count;

    for (i = first; i < last; i++) {
        int r = i / COLS - top_row[plat], c = i % COLS;
        float cx = MARGIN_X + c * CELL_W + (CELL_W - bw) / 2;
        float cy = GRID_Y + r * ROW_H;
        if (cy + bh > FOOTER_Y - 10) break;          /* only whole rows above the button bar */
        if (i == sel[plat]) continue;               /* selected drawn last, on top */
        draw_cover(&l->games[i], cx, cy, bw, bh, 0);
        if (settings.titles) gfx_text_fit(cx, cy + bh + 16, 19, 0xffffffb0, l->games[i].title, CELL_W - 30);
    }
    {
        int r = sel[plat] / COLS - top_row[plat], c = sel[plat] % COLS;
        float cx = MARGIN_X + c * CELL_W + (CELL_W - bw) / 2, cy = GRID_Y + r * ROW_H;
        draw_cover(cur, cx, cy, bw, bh, 1);
        gfx_text_fit(cx, cy + bh + 16, 21, 0xffffffff, cur->title, CELL_W - 30);   /* the selected game always shows its name */
    }
    /* preload the next row so scrolling shows covers straight away */
    for (i = last; i < last + COLS && i < l->count; i++) covers_get(&l->games[i]);
}

static void fact(float x, float y, const char *label, const char *value, float max_w)
{
    gfx_text(x, y, 17, 0xffffff8c, label);
    gfx_text_fit(x, y + 30, 30, 0xffffffff, value, max_w);
}

static void draw_info(void)
{
    GameList *l = &library[plat];
    if (l->count == 0) { mode = 0; return; }
    const Game *g = &l->games[sel[plat]];
    char buf[200];

    /* breadcrumb */
    float bx = gfx_text(MARGIN_X, 56, 26, 0xffffff90, plat_short(g->plat));
    bx = gfx_text(bx + 14, 56, 26, 0xffffff60, "\xBB");
    gfx_text_fit(bx + 14, 56, 26, 0xffffffff, g->title, 1300);

    /* cover, never stretched, inside a 560 x 640 box */
    float box_w = 560, box_h = 600, cw, ch;
    float asp = plat_aspect[g->plat];
    const Texture *t = covers_get(g);
    if (t) asp = (float) t->w / (float) t->h;
    if (box_w / asp <= box_h) { cw = box_w; ch = box_w / asp; } else { ch = box_h; cw = box_h * asp; }
    float cx = MARGIN_X + 20, cy = 190;
    gfx_rect(cx + 10, cy + 16, cw, ch, 0x00000080);
    if (t) gfx_image(t, cx, cy, cw, ch, 0xffffffff);
    else   draw_placeholder(g, cx, cy, cw, ch);
    gfx_frame(cx, cy, cw, ch, 1, 0xffffff40);

    /* text column */
    float x = MARGIN_X + 20 + box_w + 80, w = SCREEN_W - MARGIN_X - x, y = 190;
    float chip = gfx_text_w(17, plat_name(g->plat)) + 32;
    gfx_rect(x, y, chip, 38, 0xffffff24);
    gfx_frame(x, y, chip, 38, 1, 0xffffff40);
    gfx_text(x + 16, y + 8, 17, 0xffffffff, plat_name(g->plat));
    y = gfx_text_wrap(x, y + 64, 64, 0xffffffff, g->title, w, 2);

    snprintf(buf, sizeof(buf), "%s%s%s", g->region[0] ? g->region : "", g->region[0] ? "   \xB7   " : "", format_name(g->format));
    gfx_text(x, y + 6, 26, 0xffffffb0, buf);

    /* details card */
    float ky = y + 70, kh = 330;
    gfx_rect(x, ky, w, kh, 0x0000004a);
    gfx_frame(x, ky, w, kh, 1, 0xffffff1c);
    float col = (w - 80) / 3;
    fact(x + 40,           ky + 34, "REGION",   g->region[0] ? g->region : "Unknown", col - 30);
    fact(x + 40 + col,     ky + 34, "GAME ID",  g->id[0] ? g->id : "Not read yet", col - 30);
    fact(x + 40 + col * 2, ky + 34, "FORMAT",   format_name(g->format), col - 30);
    fact(x + 40,           ky + 124, "ARTWORK", g->cover[0] ? (t ? "Cover found" : "Loading...") : "No cover yet", col - 30);
    fact(x + 40 + col,     ky + 124, "CONSOLE", plat_name(g->plat), col * 2 - 30);
    gfx_text(x + 40, ky + 216, 17, 0xffffff8c, "INSTALL LOCATION");
    gfx_text_wrap(x + 40, ky + 246, 22, 0xffffffd0, g->path, w - 80, 2);
}

/* ---------------------------------------------------------------- settings window */
enum { S_HEADER, S_THEME, S_BRIGHT, S_WAVE, S_FONT, S_SIZE, S_TITLES, S_SORT, S_OPEN, S_DISC, S_RESCAN, S_RESET, S_ABOUT };
static const struct { int id; const char *label; const char *help; } set_items[] = {
    { S_HEADER, "APPEARANCE", NULL },
    { S_THEME,  "Background colour", "PlayStation colours: the PS3 XMB blue, the PlayStation logo colours, the face-button colours and the console finishes." },
    { S_BRIGHT, "Background brightness", "How bright the background is." },
    { S_WAVE,   "Background animation", "Like the PS3 home screen: the moving wave, plus sparkles and soft circles floating past." },
    { S_FONT,   "Font", "The PS3's own system fonts. Fonts your firmware doesn't have are skipped." },
    { S_HEADER, "LIBRARY", NULL },
    { S_SIZE,   "Cover size", "Small fits 8 covers a row, Medium 7 and Large 5." },
    { S_TITLES, "Game names under covers", "The selected game always shows its name." },
    { S_SORT,   "Sort order", "Order of the games in each library." },
    { S_OPEN,   "Open on", "Which library is shown when the dashboard starts." },
    { S_HEADER, "DISCS", NULL },
    { S_DISC,   "Ask when a disc is inserted", "Shows Play / Install when you put a disc in. Turn off to leave discs to the XMB." },
    { S_HEADER, "SYSTEM", NULL },
    { S_RESCAN, "Rescan library", "Press X to look for new games on the HDD (after copying games over FTP)." },
    { S_RESET,  "Reset all settings", "Press X to put every setting back to how it started." },
};
#define SET_COUNT ((int) (sizeof(set_items) / sizeof(set_items[0])))

static void apply_sort(void);
static void rescan_library(void);

static const char *set_value(int id, char *buf, size_t n)
{
    static const char *bnames[5] = { "Darkest", "Darker", "Normal", "Brighter", "Brightest" };
    static const char *sizes[3] = { "Small", "Medium", "Large" };
    static const char *open[4] = { "Last used", "PS1", "PS2", "PS3" };
    switch (id) {
        case S_THEME:  return gfx_theme_name(settings.theme);
        case S_BRIGHT: return bnames[settings.brightness];
        case S_WAVE:   return settings.wave == 2 ? "Wave + circles" : settings.wave == 1 ? "Wave only" : "Off";
        case S_FONT:   return gfx_font_name(settings.font);
        case S_SIZE:   return sizes[settings.cover_size];
        case S_TITLES: return settings.titles ? "Show" : "Hide";
        case S_SORT:   return settings.sort ? "Z to A" : "A to Z";
        case S_OPEN:   return open[settings.open_on];
        case S_DISC:   return settings.disc_popup ? "On" : "Off";
        case S_RESCAN: snprintf(buf, n, "%d games", library[0].count + library[1].count + library[2].count); return buf;
        
    }
    return "";
}

static int set_selectable(int i) { return set_items[i].id != S_HEADER && set_items[i].id != S_ABOUT; }

/* fixed columns, so labels and values can never run into each other whatever the font:
   label column on the left, a value box on the right with < > at its edges */
static void draw_settings_window(void)
{
    char buf[64];
    const float ww = 1320, wh = 760, wx = (SCREEN_W - ww) / 2, wy = (SCREEN_H - wh) / 2;   /* centred */
    const float list_top = wy + 112, list_bottom = wy + wh - 112;
    const float label_x = wx + 60, label_w = 690;
    const float box_x = wx + 790, box_w = ww - 790 - 60;
    const float ROW = 56, HEAD = 50;
    int i;

    gfx_rect(0, 0, SCREEN_W, SCREEN_H, gfx_theme_col(3, 0xb0));
    gfx_rect(wx + 8, wy + 12, ww, wh, 0x00000060);                         /* shadow */
    gfx_rect_v(wx, wy, ww, wh, gfx_theme_col(1, 0xff), gfx_theme_col(2, 0xff));
    gfx_frame(wx, wy, ww, wh, 1, 0xffffff40);
    gfx_rect(wx, wy, ww, 4, gfx_accent(0xff));
    gfx_text(label_x, wy + 30, 44, 0xffffffff, "Settings");
    {
        const char *about = "PS Dashboard " APP_VERSION "  \xB7  games start through webMAN MOD";
        gfx_text(wx + ww - 60 - gfx_text_w(20, about), wy + 52, 20, 0xffffff80, about);
    }

    /* scroll so the selected row (and its section name) is in view */
    if (set_row < set_top) set_top = set_row;
    if (set_top > 0 && set_items[set_top - 1].id == S_HEADER && set_row == set_top) set_top--;
    for (;;) {
        float yy = list_top;
        for (i = set_top; i <= set_row; i++) yy += set_items[i].id == S_HEADER ? HEAD : ROW;
        if (yy <= list_bottom || set_top >= set_row) break;
        set_top++;
    }

    float y = list_top;
    int last_drawn = set_top - 1;
    for (i = set_top; i < SET_COUNT; i++) {
        int id = set_items[i].id, on = (i == set_row);
        if (y + (id == S_HEADER ? HEAD : ROW) > list_bottom) break;
        last_drawn = i;
        if (id == S_HEADER) {
            gfx_text(label_x, y + 16, 18, gfx_accent(0xe0), set_items[i].label);
            gfx_rect(label_x, y + 42, ww - 120, 1, 0xffffff26);
            y += HEAD;
            continue;
        }
        if (on) {
            gfx_rect(wx + 36, y + 2, ww - 72, ROW - 6, 0xffffff22);
            gfx_frame(wx + 36, y + 2, ww - 72, ROW - 6, 1, 0xffffff70);
        }
        gfx_text_fit(label_x, y + 13, 26, on ? 0xffffffff : 0xffffffc0, set_items[i].label, label_w);

        const char *v = set_value(id, buf, sizeof(buf));
        u32 vc = on ? 0xffffffff : 0xffffffa0;
        if (id == S_RESCAN || id == S_RESET) {
            const char *t = on ? "Press X" : (id == S_RESCAN ? v : "");
            float tw = gfx_text_w(22, t);
            gfx_text(box_x + box_w - 44 - tw, y + 16, 22, on ? 0xffffffd0 : 0xffffff80, t);
        } else {
            float inner_x = box_x + 44, inner_w = box_w - 88;     /* room for < and > */
            float sw = (id == S_THEME) ? 46 : 0;                 /* colour swatch before the name */
            float tw = gfx_text_w(26, v);
            if (tw > inner_w - sw) tw = inner_w - sw;
            float tx = inner_x + inner_w - tw;                   /* right-aligned inside the box */
            if (id == S_THEME) gfx_rect(tx - sw, y + 15, 32, 24, gfx_theme_swatch(settings.theme));
            gfx_text_fit(tx, y + 13, 26, vc, v, inner_w - sw);
            if (on && id != S_ABOUT) {
                gfx_text(box_x + 6, y + 13, 26, 0xffffffb0, "<");
                gfx_text(box_x + box_w - 22, y + 13, 26, 0xffffffb0, ">");
            }
        }
        y += ROW;
    }

    /* scroll bar instead of text, so nothing can collide */
    if (set_top > 0 || last_drawn < SET_COUNT - 1) {
        float tx = wx + ww - 22, th = list_bottom - list_top;
        float a = (float) set_top / SET_COUNT, b = (float) (last_drawn + 1) / SET_COUNT;
        gfx_rect(tx, list_top, 4, th, 0xffffff1a);
        gfx_rect(tx, list_top + th * a, 4, th * (b - a), 0xffffffa0);
    }

    /* help for the selected setting, in its own strip at the bottom */
    gfx_rect(wx + 1, wy + wh - 100, ww - 2, 99, 0x00000030);
    gfx_rect(wx, wy + wh - 100, ww, 1, 0xffffff26);
    gfx_text_wrap(label_x, wy + wh - 84, 22, 0xffffffc0, set_items[set_row].help ? set_items[set_row].help : "", ww - 120, 2);
}

static void settings_change(int d, int pressed_x)
{
    int id = set_items[set_row].id, n, i;
    switch (id) {
        case S_THEME:  n = gfx_theme_count(); settings.theme = (settings.theme + d + n) % n; break;
        case S_BRIGHT: if (settings.brightness + d < 0 || settings.brightness + d > 4) return; settings.brightness += d; break;
        case S_WAVE:   settings.wave = (settings.wave + d + 3) % 3; break;
        case S_FONT:   /* next font the firmware actually has */
            n = gfx_font_count();
            for (i = 0, n = gfx_font_count(); i < n; i++) {
                settings.font = (settings.font + d + n) % n;
                if (gfx_font_available(settings.font)) break;
            }
            break;
        case S_SIZE:   if (settings.cover_size + d < 0 || settings.cover_size + d > 2) return; settings.cover_size += d; break;
        case S_TITLES: settings.titles = !settings.titles; break;
        case S_SORT:   settings.sort = !settings.sort; apply_sort(); break;
        case S_OPEN:   settings.open_on = (settings.open_on + d + 4) % 4; break;
        case S_DISC:   settings.disc_popup = !settings.disc_popup; break;
        case S_RESCAN: if (!pressed_x) return; rescan_library(); return;
        case S_RESET:
            if (!pressed_x) return;
            settings = settings_defaults; apply_sort();
            break;
        default: return;
    }
    settings_apply();
    settings_save();
}

static void settings_window_input(void)
{
    int i;
    if (pad_pressed & (B_SELECT | B_CIRCLE)) { show_settings = 0; return; }
    if (pad_pressed & B_DOWN) for (i = set_row + 1; i < SET_COUNT; i++) if (set_selectable(i)) { set_row = i; break; }
    if (pad_pressed & B_UP)   for (i = set_row - 1; i >= 0; i--)        if (set_selectable(i)) { set_row = i; break; }
    if (pad_pressed & B_RIGHT) settings_change(1, 0);
    if (pad_pressed & B_LEFT)  settings_change(-1, 0);
    if (pad_pressed & B_CROSS) settings_change(1, 1);
}

static void draw_message(const char *title, const char *line)
{
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, gfx_theme_col(3, 0xd0));
    gfx_rect_v(460, 380, 1000, 300, gfx_theme_col(1, 0xf8), gfx_theme_col(2, 0xf8));
    gfx_frame(460, 380, 1000, 300, 1, 0xffffff40);
    gfx_text_center(SCREEN_W / 2, 440, 40, 0xffffffff, title);
    gfx_text_wrap(520, 510, 24, 0xffffffd0, line, 880, 4);
}

static void draw_footer_band(void)
{
    /* covers anything scrolling under the button bar */
    gfx_rect_v(0, FOOTER_Y - 70, SCREEN_W, 70, gfx_theme_col(3, 0), gfx_theme_col(3, 0xe6));
    gfx_rect(0, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, gfx_theme_col(3, 0xe6));
    gfx_rect(MARGIN_X, FOOTER_Y + 2, SCREEN_W - 2 * MARGIN_X, 1, 0xffffff22);
}

static void draw_hints(void)
{
    float x = MARGIN_X, y = FOOTER_Y + 40;
    if (!show_settings) draw_footer_band();   /* the settings window dims everything itself */
    if (show_settings) {
        x = hint_pill(x, y, "UP / DOWN", "Choose");
        x = hint_pill(x, y, "LEFT / RIGHT", "Change");
        x = hint(x, y, 'o', "Close");
    } else if (mode == 3) {
        x = hint(x, y, 'x', "Select");
        x = hint(x, y, 'o', "Not now");
    } else if (mode == 1) {
        x = hint(x, y, 'x', "Play");
        x = hint(x, y, 'o', "Back");
    } else {
        x = hint(x, y, 'x', "Play");
        x = hint(x, y, 't', "Game info");
        x = hint_pill(x, y, "L1 / R1", "Switch console");
        x = hint_pill(x, y, "SELECT", "Settings");
        x = hint_pill(x, y, "START", "Exit");
    }
    { const char *ver = APP_VERSION; gfx_text(SCREEN_W - MARGIN_X - gfx_text_w(18, ver), y + 8, 18, 0xffffff60, ver); }
}

/* ---------------------------------------------------------------- main */
static void start_game(const Game *g)
{
    char line[200];
    snprintf(line, sizeof(line), "Starting %s ...  It opens by itself once the XMB has loaded - don't press any buttons.", g->title);
    gfx_begin(); gfx_background(anim * 0.2f); draw_message("Loading game", line); gfx_end();
    log_step("launch: %s", g->path);
    if (launch_game(g)) { log_step("webMAN accepted"); exit(0); }
    log_step("webMAN not reachable");        /* webMAN mounts and starts the game after we exit */
    mode = 2;
}

static int disc_can_install(void) { return disc.type == DISC_PS3; }

static void draw_disc_popup(void)
{
    char line[200];
    float x = 460, y = 330, w = 1000, h = 420;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, gfx_theme_col(3, 0xd0));
    gfx_rect_v(x, y, w, h, gfx_theme_col(1, 0xf8), gfx_theme_col(2, 0xf8));
    gfx_frame(x, y, w, h, 1, 0xffffff40);
    gfx_text(x + 60, y + 44, 20, 0xffffff8c, "DISC INSERTED");
    gfx_text_fit(x + 60, y + 80, 44, 0xffffffff, disc.title[0] ? disc.title : disc_name(disc.type), w - 120);
    if (disc.title[0]) snprintf(line, sizeof(line), "%s%s%s", disc_name(disc.type), disc.id[0] ? "   \xB7   " : "", disc.id);
    else snprintf(line, sizeof(line), "%s", disc.id[0] ? disc.id : "Game ID not readable yet");
    gfx_text_fit(x + 60, y + 144, 24, 0xffffffb0, line, w - 120);

    const char *labels[2] = { "Play", "Install to HDD" };
    int i;
    for (i = 0; i < 2; i++) {
        float bx = x + 60 + i * 300, by = y + 210, bw = 270, bh = 70;
        int on = (disc_choice == i), ok = (i == 0 || disc_can_install());
        gfx_rect(bx, by, bw, bh, on ? 0xffffff30 : 0xffffff10);
        gfx_frame(bx, by, bw, bh, on ? 2 : 1, on ? 0xffffffd0 : 0xffffff30);
        gfx_text_center(bx + bw / 2, by + 20, 28, ok ? 0xffffffff : 0xffffff50, labels[i]);
    }
    const char *note =
        disc_choice == 1 ? "webMAN copies the disc to /dev_hdd0/GAMES on the XMB and tells you when it's finished. It then appears in your PS3 library." :
        disc_can_install() ? "Starts the disc from the XMB once the dashboard has closed." :
        "Starts the disc from the XMB once the dashboard has closed.  Installing PlayStation and PS2 discs to the HDD isn't available yet.";
    gfx_text_wrap(x + 60, y + 310, 22, 0xffffffc0, note, w - 120, 3);
}

static void disc_action(void)
{
    char line[220], target[300], name[120];
    int ok;
    if (disc_choice == 1 && !disc_can_install()) return;
    if (disc_choice == 0) {
        snprintf(line, sizeof(line), "Starting %s ...  It opens by itself once the XMB has loaded - don't press any buttons.", disc.title[0] ? disc.title : disc_name(disc.type));
        gfx_begin(); gfx_background(anim * 0.2f); draw_message("Loading disc", line); gfx_end();
        log_step("disc: play");
        ok = launch_disc();
    } else {
        /* folder name: "<title> [<ID>]" with characters the HDD can't take removed */
        const char *src = disc.title[0] ? disc.title : (disc.id[0] ? disc.id : "PS3 disc");
        size_t j = 0;
        for (; *src && j + 1 < sizeof(name); src++)
            if (!strchr("\\/:*?\"<>|", *src)) name[j++] = *src;
        name[j] = 0;
        if (disc.title[0] && disc.id[0]) snprintf(target, sizeof(target), "/dev_hdd0/GAMES/%s [%s]", name, disc.id);
        else snprintf(target, sizeof(target), "/dev_hdd0/GAMES/%s", name);
        snprintf(line, sizeof(line), "Copying to %s on the XMB. webMAN shows the progress and tells you when it's done.", target);
        gfx_begin(); gfx_background(anim * 0.2f); draw_message("Installing disc", line); gfx_end();
        log_step("disc: install to %s", target);
        ok = install_disc(target);
        if (ok) sleep(3);   /* leave the message up long enough to read */
    }
    if (ok) { log_step("webMAN accepted"); exit(0); }
    log_step("webMAN not reachable");
    mode = 2;
}

/* the scanner sorts A-Z; Z-A is the same list reversed */
static int sorted_za = 0;
static void apply_sort(void)
{
    int p, i;
    if (sorted_za == settings.sort) return;
    for (p = 0; p < PLAT_COUNT; p++) {
        GameList *l = &library[p];
        for (i = 0; i < l->count / 2; i++) { Game t = l->games[i]; l->games[i] = l->games[l->count - 1 - i]; l->games[l->count - 1 - i] = t; }
        sel[p] = l->count ? l->count - 1 - sel[p] : 0; top_row[p] = 0;
    }
    sorted_za = settings.sort;
    covers_reset();
}

static void rescan_library(void)
{
    int p;
    gfx_begin(); gfx_background(anim * 0.2f); gfx_text_center(SCREEN_W / 2, 500, 36, 0xffffffe0, "Looking for games..."); gfx_end();
    covers_reset();
    library_scan();
    sorted_za = 0;
    apply_sort();
    for (p = 0; p < PLAT_COUNT; p++) { if (sel[p] >= library[p].count) sel[p] = 0; top_row[p] = 0; }
    log_step("rescan: PS1 %d, PS2 %d, PS3 %d", library[0].count, library[1].count, library[2].count);
}

s32 main(s32 argc, const char *argv[])
{
    int i;
    log_reset();
    log_step("start " APP_VERSION);
    settings_load();     /* before gfx_init: it picks the font */
    gfx_init();
    settings_apply();
    ioPadInit(7);
    log_step("pads ok");

    for (i = 0; i < 2; i++) {   /* both buffers */
        gfx_begin(); gfx_background(0); gfx_text_center(SCREEN_W / 2, 500, 36, 0xffffffe0, "Loading your library..."); gfx_end();
    }
    log_step("first frame drawn");

    covers_init();
    log_step("covers ok");
    library_scan();
    log_step("library: PS1 %d, PS2 %d, PS3 %d", library[0].count, library[1].count, library[2].count);
    apply_sort();
    for (i = 0; i < PLAT_COUNT; i++) { sel[i] = 0; top_row[i] = 0; }
    /* open on the chosen console; if it has no games, the first one that does */
    plat = settings.open_on ? settings.open_on - 1 : settings.last_plat;
    if (!library[plat].count)
        for (i = 0; i < PLAT_COUNT; i++) if (library[i].count) { plat = i; break; }

    int frames = 0;
    while (1) {
        if (frames == 1) log_step("main loop running");
        if (++frames % 600 == 0) log_step("still running, frame %d", frames);
        read_pads();
        covers_frame();
        anim += 0.06f;

        if (mode != 3 && !show_settings && disc_poll(&disc) && settings.disc_popup) { prev_mode = (mode == 2) ? 0 : mode; mode = 3; disc_choice = 0; }

        GameList *l = &library[plat];
        if (mode == 3) {
            if (pad_pressed & B_LEFT) disc_choice = 0;
            if ((pad_pressed & B_RIGHT) && disc_can_install()) disc_choice = 1;
            if (pad_pressed & B_CIRCLE) mode = prev_mode;
            else if (pad_pressed & B_CROSS) disc_action();
        } else if (show_settings) {
            settings_window_input();
        } else if (mode == 0) {
            int was = plat;
            if (pad_pressed & B_R1) plat = (plat + 1) % PLAT_COUNT;
            if (pad_pressed & B_L1) plat = (plat + PLAT_COUNT - 1) % PLAT_COUNT;
            if (plat != was && settings.open_on == 0) { settings.last_plat = plat; settings_save(); }
            if (pad_pressed & B_SELECT) { show_settings = 1; if (!set_selectable(set_row)) set_row = 1; }
            l = &library[plat];
            if (l->count) {
                int s = sel[plat];
                if ((pad_pressed & B_RIGHT) && s + 1 < l->count) s++;
                if ((pad_pressed & B_LEFT) && s > 0) s--;
                if ((pad_pressed & B_DOWN) && s + COLS < l->count) s += COLS;
                else if ((pad_pressed & B_DOWN) && s / COLS < (l->count - 1) / COLS) s = l->count - 1;
                if ((pad_pressed & B_UP) && s - COLS >= 0) s -= COLS;
                sel[plat] = s;
                if (pad_pressed & B_TRIANGLE) mode = 1;
                if (pad_pressed & B_CROSS) start_game(&l->games[sel[plat]]);
            }
            if (pad_pressed & B_START) break;
        } else if (mode == 1) {
            if (pad_pressed & (B_CIRCLE | B_TRIANGLE)) mode = 0;
            else if (pad_pressed & B_CROSS) start_game(&l->games[sel[plat]]);
        } else {
            if (pad_pressed & (B_CIRCLE | B_CROSS)) mode = 0;
        }

        gfx_begin();
        gfx_background(anim * 0.2f);
        if (mode == 1 || (mode == 3 && prev_mode == 1)) {
            draw_info();
        } else {
            draw_nav();
            draw_grid();
        }
        if (mode == 3) draw_disc_popup();
        if (show_settings) draw_settings_window();
        draw_hints();
        if (mode == 2) draw_message("webMAN MOD isn't responding",
                                    "The dashboard starts games through webMAN MOD. Check it is installed and enabled, then try again.  (Circle to close)");
        gfx_end();
    }
    return 0;
}
