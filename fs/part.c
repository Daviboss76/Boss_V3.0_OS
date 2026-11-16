#include "kernel.h"
#include "blk.h"
#include "fs.h"
#include "paging.h"
#include "timer.h"
#include "part.h"

static uint8_t tb[512]   __attribute__((aligned(4)));
static uint8_t sec[512]  __attribute__((aligned(4)));
static uint8_t sc2[512]  __attribute__((aligned(4)));
static uint8_t zbuf[4096] __attribute__((aligned(4)));

uint32_t crc32_ini(void) { return 0xFFFFFFFFu; }
uint32_t crc32_add(uint32_t c, const void *p, uint32_t n) {
    const uint8_t *b = p;
    while (n--) { c ^= *b++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1)); }
    return c;
}
uint32_t crc32_fim(uint32_t c) { return ~c; }

static uint32_t rnd(void) {
    static uint32_t s;
    if (!s) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); s = lo ^ (ticks * 2654435761u) ^ 0x9E3779B9u; if (!s) s = 1; }
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

static const uint8_t G_DADOS[16] = { 0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7 };
static const uint8_t G_EFI[16]   = { 0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B };
static const uint8_t G_LINUX[16] = { 0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4 };
static const uint8_t G_MSR[16]   = { 0x16,0xE3,0xC9,0xE3,0x5C,0x0B,0xB8,0x4D,0x81,0x7D,0xF9,0x2D,0xF0,0x02,0x15,0xAE };

const char *part_tipo(const pdisk_t *d, const part_t *p) {
    if (d->tabela == 2) {
        if (!memcmp(p->raw, G_EFI, 16))   return "EFI System";
        if (!memcmp(p->raw, G_DADOS, 16)) return "Dados Win/FAT";
        if (!memcmp(p->raw, G_LINUX, 16)) return "Linux";
        if (!memcmp(p->raw, G_MSR, 16))   return "MS reservada";
        return "GUID outro";
    }
    switch (p->tipo) {
    case 0x05: case 0x0F: case 0x85: return "estendida";
    case 0x07: return "NTFS/exFAT";
    case 0x0B: case 0x0C: return "FAT32";
    case 0x01: case 0x04: case 0x06: case 0x0E: return "FAT12/16";
    case 0x82: return "Linux swap";
    case 0x83: return "Linux";
    case 0xEF: return "EFI";
    case 0x27: case 0x12: return "recuperacao";
    case 0xAF: return "HFS/APFS";
    default:   return "outro";
    }
}

/* identifica o sistema de arquivos e o tamanho que ele proprio declara */
static void sonda(int dev, part_t *p) {
    strcpy(p->fs, "?");
    if (p->grande || !p->inicio) return;
    if (disk_read(dev, p->inicio, 1, sec)) return;
    uint32_t v = 0;
    if (!memcmp(sec + 3, "NTFS    ", 8)) { strcpy(p->fs, "NTFS"); memcpy(&v, sec + 0x28, 4); p->fs_setores = v + 1; return; }
    if (!memcmp(sec + 3, "-FVE-FS-", 8)) { strcpy(p->fs, "BitLckr"); return; }
    if (!memcmp(sec + 3, "EXFAT   ", 8)) { strcpy(p->fs, "exFAT"); memcpy(&v, sec + 0x48, 4); p->fs_setores = v; return; }
    if (!memcmp(sec + 0x52, "FAT32   ", 8)) {
        strcpy(p->fs, "FAT32"); memcpy(&v, sec + 0x20, 4); p->fs_setores = v;
        p->dono = !memcmp(sec + 0x47, "BOSSOS     ", 11);
        return;
    }
    if (!memcmp(sec + 0x36, "FAT16   ", 8) || !memcmp(sec + 0x36, "FAT12   ", 8)) {
        strcpy(p->fs, "FAT16");
        uint16_t s16; memcpy(&s16, sec + 0x13, 2);
        if (s16) v = s16; else memcpy(&v, sec + 0x20, 4);
        p->fs_setores = v; return;
    }
    if (!disk_read(dev, p->inicio + 2, 1, sc2) && sc2[56] == 0x53 && sc2[57] == 0xEF) {
        strcpy(p->fs, "ext2-4");
        uint32_t bl, lg; memcpy(&bl, sc2 + 4, 4); memcpy(&lg, sc2 + 24, 4);
        if (lg < 8) p->fs_setores = bl << (1 + lg);
    }
}

