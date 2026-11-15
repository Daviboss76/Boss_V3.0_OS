#include "kernel.h"
#include "ide.h"
#include "ahci.h"
#include "heal.h"
#include "blk.h"

enum { B_NENHUM, B_IDE, B_AHCI };

typedef struct { int idx; blk_info_t info; } dev_t_;
static dev_t_ devs[8];
static int ndev, ativo;
static uint32_t erros;

static void adiciona(int idx, const char *prefixo, const char *modelo, uint32_t setores) {
    dev_t_ *d = &devs[ndev];
    d->idx = idx;
    ksnprintf(d->info.nome, sizeof d->info.nome, "%s%u", prefixo, (uint32_t)idx);
    ksnprintf(d->info.modelo, sizeof d->info.modelo, "%s", modelo);
    d->info.setores = setores;
    ndev++;
}

void blk_init(void) {
    ndev = 0; ativo = B_NENHUM;
    for (int i = 0; i < 4; i++) {                      /* 1o: IDE */
        const ide_info_t *d = ide_info(i);
        if (d && d->present && !d->atapi) { ativo = B_IDE; adiciona(i, "hd", d->model, d->sectors); }
    }
    if (ativo != B_NENHUM) return;
    for (int i = 0; i < 4; i++) {                      /* senao: AHCI (SATA/SSD) */
        const ahci_info_t *d = ahci_info(i);
        if (d && d->present && !d->atapi) { ativo = B_AHCI; adiciona(i, "sata", d->model, d->sectors); }
    }
}

const char *blk_backend(void) {
    return ativo == B_IDE ? "ide" : ativo == B_AHCI ? "ahci" : "nenhum";
}
int blk_count(void) { return ndev; }
const blk_info_t *blk_info(int i) { return (i >= 0 && i < ndev) ? &devs[i].info : 0; }
uint32_t blk_erros(void) { return erros; }

int blk_read(int i, uint32_t lba, uint32_t n, void *buf) {
    if (i < 0 || i >= ndev) return -1;
    for (int t = 0; t < 3; t++) {
        int r = (ativo == B_IDE) ? ide_read(devs[i].idx, lba, n, buf)
                                 : ahci_read(devs[i].idx, lba, n, buf);
        if (!r) return 0;
        erros++;
    }
    heal_log("blk: leitura falhou em %s lba=%u", devs[i].info.nome, lba);
    return -1;
}

int blk_write(int i, uint32_t lba, uint32_t n, const void *buf) {
    if (i < 0 || i >= ndev) return -1;
    for (int t = 0; t < 3; t++) {
        int r = (ativo == B_IDE) ? ide_write(devs[i].idx, lba, n, buf)
                                 : ahci_write(devs[i].idx, lba, n, buf);
        if (!r) return 0;
        erros++;
    }
    heal_log("blk: escrita falhou em %s lba=%u", devs[i].info.nome, lba);
    return -1;
}
