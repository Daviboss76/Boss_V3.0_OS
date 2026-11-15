#include "ulib.h"

static void drv(volatile mbox_t *m) {
    volatile int z = 0;
    switch (m->op) {
    case 0: m->ret = 42; break;
    case 1: *(volatile uint32_t *)0x20000 = 1; break;     /* fora do segmento: #GP */
    case 2: for (;;) ;                                    /* loop infinito: watchdog */
    case 3: __asm__ volatile ("cli"); break;              /* instrucao privilegiada: #GP */
    case 4: (void)inb(0x60); break;                       /* porta nao liberada: #GP */
    case 5: m->ret = 1 / z; break;                        /* divisao por zero: #DE */
    case 6: ((void (*)(void))0x30000)(); break;           /* salto fora do segmento: #GP */
    default: m->ret = -1;
    }
}
DRIVER(drv)