int part_scan(int dev, pdisk_t *d) {
    memset(d, 0, sizeof *d);
    d->dev = dev;
    const blk_info_t *bi = disk_info(dev);
    if (!bi) return -1;
    d->total = bi->setores;
    if (disk_read(dev, 0, 1, tb)) return -1;
    if (tb[510] != 0x55 || tb[511] != 0xAA) return 0;

    bool prot = false;
    for (int i = 0; i < 4; i++) if (tb[446 + i * 16 + 4] == 0xEE) prot = true;

    if (!prot) {
        d->tabela = 1;
        for (int i = 0; i < 4; i++) {
            const uint8_t *e = tb + 446 + i * 16;
            if (!e[4]) continue;
            part_t *p = &d->p[d->n++];
            p->slot = i; p->tipo = e[4];
            memcpy(&p->inicio, e + 8, 4); memcpy(&p->setores, e + 12, 4);
        }
    } else {
        d->tabela = 2;
        if (disk_read(dev, 1, 1, tb) || memcmp(tb, "EFI PART", 8)) { d->tabela = 3; return 0; }
        uint32_t elba, nent, esz;
        memcpy(&elba, tb + 72, 4); memcpy(&nent, tb + 80, 4); memcpy(&esz, tb + 84, 4);
        memcpy(&d->gpt_primeiro, tb + 40, 4); memcpy(&d->gpt_ultimo, tb + 48, 4);
        if (esz != 128 || !nent || nent > 128) { d->tabela = 3; return 0; }
        for (uint32_t s = 0; s < (nent + 3) / 4; s++) {
            if (disk_read(dev, elba + s, 1, tb)) break;
            for (uint32_t k = 0; k < 4 && s * 4 + k < nent; k++) {
                const uint8_t *e = tb + k * 128;
                bool vazio = true;
                for (int b = 0; b < 16; b++) if (e[b]) vazio = false;
                if (vazio) continue;
                if (d->n >= MAXP) { d->truncada = true; continue; }
                part_t *p = &d->p[d->n++];
                p->slot = s * 4 + k;
                memcpy(p->raw, e, 128);
                uint32_t ilo, ihi, flo, fhi;
                memcpy(&ilo, e + 32, 4); memcpy(&ihi, e + 36, 4);
                memcpy(&flo, e + 40, 4); memcpy(&fhi, e + 44, 4);
                p->grande = ihi || fhi;
                p->inicio = ilo; p->setores = flo - ilo + 1;
                for (int c = 0; c < 19; c++) {
                    uint16_t u = e[56 + c * 2] | (e[57 + c * 2] << 8);
                    if (!u) break;
                    p->nome[c] = u < 128 ? (char)u : '?';
                }
            }
        }
    }
    for (int i = 0; i < d->n; i++) {
        part_t *p = &d->p[i];
        if (d->tabela == 1 && (p->tipo == 0x05 || p->tipo == 0x0F || p->tipo == 0x85)) strcpy(p->fs, "estend");
        else sonda(dev, p);
    }
    return 0;
}

int part_livres(const pdisk_t *d, uint32_t *ini, uint32_t *tam, int max) {
    if ((d->tabela != 1 && d->tabela != 2) || d->truncada || d->total == 0xFFFFFFFFu) return 0;
    for (int i = 0; i < d->n; i++) if (d->p[i].grande) return 0;
    uint32_t lo = d->tabela == 2 ? d->gpt_primeiro : 2048u;
    uint32_t hi = d->tabela == 2 ? d->gpt_ultimo + 1 : d->total;
    int ord[MAXP];
    for (int i = 0; i < d->n; i++) ord[i] = i;
    for (int i = 0; i < d->n; i++)
        for (int j = i + 1; j < d->n; j++)
            if (d->p[ord[j]].inicio < d->p[ord[i]].inicio) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
    int n = 0; uint32_t cur = lo;
    for (int k = 0; k <= d->n; k++) {
        uint32_t fim = k < d->n ? d->p[ord[k]].inicio : hi;
        uint32_t g0 = (cur + 2047u) & ~2047u, g1 = fim & ~2047u;
        if (g1 > g0 && g1 - g0 >= 2048u && n < max) { ini[n] = g0; tam[n] = g1 - g0; n++; }
        if (k < d->n) {
            uint32_t e = d->p[ord[k]].inicio + d->p[ord[k]].setores;
            if (e > cur) cur = e;
        }
    }
    return n;
}

/* ---------- escrita da tabela (sempre em janelas exatas) ---------- */
static int mbr_escreve(int dev, int slot, const uint8_t *ent) {
    if (disk_read(dev, 0, 1, tb) || tb[510] != 0x55 || tb[511] != 0xAA) return -1;
    if (ent) memcpy(tb + 446 + slot * 16, ent, 16); else memset(tb + 446 + slot * 16, 0, 16);
    disk_arm(dev, 0, 1);                                  /* le-modifica-grava: codigo de boot preservado */
    int r = disk_write(dev, 0, 1, tb);
    disk_disarm();
    return r ? -1 : 0;
}

