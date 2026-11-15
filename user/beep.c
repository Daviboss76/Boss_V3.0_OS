#include "ulib.h"

static void drv(volatile mbox_t *m) {
    if (m->op != 1) { m->ret = -1; return; }
    uint32_t hz = m->arg[0], ms = m->arg[1];
    if (hz < 20 || hz > 20000) { m->ret = -2; return; }
    if (ms > 2500) ms = 2500;
    uint32_t div = 1193182 / hz;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, div >> 8);
    outb(0x61, inb(0x61) | 3);
    uint32_t fim = sys_ticks() + ms / 10 + 1;
    while ((int32_t)(sys_ticks() - fim) < 0) ;
    outb(0x61, inb(0x61) & 0xFC);
    m->ret = 0;
}
DRIVER(drv)
