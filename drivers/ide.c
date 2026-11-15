#include "kernel.h"
#include "pci.h"
#include "ide.h"
#include "timer.h"

#define R_COUNT 2
#define R_LBA0  3
#define R_LBA1  4
#define R_LBA2  5
#define R_DEV   6
#define R_CMD   7
#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DF   0x20
#define ST_BSY  0x80
#define TIMEOUT 1000000

typedef struct { uint16_t base, ctrl; bool slave; ide_info_t info; } drive_t;
static drive_t dr[4];

static void espera400(drive_t *d) { for (int i = 0; i < 4; i++) inb(d->ctrl); }

static int espera_bsy(drive_t *d) {
    for (uint32_t i = 0; i < TIMEOUT; i++) {
        uint8_t s = inb(d->base + R_CMD);
        if (!(s & ST_BSY)) return s;
    }
    return -1;
}
static int espera_drq(drive_t *d) {
    espera400(d);
    for (uint32_t i = 0; i < TIMEOUT; i++) {
        uint8_t s = inb(d->base + R_CMD);
        if (s & (ST_ERR | ST_DF)) return -1;
        if (!(s & ST_BSY) && (s & ST_DRQ)) return 0;
    }
    return -1;
}
static void seleciona(drive_t *d, uint8_t extra) {
    outb(d->base + R_DEV, 0xA0 | (d->slave << 4) | extra);
    espera400(d);
}
static void reset(drive_t *d) {
    outb(d->ctrl, 0x06); delay_ms(5);        /* SRST */
    outb(d->ctrl, 0x02); delay_ms(5);        /* limpa SRST, mantem nIEN */
    espera_bsy(d);
}

static int identify(drive_t *d) {
    uint16_t id[256];
    seleciona(d, 0);
    uint8_t st = inb(d->base + R_CMD);
    if (st == 0x00 || st == 0xFF) return -1;           /* nada conectado */

    outb(d->base + R_COUNT, 0); outb(d->base + R_LBA0, 0);
    outb(d->base + R_LBA1, 0);  outb(d->base + R_LBA2, 0);
    outb(d->base + R_CMD, 0xEC);                        /* IDENTIFY */
    if (inb(d->base + R_CMD) == 0) return -1;
    if (espera_bsy(d) < 0) return -1;

    bool atapi = false;
    uint8_t m = inb(d->base + R_LBA1), h = inb(d->base + R_LBA2);
    if (m == 0x14 && h == 0xEB) {                       /* ATAPI */
        atapi = true;
        outb(d->base + R_CMD, 0xA1);                    /* IDENTIFY PACKET */
        espera400(d);
        if (espera_bsy(d) < 0) return -1;
    } else if (m || h) return -1;                       /* nao e ATA */

    if (espera_drq(d)) return -1;
    insw(d->base, id, 256);

    for (int i = 0; i < 20; i++) {
        d->info.model[i * 2]     = id[27 + i] >> 8;
        d->info.model[i * 2 + 1] = id[27 + i] & 0xFF;
    }
    d->info.model[40] = 0;
    for (int i = 39; i >= 0 && (d->info.model[i] == ' ' || !d->info.model[i]); i--)
        d->info.model[i] = 0;

    d->info.present = true;
    d->info.atapi = atapi;
    if (!atapi) {
        d->info.lba48 = (id[83] >> 10) & 1;
        uint32_t s28 = id[60] | ((uint32_t)id[61] << 16);
        uint32_t lo  = id[100] | ((uint32_t)id[101] << 16);
        uint32_t hi  = id[102] | ((uint32_t)id[103] << 16);
        d->info.sectors = d->info.lba48 ? (hi ? 0xFFFFFFFFu : lo) : s28;
        if (!d->info.sectors) d->info.sectors = s28;
    }
    return 0;
}

static int comando(drive_t *d, uint32_t lba, uint32_t n, uint8_t *p, bool wr) {
    bool ext = d->info.lba48 && (lba >= 0x10000000u || lba + n > 0x10000000u);
    if (espera_bsy(d) < 0) return -1;

    if (ext) {                                          /* LBA48 */
        outb(d->base + R_DEV, 0x40 | (d->slave << 4));
        outb(d->base + R_COUNT, 0);
        outb(d->base + R_LBA0, lba >> 24);
        outb(d->base + R_LBA1, 0);
        outb(d->base + R_LBA2, 0);
        outb(d->base + R_COUNT, n);
        outb(d->base + R_LBA0, lba);
        outb(d->base + R_LBA1, lba >> 8);
        outb(d->base + R_LBA2, lba >> 16);
        outb(d->base + R_CMD, wr ? 0x34 : 0x24);
    } else {                                            /* LBA28 */
        outb(d->base + R_DEV, 0xE0 | (d->slave << 4) | ((lba >> 24) & 0x0F));
        outb(d->base + R_COUNT, n);
        outb(d->base + R_LBA0, lba);
        outb(d->base + R_LBA1, lba >> 8);
        outb(d->base + R_LBA2, lba >> 16);
        outb(d->base + R_CMD, wr ? 0x30 : 0x20);
    }
    for (uint32_t i = 0; i < n; i++) {
        if (espera_drq(d)) return -1;
        if (wr) outsw(d->base, p, 256); else insw(d->base, p, 256);
        p += 512;
    }
    if (wr) {
        outb(d->base + R_CMD, ext ? 0xEA : 0xE7);       /* flush cache */
        if (espera_bsy(d) < 0) return -1;
    }
    return (inb(d->ctrl) & (ST_ERR | ST_DF)) ? -1 : 0;
}

static int transferir(int n, uint32_t lba, uint32_t cnt, uint8_t *p, bool wr) {
    if (n < 0 || n > 3 || !dr[n].info.present || dr[n].info.atapi) return -1;
    drive_t *d = &dr[n];
    if (!cnt) return 0;
    if (lba >= d->info.sectors || cnt > d->info.sectors - lba) return -1;
    while (cnt) {
        uint32_t q = cnt > 128 ? 128 : cnt;
        if (comando(d, lba, q, p, wr)) { reset(d); return -1; }
        lba += q; cnt -= q; p += q * 512;
    }
    return 0;
}

int ide_read(int n, uint32_t lba, uint32_t c, void *buf) {
    return transferir(n, lba, c, buf, false);
}
int ide_write(int n, uint32_t lba, uint32_t c, const void *buf) {
    return transferir(n, lba, c, (uint8_t *)buf, true);
}
const ide_info_t *ide_info(int n) { return (n >= 0 && n < 4) ? &dr[n].info : 0; }

static int probe(pci_dev_t *p) {
    uint16_t base[2], ctrl[2];
    if (p->prog_if & 1) { base[0] = p->bar[0] & ~3u; ctrl[0] = (p->bar[1] & ~3u) + 2; }
    else                { base[0] = 0x1F0;           ctrl[0] = 0x3F6; }
    if (p->prog_if & 4) { base[1] = p->bar[2] & ~3u; ctrl[1] = (p->bar[3] & ~3u) + 2; }
    else                { base[1] = 0x170;           ctrl[1] = 0x376; }

    pci_enable_busmaster(p);
    for (int i = 0; i < 4; i++) {
        memset(&dr[i], 0, sizeof dr[i]);
        dr[i].base = base[i / 2]; dr[i].ctrl = ctrl[i / 2]; dr[i].slave = i & 1;
        outb(dr[i].ctrl, 0x02);                         /* nIEN: sem IRQ, so polling */
        identify(&dr[i]);
    }
    return 0;
}
pci_driver_t drv_ide = { "ide", PCI_ANY16, PCI_ANY16, 0x01, 0x01, probe };
