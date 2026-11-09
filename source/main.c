/*
 * PS Dashboard - a PlayStation library for the PS3
 * Stage 1: PS1 / PS2 / PS3 cover grids, game info panel, launching through webMAN MOD.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
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
#include "info.h"
#include "lists.h"
#include "sysinfo.h"
#include "sound.h"
#include "rip.h"
#include <sys/systime.h>
#include <sys/process.h>
#include <lv2/process.h>
#include <lv2/sysfs.h>
#include <sys/file.h>
#include <strings.h>

#define APP_VERSION "v0.22"

/* ---------------------------------------------------------------- input */
enum {
    B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_CROSS = 16, B_CIRCLE = 32,
    B_TRIANGLE = 64, B_SQUARE = 128, B_L1 = 256, B_R1 = 512, B_START = 1024, B_SELECT = 2048,
    B_L2 = 4096, B_R2 = 8192, B_L3 = 16384, B_R3 = 32768
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
        if (d.BTN_L2)       now |= B_L2;
        if (d.BTN_R2)       now |= B_R2;
        if (d.BTN_L3)       now |= B_L3;
        if (d.BTN_R3)       now |= B_R3;
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

/* filled five-point star (favourites) */
static void star(float cx, float cy, float r, u32 c)
{
    int i;
    tiny3d_SetPolygon(TINY3D_QUADS);
    for (i = 0; i < 10; i++) {
        float a0 = -1.5707963f + i * 0.6283185f, a1 = a0 + 0.6283185f;
        float r0 = (i & 1) ? r * 0.42f : r, r1 = (i & 1) ? r : r * 0.42f;
        tiny3d_VertexPos(cx, cy, 1000);                                   tiny3d_VertexColor(c);
        tiny3d_VertexPos(cx, cy, 1000);                                   tiny3d_VertexColor(c);
        tiny3d_VertexPos(cx + r1 * cosf(a1), cy + r1 * sinf(a1), 1000);   tiny3d_VertexColor(c);
        tiny3d_VertexPos(cx + r0 * cosf(a0), cy + r0 * sinf(a0), 1000);   tiny3d_VertexColor(c);
    }
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
static const float size_cover[3] = { 178.0f, 204.0f, 250.0f };
static const float size_row[3]   = { 266.0f, 292.0f, 330.0f };
#define COLS       (cols_for(plat))
#define CELL_W     ((SCREEN_W - 2 * MARGIN_X) / COLS)
#define COVER_H    (size_cover[settings.cover_size])
#define ROW_H      (size_row[settings.cover_size])
#define VIS_ROWS   3
#define FOOTER_Y   948.0f

static const float plat_aspect[PLAT_COUNT] = { 1.0f, 0.70f, 0.86f };   /* PS1 jewel, PS2 DVD, PS3 Blu-ray */
static const u32   plat_hue[PLAT_COUNT][2] = { { 0x545862ff, 0x1c1d22ff }, { 0x1d3fb8ff, 0x070d33ff }, { 0x46566eff, 0x10161fff } };

static int plat = PLAT_PS1;
static int info_scroll = 0;     /* first synopsis line shown on the Game info page */

/* as many covers per row as fit with an even gap, so narrow PS2 cases sit closer together */
static int cols_for(int p)
{
    float bh = size_cover[settings.cover_size], bw = bh * plat_aspect[p], gap = bh * 0.2f;
    int n = (int) ((SCREEN_W - 2 * MARGIN_X + gap) / (bw + gap));
    return n < 3 ? 3 : n > 12 ? 12 : n;
}
static int show_settings = 0, set_row = 0, set_top = 0;   /* Settings window (SELECT) */
static int sel[PLAT_COUNT];
static int top_row[PLAT_COUNT];
static int mode = 0;            /* 0 grid, 1 info panel, 2 webMAN error, 3 disc inserted */
static int prev_mode = 0, disc_choice = 0;
static DiscInfo disc;
static float anim = 0.0f;       /* selection pulse */

/* tabs: 0 Home, 1 PS1, 2 PS2, 3 PS3 ("plat" follows the console tab) */
static int tab = 1;
#define ON_HOME (tab == 0)

/* what a console tab shows: its games, filtered by the search text */
static Game **vlist = NULL;
static int vn = 0, vcap = 0;
static char query[32] = "";
static int kb_row = 0, kb_col = 0;        /* search keyboard cursor */
static int disc_pick = 0;                 /* multi-disc picker */
static const Game *pick_game = NULL;

/* Home shelves */
#define SHELF_MAX 64
static Game *shelf[2][SHELF_MAX];
static int shelf_n[2], home_row = 0, home_sel[2], home_left[2];

static int contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return 1;
    for (; *hay; hay++) if (!strncasecmp(hay, needle, n)) return 1;
    return 0;
}

static void view_build(void)
{
    GameList *l = &library[plat];
    int i;
    if (vcap < l->count + 1) { vcap = l->count + 64; vlist = realloc(vlist, vcap * sizeof(Game *)); }
    vn = 0;
    for (i = 0; i < l->count; i++)
        if (contains_ci(l->games[i].title, query)) vlist[vn++] = &l->games[i];
    if (sel[plat] >= vn) sel[plat] = vn ? vn - 1 : 0;
}

static void home_build(void)
{
    shelf_n[0] = recent_list(shelf[0], SHELF_MAX);
    shelf_n[1] = fav_list(shelf[1], SHELF_MAX);
    int r;
    for (r = 0; r < 2; r++) if (home_sel[r] >= shelf_n[r]) home_sel[r] = shelf_n[r] ? shelf_n[r] - 1 : 0;
}

static Game *cur_game(void)
{
    if (ON_HOME) return shelf_n[home_row] ? shelf[home_row][home_sel[home_row]] : NULL;
    return vn ? vlist[sel[plat]] : NULL;
}

static void set_tab(int t)
{
    tab = t;
    if (t > 0) plat = t - 1;
    query[0] = 0;
    if (ON_HOME) home_build(); else view_build();
}

static void draw_placeholder(const Game *g, float x, float y, float w, float h)
{
    gfx_rect_v(x, y, w, h, plat_hue[g->plat][0], plat_hue[g->plat][1]);
    if (w < 110) return;                       /* too small for a name (list view thumbnails) */
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
    static const char *labels[4] = { "Home", "PS1", "PS2", "PS3" };
    float x = MARGIN_X;
    int t, total = 0;
    for (t = 0; t < 4; t++) {
        int on = (t == tab);
        float size = on ? 46 : 32;
        float w = gfx_text_w(size, labels[t]);
        gfx_text(x, on ? 52 : 64, size, on ? 0xffffffff : 0xffffff90, labels[t]);
        if (on) gfx_rect(x, 112, w, 4, gfx_accent(0xff));
        x += w + 56;
    }
    for (t = 0; t < PLAT_COUNT; t++) total += library[t].count;
    char buf[160];
    const char *si = settings.sysinfo ? sysinfo_text() : "";
    snprintf(buf, sizeof(buf), "%d games%s%s", total, si[0] ? "   \xB7   " : "", si);
    gfx_text(SCREEN_W - MARGIN_X - gfx_text_w(22, buf), 70, 22, 0xffffffa0, buf);
}

/* small marks drawn on a cover: star for favourites, disc count */
static void draw_badges(const Game *g, float x, float y, float w)
{
    if (g->fav) {
        circle_fill(x + w - 20, y + 20, 15, 0x000000a8);
        star(x + w - 20, y + 21, 11, 0xffd84aff);
    }
    if (g->ndiscs) {
        char b[8]; snprintf(b, sizeof(b), "%d CD", g->ndiscs);
        float bw = gfx_text_w(15, b) + 12;
        gfx_rect(x + 6, y + 6, bw, 24, 0x000000a0);
        gfx_text(x + 12, y + 9, 15, 0xffffffe0, b);
    }
}

static void draw_heading(void)
{
    char buf[160];
    float x;
    if (query[0]) {
        x = gfx_text(MARGIN_X, 168, 52, 0xffffffff, "Search");
        snprintf(buf, sizeof(buf), "  \xB7  \"%s\" in %s  \xB7  %d found", query, plat_short(plat), vn);
        gfx_text(x, 184, 30, 0xffffff90, buf);
    } else {
        x = gfx_text(MARGIN_X, 168, 52, 0xffffffff, plat_name(plat));
        snprintf(buf, sizeof(buf), "  \xB7  %d game%s", vn, vn == 1 ? "" : "s");
        gfx_text(x, 184, 34, 0xffffff90, buf);
    }
    if (vn) {
        char pos[48];
        snprintf(pos, sizeof(pos), "%d / %d   \xB7   %s", sel[plat] + 1, vn, settings.sort ? "Z-A" : "A-Z");
        gfx_text(SCREEN_W - MARGIN_X - gfx_text_w(22, pos), 196, 22, 0xffffff80, pos);
    }
}

static int draw_empty(void)
{
    char buf[160];
    if (vn) return 0;
    if (query[0]) {
        gfx_text_center(SCREEN_W / 2, 520, 34, 0xffffffe0, "No games match your search");
        gfx_text_center(SCREEN_W / 2, 572, 24, 0xffffffa0, "Press L3 to change it");
        return 1;
    }
    static const char *where[PLAT_COUNT] = { "PSXISO", "PS2ISO", "GAMES or PS3ISO" };
    gfx_text_center(SCREEN_W / 2, 520, 34, 0xffffffe0, "No games found here yet");
    snprintf(buf, sizeof(buf), "Put your %s games in %s on the internal HDD or a USB drive", plat_short(plat), where[plat]);
    gfx_text_center(SCREEN_W / 2, 572, 24, 0xffffffa0, buf);
    return 1;
}

static void draw_grid(void)
{
    int i;
    draw_heading();
    if (draw_empty()) return;

    /* keep the selected row visible */
    int srow = sel[plat] / COLS;
    if (srow < top_row[plat]) top_row[plat] = srow;
    if (srow > top_row[plat] + 1) top_row[plat] = srow - 1;

    const Game *cur = vlist[sel[plat]];
    float bh = COVER_H, bw = COVER_H * plat_aspect[plat];
    int first = top_row[plat] * COLS, last = first + COLS * VIS_ROWS;
    if (last > vn) last = vn;

    for (i = first; i < last; i++) {
        int r = i / COLS - top_row[plat], c = i % COLS;
        float cx = MARGIN_X + c * CELL_W + (CELL_W - bw) / 2;
        float cy = GRID_Y + r * ROW_H;
        if (cy + bh > FOOTER_Y - 10) break;          /* only whole rows above the button bar */
        if (i == sel[plat]) continue;               /* selected drawn last, on top */
        draw_cover(vlist[i], cx, cy, bw, bh, 0);
        draw_badges(vlist[i], cx, cy, bw);
        if (settings.titles) gfx_text_fit(cx, cy + bh + 16, 19, 0xffffffb0, vlist[i]->title, CELL_W - 30);
    }
    {
        int r = sel[plat] / COLS - top_row[plat], c = sel[plat] % COLS;
        float cx = MARGIN_X + c * CELL_W + (CELL_W - bw) / 2, cy = GRID_Y + r * ROW_H;
        draw_cover(cur, cx, cy, bw, bh, 1);
        draw_badges(cur, cx, cy - bh * 0.06f, bw);
        gfx_text_fit(cx, cy + bh + 16, 21, 0xffffffff, cur->title, CELL_W - 30);   /* the selected game always shows its name */
    }
    /* preload the next row so scrolling shows covers straight away */
    for (i = last; i < last + COLS && i < vn; i++) covers_get(vlist[i]);
}

/* list view: one game per row */
#define LIST_Y     270.0f
#define LIST_ROW   86.0f
#define LIST_ROWS  7
static void draw_list(void)
{
    int i;
    char buf[160];
    draw_heading();
    if (draw_empty()) return;
    if (sel[plat] < top_row[plat]) top_row[plat] = sel[plat];
    if (sel[plat] >= top_row[plat] + LIST_ROWS) top_row[plat] = sel[plat] - LIST_ROWS + 1;
    for (i = top_row[plat]; i < vn && i < top_row[plat] + LIST_ROWS; i++) {
        const Game *g = vlist[i];
        int on = (i == sel[plat]);
        float y = LIST_Y + (i - top_row[plat]) * LIST_ROW, x = MARGIN_X;
        if (on) {
            gfx_rect(x - 16, y - 4, SCREEN_W - 2 * MARGIN_X + 32, LIST_ROW - 8, 0xffffff22);
            gfx_frame(x - 16, y - 4, SCREEN_W - 2 * MARGIN_X + 32, LIST_ROW - 8, 1, 0xffffff70);
        }
        float ch = LIST_ROW - 22, cw = ch * plat_aspect[g->plat];
        const Texture *t = covers_get(g);
        if (t) { float sc = fminf(ch / t->h, ch * 1.2f / t->w); gfx_image(t, x + (ch * 1.2f - t->w * sc) / 2, y + 3 + (ch - t->h * sc) / 2, t->w * sc, t->h * sc, 0xffffffff); }
        else draw_placeholder(g, x + (ch * 1.2f - cw) / 2, y + 3, cw, ch);
        float tx = x + ch * 1.2f + 30;
        gfx_text_fit(tx, y + 10, 30, on ? 0xffffffff : 0xffffffc8, g->title, 1050);
        snprintf(buf, sizeof(buf), "%s%s%s%s%s", g->region[0] ? g->region : "", g->region[0] ? "  \xB7  " : "", format_name(g->format),
                 g->where[0] && strcmp(g->where, "HDD") ? "  \xB7  " : "", g->where[0] && strcmp(g->where, "HDD") ? g->where : "");
        gfx_text(tx, y + 48, 19, 0xffffff80, buf);
        float rx = SCREEN_W - MARGIN_X;
        if (g->fav) { rx -= 30; star(rx + 14, y + 38, 13, 0xffd84aff); }
        if (g->ndiscs) { snprintf(buf, sizeof(buf), "%d discs", g->ndiscs); rx -= gfx_text_w(20, buf) + 24; gfx_text(rx, y + 24, 20, 0xffffffa0, buf); }
    }
    for (i = top_row[plat] + LIST_ROWS; i < top_row[plat] + LIST_ROWS + 3 && i < vn; i++) covers_get(vlist[i]);
}

/* large artwork view: one big cover in the middle, neighbours either side */
static void draw_large(void)
{
    int k;
    char buf[200];
    draw_heading();
    if (draw_empty()) return;
    const float big = 470, cy = 270, gap = 46;
    float hk[4] = { big, 300, 250, 210 }, xk[4];            /* height and left edge of each neighbour */
    xk[0] = 0;
    {
        float off = big * plat_aspect[plat] / 2 + gap;
        for (k = 1; k <= 3; k++) { xk[k] = off; off += hk[k] * plat_aspect[plat] + gap * 0.6f; }
    }
    for (k = 3; k >= 1; k--) {                       /* neighbours, furthest first */
        int s;
        for (s = -1; s <= 1; s += 2) {
            int i = sel[plat] + s * k;
            if (i < 0 || i >= vn) continue;
            float h = hk[k], w = h * plat_aspect[plat];
            float cx = s > 0 ? SCREEN_W / 2 + xk[k] : SCREEN_W / 2 - xk[k] - w;
            if (cx + w < 0 || cx > SCREEN_W) continue;
            draw_cover(vlist[i], cx, cy + (big - h), w, h, 0);
            gfx_rect(cx, cy + (big - h), w, h, gfx_theme_col(3, 0x30 + 0x28 * k));
        }
    }
    const Game *g = vlist[sel[plat]];
    float w = big * plat_aspect[plat];
    draw_cover(g, SCREEN_W / 2 - w / 2, cy, w, big, 1);
    draw_badges(g, SCREEN_W / 2 - w / 2, cy - big * 0.06f, w);
    gfx_text_center(SCREEN_W / 2, cy + big + 40, 40, 0xffffffff, g->title);
    snprintf(buf, sizeof(buf), "%s%s%s", g->region[0] ? g->region : "", g->region[0] ? "  \xB7  " : "", format_name(g->format));
    gfx_text_center(SCREEN_W / 2, cy + big + 96, 22, 0xffffff90, buf);
}

/* Home: Recently played and Favourites shelves */
static void draw_home(void)
{
    static const char *names[2] = { "Recently played", "Favourites" };
    static const char *empty[2] = { "Games you start from the dashboard show up here.", "Press SQUARE on any game to add it to your favourites." };
    int r, i;
    for (r = 0; r < 2; r++) {
        float y = 166 + r * 380, h = 250;
        gfx_text(MARGIN_X, y, 26, r == home_row ? 0xffffffff : 0xffffff90, names[r]);
        if (!shelf_n[r]) {
            gfx_text(MARGIN_X, y + 60, 22, 0xffffff70, empty[r]);
            continue;
        }
        if (home_sel[r] < home_left[r]) home_left[r] = home_sel[r];
        float x = MARGIN_X;
        int last = home_left[r];
        /* fit as many as there is room for, keeping the selected one in view */
        for (;;) {
            float xx = MARGIN_X; int k;
            for (k = home_left[r]; k < shelf_n[r]; k++) { xx += h * plat_aspect[shelf[r][k]->plat] + 34; if (xx > SCREEN_W - MARGIN_X) break; }
            last = k - 1;
            if (home_sel[r] <= last || home_left[r] >= home_sel[r]) break;
            home_left[r]++;
        }
        for (i = home_left[r]; i <= last && i < shelf_n[r]; i++) {
            Game *g = shelf[r][i];
            float w = h * plat_aspect[g->plat];
            int on = (r == home_row && i == home_sel[r]);
            draw_cover(g, x, y + 62, w, h, on);
            draw_badges(g, x, y + 62 - (on ? h * 0.06f : 0), w);
            if (on) gfx_text_fit(x, y + 62 + h + 14, 21, 0xffffffff, g->title, 520);
            x += w + 34;
        }
    }
}

static void draw_browse(void)
{
    draw_nav();
    if (ON_HOME) draw_home();
    else if (settings.view == 1) draw_list();
    else if (settings.view == 2) draw_large();
    else draw_grid();
}

/* index of the first game whose title starts with a different letter (dir +1 / -1) */
static int letter_jump(int from, int dir)
{
    if (!vn) return 0;
    char c = toupper((unsigned char) vlist[from]->title[0]);
    int i = from;
    while (i + dir >= 0 && i + dir < vn && toupper((unsigned char) vlist[i + dir]->title[0]) == c) i += dir;
    i += dir;
    if (i < 0) return 0;
    if (i >= vn) return vn - 1;
    if (dir < 0) {                                    /* go to the start of that letter */
        char c2 = toupper((unsigned char) vlist[i]->title[0]);
        while (i > 0 && toupper((unsigned char) vlist[i - 1]->title[0]) == c2) i--;
    }
    return i;
}

/* ---------------------------------------------------------------- search keyboard */
static const char *kb_rows[] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789-'&!" };
static int kb_purpose = 0;          /* 0 search, 1 naming a disc image */
static char rip_name[64] = "";
static void rip_start(void);
static const char *kb_keys[] = { "Space", "Delete", "Clear", "Done" };
#define KB_ROWS 5

static void draw_keyboard(void)
{
    const float kw = 92, kh = 74, gap = 10;
    float pw = 10 * (kw + gap) + 70, ph = KB_ROWS * (kh + gap) + 190;
    float px = (SCREEN_W - pw) / 2, py = (SCREEN_H - ph) / 2 - 20;
    int r, c;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, gfx_theme_col(3, 0xa0));
    gfx_rect_v(px, py, pw, ph, gfx_theme_col(1, 0xff), gfx_theme_col(2, 0xff));
    gfx_frame(px, py, pw, ph, 1, 0xffffff40);
    gfx_rect(px, py, pw, 4, gfx_accent(0xff));
    gfx_text(px + 35, py + 26, 30, 0xffffffff, kb_purpose ? "Name this game" : "Search");
    if (kb_purpose && disc.id[0]) {
        char b[64]; snprintf(b, sizeof(b), "[%s] is added to the name for you", disc.id);
        gfx_text(px + pw - 35 - gfx_text_w(18, b), py + 36, 18, 0xffffff90, b);
    }
    /* the text so far */
    gfx_rect(px + 35, py + 76, pw - 70, 56, 0x00000060);
    gfx_frame(px + 35, py + 76, pw - 70, 56, 1, 0xffffff40);
    {
        const char *txt = kb_purpose ? rip_name : query;
        float tx = gfx_text(px + 50, py + 88, 30, 0xffffffff, txt);
        if (((int) (anim * 2)) & 1) gfx_rect(tx + 2, py + 88, 3, 32, 0xffffffd0);
        char b[48];
        if (kb_purpose) snprintf(b, sizeof(b), "%s", disc_name(disc.type)); else snprintf(b, sizeof(b), "%d found", vn);
        gfx_text(px + pw - 50 - gfx_text_w(20, b), py + 94, 20, 0xffffff90, b);
    }
    for (r = 0; r < KB_ROWS; r++) {
        int n = r < 4 ? (int) strlen(kb_rows[r]) : 4;
        for (c = 0; c < n; c++) {
            float w = r < 4 ? kw : (10 * (kw + gap) - 3 * gap) / 4;
            float x = px + 35 + c * (w + gap), y = py + 160 + r * (kh + gap);
            int on = (r == kb_row && c == kb_col);
            gfx_rect(x, y, w, kh, on ? 0xffffff40 : 0xffffff12);
            gfx_frame(x, y, w, kh, on ? 2 : 1, on ? 0xffffffe0 : 0xffffff30);
            if (r < 4) { char k[2] = { kb_rows[r][c], 0 }; gfx_text_center(x + w / 2, y + 18, 34, 0xffffffff, k); }
            else gfx_text_center(x + w / 2, y + 22, 26, 0xffffffff, (c == 3 && kb_purpose) ? "Make ISO" : kb_keys[c]);
        }
    }
}

