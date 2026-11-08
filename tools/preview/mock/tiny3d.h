/* PC stand-in for Tiny3D: records draw calls for the preview renderer */
#ifndef MOCK_TINY3D
#define MOCK_TINY3D
#include "ppu-types.h"
typedef enum { TINY3D_POINTS = 1, TINY3D_LINES, TINY3D_LINE_LOOP, TINY3D_LINE_STRIP, TINY3D_TRIANGLES,
    TINY3D_TRIANGLE_STRIP, TINY3D_TRIANGLE_FAN, TINY3D_QUADS, TINY3D_QUAD_STRIP, TINY3D_POLYGON } type_polygon;
typedef enum { TINY3D_TEX_FORMAT_L8 = 0x100, TINY3D_TEX_FORMAT_A1R5G5B5 = 0x200, TINY3D_TEX_FORMAT_A4R4G4B4 = 0x300,
    TINY3D_TEX_FORMAT_R5G6B5 = 0x400, TINY3D_TEX_FORMAT_A8R8G8B8 = 0x500 } text_format;
#define TINY3D_CLEAR_ALL 7
#define TINY3D_ALPHA_FUNC_GEQUAL 0
#define TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA 0
#define TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA 0
#define TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA 0
#define TINY3D_BLEND_FUNC_DST_ALPHA_ZERO 0
#define TINY3D_BLEND_RGB_FUNC_ADD 0
#define TINY3D_BLEND_ALPHA_FUNC_ADD 0
#define TEXTURE_LINEAR 1
typedef struct { u16 width, height; } VideoResolution;
extern VideoResolution Video_Resolution;
int  tiny3d_Init(u32 v);
void *tiny3d_AllocTexture(u32 size);
u32  tiny3d_TextureOffset(void *p);
void tiny3d_Clear(u32 color, int flags);
void tiny3d_AlphaTest(int e, u32 ref, int f);
void tiny3d_BlendFunc(int e, int a, int b, int c);
void tiny3d_Project2D(void);
void tiny3d_Flip(void);
void tiny3d_UserViewport(int on, float px, float py, float sx, float sy, float s3x, float s3y);
int  tiny3d_SetPolygon(type_polygon t);
void tiny3d_VertexPos(float x, float y, float z);
void tiny3d_VertexColor(u32 rgba);
void tiny3d_VertexTexture(float u, float v);
int  tiny3d_End(void);
void tiny3d_SetTexture(u32 unit, u32 offset, u32 w, u32 h, u32 stride, text_format f, int smooth);
#endif
