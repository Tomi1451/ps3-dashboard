/* covers.c - cover art cache: decode JPG/PNG, shrink with area averaging, upload to RSX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <tiny3d.h>
#include <jpgdec/jpgdec.h>
#include <pngdec/pngdec.h>
#include <sysmodule/sysmodule.h>

#include "covers.h"
#include "log.h"

#define SLOT_DIM   512                 /* max texture size per cover */
#define SLOTS      56                  /* 56 x 1 MB of video memory */
#define LOADS_PER_FRAME 2              /* keeps navigation smooth */

typedef struct {
    const Game *owner;                 /* which game is in this slot */
    u32  last_used;
    int  state;                        /* 0 empty, 1 ready, 2 no image */
    u32 *pixels;
    Texture tex;
} Slot;

static Slot slots[SLOTS];
static u32  frame_no = 0;
static int  loads_left = LOADS_PER_FRAME;

void covers_init(void)
{
    int i;
    sysModuleLoad(SYSMODULE_JPGDEC);
    sysModuleLoad(SYSMODULE_PNGDEC);
    for (i = 0; i < SLOTS; i++) {
        slots[i].pixels = gfx_tex_alloc(SLOT_DIM * SLOT_DIM * 4);
        slots[i].owner = NULL;
        slots[i].state = 0;
    }
}

void covers_frame(void)
{
    frame_no++;
    loads_left = LOADS_PER_FRAME;
}

/* copy/shrink src (ARGB) into dst so it fits SLOT_DIM, keeping its shape */
static void shrink_into(Slot *s, const u32 *src, u32 sw, u32 sh, u32 spitch)
{
    u32 dw = sw, dh = sh;
    if (dw > SLOT_DIM || dh > SLOT_DIM) {
        if (sw >= sh) { dw = SLOT_DIM; dh = (u32) ((u64) sh * SLOT_DIM / sw); }
        else          { dh = SLOT_DIM; dw = (u32) ((u64) sw * SLOT_DIM / sh); }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    u32 tw = (dw + 15) & ~15;          /* row pitch multiple of 64 bytes */
    u32 x, y;
    const u8 *base = (const u8 *) src;

    for (y = 0; y < dh; y++) {
        u32 y0 = (u32) ((u64) y * sh / dh), y1 = (u32) ((u64) (y + 1) * sh / dh);
        if (y1 <= y0) y1 = y0 + 1;
        u32 *drow = s->pixels + y * tw;
        for (x = 0; x < dw; x++) {
            u32 x0 = (u32) ((u64) x * sw / dw), x1 = (u32) ((u64) (x + 1) * sw / dw);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 - x0 == 1 && y1 - y0 == 1) {
                drow[x] = ((const u32 *) (base + y0 * spitch))[x0];
                continue;
            }
            u32 a = 0, r = 0, g = 0, b = 0, n = 0, yy, xx;
            for (yy = y0; yy < y1; yy++) {
                const u32 *srow = (const u32 *) (base + yy * spitch);
                for (xx = x0; xx < x1; xx++) {
                    u32 p = srow[xx];
                    a += p >> 24; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
                }
            }
            drow[x] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
        for (x = dw; x < tw; x++) drow[x] = 0;
    }
    s->tex.offset = tiny3d_TextureOffset(s->pixels);
    s->tex.tex_w = tw;
    s->tex.tex_h = dh;
    s->tex.w = dw;
    s->tex.h = dh;
}

static int decode(const char *path, Slot *s)
{
    if (strlen(path) > 4 && strcasecmp(path + strlen(path) - 4, ".png") == 0) {
        pngData png;
        memset(&png, 0, sizeof(png));
        if (pngLoadFromFile(path, &png) != 0 || !png.bmp_out) return 0;
        shrink_into(s, (const u32 *) png.bmp_out, png.width, png.height, png.pitch);
        free(png.bmp_out);
        return 1;
    } else {
        jpgData jpg;
        memset(&jpg, 0, sizeof(jpg));
        if (jpgLoadFromFile(path, &jpg) != 0 || !jpg.bmp_out) return 0;
        shrink_into(s, (const u32 *) jpg.bmp_out, jpg.width, jpg.height, jpg.pitch);
        free(jpg.bmp_out);
        return 1;
    }
}

const Texture *covers_get(const Game *g)
{
    int i, lru = 0;
    if (!g || !g->cover[0]) return NULL;

    for (i = 0; i < SLOTS; i++)
        if (slots[i].owner == g) {
            slots[i].last_used = frame_no;
            return slots[i].state == 1 ? &slots[i].tex : NULL;
        }

    if (loads_left <= 0) return NULL;      /* try again next frame */
    loads_left--;

    for (i = 0; i < SLOTS; i++) {
        if (!slots[i].pixels) continue;
        if (slots[i].owner == NULL) { lru = i; break; }
        if (slots[i].last_used < slots[lru].last_used) lru = i;
    }
    Slot *s = &slots[lru];
    if (!s->pixels) return NULL;
    s->owner = g;
    s->last_used = frame_no;
    s->state = decode(g->cover, s) ? 1 : 2;
    { static int fails = 0; if (s->state == 2 && fails++ < 10) log_step("cover failed: %s", g->cover); }
    return s->state == 1 ? &s->tex : NULL;
}
