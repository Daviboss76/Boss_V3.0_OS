/* rtc.c */
#include "kernel.h"
#include "rtc.h"

static uint8_t cmos(uint8_t r) { outb(0x70, r); return inb(0x71); }
static uint8_t bcd(uint8_t v)  { return (v & 0x0F) + (v >> 4) * 10; }

void rtc_ler(rtc_hora_t *t) {
    while (cmos(0x0A) & 0x80) ;
    uint8_t s = cmos(0x00), m = cmos(0x02), h = cmos(0x04);
    if (!(cmos(0x0B) & 0x04)) { s = bcd(s); m = bcd(m); h = bcd(h); }
    t->h = h; t->m = m; t->s = s;
}
