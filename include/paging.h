#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>
#include <stdbool.h>

#define PTE_P   0x001u
#define PTE_RW  0x002u
#define PTE_US  0x004u
#define PTE_PWT 0x008u
#define PTE_PCD 0x010u

void     paging_init(void);
uint32_t frame_alloc(void);
uint32_t frame_alloc_contig(uint32_t n);
void     frame_free(uint32_t pa);
uint32_t frames_used(void);
uint32_t frames_total(void);
void     paging_wp(bool ligado);
uint32_t paging_cr2(void);
void     paging_user_set(uint32_t pt64, uint32_t pt65);
void     paging_map_mmio(uint32_t fisico, uint32_t tam);   /* identidade, sem cache (BARs de PCI) */
void paging_allow_user(uint32_t addr, uint32_t tam);

#endif