static void keyboard_input(void)
{
    int n = kb_row < 4 ? (int) strlen(kb_rows[kb_row]) : 4;
    if (pad_pressed & B_RIGHT) { kb_col = (kb_col + 1) % n; sound_play(SND_MOVE); }
    if (pad_pressed & B_LEFT)  { kb_col = (kb_col + n - 1) % n; sound_play(SND_MOVE); }
    if (pad_pressed & (B_UP | B_DOWN)) {
        kb_row = (kb_row + ((pad_pressed & B_DOWN) ? 1 : KB_ROWS - 1)) % KB_ROWS;
        int m = kb_row < 4 ? (int) strlen(kb_rows[kb_row]) : 4;
        if (kb_col >= m) kb_col = m - 1;
        sound_play(SND_MOVE);
    }
    char *txt = kb_purpose ? rip_name : query;
    size_t cap = kb_purpose ? sizeof(rip_name) : sizeof(query);
    size_t len = strlen(txt);
    int changed = 0;
    if (pad_pressed & B_CROSS) {
        if (kb_row < 4) {
            if (len < cap - 1) {
                char c = kb_rows[kb_row][kb_col];
                /* names: Title Case as you type (searching ignores case anyway) */
                if (kb_purpose && len && txt[len - 1] != ' ' && c >= 'A' && c <= 'Z') c += 32;
                txt[len] = c; txt[len + 1] = 0; changed = 1;
            }
        }
        else if (kb_col == 0) { if (len && len < cap - 1) { txt[len] = ' '; txt[len + 1] = 0; changed = 1; } }
        else if (kb_col == 1) { if (len) { txt[len - 1] = 0; changed = 1; } }
        else if (kb_col == 2) { txt[0] = 0; changed = 1; }
        else { mode = 0; sound_play(SND_OK); if (kb_purpose) { rip_start(); return; } }
    }
    if (pad_pressed & B_SQUARE) { if (len) { txt[len - 1] = 0; changed = 1; } }     /* quick delete */
    if (pad_pressed & (B_CIRCLE | B_START | B_L3)) { mode = 0; sound_play(SND_BACK); }
    if (changed) {
        sound_play(SND_MOVE);
        if (!kb_purpose) { sel[plat] = 0; top_row[plat] = 0; view_build(); }
    }
}

