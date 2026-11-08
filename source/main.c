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

/* ---------------------------------------------------------------- input */
enum {
    B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_CROSS = 16, B_CIRCLE = 32,
    B_TRIANGLE = 64, B_SQUARE = 128, B_L1 = 256, B_R1 = 512, B_START = 1024
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
#define COLS       7
#define CELL_W     ((SCREEN_W - 2 * MARGIN_X) / COLS)
#define COVER_H    204.0f
#define ROW_H      292.0f
#define VIS_ROWS   3

static const float plat_aspect[PLAT_COUNT] = { 1.0f, 0.70f, 0.86f };   /* PS1 jewel, PS2 DVD, PS3 Blu-ray */
static const u32   plat_hue[PLAT_COUNT][2] = { { 0x545862ff, 0x1c1d22ff }, { 0x1d3fb8ff, 0x070d33ff }, { 0x46566eff, 0x10161fff } };

static int plat = PLAT_PS1;
static int sel[PLAT_COUNT];
static int top_row[PLAT_COUNT];
static int mode = 0;            /* 0 grid, 1 info panel, 2 webMAN error */
static float anim = 0.0f;       /* selection pulse */

static void draw_placeholder(const Game *g, float x, float y, float w, float h)
{
    gfx_rect_v(x, y, w, h, plat_hue[g->plat][0], plat_hue[g->plat][1]);
    gfx_rect(x, y, w, h * 0.09f, 0xffffffe0);
    gfx_text(x + 8, y + h * 0.015f, h * 0.06f, 0x111827ff, plat_short(g->plat));
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
            gfx_frame(x, y, w, h, i * 5.0f, RGBA(120, 180, 255, (int) ((10 + 10 * p) * (7 - i) / 3)));
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
        int on = p == plat;
        float size = on ? 46 : 32;
        float w = gfx_text_w(size, labels[p]);
        gfx_text(x, on ? 52 : 64, size, on ? 0xffffffff : 0xffffff90, labels[p]);
        if (on) gfx_rect(x, 112, w, 4, 0x9cc8ffff);
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
    gfx_text_fit(MARGIN_X + 1000, 196, 24, 0xffffffc0, cur->title, SCREEN_W - MARGIN_X * 2 - 1000);

    float bh = COVER_H, bw = COVER_H * plat_aspect[plat];
    int first = top_row[plat] * COLS, last = first + COLS * VIS_ROWS;
    if (last > l->count) last = l->count;

    for (i = first; i < last; i++) {
        int r = i / COLS - top_row[plat], c = i % COLS;
        float cx = MARGIN_X + c * CELL_W + (CELL_W - bw) / 2;
        float cy = GRID_Y + r * ROW_H;
        if (cy > SCREEN_H - 120) break;
        if (i == sel[plat]) continue;               /* selected drawn last, on top */
        draw_cover(&l->games[i], cx, cy, bw, bh, 0);
        gfx_text_fit(cx, cy + bh + 16, 19, 0xffffffb0, l->games[i].title, bw + 30);
    }
    {
        int r = sel[plat] / COLS - top_row[plat], c = sel[plat] % COLS;
        float cx = MARGIN_X + c * CELL_W + (CELL_W - bw) / 2, cy = GRID_Y + r * ROW_H;
        draw_cover(cur, cx, cy, bw, bh, 1);
        gfx_text_fit(cx, cy + bh + 16, 21, 0xffffffff, cur->title, bw + 40);
    }
    /* preload the next row so scrolling shows covers straight away */
    for (i = last; i < last + COLS && i < l->count; i++) covers_get(&l->games[i]);
}

static void draw_info(void)
{
    GameList *l = &library[plat];
    if (l->count == 0) { mode = 0; return; }
    const Game *g = &l->games[sel[plat]];
    char buf[160];

    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x020a22c8);
    gfx_rect_v(200, 150, 1520, 760, 0x143078f8, 0x061440f8);
    gfx_frame(200, 150, 1520, 760, 1, 0xffffff40);

    float ch = 520, cw = ch * plat_aspect[g->plat];
    draw_cover(g, 260, 230, cw, ch, 0);

    float x = 260 + cw + 70, y = 230;
    gfx_rect(x, y, gfx_text_w(18, plat_name(g->plat)) + 28, 34, 0xffffff26);
    gfx_text(x + 14, y + 7, 18, 0xffffffff, plat_name(g->plat));
    gfx_text_wrap(x, y + 60, 56, 0xffffffff, g->title, 1660 - x, 2);