static void hdr_crc(uint8_t *h, uint32_t hsz) {
    uint32_t z = 0, c;
    memcpy(h + 16, &z, 4);
    c = crc32_fim(crc32_add(crc32_ini(), h, hsz));
    memcpy(h + 16, &c, 4);
}

/* grava/zera uma entrada GPT e atualiza CRCs, copia primaria e copia de reserva */
static int gpt_commit(int dev, int slot, const uint8_t *ent) {
    static uint8_t hdr[512] __attribute__((aligned(4)));
    static uint8_t bk[512]  __attribute__((aligned(4)));
    if (disk_read(dev, 1, 1, hdr) || memcmp(hdr, "EFI PART", 8)) return -1;
    uint32_t elba, nent, esz, alt, hsz;
    memcpy(&elba, hdr + 72, 4); memcpy(&nent, hdr + 80, 4); memcpy(&esz, hdr + 84, 4);
    memcpy(&alt, hdr + 32, 4);  memcpy(&hsz, hdr + 12, 4);
    if (esz != 128 || !nent || nent > 128 || (uint32_t)slot >= nent || hsz < 92 || hsz > 512) return -1;
    uint32_t nsec = (nent + 3) / 4, aent = alt - nsec, crc = crc32_ini();
    for (uint32_t s = 0; s < nsec; s++) {
        if (disk_read(dev, elba + s, 1, sec)) return -1;
        if (s == (uint32_t)slot / 4) {
            uint8_t *e = sec + (slot % 4) * 128;
            if (ent) memcpy(e, ent, 128); else memset(e, 0, 128);
            disk_arm(dev, elba + s, 1); disk_arm(dev, aent + s, 1);
            int r = disk_write(dev, elba + s, 1, sec);
            if (!r) r = disk_write(dev, aent + s, 1, sec);
            disk_disarm();
            if (r) return -1;
        }
        crc = crc32_add(crc, sec, (s == nsec - 1 && (nent % 4)) ? (nent % 4) * 128 : 512);
    }
    crc = crc32_fim(crc);
    memcpy(hdr + 88, &crc, 4);
    hdr_crc(hdr, hsz);
    memcpy(bk, hdr, 512);
    uint32_t um = 1;
    memcpy(bk + 24, &alt, 4); memcpy(bk + 32, &um, 4); memcpy(bk + 72, &aent, 4);
    hdr_crc(bk, hsz);
    disk_arm(dev, 1, 1); disk_arm(dev, alt, 1);
    int r = disk_write(dev, 1, 1, hdr);
    if (!r) r = disk_write(dev, alt, 1, bk);
    disk_disarm();
    return r ? -1 : 0;
}

static int gpt_livre(int dev) {
    if (disk_read(dev, 1, 1, tb) || memcmp(tb, "EFI PART", 8)) return -1;
    uint32_t elba, nent;
    memcpy(&elba, tb + 72, 4); memcpy(&nent, tb + 80, 4);
    for (uint32_t s = 0; s < (nent + 3) / 4; s++) {
        if (disk_read(dev, elba + s, 1, sc2)) return -1;
        for (uint32_t k = 0; k < 4 && s * 4 + k < nent; k++) {
            bool vazio = true;
            for (int b = 0; b < 16; b++) if (sc2[k * 128 + b]) vazio = false;
            if (vazio) return (int)(s * 4 + k);
        }
    }
    return -1;
}

int part_tabela_mbr(int dev) {
    memset(tb, 0, 512); tb[510] = 0x55; tb[511] = 0xAA;
    disk_arm(dev, 0, 1);
    int r = disk_write(dev, 0, 1, tb);
    disk_disarm();
    return r ? -1 : 0;
}

