/* gfx.h - drawing helpers on a virtual 1920x1080 screen (Tiny3D + libfont) */
#ifndef GFX_H
#define GFX_H

#include <ppu-types.h>

#define SCREEN_W 1920.0f
#define SCREEN_H 1080.0f

/* colours are 0xRRGGBBAA */
#define RGBA(r, g, b, a) (((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a))

typedef struct {
    u32 offset;      /* RSX offset of the pixels */
    u32 tex_w;       /* allocated width (padded)  */
    u32 tex_h;
    u32 w, h;        /* real image size inside the texture */
} Texture;

void  gfx_init(void);
u32  *gfx_tex_alloc(u32 bytes);           /* carve RSX texture memory (16-byte aligned) */
void  gfx_begin(void);
void  gfx_end(void);

void  gfx_rect(float x, float y, float w, float h, u32 rgba);
void  gfx_rect_v(float x, float y, float w, float h, u32 top, u32 bottom);
void  gfx_frame(float x, float y, float w, float h, float t, u32 rgba);
void  gfx_image(const Texture *t, float x, float y, float w, float h, u32 tint);
void  gfx_image_fit(const Texture *t, float x, float y, float w, float h, u32 tint); /* keeps aspect, no stretching */

void  gfx_background(float phase);        /* PS3-blue radial gradient + soft wave */

float gfx_text(float x, float y, float size, u32 rgba, const char *s);
float gfx_text_w(float size, const char *s);
void  gfx_text_center(float cx, float y, float size, u32 rgba, const char *s);
void  gfx_text_fit(float x, float y, float size, u32 rgba, const char *s, float max_w); /* adds "..." if too long */

#endif
