/* gfx.c - drawing helpers on a virtual 1920x1080 screen */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <tiny3d.h>
#include <libfont.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "gfx.h"
#include "dejavusans_ttf_bin.h"
#include "log.h"

/* Tiny3D calls this (weak symbol) when something goes wrong inside it */
void tiny3d_alarm(int leds)
{
    static int n = 0;
    if (n++ < 20) log_step("TINY3D ALARM %d", leds);
}

static u8 *tex_mem_ptr;     /* next free byte of RSX texture memory */
static u8 *tex_mem_end;

/* ------------------------------------------------------------------ fonts */
static FT_Library ft_lib;
static FT_Face    ft_face;

#define FONT_CELL 64
static short adv[256];        /* advance of each char in FONT_CELL units (same numbers libfont uses) */
static int   font_ok = 0;
int gfx_font_choice = 0;      /* font to load in gfx_init (set from settings before it) */

static void ttf_to_bitmap(u8 chr, u8 *bitmap, short *w, short *h, short *y_correction)
{
    FT_Set_Pixel_Sizes(ft_face, (*w), (*h));
    FT_GlyphSlot slot = ft_face->glyph;
    memset(bitmap, 0, (*w) * (*h));
    if (FT_Load_Char(ft_face, (unsigned char) chr, FT_LOAD_RENDER)) { (*w) = 0; adv[chr] = 0; return; }

    int n, m, ww = 0;
    *y_correction = (*h) - 1 - slot->bitmap_top;
    for (n = 0; n < (int) slot->bitmap.rows; n++) {
        for (m = 0; m < (int) slot->bitmap.width; m++) {
            if (m >= (*w) || n >= (*h)) continue;
            bitmap[m] = (u8) slot->bitmap.buffer[ww + m];
        }
        bitmap += *w;
        ww += slot->bitmap.width;
    }
    *w = ((slot->advance.x + 31) >> 6) + ((slot->bitmap_left < 0) ? -slot->bitmap_left : 0);
    *h = slot->bitmap.rows;
    adv[chr] = *w;
}

/* Fonts: the PS3's own system fonts (in flash, so nothing extra to install) plus the
   bundled DejaVu. Each choice lists the file names used across firmware versions. */
typedef struct { const char *name; const char *files[3]; } FontChoice;
static const FontChoice fonts[] = {
    { "PS3 System (Seurat)",  { "SCE-PS3-SR-R-LATIN2.TTF", "SCE-PS3-SR-R-LATIN.TTF", NULL } },
    { "Seurat Bold",          { "SCE-PS3-SR-B-LATIN2.TTF", "SCE-PS3-SR-B-LATIN.TTF", NULL } },
    { "Rodin",                { "SCE-PS3-RD-R-LATIN2.TTF", "SCE-PS3-RD-R-LATIN.TTF", NULL } },
    { "Rodin Light",          { "SCE-PS3-RD-L-LATIN2.TTF", "SCE-PS3-RD-L-LATIN.TTF", NULL } },
    { "Rodin Bold",           { "SCE-PS3-RD-B-LATIN2.TTF", "SCE-PS3-RD-B-LATIN.TTF", NULL } },
    { "New Rodin",            { "SCE-PS3-NR-R-JPN.TTF", "SCE-PS3-NR-R-EXT.TTF", NULL } },
    { "Matisse (serif)",      { "SCE-PS3-MT-R-LATIN2.TTF", "SCE-PS3-MT-R-LATIN.TTF", NULL } },
    { "VAG Rounded",          { "SCE-PS3-VR-R-LATIN2.TTF", "SCE-PS3-VR-R-LATIN.TTF", NULL } },
    { "DejaVu Sans (built in)", { NULL } },
};
#define FONT_COUNT ((int) (sizeof(fonts) / sizeof(fonts[0])))
#define FONT_BUILTIN (FONT_COUNT - 1)
static int font_cur = -1;
static signed char font_avail[FONT_COUNT];    /* 0 unknown, 1 yes, -1 no */
static u8 *font_mem = NULL;

int         gfx_font_count(void) { return FONT_COUNT; }
const char *gfx_font_name(int i) { return fonts[i].name; }
int         gfx_font_current(void) { return font_cur; }