/* codigos: 0 ok, -1 E/S, -2 sem entrada livre, -3 invalido, -4 FS maior/desconhecido, -5 sem espaco */
int part_criar(pdisk_t *d, uint32_t ini, uint32_t tam) {
    uint32_t li[8], lt[8];
    int ng = part_livres(d, li, lt, 8), ok = 0;
    for (int g = 0; g < ng; g++) if (ini >= li[g] && ini + tam <= li[g] + lt[g] && ini + tam > ini) ok = 1;
    if (!ok || tam < 2048u) return -3;

    if (d->tabela == 1) {
        int slot = -1;
        for (int s = 0; s < 4 && slot < 0; s++) {
            bool usado = false;
            for (int i = 0; i < d->n; i++) if (d->p[i].slot == s) usado = true;
            if (!usado) slot = s;
        }
        if (slot < 0) return -2;
        uint8_t e[16]; memset(e, 0, 16);
        e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF; e[4] = 0x0C; e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
        memcpy(e + 8, &ini, 4); memcpy(e + 12, &tam, 4);
        return mbr_escreve(d->dev, slot, e);
    }
    if (d->tabela == 2) {
        int slot = gpt_livre(d->dev);
        if (slot < 0) return -2;
        uint8_t e[128]; memset(e, 0, 128);
        memcpy(e, G_DADOS, 16);
        for (int k = 0; k < 16; k++) e[16 + k] = rnd() & 0xFF;
        e[16 + 7] = (e[16 + 7] & 0x0F) | 0x40; e[16 + 8] = (e[16 + 8] & 0x3F) | 0x80;
        uint32_t ult = ini + tam - 1;
        memcpy(e + 32, &ini, 4); memcpy(e + 40, &ult, 4);
        const char *nm = "BOSSOS";
        for (int k = 0; nm[k]; k++) e[56 + k * 2] = nm[k];
        return gpt_commit(d->dev, slot, e);
    }
    return -3;
}

int part_apagar(pdisk_t *d, int i) {
    if (i < 0 || i >= d->n || d->truncada) return -3;
    if (d->tabela == 1) return mbr_escreve(d->dev, d->p[i].slot, 0);
    if (d->tabela == 2) return gpt_commit(d->dev, d->p[i].slot, 0);
    return -3;
}

int part_redim(pdisk_t *d, int i, uint32_t novo) {
    if (i < 0 || i >= d->n || d->truncada) return -3;
    part_t *p = &d->p[i];
    if (p->grande || novo < 2048u || novo == p->setores) return -3;
    if (novo < p->setores) {
        if (!p->fs_setores || p->fs_setores > novo) return -4;     /* o FS precisa ter sido encolhido antes */
    } else {
        uint32_t lim = d->tabela == 2 ? d->gpt_ultimo + 1 : d->total, fim = p->inicio + p->setores;
        for (int k = 0; k < d->n; k++)
            if (k != i && d->p[k].inicio >= fim && d->p[k].inicio < lim) lim = d->p[k].inicio;
        if (p->inicio + novo > lim || p->inicio + novo < p->inicio) return -5;
    }
    if (d->tabela == 1) {
        if (disk_read(d->dev, 0, 1, tb)) return -1;
        memcpy(tb + 446 + p->slot * 16 + 12, &novo, 4);
        disk_arm(d->dev, 0, 1);
        int r = disk_write(d->dev, 0, 1, tb);
        disk_disarm();
        return r ? -1 : 0;
    }
    if (d->tabela == 2) {
        uint8_t e[128]; memcpy(e, p->raw, 128);
        uint32_t ult = p->inicio + novo - 1;
        memcpy(e + 40, &ult, 4);
        return gpt_commit(d->dev, p->slot, e);
    }
    return -3;
}

