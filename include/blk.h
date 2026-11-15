#ifndef BLK_H
#define BLK_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char nome[8];          /* "hd0" (IDE) ou "sata0" (AHCI) */
    char modelo[41];
    uint32_t setores;
} blk_info_t;

void blk_init(void);                   /* escolhe o backend: IDE se existir, senao AHCI */
const char *blk_backend(void);         /* "ide", "ahci" ou "nenhum" */
int  blk_count(void);
const blk_info_t *blk_info(int i);
int  blk_read(int i, uint32_t lba, uint32_t n, void *buf);          /* 0 = ok */
int  blk_write(int i, uint32_t lba, uint32_t n, const void *buf);
uint32_t blk_erros(void);
#endif
