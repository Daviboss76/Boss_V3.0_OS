#include "kernel.h"
#include "arch.h"
#include "timer.h"
#include "ring3.h"
#include "heal.h"

volatile uint32_t ticks;

static void irq0(regs_t *r) {
    ticks++;
    if (ticks % 500 == 0) heal_due = true;     /* a cada 5 s */
    ring3_tick(r);                             /* watchdog dos drivers de ring 3 */
}

void timer_init(uint32_t hz) {
    uint32_t div = 1193182 / hz;
    outb(0x43, 0x36);                          /* canal 0, modo 3 */
    outb(0x40, div & 0xFF);
    outb(0x40, div >> 8);
    irq_register(0, irq0);
}

void delay_ms(uint32_t ms) {
    while (ms--) {
        outb(0x61, (inb(0x61) & 0xFD) | 1);
        outb(0x43, 0xB0);
        outb(0x42, 1193 & 0xFF);
        outb(0x42, 1193 >> 8);
        for (uint32_t i = 0; i < 1000000 && !(inb(0x61) & 0x20); i++) ;
    }
}
