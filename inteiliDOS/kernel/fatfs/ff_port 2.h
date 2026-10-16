#ifndef INTEILIDOS_FF_PORT_H
#define INTEILIDOS_FF_PORT_H
#include "../memory.h"
#define memcpy kmemcpy
#define memset kmemset
#define memcmp kmemcmp
#define strlen kstrlen
static inline char *ff_strchr(const char *s, int ch) {
    do { if (*s == (char)ch) return (char *)s; } while (*s++);
    return 0;
}
#define strchr ff_strchr
#endif
