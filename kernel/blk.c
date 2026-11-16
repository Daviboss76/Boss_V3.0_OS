#include "kernel.h"
#include "ide.h"
#include "ahci.h"
#include "heal.h"
#include "part.h"
#include "blk.h"

typedef struct { int be, idx; blk_info_t info; } rdisk_t;                 /* be: 0 IDE, 1 AHCI */
typedef struct { int disco; uint32_t ini, tam; blk_info_t info; } vol_t;
typedef struct { int d; uint32_t lba, n; } jan_t;

static rdisk_t discos[8];
static vol_t vols[8];
static jan_t jan[8];
static int ndisc, nvol, njan, tem_ide, tem_ahci;
static uint32_t erros;
static pdisk_t scan;

static void novo_disco(int be, int idx, const char *modelo, uint32_t setores) {
    rdisk_t *d = &discos[ndisc];
    d->be = be; d->idx = idx;
    ksnprintf(d->info.nome, sizeof d->info.nome, "disco%u", (uint32_t)ndisc);
    ksnprintf(d->info.modelo, sizeof d->info.modelo, "%s", modelo);
    d->info.setores = setores;
    ndisc++;
}

static int raw_rw(rdisk_t *d, uint32_t lba, uint32_t n, void *buf, bool wr) {
    for (int t = 0; t < 3; t++) {
        int r;
        if (d->be == 0) r = wr ? ide_write(d->idx, lba, n, buf)  : ide_read(d->idx, lba, n, buf);
        else            r = wr ? ahci_write(d->idx, lba, n, buf) : ahci_read(d->idx, lba, n, buf);
        if (!r) return 0;
        erros++;
    }
    heal_log("%s: E/S falhou (%s) lba=%u", d->info.nome, wr ? "escrita" : "leitura", lba);
    return -1;
}

/* ---------- discos fisicos ---------- */
int disk_count(void) { return ndisc; }
const blk_info_t *disk_info(int d) { return (d >= 0 && d < ndisc) ? &discos[d].info : 0; }

int disk_read(int d, uint32_t lba, uint32_t n, void *buf) {
    if (d < 0 || d >= ndisc) return -1;
    return raw_rw(&discos[d], lba, n, buf, false);
}

void disk_arm(int d, uint32_t lba, uint32_t n) { if (njan < 8) { jan[njan].d = d; jan[njan].lba = lba; jan[njan].n = n; njan++; } }
void disk_disarm(void) { njan = 0; }

static bool permitido(int d, uint32_t lba, uint32_t n) {
    for (int i = 0; i < njan; i++)
        if (jan[i].d == d && lba >= jan[i].lba && lba + n <= jan[i].lba + jan[i].n) return true;
    return false;
}

int disk_write(int d, uint32_t lba, uint32_t n, const void *buf) {
    if (d < 0 || d >= ndisc) return -1;
    if (!permitido(d, lba, n)) {
        heal_log("%s: escrita BLOQUEADA lba=%u n=%u", discos[d].info.nome, lba, n);
        return -2;
    }
    return raw_rw(&discos[d], lba, n, (void *)buf, true);
}

/* ---------- volumes BossOS (particoes com rotulo BOSSOS) ---------- */
void blk_rescan(void) {
    nvol = 0;
    for (int d = 0; d < ndisc; d++) {
        if (part_scan(d, &scan)) continue;
        for (int i = 0; i < scan.n && nvol < 8; i++) {
            part_t *p = &scan.p[i];
            if (!p->dono || p->grande) continue;
            vol_t *v = &vols[nvol];
            v->disco = d; v->ini = p->inicio; v->tam = p->setores;
            ksnprintf(v->info.nome, sizeof v->info.nome, "vol%u", (uint32_t)nvol);
            ksnprintf(v->info.modelo, sizeof v->info.modelo, "%s", discos[d].info.modelo);
            v->info.setores = p->setores + 1;           /* +1: setor 0 sintetico */
            nvol++;
        }
    }
}

void blk_init(void) {
    ndisc = 0; tem_ide = tem_ahci = 0;
    for (int i = 0; i < 4 && ndisc < 8; i++) {
        const ide_info_t *d = ide_info(i);
        if (d && d->present && !d->atapi) { novo_disco(0, i, d->model, d->sectors); tem_ide = 1; }
    }
    for (int i = 0; i < 4 && ndisc < 8; i++) {
        const ahci_info_t *d = ahci_info(i);
        if (d && d->present && !d->atapi) { novo_disco(1, i, d->model, d->sectors); tem_ahci = 1; }
    }
    blk_rescan();
}

const char *blk_backend(void) {
    return tem_ide && tem_ahci ? "ide+ahci" : tem_ide ? "ide" : tem_ahci ? "ahci" : "nenhum";
}
uint32_t blk_erros(void) { return erros; }
int blk_count(void) { return nvol; }
const blk_info_t *blk_info(int i) { return (i >= 0 && i < nvol) ? &vols[i].info : 0; }

bool blk_vol_info(int i, int *disco, uint32_t *ini, uint32_t *tam) {
    if (i < 0 || i >= nvol) return false;
    *disco = vols[i].disco; *ini = vols[i].ini; *tam = vols[i].tam;
    return true;
}
void blk_prefer(int disco, uint32_t ini) {
    for (int i = 0; i < nvol; i++)
        if (vols[i].disco == disco && vols[i].ini == ini) { vol_t t = vols[0]; vols[0] = vols[i]; vols[i] = t; return; }
}

/* O volume aparece ao FAT32 como um "disco" com um MBR sintetico no setor 0
   (1 particao FAT32 em LBA 1); o resto mapeia direto para a particao real. */
static void sintetiza(vol_t *v, uint8_t *p) {
    memset(p, 0, 512);
    p[446 + 4] = 0x0C;
    uint32_t um = 1, tam = v->tam;
    memcpy(p + 446 + 8, &um, 4);
    memcpy(p + 446 + 12, &tam, 4);
    p[510] = 0x55; p[511] = 0xAA;
}

int blk_read(int i, uint32_t lba, uint32_t n, void *buf) {
    if (i < 0 || i >= nvol || !n) return -1;
    vol_t *v = &vols[i];
    if (lba + n < lba || lba + n > v->tam + 1) return -1;
    uint8_t *p = buf;
    if (lba == 0) {
        sintetiza(v, p);
        if (!--n) return 0;
        lba = 1; p += 512;
    }
    return disk_read(v->disco, v->ini + (lba - 1), n, p);
}

int blk_write(int i, uint32_t lba, uint32_t n, const void *buf) {
    if (i < 0 || i >= nvol || !n || lba == 0) return -1;          /* nunca grava o MBR sintetico */
    vol_t *v = &vols[i];
    if (lba + n < lba || lba + n > v->tam + 1) return -1;
    return raw_rw(&discos[v->disco], v->ini + (lba - 1), n, (void *)buf, true);
}
