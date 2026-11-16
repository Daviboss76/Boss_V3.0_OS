#include "kernel.h"
#include "paging.h"
#include "abi.h"

#define POOL_INI 0x00800000u              /* frames de 8 MB a 24 MB */
#define POOL_N   4096u

extern uint8_t __kernel_start[], __guard_end[];

static uint32_t pgdir[1024] __attribute__((aligned(4096)));
static uint32_t pt0[1024]   __attribute__((aligned(4096)));   /* 0..4 MB   */
static uint32_t pt1[1024]   __attribute__((aligned(4096)));   /* 4..8 MB   */
static uint8_t  mapa[POOL_N / 8];
static uint32_t usados, dica;

static inline uint32_t le_cr0(void) { uint32_t v; __asm__ volatile ("mov %%cr0,%0" : "=r"(v)); return v; }
static inline void esc_cr0(uint32_t v) { __asm__ volatile ("mov %0,%%cr0" :: "r"(v) : "memory"); }
static inline void recarrega_cr3(void) {
    uint32_t v; __asm__ volatile ("mov %%cr3,%0; mov %0,%%cr3" : "=r"(v) :: "memory");
}

void paging_init(void) {
    uint32_t ks = (uint32_t)__kernel_start, ke = (uint32_t)__guard_end;

    /* 0..4 MB: pagina 0 ausente (pega NULL); texto+rodata do kernel somente leitura */
    for (uint32_t i = 1; i < 1024; i++) {
        uint32_t pa = i << 12;
        pt0[i] = pa | ((pa >= ks && pa < ke) ? PTE_P : (PTE_P | PTE_RW));
    }
    /* 4..8 MB: identidade; so os slots dos drivers sincronos ficam acessiveis ao usuario */
    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t pa = 0x400000u + (i << 12), fl = PTE_P | PTE_RW;
        if (pa >= SLOT_BASE && pa < SLOT_BASE + N_SLOTS * SLOT_SIZE) fl |= PTE_US;
        pt1[i] = pa | fl;
    }
    pgdir[0] = (uint32_t)pt0 | PTE_P | PTE_RW;
    pgdir[1] = (uint32_t)pt1 | PTE_P | PTE_RW | PTE_US;
    for (uint32_t i = 2; i < 6; i++)                       /* 8..24 MB: pool de frames (4 MB) */
        pgdir[i] = (i << 22) | PTE_P | PTE_RW | 0x80;
    for (uint32_t i = 896; i < 1024; i++)                  /* 0xE0000000+: MMIO PCI sem cache */
        pgdir[i] = (i << 22) | PTE_P | PTE_RW | PTE_PWT | PTE_PCD | 0x80;

    uint32_t cr4;
    __asm__ volatile ("mov %%cr4,%0" : "=r"(cr4));
    cr4 |= 0x10;                                            /* PSE */
    __asm__ volatile ("mov %0,%%cr4" :: "r"(cr4));
    __asm__ volatile ("mov %0,%%cr3" :: "r"((uint32_t)pgdir) : "memory");
    esc_cr0(le_cr0() | 0x80010000u);                        /* PG + WP */
}

uint32_t frame_alloc(void) {
    for (uint32_t k = 0; k < POOL_N; k++) {
        uint32_t i = (dica + k) % POOL_N;
        if (!(mapa[i >> 3] & (1u << (i & 7)))) {
            mapa[i >> 3] |= 1u << (i & 7);
            usados++; dica = i + 1;
            uint32_t pa = POOL_INI + i * 4096;
            memset((void *)pa, 0, 4096);
            return pa;
        }
    }
    return 0;
}

uint32_t frame_alloc_contig(uint32_t n) {
    uint32_t run = 0, ini = 0;
    for (uint32_t i = 0; i < POOL_N; i++) {
        if (mapa[i >> 3] & (1u << (i & 7))) { run = 0; continue; }
        if (!run) ini = i;
        if (++run == n) {
            for (uint32_t k = ini; k < ini + n; k++) mapa[k >> 3] |= 1u << (k & 7);
            usados += n;
            uint32_t pa = POOL_INI + ini * 4096;
            memset((void *)pa, 0, n * 4096);
            return pa;
        }
    }
    return 0;
}

void frame_free(uint32_t pa) {
    if (pa < POOL_INI || pa >= POOL_INI + POOL_N * 4096) return;
    uint32_t i = (pa - POOL_INI) >> 12;
    if (mapa[i >> 3] & (1u << (i & 7))) { mapa[i >> 3] &= (uint8_t)~(1u << (i & 7)); usados--; }
}

uint32_t frames_used(void)  { return usados; }
uint32_t frames_total(void) { return POOL_N; }

void paging_wp(bool on) {
    uint32_t c = le_cr0();
    esc_cr0(on ? (c | 0x10000u) : (c & ~0x10000u));
}
uint32_t paging_cr2(void) { uint32_t v; __asm__ volatile ("mov %%cr2,%0" : "=r"(v)); return v; }

void paging_user_set(uint32_t a, uint32_t b) {              /* PDE 64 = 0x10000000, PDE 65 = 0x10400000 */
    pgdir[64] = a ? (a | PTE_P | PTE_RW | PTE_US) : 0;
    pgdir[65] = b ? (b | PTE_P | PTE_RW | PTE_US) : 0;
    recarrega_cr3();
}