static void fact(float x, float y, const char *label, const char *value, float max_w)
{
    gfx_text(x, y, 17, 0xffffff8c, label);
    gfx_text_fit(x, y + 30, 30, 0xffffffff, value, max_w);
}

static int info_lines_visible = 1, info_lines_total = 0;

static void draw_info(void)
{
    const Game *g = cur_game();
    if (!g) { mode = 0; return; }
    const GameInfo *gi = info_get(g);
    char buf[200];

    /* the game's own background picture (PS3 folder games have PS3_GAME/PIC1.PNG) */
    if (g->format == FMT_FOLDER) {
        char pic[460];
        snprintf(pic, sizeof(pic), "%s/PS3_GAME/PIC1.PNG", g->path);
        const Texture *bd = backdrop_get(pic);
        if (bd) {
            gfx_image(bd, 0, 0, SCREEN_W, SCREEN_H, 0xffffffff);
            gfx_rect(0, 0, SCREEN_W, SCREEN_H, gfx_theme_col(3, 0xa8));     /* darken so text stays readable */
        }
    }

    /* breadcrumb */
    float bx = gfx_text(MARGIN_X, 56, 26, 0xffffff90, plat_short(g->plat));
    bx = gfx_text(bx + 14, 56, 26, 0xffffff60, "\xBB");
    gfx_text_fit(bx + 14, 56, 26, 0xffffffff, g->title, 1300);

    /* cover, never stretched, inside a 560 x 600 box; install location under it */
    float box_w = 560, box_h = 600, cw, ch;
    float asp = plat_aspect[g->plat];
    const Texture *t = covers_get(g);
    if (t) asp = (float) t->w / (float) t->h;
    if (box_w / asp <= box_h) { cw = box_w; ch = box_w / asp; } else { ch = box_h; cw = box_h * asp; }
    float cx = MARGIN_X + 20, cy = 150;
    gfx_rect(cx + 10, cy + 16, cw, ch, 0x00000080);
    if (t) gfx_image(t, cx, cy, cw, ch, 0xffffffff);
    else   draw_placeholder(g, cx, cy, cw, ch);
    gfx_frame(cx, cy, cw, ch, 1, 0xffffff40);
    gfx_text(cx, cy + ch + 36, 17, 0xffffff8c, "INSTALL LOCATION");
    gfx_text_wrap(cx, cy + ch + 64, 20, 0xffffffb0, g->path, box_w, 2);

    /* text column */
    float x = MARGIN_X + 20 + box_w + 80, w = SCREEN_W - MARGIN_X - x, y = 150;
    float chip = gfx_text_w(17, plat_name(g->plat)) + 32;
    gfx_rect(x, y, chip, 38, 0xffffff24);
    gfx_frame(x, y, chip, 38, 1, 0xffffff40);
    gfx_text(x + 16, y + 8, 17, 0xffffffff, plat_name(g->plat));
    y = gfx_text_wrap(x, y + 60, 58, 0xffffffff, g->title, w, 2);

    {
        char d[24] = "";
        if (g->ndiscs) snprintf(d, sizeof(d), "   \xB7   %d discs", g->ndiscs);
        snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s", g->region[0] ? g->region : "", g->region[0] ? "   \xB7   " : "", format_name(g->format), d,
                 g->where[0] && strcmp(g->where, "HDD") ? "   \xB7   " : "", g->where[0] && strcmp(g->where, "HDD") ? g->where : "", g->fav ? "   \xB7   Favourite" : "");
    }
    gfx_text(x, y + 4, 24, 0xffffffb0, buf);

    /* facts: 3 x 2, blank when not known */
    const char *id = g->id[0] ? g->id : gi->id;
    float fy = y + 60, col = w / 3;
    fact(x,           fy,      "DEVELOPER", gi->developer[0] ? gi->developer : "-", col - 30);
    fact(x + col,     fy,      "PUBLISHER", gi->publisher[0] ? gi->publisher : "-", col - 30);
    fact(x + col * 2, fy,      "RELEASED",  gi->release[0] ? gi->release : "-", col - 30);
    fact(x,           fy + 86, "GENRE",     gi->genre[0] ? gi->genre : "-", col - 30);
    fact(x + col,     fy + 86, "PLAYERS",   gi->players[0] ? gi->players : "-", col - 30);
    fact(x + col * 2, fy + 86, "GAME ID",   id[0] ? id : "-", col - 30);

    /* synopsis panel, fills the rest of the column, scrolls with up / down */
    float sy = fy + 192, sh = FOOTER_Y - 40 - sy;
    gfx_rect(x, sy, w, sh, gfx_theme_col(3, 0xc8));
    gfx_frame(x, sy, w, sh, 1, 0xffffff1c);
    gfx_text(x + 30, sy + 22, 17, 0xffffff8c, "ABOUT THIS GAME");
    const float size = 23, lh = size * 1.18f;
    info_lines_visible = (int) ((sh - 74) / lh);
    if (info_lines_visible < 1) info_lines_visible = 1;
    if (gi->synopsis[0]) {
        info_lines_total = gfx_text_wrap_window(x + 30, sy + 58, size, 0xffffffe0, gi->synopsis, w - 80, info_scroll, info_lines_visible);
        if (info_lines_total > info_lines_visible) {     /* scroll bar */
            float th = sh - 74, a = (float) info_scroll / info_lines_total, b = (float) (info_scroll + info_lines_visible) / info_lines_total;
            if (b > 1) b = 1;
            gfx_rect(x + w - 18, sy + 58, 4, th, 0xffffff1a);
            gfx_rect(x + w - 18, sy + 58 + th * a, 4, th * (b - a), 0xffffffa0);
        }
        if (gi->source[0]) {
            snprintf(buf, sizeof(buf), "Info: %s", gi->source);
            gfx_text(x + w - 30 - gfx_text_w(16, buf), sy + 24, 16, 0xffffff60, buf);
        }
    } else {
        info_lines_total = 0;
        gfx_text_wrap(x + 30, sy + 58, 23, 0xffffff90,
                      "No description yet. Run GET_GAME_INFO.bat on your PC (in the Cover Art Tool folder) "
                      "to add the synopsis, developer, publisher and release date for your games.", w - 80, 4);
    }
}

