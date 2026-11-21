#include "kernel.h"
#include "paging.h"
#include "abi.h"

#define POOL_INI 0x00800000u
#define POOL_N   4096u

extern uint8_t __kernel_start[], __guard_end[];

static uint32_t pgdir[1024] __attribute__((aligned(4096)));
static uint32_t pt0[1024]   __attribute__((aligned(4096)));
static uint32_t pt1[1024]   __attribute__((aligned(4096)));
static uint8_t  mapa[POOL_N / 8];
static uint32_t usados, dica;

static inline uint32_t le_cr0(void) { uint32_t v; __asm__ volatile ("mov %%cr0,%0" : "=r"(v)); return v; }
static inline void esc_cr0(uint32_t v) { __asm__ volatile ("mov %0,%%cr0" :: "r"(v) : "memory"); }
static inline void recarrega_cr3(void) {
    uint32_t v; __asm__ volatile ("mov %%cr3,%0; mov %0,%%cr3" : "=r"(v) :: "memory");
}

void paging_init(void) {
    uint32_t ks = (uint32_t)__kernel_start, ke = (uint32_t)__guard_end;
    for (uint32_t i = 1; i < 1024; i++) {
        uint32_t pa = i << 12;
        pt0[i] = pa | ((pa >= ks && pa < ke) ? PTE_P : (PTE_P | PTE_RW));
    }
    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t pa = 0x400000u + (i << 12), fl = PTE_P | PTE_RW;
        if (pa >= SLOT_BASE && pa < SLOT_BASE + N_SLOTS * SLOT_SIZE) fl |= PTE_US;
        pt1[i] = pa | fl;
    }
    pgdir[0] = (uint32_t)pt0 | PTE_P | PTE_RW;
    pgdir[1] = (uint32_t)pt1 | PTE_P | PTE_RW | PTE_US;
    for (uint32_t i = 2; i < 6; i++) pgdir[i] = (i << 22) | PTE_P | PTE_RW | 0x80;
    for (uint32_t i = 896; i < 1024; i++)
        pgdir[i] = (i << 22) | PTE_P | PTE_RW | PTE_PWT | PTE_PCD | 0x80;

    uint32_t cr4;
    __asm__ volatile ("mov %%cr4,%0" : "=r"(cr4));
    cr4 |= 0x10;
    __asm__ volatile ("mov %0,%%cr4" :: "r"(cr4));
    __asm__ volatile ("mov %0,%%cr3" :: "r"((uint32_t)pgdir) : "memory");
    esc_cr0(le_cr0() | 0x80010000u);
}

void paging_map_mmio(uint32_t fisico, uint32_t tam) {
    if (!tam) return;
    uint32_t ini = fisico >> 22, fim = (fisico + tam - 1) >> 22;
    for (uint32_t i = ini; i <= fim && i < 1024; i++)
        if (!pgdir[i] && i != 64 && i != 65)
            pgdir[i] = (i << 22) | PTE_P | PTE_RW | PTE_PWT | PTE_PCD | 0x80;
    recarrega_cr3();
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

void paging_user_set(uint32_t a, uint32_t b) {
    pgdir[64] = a ? (a | PTE_P | PTE_RW | PTE_US) : 0;
    pgdir[65] = b ? (b | PTE_P | PTE_RW | PTE_US) : 0;
    recarrega_cr3();
}

void paging_allow_user(uint32_t addr, uint32_t tam) {
    if (!tam) return;
    uint32_t ini = addr & ~0xFFFu;
    uint32_t fim = (addr + tam - 1) & ~0xFFFu;

    for (uint32_t va = ini; ; va += 0x1000u) {
        uint32_t pde = va >> 22;
        if (pde < 1024 && pgdir[pde]) {
            pgdir[pde] |= PTE_US;
            if (pde == 0) {
                uint32_t pte = (va >> 12) & 0x3FFu;
                pt0[pte] |= PTE_US;
            } else if (pde == 1) {
                uint32_t pte = (va >> 12) & 0x3FFu;
                pt1[pte] |= PTE_US;
            }
        }
        if (va == fim || va >= 0xFFFFF000u) break;
    }
    recarrega_cr3();
}