/* ---------- formatacao FAT32 (rotulo BOSSOS) ---------- */
int part_mkfs(int dev, uint32_t ini, uint32_t tam) {
    static uint8_t b[512] __attribute__((aligned(4)));
    uint32_t spc = tam <= 532480u ? 1 : tam <= 16777216u ? 8 : tam <= 33554432u ? 16 : tam <= 67108864u ? 32 : 64;
    uint32_t rsv = 32, nf = 2, fsz = 1, cl = 0;
    for (int it = 0; it < 4; it++) { cl = (tam - rsv - nf * fsz) / spc; fsz = ((cl + 2) * 4 + 511) / 512; }
    cl = (tam - rsv - nf * fsz) / spc;
    if (cl < 65525u || cl > 0x0FFFFFF0u) return -3;

    disk_arm(dev, ini, tam);
    int r = 0;
    uint32_t zero_ate = rsv + nf * fsz + spc;               /* reservados + FATs + cluster da raiz */
    for (uint32_t s = 0; s < zero_ate && !r; s += 8) {
        uint32_t n = zero_ate - s < 8 ? zero_ate - s : 8;
        r = disk_write(dev, ini + s, n, zbuf);
    }
    if (!r) {
        uint32_t volid = rnd(), v2 = 2;
        memset(b, 0, 512);
        b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90; memcpy(b + 3, "BOSSOS  ", 8);
        b[12] = 2; b[13] = spc; b[14] = rsv & 0xFF; b[15] = rsv >> 8; b[16] = nf;
        b[21] = 0xF8; b[24] = 63; b[26] = 255;
        memcpy(b + 28, &ini, 4); memcpy(b + 32, &tam, 4); memcpy(b + 36, &fsz, 4); memcpy(b + 44, &v2, 4);
        b[48] = 1; b[50] = 6; b[64] = 0x80; b[66] = 0x29;
        memcpy(b + 67, &volid, 4);
        memcpy(b + 71, "BOSSOS     ", 11); memcpy(b + 82, "FAT32   ", 8);
        b[510] = 0x55; b[511] = 0xAA;
        r = disk_write(dev, ini + 0, 1, b);
        if (!r) r = disk_write(dev, ini + 6, 1, b);
    }
    if (!r) {
        uint32_t s1 = 0x41615252u, s2 = 0x61417272u, ff = 0xFFFFFFFFu, s3 = 0xAA550000u;
        memset(b, 0, 512);
        memcpy(b, &s1, 4); memcpy(b + 484, &s2, 4); memcpy(b + 488, &ff, 4); memcpy(b + 492, &ff, 4); memcpy(b + 508, &s3, 4);
        r = disk_write(dev, ini + 1, 1, b);
        if (!r) r = disk_write(dev, ini + 7, 1, b);
    }
    if (!r) {
        uint32_t f0 = 0x0FFFFFF8u, f1 = 0x0FFFFFFFu;
        memset(b, 0, 512);
        memcpy(b, &f0, 4); memcpy(b + 4, &f1, 4); memcpy(b + 8, &f1, 4);
        for (uint32_t k = 0; k < nf && !r; k++) r = disk_write(dev, ini + rsv + k * fsz, 1, b);
    }
    disk_disarm();
    return r ? -1 : 0;
}

/* ---------- backup / restauracao da tabela de particoes ---------- */
#define BK_MAGIC 0x42505442u

int part_backup(int dev) {
    const blk_info_t *bi = disk_info(dev);
    if (!bi || bi->setores < 4096) return -1;
    uint32_t nl = 0, tot = bi->setores;
    if (disk_read(dev, 1, 1, tb)) return -1;
    if (!memcmp(tb, "EFI PART", 8)) nl = 33;
    uint32_t bytes = 12 + 512 * (34 + nl), fr = (bytes + 4095) / 4096;
    uint8_t *buf = (uint8_t *)frame_alloc_contig(fr);
    if (!buf) return -1;
    uint32_t mag = BK_MAGIC; uint16_t a = 34, b = nl;
    memcpy(buf, &mag, 4); memcpy(buf + 4, &tot, 4); memcpy(buf + 8, &a, 2); memcpy(buf + 10, &b, 2);
    int r = disk_read(dev, 0, 34, buf + 12);
    if (!r && nl) r = disk_read(dev, tot - nl, nl, buf + 12 + 512 * 34);
    if (!r) {
        char nome[16];
        ksnprintf(nome, sizeof nome, "PTBK%u.BIN", (uint32_t)dev);
        r = fs_write(nome, buf, bytes);
    }
    for (uint32_t i = 0; i < fr; i++) frame_free((uint32_t)buf + i * 4096);
    return r;
}

int part_restaurar(int dev) {
    const blk_info_t *bi = disk_info(dev);
    if (!bi) return -1;
    char nome[16]; bool dir; uint32_t bytes;
    ksnprintf(nome, sizeof nome, "PTBK%u.BIN", (uint32_t)dev);
    int r = fs_stat(nome, &dir, &bytes);
    if (r) return r;
    uint32_t fr = (bytes + 4095) / 4096;
    uint8_t *buf = (uint8_t *)frame_alloc_contig(fr);
    if (!buf) return -1;
    uint32_t lido = 0;
    r = fs_read(nome, buf, bytes, &lido);
    if (!r) {
        uint32_t mag, tot; uint16_t a, b;
        memcpy(&mag, buf, 4); memcpy(&tot, buf + 4, 4); memcpy(&a, buf + 8, 2); memcpy(&b, buf + 10, 2);
        if (mag != BK_MAGIC || tot != bi->setores || a != 34 || lido < 12u + 512u * (a + b)) r = -3;
        else {
            disk_arm(dev, 0, 34);
            if (b) disk_arm(dev, tot - b, b);
            r = disk_write(dev, 0, 34, buf + 12);
            if (!r && b) r = disk_write(dev, tot - b, b, buf + 12 + 512 * 34);
            disk_disarm();
            if (r) r = -1;
        }
    }
    for (uint32_t i = 0; i < fr; i++) frame_free((uint32_t)buf + i * 4096);
    return r;
}