/* ---------------------------------------------------------------- settings window */
enum { S_HEADER, S_THEME, S_BRIGHT, S_WAVE, S_FONT, S_SIZE, S_TITLES, S_SORT, S_OPEN, S_DISC, S_RESCAN, S_RESET, S_ABOUT, S_VIEW, S_SOUNDS, S_SYSINFO };
static const struct { int id; const char *label; const char *help; } set_items[] = {
    { S_HEADER, "APPEARANCE", NULL },
    { S_THEME,  "Background colour", "PlayStation colours: the PS3 XMB blue, the PlayStation logo colours, the face-button colours and the console finishes." },
    { S_BRIGHT, "Background brightness", "How bright the background is." },
    { S_WAVE,   "Background animation", "Like the PS3 home screen: the moving wave, plus sparkles and soft circles floating past." },
    { S_FONT,   "Font", "The PS3's own system fonts. Fonts your firmware doesn't have are skipped." },
    { S_SOUNDS, "Navigation sounds", "Soft clicks and chimes as you move around. Made by the dashboard itself." },
    { S_SYSINFO,"Console info in the top bar", "Free space on the internal HDD and the CPU / RSX temperatures, updated every few seconds." },
    { S_HEADER, "LIBRARY", NULL },
    { S_VIEW,   "Library view", "Grid of covers, a list, or one large cover at a time. R3 switches view from the library too." },
    { S_SIZE,   "Cover size", "Smaller covers fit more games on each row (grid view)." },
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
        case S_VIEW:   return settings.view == 0 ? "Grid" : settings.view == 1 ? "List" : "Large artwork";
        case S_SOUNDS: return settings.sounds ? "On" : "Off";
        case S_SYSINFO:return settings.sysinfo ? "Show" : "Hide";
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
        case S_VIEW:   settings.view = (settings.view + d + 3) % 3; top_row[plat] = 0; break;
        case S_SOUNDS: settings.sounds = !settings.sounds; break;
        case S_SYSINFO:settings.sysinfo = !settings.sysinfo; break;
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
    if (pad_pressed & (B_SELECT | B_CIRCLE)) { show_settings = 0; sound_play(SND_BACK); return; }
    if (pad_pressed & (B_UP | B_DOWN)) sound_play(SND_MOVE);
    if (pad_pressed & (B_LEFT | B_RIGHT | B_CROSS)) sound_play(SND_TAB);
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
    if (!show_settings && mode != 4 && mode != 7) draw_footer_band();   /* windows dim everything themselves */
    if (show_settings) {
        x = hint_pill(x, y, "UP / DOWN", "Choose");
        x = hint_pill(x, y, "LEFT / RIGHT", "Change");
        x = hint(x, y, 'o', "Close");
    } else if (mode == 7) {
        if (rip_active()) x = hint(x, y, 'o', "Stop");
        else x = hint(x, y, 'x', "OK");
    } else if (mode == 4) {
        x = hint(x, y, 'x', "Type");
        x = hint(x, y, 's', "Delete");
        x = hint(x, y, 'o', kb_purpose ? "Cancel" : "Done");
    } else if (mode == 3 || mode == 5) {
        x = hint(x, y, 'x', "Select");
        x = hint(x, y, 'o', mode == 3 ? "Not now" : "Back");
    } else if (mode == 1) {
        x = hint(x, y, 'x', "Play");
        x = hint(x, y, 'o', "Back");
        x = hint(x, y, 's', cur_game() && cur_game()->fav ? "Unfavourite" : "Favourite");
        if (info_lines_total > info_lines_visible) x = hint_pill(x, y, "UP / DOWN", "Scroll");
    } else {
        int have = cur_game() != NULL;
        if (have) {
            x = hint(x, y, 'x', "Play");
            x = hint(x, y, 't', "Info");
            x = hint(x, y, 's', cur_game()->fav ? "Unfavourite" : "Favourite");
        }
        x = hint_pill(x, y, "L1 / R1", "Tabs");
        if (!ON_HOME) {
            x = hint_pill(x, y, "L2 / R2", "A-Z");
            x = hint_pill(x, y, "L3", query[0] ? "Search*" : "Search");
            x = hint_pill(x, y, "R3", "View");
        }
        x = hint_pill(x, y, "SELECT", "Settings");
        x = hint_pill(x, y, "START", "Exit");
    }
    { const char *ver = APP_VERSION; gfx_text(SCREEN_W - MARGIN_X - gfx_text_w(18, ver), y + 8, 18, 0xffffff60, ver); }
}

