#ifndef MOCK_LIBFONT
#define MOCK_LIBFONT
#include "ppu-types.h"
void ResetFont(void);
u8 *AddFontFromTTF(u8 *texture, u8 first_char, u8 last_char, int w, int h,
                   void (*cb)(u8 chr, u8 *bitmap, short *w, short *h, short *y_correction));
void SetFontSize(int sx, int sy);
void SetFontColor(u32 color, u32 bkcolor);
void SetFontAutoCenter(int on);
void SetFontAutoNewLine(int w);
void SetFontScreenLimits(float w, float h);
void SetFontZ(float z);
float DrawString(float x, float y, char *str);
#endif