    y = 520;
    gfx_text(x, y, 18, 0xffffff90, "FORMAT");          gfx_text(x, y + 26, 26, 0xffffffff, format_name(g->format));
    gfx_text(x + 320, y, 18, 0xffffff90, "GAME ID");   gfx_text(x + 320, y + 26, 26, 0xffffffff, g->id[0] ? g->id : "-");
    gfx_text(x + 640, y, 18, 0xffffff90, "REGION");    gfx_text(x + 640, y + 26, 26, 0xffffffff, "-");
    gfx_text(x, y + 100, 18, 0xffffff90, "INSTALL LOCATION");
    gfx_text_fit(x, y + 126, 22, 0xffffffd0, g->path, 1660 - x);
    gfx_text(x, y + 200, 18, 0xffffff90, "COVER");
    gfx_text_fit(x, y + 226, 22, 0xffffffd0, g->cover[0] ? g->cover : "No cover found yet - run the Cover Art Tool on your PC", 1660 - x);

    snprintf(buf, sizeof(buf), "More game information (description, developer, year) arrives in a later stage.");
    gfx_text(x, 840, 18, 0xffffff70, buf);

    float hx = 260;
    hx = hint(hx, 850, 'x', "Play");
    hint(hx, 850, 'o', "Back");
}

static void draw_message(const char *title, const char *line)
{
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, 0x020a22d0);
    gfx_rect_v(460, 380, 1000, 300, 0x143078f8, 0x061440f8);
    gfx_frame(460, 380, 1000, 300, 1, 0xffffff40);
    gfx_text_center(SCREEN_W / 2, 440, 40, 0xffffffff, title);
    gfx_text_wrap(520, 510, 24, 0xffffffd0, line, 880, 4);
}

static void draw_hints(void)
{
    float x = MARGIN_X, y = 1000;
    gfx_rect(MARGIN_X, 984, SCREEN_W - 2 * MARGIN_X, 1, 0xffffff24);
    x = hint(x, y, 'x', "Play");
    x = hint(x, y, 't', "Game info");
    x = hint_pill(x, y, "L1 / R1", "Switch console");
    x = hint_pill(x, y, "START", "Exit");
    gfx_text(SCREEN_W - MARGIN_X - gfx_text_w(18, "v0.3"), y + 8, 18, 0xffffff60, "v0.3");
}

/* ---------------------------------------------------------------- main */
static void start_game(const Game *g)
{
    char line[200];
    snprintf(line, sizeof(line), "Starting %s ...", g->title);
    gfx_begin(); gfx_background(anim * 0.2f); draw_message("Loading game", line); gfx_end();
    log_step("launch: %s", g->path);
    if (launch_game(g)) { log_step("webMAN accepted"); exit(0); }
    log_step("webMAN not reachable");        /* webMAN mounts and starts the game after we exit */
    mode = 2;
}

s32 main(s32 argc, const char *argv[])
{
    int i;
    log_reset();
    log_step("start");
    gfx_init();
    ioPadInit(7);
    log_step("pads ok");

    /* diagnostic start-up: each piece on its own, logged before and after */
    {
        static const char *names[] = { "clear only", "gradient", "gradient+glow", "gradient+glow+lines", "full + text" };
        static const int parts[] = { 0, 1, 3, 7, 7 };
        int st, k;
        for (st = 0; st < 5; st++) {
            log_step("stage %d (%s) drawing", st + 1, names[st]);
            for (k = 0; k < 40; k++) {          /* ~2/3 second each */
                gfx_begin();
                if (parts[st]) gfx_background_parts(0, parts[st]);
                if (st == 4) gfx_text_center(SCREEN_W / 2, 500, 36, 0xffffffe0, "Loading your library...");
                gfx_end();
                if (k == 0) log_step("stage %d first flip done", st + 1);
            }
            log_step("stage %d ok", st + 1);
        }
    }
    log_step("first frame drawn");

    covers_init();
    log_step("covers ok");
    library_scan();
    log_step("library: PS1 %d, PS2 %d, PS3 %d", library[0].count, library[1].count, library[2].count);
    for (i = 0; i < PLAT_COUNT; i++) { sel[i] = 0; top_row[i] = 0; }
    /* open on the first console that has games */
    for (i = 0; i < PLAT_COUNT; i++) if (library[i].count) { plat = i; break; }

    int frames = 0;
    while (1) {
        if (frames == 1) log_step("main loop running");
        if (++frames % 600 == 0) log_step("still running, frame %d", frames);
        read_pads();
        covers_frame();
        anim += 0.06f;

        GameList *l = &library[plat];
        if (mode == 0) {
            if (pad_pressed & B_R1) plat = (plat + 1) % PLAT_COUNT;
            if (pad_pressed & B_L1) plat = (plat + PLAT_COUNT - 1) % PLAT_COUNT;
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
        draw_nav();
        draw_grid();
        draw_hints();
        if (mode == 1) draw_info();
        if (mode == 2) draw_message("webMAN MOD isn't responding",
                                    "The dashboard starts games through webMAN MOD. Check it is installed and enabled, then try again.  (Circle to close)");
        gfx_end();
    }
    return 0;
}