/* ---------------------------------------------------------------- main */
static void leave_app(void) { sound_shutdown(); exit(0); }

static void start_disc(const Game *g, int disc_no)    /* disc_no 1..4 */
{
    char line[220];
    const char *path = disc_no <= 1 ? g->path : g->discs[disc_no - 2];
    if (g->ndiscs) snprintf(line, sizeof(line), "Starting %s (disc %d) ...  It opens by itself once the XMB has loaded - don't press any buttons.", g->title, disc_no);
    else snprintf(line, sizeof(line), "Starting %s ...  It opens by itself once the XMB has loaded - don't press any buttons.", g->title);
    sound_play(SND_OK);
    gfx_begin(); gfx_background(anim * 0.2f); draw_message("Loading game", line); gfx_end();
    log_step("launch: %s", path);
    recent_add(g);
    if (launch_path(path)) { log_step("webMAN accepted"); leave_app(); }
    log_step("webMAN not reachable");        /* webMAN mounts and starts the game after we exit */
    mode = 2;
}

static void start_game(const Game *g)
{
    if (!g) return;
    if (g->ndiscs) { pick_game = g; disc_pick = 0; prev_mode = mode; mode = 5; sound_play(SND_TAB); return; }
    start_disc(g, 1);
}

static void draw_disc_picker(void)
{
    const Game *g = pick_game;
    float w = 900, h = 200 + g->ndiscs * 76, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    int i;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, gfx_theme_col(3, 0xc0));
    gfx_rect_v(x, y, w, h, gfx_theme_col(1, 0xff), gfx_theme_col(2, 0xff));
    gfx_frame(x, y, w, h, 1, 0xffffff40);
    gfx_text(x + 50, y + 36, 20, 0xffffff8c, "WHICH DISC?");
    gfx_text_fit(x + 50, y + 70, 36, 0xffffffff, g->title, w - 100);
    for (i = 0; i < g->ndiscs; i++) {
        float by = y + 140 + i * 76;
        int on = (i == disc_pick);
        const char *p = i == 0 ? g->path : g->discs[i - 1];
        const char *base = strrchr(p, '/'); base = base ? base + 1 : p;
        char b[32]; snprintf(b, sizeof(b), "Disc %d", i + 1);
        gfx_rect(x + 40, by, w - 80, 64, on ? 0xffffff30 : 0xffffff10);
        gfx_frame(x + 40, by, w - 80, 64, on ? 2 : 1, on ? 0xffffffd0 : 0xffffff30);
        gfx_text(x + 64, by + 16, 28, 0xffffffff, b);
        gfx_text_fit(x + 220, by + 21, 20, 0xffffff90, p[0] ? base : "(missing)", w - 300);
    }
}

