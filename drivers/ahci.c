#include "kernel.h"
#include "pci.h"
#include "ahci.h"

#define MAXP 4
#define TIMEOUT 3000000u

/* registradores do HBA (indices em dwords) */
#define H_GHC   1
#define H_PI    3
#define H_CAP2  9
#define H_BOHC  10
/* registradores de porta (indices em dwords) */
#define P_CLB   0
#define P_CLBU  1
#define P_FB    2
#define P_FBU   3
#define P_IS    4
#define P_IE    5
#define P_CMD   6
#define P_TFD   8
#define P_SIG   9
#define P_SSTS  10
#define P_SERR  12
#define P_CI    14

#define CMD_ST  (1u << 0)
#define CMD_FRE (1u << 4)
#define CMD_FR  (1u << 14)
#define CMD_CR  (1u << 15)
#define TFD_ERR 0x01u
#define TFD_DRQ 0x08u
#define TFD_BSY 0x80u
#define IS_TFES (1u << 30)

typedef struct { uint32_t flags, prdbc, ctba, ctbau, rsv[4]; } cmdhdr_t;
typedef struct { uint32_t dba, dbau, rsv, dbc; } prd_t;
typedef struct {
    uint8_t cfis[64], acmd[16], rsv[48];
    prd_t prdt[1];
    uint8_t pad[112];                                  /* 256 bytes por tabela */
} cmdtbl_t;

typedef struct {
    volatile uint32_t *r;
    bool presente, atapi, lba48;
    uint32_t setores;
} port_t;

static volatile uint32_t *hba;
static port_t ports[MAXP];
static ahci_info_t infos[MAXP];
static cmdhdr_t clb[MAXP][32] __attribute__((aligned(1024)));
static uint8_t  fisrx[MAXP][256] __attribute__((aligned(256)));
static cmdtbl_t ctbl[MAXP] __attribute__((aligned(128)));
static uint8_t  bounce[512] __attribute__((aligned(4)));
static uint16_t idbuf[256] __attribute__((aligned(4)));

static int para(volatile uint32_t *p) {
    p[P_CMD] &= ~CMD_ST;
    for (uint32_t i = 0; i < TIMEOUT && (p[P_CMD] & CMD_CR); i++) ;
    if (p[P_CMD] & CMD_CR) return -1;
    p[P_CMD] &= ~CMD_FRE;
    for (uint32_t i = 0; i < TIMEOUT && (p[P_CMD] & CMD_FR); i++) ;
    return (p[P_CMD] & CMD_FR) ? -1 : 0;
}
static void inicia(volatile uint32_t *p) {
    for (uint32_t i = 0; i < TIMEOUT && (p[P_CMD] & CMD_CR); i++) ;
    p[P_CMD] |= CMD_FRE;
    p[P_CMD] |= CMD_ST;
}
static void recupera(int n) {                          /* autorreparo da porta */
    volatile uint32_t *p = ports[n].r;
    para(p);
    p[P_SERR] = 0xFFFFFFFFu;
    p[P_IS]   = 0xFFFFFFFFu;
    inicia(p);
}

static int comando(int n, uint8_t cmd, uint32_t lba, uint32_t cnt,
                   void *buf, uint32_t bytes, bool wr) {
    port_t *pt = &ports[n];
    volatile uint32_t *p = pt->r;
    uint32_t i;
    for (i = 0; i < TIMEOUT && (p[P_TFD] & (TFD_BSY | TFD_DRQ)); i++) ;
    if (i == TIMEOUT) { recupera(n); return -1; }
    p[P_IS] = 0xFFFFFFFFu;

    cmdtbl_t *t = &ctbl[n];
    memset(t, 0, sizeof *t);
    uint8_t *f = t->cfis;
    f[0] = 0x27; f[1] = 0x80; f[2] = cmd;
    f[7] = (cmd == 0xEC) ? 0 : (uint8_t)(0x40 | (pt->lba48 ? 0 : ((lba >> 24) & 0x0F)));
    f[4] = lba; f[5] = lba >> 8; f[6] = lba >> 16;
    if (pt->lba48) f[8] = lba >> 24;
    f[12] = cnt; f[13] = cnt >> 8;

    uint32_t prdtl = 0;
    if (bytes) { t->prdt[0].dba = (uint32_t)buf; t->prdt[0].dbc = bytes - 1; prdtl = 1; }
    cmdhdr_t *h = &clb[n][0];
    h->flags = 5 | (wr ? (1u << 6) : 0) | (prdtl << 16);
    h->prdbc = 0; h->ctba = (uint32_t)t; h->ctbau = 0;

    __asm__ volatile ("" ::: "memory");
    p[P_CI] = 1;
    for (i = 0; i < TIMEOUT; i++) {
        if (!(p[P_CI] & 1)) break;
        if (p[P_IS] & IS_TFES) { recupera(n); return -1; }
    }
    __asm__ volatile ("" ::: "memory");
    if (i == TIMEOUT) { recupera(n); return -1; }
    if ((p[P_IS] & IS_TFES) || (p[P_TFD] & TFD_ERR)) { recupera(n); return -1; }
    return 0;
}