/* opens choice i into ft_face; returns 1 if it worked */
static int open_font(int i)
{
    char path[160];
    int k;
    if (i == FONT_BUILTIN)
        return FT_New_Memory_Face(ft_lib, (const FT_Byte *) dejavusans_ttf_bin, dejavusans_ttf_bin_size, 0, &ft_face) == 0;
    for (k = 0; fonts[i].files[k]; k++) {
        snprintf(path, sizeof(path), "/dev_flash/data/font/%s", fonts[i].files[k]);
        if (FT_New_Face(ft_lib, path, 0, &ft_face) == 0) return 1;
    }
    return 0;
}

int gfx_font_available(int i)
{
    if (i < 0 || i >= FONT_COUNT) return 0;
    if (!font_avail[i]) {
        FT_Init_FreeType(&ft_lib);
        font_avail[i] = open_font(i) ? 1 : -1;
        if (font_avail[i] > 0) FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_lib);
    }
    return font_avail[i] > 0;
}

/* (re)builds the glyph texture in the same memory every time, so it can change at any moment */
int gfx_font_set(int want)
{
    int i, order[FONT_COUNT + 1], n = 0, got = -1;
    if (want < 0 || want >= FONT_COUNT) want = 0;
    if (want == font_cur && font_ok) return font_cur;
    order[n++] = want;
    for (i = 0; i < FONT_COUNT; i++) if (i != want) order[n++] = i;   /* fallbacks: system font first, DejaVu last */

    FT_Init_FreeType(&ft_lib);
    for (i = 0; i < n; i++)
        if (open_font(order[i])) { got = order[i]; break; }
    if (got >= 0) {
        ResetFont();
        memset(adv, 0, sizeof(adv));
        u8 *end = AddFontFromTTF(font_mem, 32, 255, FONT_CELL, FONT_CELL, ttf_to_bitmap);
        FT_Done_Face(ft_face);
        if (tex_mem_ptr == font_mem) tex_mem_ptr = end;   /* first load reserves the space */
        font_ok = 1;
        font_cur = got;
        font_avail[got] = 1;
        log_step("font: %s", fonts[got].name);
    }
    FT_Done_FreeType(ft_lib);
    return font_cur;
}

/* ------------------------------------------------------------------ setup */
void gfx_init(void)
{
    tiny3d_Init(4 * 1024 * 1024);
    log_step("tiny3d_Init ok, video %dx%d", Video_Resolution.width, Video_Resolution.height);

    /* plain blue frame straight away (shows the app started) */
    tiny3d_Clear(0xff0d3591, TINY3D_CLEAR_ALL);
    tiny3d_Flip();

    u32 size = 72 * 1024 * 1024;   /* font + cover textures */
    u8 *mem = (u8 *) tiny3d_AllocTexture(size);
    if (!mem) { log_step("texture alloc FAILED"); exit(0); }
    log_step("texture memory ok");
    tex_mem_ptr = mem;
    tex_mem_end = mem + size;

    font_mem = tex_mem_ptr;              /* every font is 224 glyphs of 64x64, so all fit the same space */
    gfx_font_set(gfx_font_choice);
    log_step("font %s", font_ok ? "ok" : "FAILED");

    /* virtual 1920x1080 screen at any output resolution */
    float sx = (float) Video_Resolution.width, sy = (float) Video_Resolution.height;
    tiny3d_UserViewport(1, 0.0f, 0.0f, sx / SCREEN_W, sy / SCREEN_H, sx / 1920.0f, sy / 1080.0f);
    SetFontScreenLimits(SCREEN_W, SCREEN_H);
    /* text at the same depth as every shape: Tiny3D depth-tests with LESS-OR-EQUAL, so text
       at z 0 used to "win" against panels drawn over it later and showed through them */
    SetFontZ(1000.0f);
}

u32 *gfx_tex_alloc(u32 bytes)
{
    bytes = (bytes + 15) & ~15;
    if (tex_mem_ptr + bytes > tex_mem_end) return NULL;
    u32 *p = (u32 *) tex_mem_ptr;
    tex_mem_ptr += bytes;
    return p;
}

void gfx_begin(void)
{
    tiny3d_Clear(0xff020a22, TINY3D_CLEAR_ALL);
    tiny3d_AlphaTest(1, 0x01, TINY3D_ALPHA_FUNC_GEQUAL);
    tiny3d_BlendFunc(1, TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
                     TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_DST_ALPHA_ZERO,
                     TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD);
    tiny3d_Project2D();
}

void gfx_end(void)
{
    tiny3d_Flip();
}