static int disc_can_install(void) { return disc.type == DISC_PS3 || disc.type == DISC_PS1 || disc.type == DISC_PS2; }
static int disc_choices(void) { return (disc.type == DISC_PS3 || disc.type == DISC_PS1 || disc.type == DISC_PS2) ? 2 : 1; }

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

    const char *labels[2] = { "Play", disc.type == DISC_PS3 ? "Install to HDD" : "Make ISO" };
    int i;
    for (i = 0; i < disc_choices(); i++) {
        float bx = x + 60 + i * 300, by = y + 210, bw = 270, bh = 70;
        int on = (disc_choice == i), ok = 1;
        gfx_rect(bx, by, bw, bh, on ? 0xffffff30 : 0xffffff10);
        gfx_frame(bx, by, bw, bh, on ? 2 : 1, on ? 0xffffffd0 : 0xffffff30);
        gfx_text_center(bx + bw / 2, by + 20, 28, ok ? 0xffffffff : 0xffffff50, labels[i]);
    }
    const char *note =
        disc_choice == 0 ? "Starts the disc from the XMB once the dashboard has closed." :
        disc.type == DISC_PS3 ? "webMAN copies the disc to /dev_hdd0/GAMES on the XMB and tells you when it's finished. It then appears in your PS3 library." :
        disc.type == DISC_PS1 ? "Copies the whole disc - video, music and CD audio tracks included - to PSXISO as a BIN + CUE, right here. You name it first." :
                                "Copies the disc to PS2ISO as an ISO, right here. You name it first.";
    gfx_text_wrap(x + 60, y + 310, 22, 0xffffffc0, note, w - 120, 3);
}

