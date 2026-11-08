#ifndef MOCK_PAD
#define MOCK_PAD
#include "../ppu-types.h"
#define MAX_PADS 7
typedef struct { u32 status[MAX_PADS]; } padInfo;
typedef struct { int len; int BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CROSS, BTN_CIRCLE, BTN_TRIANGLE,
                 BTN_SQUARE, BTN_L1, BTN_R1, BTN_START, BTN_SELECT, BTN_L2, BTN_R2; } padData;
int ioPadInit(int n);
int ioPadGetInfo(padInfo *i);
int ioPadGetData(int port, padData *d);
#endif