/* ------------------------------------------------------------------ shapes */
void gfx_rect(float x, float y, float w, float h, u32 c)
{
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x, y, 1000);         tiny3d_VertexColor(c);
    tiny3d_VertexPos(x + w, y, 1000);     tiny3d_VertexColor(c);
    tiny3d_VertexPos(x + w, y + h, 1000); tiny3d_VertexColor(c);
    tiny3d_VertexPos(x, y + h, 1000);     tiny3d_VertexColor(c);
    tiny3d_End();
}

void gfx_rect_v(float x, float y, float w, float h, u32 top, u32 bottom)
{
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x, y, 1000);         tiny3d_VertexColor(top);
    tiny3d_VertexPos(x + w, y, 1000);     tiny3d_VertexColor(top);
    tiny3d_VertexPos(x + w, y + h, 1000); tiny3d_VertexColor(bottom);
    tiny3d_VertexPos(x, y + h, 1000);     tiny3d_VertexColor(bottom);
    tiny3d_End();
}

void gfx_frame(float x, float y, float w, float h, float t, u32 c)
{
    gfx_rect(x - t, y - t, w + 2 * t, t, c);
    gfx_rect(x - t, y + h, w + 2 * t, t, c);
    gfx_rect(x - t, y, t, h, c);
    gfx_rect(x + w, y, t, h, c);
}

void gfx_image(const Texture *t, float x, float y, float w, float h, u32 tint)
{
    float u = (float) t->w / (float) t->tex_w, v = (float) t->h / (float) t->tex_h;
    tiny3d_SetTexture(0, t->offset, t->tex_w, t->tex_h, t->tex_w * 4, TINY3D_TEX_FORMAT_A8R8G8B8, TEXTURE_LINEAR);
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x, y, 1000);         tiny3d_VertexColor(tint); tiny3d_VertexTexture(0.0f, 0.0f);
    tiny3d_VertexPos(x + w, y, 1000);     tiny3d_VertexColor(tint); tiny3d_VertexTexture(u, 0.0f);
    tiny3d_VertexPos(x + w, y + h, 1000); tiny3d_VertexColor(tint); tiny3d_VertexTexture(u, v);
    tiny3d_VertexPos(x, y + h, 1000);     tiny3d_VertexColor(tint); tiny3d_VertexTexture(0.0f, v);
    tiny3d_End();
}

void gfx_image_fit(const Texture *t, float x, float y, float w, float h, u32 tint)
{
    float s = fminf(w / (float) t->w, h / (float) t->h);
    float dw = t->w * s, dh = t->h * s;
    gfx_image(t, x + (w - dw) / 2.0f, y + (h - dh), dw, dh, tint);  /* sits on the shelf */
}

/* ------------------------------------------------------------------ background */
#define GX 24
#define GY 14
static u32 bg_col[GX + 1][GY + 1];
static int bg_ready = 0;

static u32 lerp_col(u32 a, u32 b, float t)
{
    int ar = a >> 24, ag = (a >> 16) & 255, ab = (a >> 8) & 255;
    int br = b >> 24, bg = (b >> 16) & 255, bb = (b >> 8) & 255;
    return RGBA(ar + (br - ar) * t, ag + (bg - ag) * t, ab + (bb - ab) * t, 255);
}

/* ------------------------------------------------------------------ themes
   PlayStation colour palette: the PS3 XMB blue, the four colours of the PlayStation logo,
   the four face-button colours, and the console finishes. Each is a base colour; the
   background is a radial gradient from it (bright centre, deep edges). */
typedef struct { const char *name; u32 c[4]; u32 accent; } Theme;
#define T4(r, g, b) { RGBA(r, g, b, 255), RGBA((r) * 0.48f, (g) * 0.48f, (b) * 0.48f, 255), \
                      RGBA((r) * 0.22f, (g) * 0.22f, (b) * 0.22f, 255), RGBA((r) * 0.08f, (g) * 0.08f, (b) * 0.08f, 255) }
