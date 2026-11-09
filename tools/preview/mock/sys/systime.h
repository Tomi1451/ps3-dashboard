#pragma once
static inline int sysGetCurrentTime(unsigned long long *s, unsigned long long *ns) { static unsigned long long t = 0; t += 16; *s = t / 1000 + 100; *ns = (t % 1000) * 1000000ULL; return 0; }
