#ifndef INTEILIDOS_NATIVE_API_H
#define INTEILIDOS_NATIVE_API_H
#include <stdint.h>
#define INTEILIDOS_API_MAGIC 0x494E4150u
#define INTEILIDOS_API_VERSION 1u
/* Optional cdecl entry argument. Older void entry(void) programs ignore it. */
typedef struct {
    uint32_t magic, version, size;
    int (*key_poll)(void);                /* shared OS keyboard ring, -1 if empty */
    uint32_t (*ticks)(void);              /* host 1000 Hz clock */
    void (*tone)(uint32_t hz);
    void (*silence)(void);
    int (*volume)(void);
} native_api_t;
#endif