static const Theme themes[] = {
    { "PS3 Classic Blue",      { 0x2563d9ff, 0x0d3591ff, 0x061a52ff, 0x020a22ff }, 0x9cc8ffff },
    { "PlayStation Blue",      T4(0x10, 0x4c, 0xb8), 0x8fb6ffff },
    { "PlayStation Red",       T4(0xc4, 0x14, 0x2a), 0xff9aa6ff },
    { "PlayStation Yellow",    T4(0xc8, 0x8c, 0x00), 0xffd970ff },
    { "PlayStation Green",     T4(0x00, 0x96, 0x58), 0x86f0c0ff },
    { "Triangle Teal",         T4(0x22, 0xa8, 0x92), 0x9ef3e2ff },
    { "Circle Red",            T4(0xd0, 0x3c, 0x4c), 0xffa8b2ff },
    { "Cross Blue",            T4(0x4a, 0x7c, 0xd4), 0xb4d0ffff },
    { "Square Pink",           T4(0xb4, 0x4c, 0xb0), 0xffb4f8ff },
    { "PS1 Grey",              T4(0x86, 0x88, 0x92), 0xe0e2ecff },
    { "PS2 Midnight",          T4(0x22, 0x2c, 0x6a), 0xa8b4ffff },
    { "PS3 Piano Black",       T4(0x3a, 0x3a, 0x40), 0xd0d0d8ff },
};
#define THEME_COUNT ((int) (sizeof(themes) / sizeof(themes[0])))
static int theme = 0, wave_on = 1;
static float bright = 1.0f;

int         gfx_theme_count(void) { return THEME_COUNT; }
const char *gfx_theme_name(int i) { return themes[i].name; }
u32         gfx_theme_swatch(int i) { return themes[i].c[0]; }

static u32 scale_col(u32 c, float k, u32 a)
{
    float r = (c >> 24) * k, g = ((c >> 16) & 255) * k, b = ((c >> 8) & 255) * k;
    return RGBA(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b, a);
}

void gfx_theme_set(int t, int brightness, int wave)
{
    static const float steps[5] = { 0.55f, 0.75f, 1.0f, 1.18f, 1.35f };
    theme = (t >= 0 && t < THEME_COUNT) ? t : 0;
    bright = steps[(brightness >= 0 && brightness < 5) ? brightness : 2];
    wave_on = wave;
    bg_ready = 0;
}

/* level 0 = gradient centre ... 3 = darkest edge; used for panels and the footer */
/* background style: 0 PS3 XMB (colour setting applies), 1 PS1 inspired, 2 PS2 inspired */
static int bg_style = 0;
static const u32 ps1_pal[4] = { 0x5a5c66ff, 0x34353cff, 0x1c1d22ff, 0x0b0b0eff };
static const u32 ps2_pal[4] = { 0x1a3a8cff, 0x0b1c4dff, 0x050c24ff, 0x01030bff };
void gfx_bg_style(int st) { if (st != bg_style) { bg_style = st; bg_ready = 0; } }

u32 gfx_theme_col(int level, u32 alpha)
{
    if (bg_style == 1) return scale_col(ps1_pal[level & 3], 1.0f, alpha);
    if (bg_style == 2) return scale_col(ps2_pal[level & 3], 1.0f, alpha);
    return scale_col(themes[theme].c[level & 3], level == 3 ? 1.0f : bright, alpha);
}
u32 gfx_accent(u32 alpha)
{
    if (bg_style == 1) return 0xd8dce6ff & 0xffffff00 | alpha;
    if (bg_style == 2) return 0x7fd4ffff & 0xffffff00 | alpha;
    return (themes[theme].accent & 0xffffff00) | alpha;
}

static void bg_prepare(void)
{
    const u32 *pal = bg_style == 1 ? ps1_pal : bg_style == 2 ? ps2_pal : themes[theme].c;
    u32 c0 = scale_col(pal[0], bright, 255), c1 = scale_col(pal[1], bright, 255),
        c2 = scale_col(pal[2], bright, 255), c3 = scale_col(pal[3], bright, 255);
    int i, j;
    for (i = 0; i <= GX; i++)
        for (j = 0; j <= GY; j++) {
            float x = SCREEN_W * i / GX, y = SCREEN_H * j / GY;
            float dx = (x - 960.0f) / 1150.0f, dy = (y - 300.0f) / 950.0f;
            float r = sqrtf(dx * dx + dy * dy);
            u32 c;
            if (r < 0.34f) c = lerp_col(c0, c1, r / 0.34f);
            else if (r < 0.66f) c = lerp_col(c1, c2, (r - 0.34f) / 0.32f);
            else c = lerp_col(c2, c3, fminf(1.0f, (r - 0.66f) / 0.34f));
            bg_col[i][j] = c;
        }
    bg_ready = 1;
}

static float wave_y(float x, float phase, float off)
{
    return 770.0f + off + 62.0f * sinf(x * 0.0029f + phase) + 26.0f * sinf(x * 0.0012f - phase * 0.7f + off * 0.05f);
}

static void draw_particles(float phase);

