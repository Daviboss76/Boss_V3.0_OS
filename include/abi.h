#ifndef ABI_H
#define ABI_H
#include <stdint.h>

#define SLOT_BASE   0x400000u     /* memoria dos drivers */
#define SLOT_SIZE   0x10000u      /* 64 KB por driver */
#define MBOX_OFF    0xF000u       /* caixa de correio */
#define USER_STACK  0xF000u
#define N_SLOTS     6

typedef struct {
    uint32_t op;
    uint32_t arg[4];
    int32_t  ret;
    uint32_t len;
    uint8_t  buf[256];
} mbox_t;

#endif

