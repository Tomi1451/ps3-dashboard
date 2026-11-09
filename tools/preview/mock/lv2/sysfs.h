#pragma once
typedef struct { unsigned int st_mode; } sysFSStat;
static inline int sysLv2FsStat(const char *p, sysFSStat *s) { return -1; }