void gfx_background_parts(float phase, int parts)
{
    int i, j;
    if (!bg_ready) bg_prepare();

    if (parts & 1) {            /* gradient: one small batch per row */
        for (j = 0; j < GY; j++) {
            tiny3d_SetPolygon(TINY3D_QUADS);
            for (i = 0; i < GX; i++) {
                float x0 = SCREEN_W * i / GX, x1 = SCREEN_W * (i + 1) / GX;
                float y0 = SCREEN_H * j / GY, y1 = SCREEN_H * (j + 1) / GY;
                tiny3d_VertexPos(x0, y0, 1000); tiny3d_VertexColor(bg_col[i][j]);
                tiny3d_VertexPos(x1, y0, 1000); tiny3d_VertexColor(bg_col[i + 1][j]);
                tiny3d_VertexPos(x1, y1, 1000); tiny3d_VertexColor(bg_col[i + 1][j + 1]);
                tiny3d_VertexPos(x0, y1, 1000); tiny3d_VertexColor(bg_col[i][j + 1]);
            }
            tiny3d_End();
        }
    }

    const float step = 40.0f;
    float x;
    if (parts & 2) {            /* soft glowing ribbon: one batch per segment */
        for (x = -step; x < SCREEN_W + step; x += step) {
            float ya = wave_y(x, phase, 14), yb = wave_y(x + step, phase, 14);
            u32 mid = gfx_accent(0x3a), clear = gfx_accent(0);
            tiny3d_SetPolygon(TINY3D_QUADS);
            tiny3d_VertexPos(x, ya - 46, 1000);        tiny3d_VertexColor(clear);
            tiny3d_VertexPos(x + step, yb - 46, 1000); tiny3d_VertexColor(clear);
            tiny3d_VertexPos(x + step, yb, 1000);      tiny3d_VertexColor(mid);
            tiny3d_VertexPos(x, ya, 1000);             tiny3d_VertexColor(mid);

            tiny3d_VertexPos(x, ya, 1000);             tiny3d_VertexColor(mid);
            tiny3d_VertexPos(x + step, yb, 1000);      tiny3d_VertexColor(mid);
            tiny3d_VertexPos(x + step, yb + 46, 1000); tiny3d_VertexColor(clear);
            tiny3d_VertexPos(x, ya + 46, 1000);        tiny3d_VertexColor(clear);
            tiny3d_End();
        }
    }

    if (parts & 8) draw_particles(phase);

    if (parts & 4) {            /* fine lines as thin quads */
        for (j = 0; j < 7; j++) {
            u32 c = gfx_accent(40 + 10 * (3 - abs(j - 3)));
            tiny3d_SetPolygon(TINY3D_QUADS);
            for (x = -24; x <= SCREEN_W; x += 24) {
                float ya = wave_y(x, phase, j * 7.0f), yb = wave_y(x + 24, phase, j * 7.0f);
                tiny3d_VertexPos(x, ya, 1000);           tiny3d_VertexColor(c);
                tiny3d_VertexPos(x + 24, yb, 1000);      tiny3d_VertexColor(c);
                tiny3d_VertexPos(x + 24, yb + 1.5f, 1000); tiny3d_VertexColor(c);
                tiny3d_VertexPos(x, ya + 1.5f, 1000);    tiny3d_VertexColor(c);
            }
            tiny3d_End();
        }
    }
}

/* ------------------------------------------------------------------ floating circles
   Like the PS3 home screen: little sparkles riding along the wave and big soft "bokeh"
   circles drifting slowly upwards. Positions are worked out from the time, nothing stored. */
