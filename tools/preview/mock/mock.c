/* records every draw call of a frame; dumps chosen frames for tools/preview/render.py */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "tiny3d.h"
#include "libfont.h"
#include "io/pad.h"

VideoResolution Video_Resolution = { 1920, 1080 };
static FILE *out = NULL;
static int frame = 0;
static int cur_tex = -1, poly = 0;
static float vx, vy, vu, vv; static u32 vc = 0xffffffff; static int have_vertex = 0, vtex = 0;
static char *script = NULL;          /* PREVIEW_SCRIPT: per-frame button letters */
static int dump_frames[64], n_dump = 0, last_frame = 0;

static void open_frame(void)
{
    int i;
    out = NULL;
    for (i = 0; i < n_dump; i++) if (dump_frames[i] == frame) {
        char p[256]; snprintf(p, sizeof(p), "%s/frame_%04d.txt", getenv("PREVIEW_OUT"), frame);
        out = fopen(p, "w");
    }
}
static void init_once(void)
{
    static int done = 0; if (done) return; done = 1;
    script = getenv("PREVIEW_SCRIPT");
    char *d = getenv("PREVIEW_DUMP"), *tok;
    char buf[512]; strncpy(buf, d ? d : "30", sizeof(buf) - 1);
    for (tok = strtok(buf, ","); tok && n_dump < 64; tok = strtok(NULL, ",")) {
        dump_frames[n_dump++] = atoi(tok);
        if (atoi(tok) > last_frame) last_frame = atoi(tok);
    }
    open_frame();
}
int  tiny3d_Init(u32 v) { init_once(); return 0; }
void *tiny3d_AllocTexture(u32 size) { return calloc(1, size); }
static void *tex_ptrs[4096]; static int n_tex = 0;
u32  tiny3d_TextureOffset(void *p) { tex_ptrs[n_tex] = p; return n_tex++; }
void tiny3d_Clear(u32 c, int f) { if (out) fprintf(out, "CLEAR %08x\n", c); }
void tiny3d_AlphaTest(int e, u32 r, int f) {}
void tiny3d_BlendFunc(int e, int a, int b, int c) {}
void tiny3d_Project2D(void) {}
void tiny3d_UserViewport(int on, float px, float py, float sx, float sy, float a, float b) {}
void tiny3d_SetTexture(u32 unit, u32 off, u32 w, u32 h, u32 st, text_format f, int s) { cur_tex = off; }
static void flush_vertex(void) { if (have_vertex && out) fprintf(out, "V %.2f %.2f %08x %.4f %.4f\n", vx, vy, vc, vu, vv); have_vertex = 0; }
int  tiny3d_SetPolygon(type_polygon t) { poly = t; vtex = 0; if (out) fprintf(out, "POLY %d\n", t); return 0; }
void tiny3d_VertexPos(float x, float y, float z) { flush_vertex(); vx = x; vy = y; vu = vv = 0; have_vertex = 1; }
void tiny3d_VertexColor(u32 c) { vc = c; }
void tiny3d_VertexTexture(float u, float v) { vu = u; vv = v; vtex = 1; }
int  tiny3d_End(void) { flush_vertex(); if (out) fprintf(out, "END %d\n", vtex ? cur_tex : -1); return 0; }
void tiny3d_Flip(void)
{
    if (out) { fclose(out); out = NULL; }
    frame++;
    if (frame > last_frame) exit(0);
    open_frame();
}

/* ---- libfont stand-in: same placement maths as libfont.c ---- */
static short fw[256], fy[256]; static int cell_w = 64, cell_h = 64, fsx = 64, fsy = 64; static u32 fcol = 0xffffffff;
void ResetFont(void) {}
u8 *AddFontFromTTF(u8 *texture, u8 first, u8 last, int w, int h, void (*cb)(u8, u8 *, short *, short *, short *))
{
    static u8 bmp[257 * 256]; int n;
    char p[256]; snprintf(p, sizeof(p), "%s/glyphs.bin", getenv("PREVIEW_OUT"));
    FILE *g = fopen(p, "wb");
    cell_w = w; cell_h = h;
    for (n = 0; n < 256; n++) {
        short ww = w, hh = h, yc = 0;
        memset(bmp, 0, sizeof(bmp));
        if (n >= first && n <= last) { fw[n] = w; cb((u8) n, bmp, &ww, &hh, &yc); fw[n] = ww; fy[n] = yc; }
        else { fw[n] = 0; fy[n] = 0; }
        fwrite(bmp, 1, w * h, g);
    }
    fclose(g);
    return texture + 16;
}
void SetFontSize(int sx, int sy) { fsx = sx; fsy = sy; }
void SetFontColor(u32 c, u32 bk) { fcol = c; }
void SetFontAutoCenter(int on) {}
void SetFontAutoNewLine(int w) {}
void SetFontScreenLimits(float w, float h) {}
void SetFontZ(float z) {}
float DrawString(float x, float y, char *str)
{
    while (*str) {
        unsigned char c = (unsigned char) *str++;
        if (out && c >= 32 && (fcol & 0xff))
            fprintf(out, "CHAR %d %.2f %.2f %d %d %08x\n", c, x, y + (float) (fy[c] * fsy) / cell_h, fsx, fsy, fcol);
        x += fsx * fw[c] / cell_w;
    }
    return x;
}

/* ---- pad: PREVIEW_SCRIPT like "....R.R..T" one char per frame ---- */
int ioPadInit(int n) { return 0; }
int ioPadGetInfo(padInfo *i) { memset(i, 0, sizeof(*i)); i->status[0] = 1; return 0; }
int ioPadGetData(int port, padData *d)
{
    memset(d, 0, sizeof(*d)); d->len = 1;
    char c = (script && frame < (int) strlen(script)) ? script[frame] : '.';
    switch (c) {
        case 'U': d->BTN_UP = 1; break;  case 'D': d->BTN_DOWN = 1; break;
        case 'L': d->BTN_LEFT = 1; break; case 'R': d->BTN_RIGHT = 1; break;
        case 'X': d->BTN_CROSS = 1; break; case 'O': d->BTN_CIRCLE = 1; break;
        case 'T': d->BTN_TRIANGLE = 1; break; case 'S': d->BTN_SQUARE = 1; break; case 'E': d->BTN_SELECT = 1; break;
        case '[': d->BTN_L1 = 1; break; case ']': d->BTN_R1 = 1; break;
    }
    return 0;
}
