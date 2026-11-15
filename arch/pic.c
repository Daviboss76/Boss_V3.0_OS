#include "kernel.h"
#include "arch.h"

#define P1 0x20
#define P2 0xA0

static void espera(void) { outb(0x80, 0); }

void pic_init(void) {
    outb(P1, 0x11); espera(); outb(P2, 0x11); espera();
    outb(P1 + 1, 0x20); espera(); outb(P2 + 1, 0x28); espera();   /* IRQ0-7 -> 0x20, IRQ8-15 -> 0x28 */
    outb(P1 + 1, 0x04); espera(); outb(P2 + 1, 0x02); espera();
    outb(P1 + 1, 0x01); espera(); outb(P2 + 1, 0x01); espera();
    outb(P1 + 1, 0xFB);                                           /* so a cascata (IRQ2) ligada */
    outb(P2 + 1, 0xFF);
}
void pic_mask(int irq) {
    uint16_t p = irq < 8 ? P1 + 1 : P2 + 1;
    outb(p, inb(p) | (1 << (irq & 7)));
}
void pic_unmask(int irq) {
    uint16_t p = irq < 8 ? P1 + 1 : P2 + 1;
    outb(p, inb(p) & ~(1 << (irq & 7)));
}
void pic_eoi(int irq) {
    if (irq >= 8) outb(P2, 0x20);
    outb(P1, 0x20);
}
bool pic_spurious(int irq) {
    if (irq == 7)  { outb(P1, 0x0B); return !(inb(P1) & 0x80); }
    if (irq == 15) {
        outb(P2, 0x0B);
        if (!(inb(P2) & 0x80)) { outb(P1, 0x20); return true; }
    }
    return false;
}