static float hash01(int i, int k)            /* fixed pseudo-random 0..1 per particle */
{
    u32 h = (u32) i * 374761393u + (u32) k * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

/* soft circle: bright centre fading to nothing at the rim, drawn as a fan of quads */
static void soft_circle(float cx, float cy, float r, u32 rgb, int alpha, int N)
{
    u32 c0 = (rgb & 0xffffff00) | (u32) alpha, c1 = (rgb & 0xffffff00) | (u32) (alpha * 0.55f), c2 = rgb & 0xffffff00;
    float rm = r * 0.45f;
    int k;
    tiny3d_SetPolygon(TINY3D_QUADS);
    for (k = 0; k < N; k++) {
        float a0 = k * 6.2831853f / N, a1 = (k + 1) * 6.2831853f / N;
        float s0 = sinf(a0), c0s = cosf(a0), s1 = sinf(a1), c1s = cosf(a1);
        /* inner disc */
        tiny3d_VertexPos(cx, cy, 1000);                          tiny3d_VertexColor(c0);
        tiny3d_VertexPos(cx, cy, 1000);                          tiny3d_VertexColor(c0);
        tiny3d_VertexPos(cx + c1s * rm, cy + s1 * rm, 1000);     tiny3d_VertexColor(c1);
        tiny3d_VertexPos(cx + c0s * rm, cy + s0 * rm, 1000);     tiny3d_VertexColor(c1);
        /* soft edge */
        tiny3d_VertexPos(cx + c0s * rm, cy + s0 * rm, 1000);     tiny3d_VertexColor(c1);
        tiny3d_VertexPos(cx + c1s * rm, cy + s1 * rm, 1000);     tiny3d_VertexColor(c1);
        tiny3d_VertexPos(cx + c1s * r, cy + s1 * r, 1000);       tiny3d_VertexColor(c2);
        tiny3d_VertexPos(cx + c0s * r, cy + s0 * r, 1000);       tiny3d_VertexColor(c2);
    }
    tiny3d_End();
}

static void draw_particles(float phase)
{
    int i;
    u32 tint = gfx_accent(0xff) | 0xff;

    /* a few very faint big circles far behind, drifting up */
    for (i = 0; i < 5; i++) {
        float r = 70 + 90 * hash01(i, 1);
        float span = SCREEN_H + 2 * r + 200;
        float y = SCREEN_H + r - fmodf(hash01(i, 3) * span + phase * (6 + 8 * hash01(i, 2)), span);
        float x = hash01(i, 4) * SCREEN_W + 60 * sinf(phase * 0.3f + i * 1.3f);
        float edge = fminf(1.0f, fminf(y + r, SCREEN_H + r - y) / 300.0f);
        if (edge > 0) soft_circle(x, y, r, tint, (int) ((14 + 12 * hash01(i, 5)) * edge), 16);
    }

    /* lots of small sparkles: each appears somewhere new, fades in, flashes, fades out */
    for (i = 0; i < 110; i++) {
        float life = 2.0f + 3.0f * hash01(i, 10);              /* in phase units (~3-7 s) */
        float t = (phase + hash01(i, 11) * life) / life;
        int n = (int) t;                                        /* which life this is */
        t -= n;                                                 /* 0..1 through this life */

        /* new place every life: most gather around the wave, the rest anywhere */
        float x = hash01(i * 7 + n, 12) * (SCREEN_W + 80) - 40;
        float y;
        if (hash01(i, 13) < 0.7f) y = wave_y(x, phase, 14) + (hash01(i * 7 + n, 14) - 0.5f) * 260;
        else y = 120 + hash01(i * 7 + n, 15) * (SCREEN_H - 240);
        x += (t - 0.5f) * 40 * (hash01(i, 16) - 0.3f);           /* slow drift while alive */
        y -= t * 18;

        float fade = sinf(t * 3.14159f); fade *= fade;          /* in and out */
        float d = (t - 0.5f) / 0.07f;
        float flash = expf(-d * d);                             /* the sparkle */
        float r = 0.8f + 1.4f * hash01(i, 17);
        int a = (int) (200 * fade * (0.35f + 0.65f * flash));
        if (a < 4) continue;
        soft_circle(x, y, r * (2.2f + 1.3f * flash), 0xffffffff, a, 8);
        if (flash > 0.3f)                                       /* coloured glow round the brightest */
            soft_circle(x, y, r * 5.0f, tint, (int) (45 * flash * fade), 10);
    }
}

/* ------------------------------------------------------------------ PS1 inspired
   charcoal grey, with the four face-button shapes (triangle, circle, cross, square) in their
   colours drifting and turning slowly - big and faint, like the PS1's own menus */
static void quad_line(float x0, float y0, float x1, float y1, float t, u32 c)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.01f) return;
    float nx = -dy / l * t / 2, ny = dx / l * t / 2;
    tiny3d_VertexPos(x0 + nx, y0 + ny, 1000); tiny3d_VertexColor(c);
    tiny3d_VertexPos(x1 + nx, y1 + ny, 1000); tiny3d_VertexColor(c);
    tiny3d_VertexPos(x1 - nx, y1 - ny, 1000); tiny3d_VertexColor(c);
    tiny3d_VertexPos(x0 - nx, y0 - ny, 1000); tiny3d_VertexColor(c);
}

