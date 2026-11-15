#include "kernel.h"
#include "arch.h"
#include "abi.h"

typedef struct __attribute__((packed)) {
    uint16_t lim0, base0;
    uint8_t  base1, acesso, gran, base2;
} gdt_ent_t;

typedef struct __attribute__((packed)) { uint16_t limite; uint32_t base; } gdtr_t;

typedef struct __attribute__((packed)) {
    uint32_t link, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
    uint8_t  iomap[8193];                /* 65536 portas + byte terminador */
} tss_t;

extern void gdt_load(gdtr_t *p);
extern void tss_load(void);

static gdt_ent_t gdt[6];
static tss_t tss;
static uint8_t ring0_stack[8192] __attribute__((aligned(16)));

static void set(int i, uint32_t base, uint32_t lim, uint8_t acc, uint8_t gran) {
    gdt[i].lim0   = lim & 0xFFFF;
    gdt[i].base0  = base & 0xFFFF;
    gdt[i].base1  = (base >> 16) & 0xFF;
    gdt[i].acesso = acc;
    gdt[i].gran   = ((lim >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].base2  = (base >> 24) & 0xFF;
}

/* 0x18 = codigo ring 3 (sel 0x1B), 0x20 = dados ring 3 (sel 0x23) */
void gdt_set_user(uint32_t base, uint32_t limite) {
    set(3, base, limite, 0xFA, 0x40);
    set(4, base, limite, 0xF2, 0x40);
}

void tss_io_deny_all(void) {
    memset(tss.iomap, 0xFF, sizeof tss.iomap);
}
void tss_io_allow(uint16_t p) {
    tss.iomap[p >> 3] &= (uint8_t)~(1u << (p & 7));
}

void gdt_init(void) {
    set(0, 0, 0, 0, 0);
    set(1, 0, 0xFFFFF, 0x9A, 0xCF);          /* 0x08 codigo ring 0 */
    set(2, 0, 0xFFFFF, 0x92, 0xCF);          /* 0x10 dados  ring 0 */
    gdt_set_user(SLOT_BASE, SLOT_SIZE - 1);

    memset(&tss, 0, sizeof tss);
    tss.ss0  = 0x10;
    tss.esp0 = (uint32_t)(ring0_stack + sizeof ring0_stack);
    tss.iomap_base = offsetof(tss_t, iomap);
    tss_io_deny_all();
    set(5, (uint32_t)&tss, sizeof(tss_t) - 1, 0x89, 0x00);   /* 0x28 TSS */

    gdtr_t p = { sizeof gdt - 1, (uint32_t)gdt };
    gdt_load(&p);
    tss_load();
}