static int identify(int n) {
    port_t *pt = &ports[n];
    pt->lba48 = true; pt->setores = 0;
    if (comando(n, 0xEC, 0, 0, idbuf, 512, false)) return -1;
    if ((idbuf[106] & 0xD000) == 0x5000) return -1;    /* setor logico > 512 B: nao suportado */
    for (int i = 0; i < 20; i++) {
        infos[n].model[i * 2]     = idbuf[27 + i] >> 8;
        infos[n].model[i * 2 + 1] = idbuf[27 + i] & 0xFF;
    }
    infos[n].model[40] = 0;
    for (int i = 39; i >= 0 && (infos[n].model[i] == ' ' || !infos[n].model[i]); i--)
        infos[n].model[i] = 0;
    pt->lba48 = (idbuf[83] >> 10) & 1;
    uint32_t s28 = idbuf[60] | ((uint32_t)idbuf[61] << 16);
    uint32_t lo  = idbuf[100] | ((uint32_t)idbuf[101] << 16);
    uint32_t hi  = idbuf[102] | ((uint32_t)idbuf[103] << 16);
    pt->setores = pt->lba48 ? (hi ? 0xFFFFFFFFu : lo) : s28;
    if (!pt->setores) pt->setores = s28;
    return 0;
}

static int xfer(int n, uint32_t lba, uint32_t cnt, uint8_t *buf, bool wr) {
    if (n < 0 || n >= MAXP) return -1;
    port_t *pt = &ports[n];
    if (!pt->presente || pt->atapi) return -1;
    if (!cnt) return 0;
    if (lba >= pt->setores || cnt > pt->setores - lba) return -1;
    uint8_t cmd = pt->lba48 ? (wr ? 0x35 : 0x25) : (wr ? 0xCA : 0xC8);
    while (cnt) {
        if ((uint32_t)buf & 1) {                       /* DMA exige endereco par: usa bounce */
            if (wr) memcpy(bounce, buf, 512);
            if (comando(n, cmd, lba, 1, bounce, 512, wr)) return -1;
            if (!wr) memcpy(buf, bounce, 512);
            lba++; cnt--; buf += 512;
        } else {
            uint32_t q = cnt > 128 ? 128 : cnt;
            if (comando(n, cmd, lba, q, buf, q * 512, wr)) return -1;
            lba += q; cnt -= q; buf += q * 512;
        }
    }
    if (wr && comando(n, pt->lba48 ? 0xEA : 0xE7, 0, 0, 0, 0, false)) return -1;
    return 0;
}

int ahci_read(int n, uint32_t lba, uint32_t c, void *buf) { return xfer(n, lba, c, buf, false); }
int ahci_write(int n, uint32_t lba, uint32_t c, const void *buf) { return xfer(n, lba, c, (uint8_t *)buf, true); }
const ahci_info_t *ahci_info(int n) { return (n >= 0 && n < MAXP) ? &infos[n] : 0; }

static int probe(pci_dev_t *d) {
    if (d->prog_if != 0x01 || hba) return -1;          /* so AHCI, e so o 1o controlador */
    uint32_t bar = d->bar[5] & ~0xFu;
    if (!bar) return -1;
    pci_enable_busmaster(d);
    hba = (volatile uint32_t *)bar;

    hba[H_GHC] |= (1u << 31);                          /* AHCI enable */
    if (hba[H_CAP2] & 1) {                             /* BIOS handoff */
        hba[H_BOHC] |= 2;
        for (uint32_t i = 0; i < TIMEOUT && (hba[H_BOHC] & 1); i++) ;
    }

    uint32_t pi = hba[H_PI];
    int n = 0;
    for (int i = 0; i < 32 && n < MAXP; i++) {
        if (!(pi & (1u << i))) continue;
        volatile uint32_t *p = hba + (0x100 + i * 0x80) / 4;
        uint32_t ssts = p[P_SSTS];
        if ((ssts & 0xF) != 3 || ((ssts >> 8) & 0xF) != 1) continue;   /* sem dispositivo ativo */
        uint32_t sig = p[P_SIG];
        if (sig != 0x00000101u && sig != 0xEB140101u) continue;

        ports[n].r = p;
        para(p);
        memset(clb[n], 0, sizeof clb[n]);
        memset(fisrx[n], 0, sizeof fisrx[n]);
        p[P_CLB] = (uint32_t)clb[n];   p[P_CLBU] = 0;
        p[P_FB]  = (uint32_t)fisrx[n]; p[P_FBU]  = 0;
        p[P_SERR] = 0xFFFFFFFFu; p[P_IS] = 0xFFFFFFFFu; p[P_IE] = 0;   /* so polling */
        inicia(p);

        ports[n].atapi = (sig == 0xEB140101u);
        if (ports[n].atapi) {
            ports[n].presente = true;
            ksnprintf(infos[n].model, sizeof infos[n].model, "ATAPI");
        } else if (identify(n) == 0) {
            ports[n].presente = true;
        } else {
            n++; continue;
        }
        infos[n].present = true;
        infos[n].atapi = ports[n].atapi;
        infos[n].sectors = ports[n].setores;
        n++;
    }
    return 0;
}
pci_driver_t drv_ahci = { "ahci", PCI_ANY16, PCI_ANY16, 0x01, 0x06, probe };