static void shape(int kind, float cx, float cy, float r, float rot, float t, u32 c)
{
    int i, n = kind == 0 ? 3 : kind == 1 ? 40 : 4;
    tiny3d_SetPolygon(TINY3D_QUADS);
    if (kind == 2) {                                        /* cross: two bars */
        for (i = 0; i < 2; i++) {
            float a = rot + 0.785398f + i * 1.570796f;
            quad_line(cx - cosf(a) * r, cy - sinf(a) * r, cx + cosf(a) * r, cy + sinf(a) * r, t, c);
        }
    } else {                                                /* triangle, circle, square outlines */
        float a0 = rot + (kind == 0 ? -1.570796f : kind == 3 ? 0.785398f : 0);
        for (i = 0; i < n; i++) {
            float a = a0 + i * 6.2831853f / n, b = a0 + (i + 1) * 6.2831853f / n;
            quad_line(cx + cosf(a) * r, cy + sinf(a) * r, cx + cosf(b) * r, cy + sinf(b) * r, t, c);
        }
    }
    tiny3d_End();
}

static void draw_ps1(float phase, int animate)
{
    static const u32 col[4] = { 0x40e2a0ff, 0xff6666ff, 0x7cb2e8ff, 0xf28ce6ff };   /* triangle circle cross square */
    int i;
    float t = animate ? phase : 0;
    for (i = 0; i < 12; i++) {
        int kind = i % 4;
        float r = 70 + 150 * hash01(i, 21);
        float x = fmodf(hash01(i, 22) * (SCREEN_W + 400) + t * (8 + 10 * hash01(i, 23)), SCREEN_W + 400) - 200;
        float y = 120 + hash01(i, 24) * (SCREEN_H - 240) + 30 * sinf(t * 0.4f + i);
        float rot = t * (0.05f + 0.08f * hash01(i, 25)) * (i & 1 ? 1 : -1) + i;
        u32 c = (col[kind] & 0xffffff00) | (u32) (22 + 22 * hash01(i, 26));
        shape(kind, x, y, r, rot, 5 + 6 * hash01(i, 27), c);
    }
    /* a soft light band across the bottom, like the PS1 menu floor */
    gfx_rect_v(0, SCREEN_H - 260, SCREEN_W, 260, 0xffffff00, 0xffffff10);
}

/* ------------------------------------------------------------------ PS2 inspired
   near-black blue space with rows of glassy "towers" of blocks rising and falling, and
   specks of light drifting up - a nod to the PS2 start-up screen */
static void block(float x, float y, float s, u32 front, u32 top)
{
    float d = s * 0.35f;                                    /* depth of the top face */
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x, y, 1000);         tiny3d_VertexColor(front);
    tiny3d_VertexPos(x + s, y, 1000);     tiny3d_VertexColor(front);
    tiny3d_VertexPos(x + s, y + s, 1000); tiny3d_VertexColor(front & 0xffffff00 | ((front & 0xff) / 3));
    tiny3d_VertexPos(x, y + s, 1000);     tiny3d_VertexColor(front & 0xffffff00 | ((front & 0xff) / 3));
    tiny3d_VertexPos(x + d, y - d, 1000);     tiny3d_VertexColor(top);
    tiny3d_VertexPos(x + s + d, y - d, 1000); tiny3d_VertexColor(top);
    tiny3d_VertexPos(x + s, y, 1000);         tiny3d_VertexColor(top);
    tiny3d_VertexPos(x, y, 1000);             tiny3d_VertexColor(top);
    tiny3d_End();
}

static void draw_ps2(float phase, int animate)
{
    int layer, i, k;
    float t = animate ? phase : 0;
    for (layer = 0; layer < 2; layer++) {                  /* far row first, then near */
        float s = layer ? 46 : 28, base = layer ? SCREEN_H - 40 : SCREEN_H - 230;
        int n = layer ? 16 : 24;
        for (i = 0; i < n; i++) {
            float x = (i + 0.5f) * SCREEN_W / n - s / 2 + (layer ? 0 : 20);
            float h = 1 + 5 * (0.5f + 0.5f * sinf(t * (0.25f + 0.3f * hash01(i, 31 + layer)) + i * 1.7f));
            int blocks = (int) h;
            float frac = h - blocks;
            for (k = 0; k <= blocks; k++) {
                float a = (layer ? 0.9f : 0.5f) * (1.0f - (float) k / 10.0f) * (k == blocks ? frac : 1.0f);
                if (a <= 0.02f) continue;
                u32 front = 0x4aa8ff00 | (u32) (42 * a), top = 0xbfe8ff00 | (u32) (80 * a);
                block(x, base - (k + 1) * s * 1.08f, s, front, top);
            }
        }
    }
    /* glow along the floor */
    gfx_rect_v(0, SCREEN_H - 300, SCREEN_W, 300, 0x3c8cff00, 0x3c8cff26);
    /* specks drifting up */
    if (animate)
        for (i = 0; i < 50; i++) {
            float span = SCREEN_H + 40;
            float y = SCREEN_H + 20 - fmodf(hash01(i, 41) * span + t * (10 + 20 * hash01(i, 42)), span);
            float x = hash01(i, 43) * SCREEN_W + 20 * sinf(t * 0.5f + i);
            int a = (int) (90 * (0.4f + 0.6f * sinf(t * 2 + i)));
            if (a > 4) soft_circle(x, y, 2.5f + 2 * hash01(i, 44), 0xbfe8ffff, a, 8);
        }
}

