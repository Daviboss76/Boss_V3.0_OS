#ifndef PART_H
#define PART_H
#include <stdint.h>
#include <stdbool.h>

#define MAXP 12

typedef struct {
    uint32_t inicio, setores, fs_setores;     /* fs_setores: tamanho que o proprio FS declara (0 = desconhecido) */
    uint8_t  tipo, slot;
    bool     grande, dono;                    /* grande: LBA > 32 bits; dono: FAT32 rotulo BOSSOS */
    uint8_t  raw[128];                        /* GPT: entrada original */
    char     fs[8], nome[20];
} part_t;

typedef struct {
    int dev, tabela, n;                       /* tabela: 0 nenhuma, 1 MBR, 2 GPT, 3 GPT invalida */
    bool truncada;
    uint32_t total, gpt_primeiro, gpt_ultimo;
    part_t p[MAXP];
} pdisk_t;

uint32_t crc32_ini(void);
uint32_t crc32_add(uint32_t c, const void *p, uint32_t n);
uint32_t crc32_fim(uint32_t c);

int  part_scan(int dev, pdisk_t *d);
int  part_livres(const pdisk_t *d, uint32_t *ini, uint32_t *tam, int max);
const char *part_tipo(const pdisk_t *d, const part_t *p);
int  part_tabela_mbr(int dev);
int  part_criar(pdisk_t *d, uint32_t ini, uint32_t tam);
int  part_apagar(pdisk_t *d, int i);
int  part_redim(pdisk_t *d, int i, uint32_t novo);
int  part_mkfs(int dev, uint32_t ini, uint32_t tam);
int  part_backup(int dev);
int  part_restaurar(int dev);
#endif