static void disc_action(void)
{
    char line[220], target[300], name[120];
    int ok;
    if (disc_choice == 1 && disc.type != DISC_PS3) {          /* Make ISO, in the dashboard */
        rip_name[0] = 0; kb_purpose = 1; kb_row = kb_col = 0; mode = 4;
        return;
    }
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
    if (ok) { log_step("webMAN accepted"); leave_app(); }
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
    view_build(); home_build();
}

static void rescan_library(void)
{
    int p;
    gfx_begin(); gfx_background(anim * 0.2f); gfx_text_center(SCREEN_W / 2, 500, 36, 0xffffffe0, "Looking for games..."); gfx_end();
    covers_reset();
    info_forget();
    library_scan();
    lists_load();
    sorted_za = 0;
    apply_sort();
    for (p = 0; p < PLAT_COUNT; p++) { if (sel[p] >= library[p].count) sel[p] = 0; top_row[p] = 0; }
    view_build(); home_build();
    log_step("rescan: PS1 %d, PS2 %d, PS3 %d", library[0].count, library[1].count, library[2].count);
}

/* ---------------------------------------------------------------- Make ISO */
static u64 rip_t0 = 0;
static u64 now_ms(void) { u64 sec = 0, ns = 0; sysGetCurrentTime(&sec, &ns); return sec * 1000 + ns / 1000000; }

static void rip_start(void)
{
    gfx_begin(); gfx_background(anim * 0.2f); draw_message("Getting the disc ready", "Making sure nothing else is using the drive..."); gfx_end();
    /* a mounted game image would be read instead of the real disc - make sure it's gone */
    int tries, m = 1;
    for (tries = 0; tries < 5; tries++) {
        unmount_disc();
        sleep(2);
        m = disc_image_mounted();
        log_step("rip: game image still mounted = %d", m);
        if (m <= 0) break;
    }
    if (m == 1) {
        gfx_begin(); gfx_background(anim * 0.2f);
        draw_message("Couldn't free the drive", "A game image is still mounted and would be copied instead of your disc. Restart the console and try again.");
        gfx_end();
        sleep(4);
        mode = 0;
        return;
    }
    if (!rip_begin(disc.type == DISC_PS1 ? RIP_PS1 : RIP_PS2, rip_name, disc.id)) { mode = 7; return; }
    rip_t0 = now_ms();
    mode = 7;
}

static void draw_rip(void)
{
    char b[200];
    float w = 1100, h = 420, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 20;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, gfx_theme_col(3, 0xc8));
    gfx_rect_v(x, y, w, h, gfx_theme_col(1, 0xff), gfx_theme_col(2, 0xff));
    gfx_frame(x, y, w, h, 1, 0xffffff40);
    gfx_rect(x, y, w, 4, gfx_accent(0xff));
    if (rip_active()) {
        float p = rip_progress();
        u64 ms = now_ms() - rip_t0;
        double done = rip_bytes_done() / 1048576.0, total = rip_bytes_total() / 1048576.0;
        double speed = ms > 500 ? done / (ms / 1000.0) : 0;
        gfx_text(x + 50, y + 40, 20, 0xffffff8c, "MAKING ISO");
        gfx_text_fit(x + 50, y + 74, 40, 0xffffffff, rip_message(), w - 100);
        gfx_rect(x + 50, y + 170, w - 100, 30, 0x00000070);
        gfx_rect(x + 50, y + 170, (w - 100) * p, 30, gfx_accent(0xe0));
        gfx_frame(x + 50, y + 170, w - 100, 30, 1, 0xffffff40);
        snprintf(b, sizeof(b), "%d%%   \xB7   %.0f of %.0f MB", (int) (p * 100), done, total);
        gfx_text(x + 50, y + 224, 26, 0xffffffe0, b);
        if (speed > 0.05) {
            int left = (int) ((total - done) / speed);
            snprintf(b, sizeof(b), "%.1f MB/s   \xB7   about %d:%02d left", speed, left / 60, left % 60);
            gfx_text(x + w - 50 - gfx_text_w(22, b), y + 228, 22, 0xffffffa0, b);
        }
        gfx_text_wrap(x + 50, y + 300, 21, 0xffffff90, "Leave the disc in and don't turn the console off. Press CIRCLE to stop - the unfinished file is removed.", w - 100, 2);
    } else if (rip_done()) {
        gfx_text(x + 50, y + 40, 20, 0xffffff8c, "FINISHED");
        gfx_text_fit(x + 50, y + 74, 40, 0xffffffff, rip_message(), w - 100);
        gfx_text_wrap(x + 50, y + 150, 22, 0xffffffd0, rip_output(), w - 100, 2);
        gfx_text_wrap(x + 50, y + 230, 22, 0xffffffb0, "It's now in your library. Press X to go to it. Run the PC tools again to add its cover and game info.", w - 100, 3);
    } else {
        gfx_text(x + 50, y + 40, 20, 0xffffff8c, "COULDN'T MAKE THE ISO");
        gfx_text_wrap(x + 50, y + 80, 26, 0xffffffff, rip_message(), w - 100, 3);
        gfx_text_wrap(x + 50, y + 230, 21, 0xffffffa0,
                      "Nothing was left half-written on the HDD. Clean the disc and try again - the log has the details.", w - 100, 3);
    }
}

static void rescan_library(void);

static void rip_input(void)
{
    if (rip_active()) {
        if (pad_pressed & B_CIRCLE) { rip_cancel(); sound_play(SND_BACK); return; }
        rip_step();
        if (disc.type == DISC_PS2 && rip_active()) rip_step();     /* DVDs read faster */
        if (rip_done()) sound_play(SND_STAR);
        return;
    }
    if (pad_pressed & (B_CROSS | B_CIRCLE)) {
        if (rip_done()) {                                         /* show the new game */
            const char *out = rip_output();
            rescan_library();
            int t, i;
            for (t = 0; t < PLAT_COUNT; t++)
                for (i = 0; i < library[t].count; i++)
                    if (!strcmp(library[t].games[i].path, out)) {
                        set_tab(t + 1);
                        for (i = 0; i < vn; i++) if (!strcmp(vlist[i]->path, out)) sel[plat] = i;
                        t = PLAT_COUNT; break;
                    }
        }
        mode = 0;
        sound_play(SND_OK);
    }
}