void gfx_background(float phase)
{
    if (bg_style == 1) { gfx_background_parts(phase, 1); draw_ps1(phase, wave_on != 0); return; }
    if (bg_style == 2) { gfx_background_parts(phase, 1); draw_ps2(phase, wave_on != 0); return; }
    gfx_background_parts(phase, wave_on == 0 ? 1 : wave_on == 1 ? 7 : 15);
}

/* ------------------------------------------------------------------ text */
/* Width the same way libfont advances: integer (size * adv / cell) per character. */
float gfx_text_w(float size, const char *s)
{
    int sx = (int) size, w = 0;
    const unsigned char *p = (const unsigned char *) s;
    while (*p) w += sx * adv[*p++] / FONT_CELL;
    return (float) w;
}

float gfx_text(float x, float y, float size, u32 c, const char *s)
{
    if (!font_ok) return x;
    SetFontSize((int) size, (int) size);
    SetFontColor(c, 0x00000000);
    SetFontAutoCenter(0);
    SetFontAutoNewLine(0);
    return DrawString(x, y, (char *) s);
}

void gfx_text_center(float cx, float y, float size, u32 c, const char *s)
{
    gfx_text(cx - gfx_text_w(size, s) / 2.0f, y, size, c, s);
}

void gfx_text_fit(float x, float y, float size, u32 c, const char *s, float max_w)
{
    char buf[256];
    int n = strlen(s);
    if (gfx_text_w(size, s) <= max_w) { gfx_text(x, y, size, c, s); return; }
    if (n > 250) n = 250;
    float dots = gfx_text_w(size, "...");
    memcpy(buf, s, n); buf[n] = 0;
    while (n > 0 && gfx_text_w(size, buf) + dots > max_w) buf[--n] = 0;
    strcpy(buf + n, "...");
    gfx_text(x, y, size, c, buf);
}

/* word-wrap inside max_w, at most max_lines lines (last one gets "...") */
/* word-wraps s and draws lines [first, first + max_lines); returns how many lines s needs in total */
int gfx_text_wrap_window(float x, float y, float size, u32 c, const char *s, float max_w, int first, int max_lines)
{
    char line[256];
    const char *p = s;
    float lh = size * 1.18f;
    int n = 0;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        int len = 0, last_space = -1;
        while (p[len] && len < 250) {
            memcpy(line, p, len + 1); line[len + 1] = 0;
            if (gfx_text_w(size, line) > max_w) break;
            if (p[len] == ' ') last_space = len;
            len++;
        }
        int take = len;
        if (p[len] && last_space > 0) take = last_space;
        if (take == 0) take = 1;
        if (n >= first && n < first + max_lines) {
            memcpy(line, p, take); line[take] = 0;
            gfx_text(x, y + (n - first) * lh, size, c, line);
        }
        n++;
        p += take;
    }
    return n;
}

float gfx_text_wrap(float x, float y, float size, u32 c, const char *s, float max_w, int max_lines)
{
    char line[256];
    const char *p = s;
    float lh = size * 1.18f;
    int lines = 0;
    while (*p && lines < max_lines) {
        while (*p == ' ') p++;
        int len = 0, last_space = -1;
        while (p[len] && len < 250) {
            memcpy(line, p, len + 1); line[len + 1] = 0;
            if (gfx_text_w(size, line) > max_w) break;
            if (p[len] == ' ') last_space = len;
            len++;
        }
        int take = len;
        if (p[len] && last_space > 0) take = last_space;      /* break at a space */
        if (take == 0) take = 1;
        memcpy(line, p, take); line[take] = 0;
        lines++;
        if (lines == max_lines && p[take]) gfx_text_fit(x, y, size, c, p, max_w);
        else gfx_text(x, y, size, c, line);
        p += take;
        y += lh;
    }
    return y;
}
