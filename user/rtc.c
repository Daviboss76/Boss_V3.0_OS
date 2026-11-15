#include "ulib.h"

static uint8_t cmos(uint8_t r) { outb(0x70, r); return inb(0x71); }
static uint8_t bcd(uint8_t v)  { return (v & 0x0F) + (v >> 4) * 10; }

static void drv(volatile mbox_t *m) {
    if (m->op != 1) { m->ret = -1; return; }
    for (uint32_t i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++) ;
    uint8_t s = cmos(0), mi = cmos(2), h = cmos(4);
    if (!(cmos(0x0B) & 4)) { s = bcd(s); mi = bcd(mi); h = bcd(h); }
    m->ret = (h << 16) | (mi << 8) | s;
}
DRIVER(drv)