/* moving the selection on a console tab, for the current view */
static void browse_move(void)
{
    if (!vn) return;
    int s = sel[plat], was = s;
    if (settings.view == 1) {                                    /* list */
        if (pad_pressed & B_DOWN) s++;
        if (pad_pressed & B_UP) s--;
        if (pad_pressed & B_RIGHT) s += LIST_ROWS;
        if (pad_pressed & B_LEFT) s -= LIST_ROWS;
    } else if (settings.view == 2) {                             /* large artwork */
        if (pad_pressed & B_RIGHT) s++;
        if (pad_pressed & B_LEFT) s--;
        if (pad_pressed & B_DOWN) s += 5;
        if (pad_pressed & B_UP) s -= 5;
    } else {                                                     /* grid */
        if (pad_pressed & B_RIGHT) s++;
        if (pad_pressed & B_LEFT) s--;
        if (pad_pressed & B_DOWN) { if (s + COLS < vn) s += COLS; else if (s / COLS < (vn - 1) / COLS) s = vn - 1; }
        if (pad_pressed & B_UP) { if (s - COLS >= 0) s -= COLS; }
    }
    if (pad_pressed & B_R2) s = letter_jump(s, 1);
    if (pad_pressed & B_L2) s = letter_jump(s, -1);
    if (s < 0) s = 0;
    if (s >= vn) s = vn - 1;
    if (s != was) sound_play(SND_MOVE);
    sel[plat] = s;
}

static void home_move(void)
{
    int r = home_row;
    if ((pad_pressed & B_DOWN) && home_row == 0) { home_row = 1; sound_play(SND_MOVE); }
    if ((pad_pressed & B_UP) && home_row == 1) { home_row = 0; sound_play(SND_MOVE); }
    r = home_row;
    if ((pad_pressed & B_RIGHT) && home_sel[r] + 1 < shelf_n[r]) { home_sel[r]++; sound_play(SND_MOVE); }
    if ((pad_pressed & B_LEFT) && home_sel[r] > 0) { home_sel[r]--; sound_play(SND_MOVE); }
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
    lists_load();
    apply_sort();
    for (i = 0; i < PLAT_COUNT; i++) { sel[i] = 0; top_row[i] = 0; }
    /* open on the chosen console; if it has no games, the first one that does */
    plat = settings.open_on ? settings.open_on - 1 : settings.last_plat;
    if (!library[plat].count)
        for (i = 0; i < PLAT_COUNT; i++) if (library[i].count) { plat = i; break; }
    set_tab(plat + 1);

    int frames = 0;
    while (1) {
        if (frames == 1) log_step("main loop running");
        if (++frames % 600 == 0) log_step("still running, frame %d", frames);
        read_pads();
        covers_frame();
        if (settings.sysinfo) sysinfo_poll();
        anim += 0.06f;

        if (mode != 3 && mode != 4 && mode != 5 && mode != 7 && !show_settings && disc_poll(&disc) && settings.disc_popup) {
            prev_mode = (mode == 2) ? 0 : mode; mode = 3; disc_choice = 0; sound_play(SND_TAB);
        }

        Game *g = cur_game();
        if (mode == 3) {
            if ((pad_pressed & B_LEFT) && disc_choice > 0) { disc_choice--; sound_play(SND_MOVE); }
            if ((pad_pressed & B_RIGHT) && disc_choice + 1 < disc_choices()) { disc_choice++; sound_play(SND_MOVE); }
            if (pad_pressed & B_CIRCLE) { mode = prev_mode; sound_play(SND_BACK); }
            else if (pad_pressed & B_CROSS) disc_action();
        } else if (mode == 7) {
            rip_input();
        } else if (mode == 4) {
            keyboard_input();
        } else if (mode == 5) {
            if ((pad_pressed & B_DOWN) && disc_pick + 1 < pick_game->ndiscs) { disc_pick++; sound_play(SND_MOVE); }
            if ((pad_pressed & B_UP) && disc_pick > 0) { disc_pick--; sound_play(SND_MOVE); }
            if (pad_pressed & B_CIRCLE) { mode = prev_mode; sound_play(SND_BACK); }
            else if (pad_pressed & B_CROSS) {
                const char *p = disc_pick == 0 ? pick_game->path : pick_game->discs[disc_pick - 1];
                if (p[0]) start_disc(pick_game, disc_pick + 1);
            }
        } else if (show_settings) {
            settings_window_input();
        } else if (mode == 0) {
            int t = tab;
            if (pad_pressed & B_R1) t = (tab + 1) % 4;
            if (pad_pressed & B_L1) t = (tab + 3) % 4;
            if (t != tab) {
                set_tab(t);
                sound_play(SND_TAB);
                if (t > 0 && settings.open_on == 0) { settings.last_plat = plat; settings_save(); }
            }
            if (pad_pressed & B_SELECT) { show_settings = 1; if (!set_selectable(set_row)) set_row = 1; sound_play(SND_TAB); }
            if (ON_HOME) home_move(); else {
                browse_move();
                if (pad_pressed & B_L3) { mode = 4; kb_purpose = 0; kb_row = kb_col = 0; sound_play(SND_TAB); }
                if (pad_pressed & B_R3) { settings.view = (settings.view + 1) % 3; top_row[plat] = 0; settings_save(); sound_play(SND_TAB); }
            }
            g = cur_game();
            if (g) {
                if (pad_pressed & B_SQUARE) { fav_toggle(g); sound_play(SND_STAR); if (ON_HOME) home_build(); }
                if (pad_pressed & B_TRIANGLE) { mode = 1; info_scroll = 0; sound_play(SND_OK); }
                else if (pad_pressed & B_CROSS) start_game(g);
            }
            if (pad_pressed & B_START) break;
        } else if (mode == 1) {
            if ((pad_pressed & B_DOWN) && info_scroll + info_lines_visible < info_lines_total) info_scroll++;
            if ((pad_pressed & B_UP) && info_scroll > 0) info_scroll--;
            if (g && (pad_pressed & B_SQUARE)) { fav_toggle(g); sound_play(SND_STAR); }
            if (pad_pressed & (B_CIRCLE | B_TRIANGLE)) { mode = 0; sound_play(SND_BACK); if (ON_HOME) home_build(); }
            else if (g && (pad_pressed & B_CROSS)) start_game(g);
        } else {
            if (pad_pressed & (B_CIRCLE | B_CROSS)) mode = 0;
        }

        gfx_begin();
        gfx_background(anim * 0.2f);
        int under = (mode == 3 || mode == 5) ? prev_mode : mode;
        if (under == 1) draw_info();
        else draw_browse();
        if (mode == 3) draw_disc_popup();
        if (mode == 4) draw_keyboard();
        if (mode == 5) draw_disc_picker();
        if (mode == 7) draw_rip();
        if (show_settings) draw_settings_window();
        draw_hints();
        if (mode == 2) draw_message("webMAN MOD isn't responding",
                                    "The dashboard starts games through webMAN MOD. Check it is installed and enabled, then try again.  (Circle to close)");
        gfx_end();
    }
    sound_shutdown();
    return 0;
}
