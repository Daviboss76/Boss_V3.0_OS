#include "kernel.h"
#include "blk.h"
#include "fs.h"

#define SEC      512
#define ATTR_DIR 0x10
#define ATTR_VOL 0x08
#define ATTR_LFN 0x0F
#define EOC      0x0FFFFFF8u
#define EOC_MARK 0x0FFFFFFFu

typedef struct __attribute__((packed)) {
    uint8_t  nome[11];
    uint8_t  attr, nt, ctime_ms;
    uint16_t ctime, cdate, adate, hi;
    uint16_t mtime, mdate, lo;
    uint32_t tam;
} dirent_t;

typedef struct { uint32_t lba; uint16_t off; } loc_t;

static struct {
    bool ok; int drive;
    uint32_t part_lba, fat_lba, fat_sect, nfats, data_lba, root, spc, clusters, hint, cwd;
    char cwd_path[96];
} fs;

static uint8_t  dbuf[SEC]  __attribute__((aligned(4)));
static uint8_t  tmp[SEC]   __attribute__((aligned(4)));
static uint8_t  zero[SEC]  __attribute__((aligned(4)));
static uint32_t fcache[128];
static uint32_t fcache_lba = 0xFFFFFFFFu;

static int rd(uint32_t lba, void *b)       { return blk_read(fs.drive, lba, 1, b); }
static int wr(uint32_t lba, const void *b) { return blk_write(fs.drive, lba, 1, b); }
static uint32_t clba(uint32_t c) { return fs.data_lba + (c - 2) * fs.spc; }
static uint32_t ecl(const dirent_t *e) { return ((uint32_t)e->hi << 16) | e->lo; }

/* ---------- FAT ---------- */
static int fload(uint32_t s) {
    if (fcache_lba == s) return 0;
    if (rd(s, fcache)) return -1;
    fcache_lba = s; return 0;
}
static int fat_get(uint32_t c, uint32_t *v) {
    if (fload(fs.fat_lba + c / 128)) return -1;
    *v = fcache[c % 128] & 0x0FFFFFFF; return 0;
}
static int fat_set(uint32_t c, uint32_t v) {
    uint32_t s = c / 128;
    if (fload(fs.fat_lba + s)) return -1;
    fcache[c % 128] = (fcache[c % 128] & 0xF0000000u) | (v & 0x0FFFFFFF);
    for (uint32_t k = 0; k < fs.nfats; k++)
        if (wr(fs.fat_lba + k * fs.fat_sect + s, fcache)) return -1;
    return 0;
}
static uint32_t alloc_cluster(void) {
    for (uint32_t k = 0; k < fs.clusters; k++) {
        uint32_t c = 2 + ((fs.hint - 2 + k) % fs.clusters), v;
        if (fat_get(c, &v)) return 0;
        if (v == 0) {
            if (fat_set(c, EOC_MARK)) return 0;
            fs.hint = c + 1; return c;
        }
    }
    return 0;
}
static void free_chain(uint32_t c) {
    while (c >= 2 && c < EOC) {
        uint32_t nx; if (fat_get(c, &nx)) return;
        fat_set(c, 0); c = nx;
    }
}
static int zero_cluster(uint32_t c) {
    for (uint32_t s = 0; s < fs.spc; s++) if (wr(clba(c) + s, zero)) return -1;
    return 0;
}

/* ---------- diretorios ---------- */
enum { S_CONT = 0, S_STOP = 1, S_WRITE = 2 };
typedef int (*scan_cb)(dirent_t *e, void *ctx);
static loc_t last_loc;

static bool valida(const dirent_t *e) {
    return e->nome[0] != 0x00 && e->nome[0] != 0xE5 &&
           (e->attr & ATTR_LFN) != ATTR_LFN && !(e->attr & ATTR_VOL);
}
/* 0 = callback parou, 1 = fim do diretorio, -1 = erro de E/S */
static int dir_scan(uint32_t dir, scan_cb cb, void *ctx) {
    uint32_t c = dir;
    while (c >= 2 && c < EOC) {
        for (uint32_t s = 0; s < fs.spc; s++) {
            uint32_t lba = clba(c) + s;
            if (rd(lba, dbuf)) return -1;
            for (int o = 0; o < SEC; o += 32) {
                dirent_t *e = (dirent_t *)(dbuf + o);
                bool fim = (e->nome[0] == 0x00);
                int r = cb(e, ctx);
                if (r) {
                    last_loc.lba = lba; last_loc.off = o;
                    if (r == S_WRITE && wr(lba, dbuf)) return -1;
                    return 0;
                }
                if (fim) return 1;
            }
        }
        uint32_t nx; if (fat_get(c, &nx)) return -1;
        c = nx;
    }
    return 1;
}

static bool eq11(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 11; i++) if (a[i] != b[i]) return false;
    return true;
}
typedef struct { const uint8_t *nm; dirent_t ent; } find_ctx_t;
static int cb_find(dirent_t *e, void *p) {
    find_ctx_t *f = p;
    if (valida(e) && eq11(e->nome, f->nm)) { f->ent = *e; return S_STOP; }
    return S_CONT;
}
static int dir_find(uint32_t dir, const uint8_t *nm, dirent_t *out, loc_t *loc) {
    find_ctx_t f = { nm };
    int r = dir_scan(dir, cb_find, &f);
    if (r < 0) return FS_EIO;
    if (r > 0) return FS_ENOENT;
    *out = f.ent; if (loc) *loc = last_loc;
    return 0;
}
static int ent_update(loc_t l, const dirent_t *e) {
    if (rd(l.lba, dbuf)) return FS_EIO;
    memcpy(dbuf + l.off, e, 32);
    return wr(l.lba, dbuf) ? FS_EIO : 0;
}
static int cb_add(dirent_t *e, void *p) {
    if (e->nome[0] == 0x00 || e->nome[0] == 0xE5) { *e = *(dirent_t *)p; return S_WRITE; }
    return S_CONT;
}
static int dir_add(uint32_t dir, const dirent_t *n) {
    int r = dir_scan(dir, cb_add, (void *)n);
    if (r == 0) return 0;
    if (r < 0) return FS_EIO;
    uint32_t c = dir, nx;                               /* diretorio cheio: estende */
    for (;;) {
        if (fat_get(c, &nx)) return FS_EIO;
        if (nx < 2 || nx >= EOC) break;
        c = nx;
    }
    uint32_t nc = alloc_cluster(); if (!nc) return FS_EFULL;
    if (zero_cluster(nc) || fat_set(c, nc)) return FS_EIO;
    memset(dbuf, 0, SEC); memcpy(dbuf, n, 32);
    return wr(clba(nc), dbuf) ? FS_EIO : 0;
}

/* ---------- nomes e caminhos ---------- */
static int nome83(const char *s, uint8_t out[11]) {
    memset(out, ' ', 11);
    if (!strcmp(s, "."))  { out[0] = '.'; return 0; }
    if (!strcmp(s, "..")) { out[0] = out[1] = '.'; return 0; }
    int p = 0; bool ext = false;
    for (; *s; s++) {
        char c = *s;
        if (c == '.') { if (ext || p == 0) return FS_ENAME; ext = true; p = 8; continue; }
        if (c >= 'a' && c <= 'z') c -= 32;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '~' || c == '$')) return FS_ENAME;
        if (p >= (ext ? 11 : 8)) return FS_ENAME;
        out[p++] = c;
    }
    if (p == 0 || (ext && p == 8)) return FS_ENAME;
    return 0;
}
static void fmt_nome(const uint8_t *n, char *o) {
    int j = 0;
    for (int i = 0; i < 8 && n[i] != ' '; i++) o[j++] = (n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i];
    if (n[8] != ' ') {
        o[j++] = '.';
        for (int i = 8; i < 11 && n[i] != ' '; i++) o[j++] = (n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i];
    }
    o[j] = 0;
}

static int resolve_parent(const char *path, uint32_t *dir, char *last) {
    uint32_t d = (path[0] == '/') ? fs.root : fs.cwd;
    const char *p = path;
    last[0] = 0;
    while (*p == '/') p++;
    while (*p) {
        int n = 0; while (p[n] && p[n] != '/') n++;
        if (n > 12) return FS_ENAME;
        char comp[13]; memcpy(comp, p, n); comp[n] = 0;
        const char *q = p + n; while (*q == '/') q++;
        if (!*q) { strcpy(last, comp); *dir = d; return 0; }
        if (!strcmp(comp, "."))                  { p = q; continue; }
        if (!strcmp(comp, "..") && d == fs.root) { p = q; continue; }
        uint8_t nm[11]; int r = nome83(comp, nm); if (r) return r;
        dirent_t e; r = dir_find(d, nm, &e, 0); if (r) return r;
        if (!(e.attr & ATTR_DIR)) return FS_ENOTDIR;
        d = ecl(&e); if (!d) d = fs.root;
        p = q;
    }
    *dir = d; return 0;
}
static void sintetiza(dirent_t *e, uint32_t cl) {
    memset(e, 0, sizeof *e); e->attr = ATTR_DIR; e->hi = cl >> 16; e->lo = cl & 0xFFFF;
}
static int lookup(const char *path, dirent_t *e, loc_t *loc) {
    uint32_t d; char last[13];
    int r = resolve_parent(path, &d, last); if (r) return r;
    if (!last[0] || !strcmp(last, ".") || (!strcmp(last, "..") && d == fs.root)) {
        sintetiza(e, d); if (loc) loc->lba = 0; return 0;
    }
    uint8_t nm[11]; r = nome83(last, nm); if (r) return r;
    return dir_find(d, nm, e, loc);
}
static void normaliza(const char *rel, char *out) {
    char t[96];
    strcpy(t, rel[0] == '/' ? "/" : fs.cwd_path);
    const char *p = rel;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char comp[16]; int n = 0;
        while (p[n] && p[n] != '/') { if (n < 15) comp[n] = p[n]; n++; }
        comp[n < 15 ? n : 15] = 0; p += n;
        if (!strcmp(comp, ".")) continue;
        if (!strcmp(comp, "..")) {
            int l = strlen(t);
            while (l > 1 && t[l - 1] == '/') l--;
            while (l > 1 && t[l - 1] != '/') l--;
            if (l > 1) l--;
            t[l] = 0; continue;
        }
        int l = strlen(t);
        if (l + (int)strlen(comp) + 2 >= (int)sizeof t) continue;
        if (t[l - 1] != '/') t[l++] = '/';
        strcpy(t + l, comp);
    }
    strcpy(out, t);
}

/* ---------- API publica ---------- */
#define NEEDFS if (!fs.ok) return FS_ENOFS

static int try_bpb(int dv, uint32_t lba) {
    static uint8_t b[SEC];
    if (blk_read(dv, lba, 1, b)) return -1;
    if (b[510] != 0x55 || b[511] != 0xAA) return -1;
    uint16_t bps = b[11] | (b[12] << 8), f16 = b[22] | (b[23] << 8);
    if (bps != 512 || f16) return -1;
    uint8_t spc = b[13], nf = b[16];
    uint16_t rsv = b[14] | (b[15] << 8), t16 = b[19] | (b[20] << 8);
    uint32_t fsz, root, tot;
    memcpy(&fsz, b + 36, 4); memcpy(&root, b + 44, 4); memcpy(&tot, b + 32, 4);
    if (!tot) tot = t16;
    if (!spc || !nf || !fsz || root < 2) return -1;
    fs.drive = dv; fs.part_lba = lba; fs.fat_lba = lba + rsv; fs.fat_sect = fsz;
    fs.nfats = nf; fs.data_lba = fs.fat_lba + nf * fsz; fs.spc = spc; fs.root = root;
    fs.clusters = (tot - (rsv + nf * fsz)) / spc;
    fs.hint = 2; fs.cwd = root; strcpy(fs.cwd_path, "/");
    fcache_lba = 0xFFFFFFFFu; fs.ok = true;
    return 0;
}
int fs_mount(void) {
    static uint8_t s0[SEC];
    fs.ok = false;
    for (int dv = 0; dv < blk_count(); dv++) {
        if (blk_read(dv, 0, 1, s0) || s0[510] != 0x55 || s0[511] != 0xAA) continue;
        for (int p = 0; p < 4; p++) {
            uint8_t *pe = s0 + 446 + p * 16;
            if (pe[4] == 0x0B || pe[4] == 0x0C) {
                uint32_t lba; memcpy(&lba, pe + 8, 4);
                if (try_bpb(dv, lba) == 0) return 0;
            }
        }
    }
    return FS_ENOFS;
}
const char *fs_strerror(int e) {
    switch (e) {
    case FS_EIO: return "erro de E/S";       case FS_ENOENT: return "nao encontrado";
    case FS_EEXIST: return "ja existe";      case FS_ENOTDIR: return "nao e diretorio";
    case FS_EISDIR: return "e um diretorio"; case FS_EFULL: return "disco cheio";
    case FS_ENAME: return "nome invalido (use 8.3)";
    case FS_ENOFS: return "sem FAT32 montado";
    case FS_ENOTEMPTY: return "diretorio nao vazio";
    default: return "erro";
    }
}
const char *fs_cwd(void) { return fs.ok ? fs.cwd_path : "?"; }

int fs_chdir(const char *path) {
    NEEDFS; dirent_t e;
    int r = lookup(path, &e, 0); if (r) return r;
    if (!(e.attr & ATTR_DIR)) return FS_ENOTDIR;
    uint32_t cl = ecl(&e); fs.cwd = cl ? cl : fs.root;
    char np[96]; normaliza(path, np); strcpy(fs.cwd_path, np);
    return 0;
}

typedef struct { void (*cb)(const char *, bool, uint32_t); } list_ctx_t;
static int cb_list(dirent_t *e, void *p) {
    if (valida(e) && e->nome[0] != '.') {
        char n[13]; fmt_nome(e->nome, n);
        ((list_ctx_t *)p)->cb(n, e->attr & ATTR_DIR, e->tam);
    }
    return S_CONT;
}
int fs_list(const char *path, void (*cb)(const char *, bool, uint32_t)) {
    NEEDFS; dirent_t e;
    int r = lookup(path, &e, 0); if (r) return r;
    if (!(e.attr & ATTR_DIR)) return FS_ENOTDIR;
    list_ctx_t c = { cb };
    return dir_scan(ecl(&e), cb_list, &c) < 0 ? FS_EIO : 0;
}
int fs_stat(const char *path, bool *dir, uint32_t *tam) {
    NEEDFS; dirent_t e;
    int r = lookup(path, &e, 0); if (r) return r;
    *dir = e.attr & ATTR_DIR; *tam = e.tam; return 0;
}

int fs_read(const char *path, void *buf, uint32_t max, uint32_t *lido) {
    NEEDFS; dirent_t e;
    int r = lookup(path, &e, 0); if (r) return r;
    if (e.attr & ATTR_DIR) return FS_EISDIR;
    uint32_t left = e.tam < max ? e.tam : max, c = ecl(&e);
    uint8_t *out = buf; *lido = left;
    while (left && c >= 2 && c < EOC) {
        for (uint32_t s = 0; s < fs.spc && left; s++) {
            if (left >= SEC) { if (rd(clba(c) + s, out)) return FS_EIO; out += SEC; left -= SEC; }
            else { if (rd(clba(c) + s, tmp)) return FS_EIO; memcpy(out, tmp, left); left = 0; }
        }
        uint32_t nx; if (fat_get(c, &nx)) return FS_EIO;
        c = nx;
    }
    return 0;
}

int fs_write(const char *path, const void *buf, uint32_t n) {
    NEEDFS;
    uint32_t d; char last[13];
    int r = resolve_parent(path, &d, last); if (r) return r;
    if (!last[0]) return FS_EISDIR;
    uint8_t nm[11]; r = nome83(last, nm); if (r) return r;
    if (nm[0] == '.') return FS_EISDIR;

    dirent_t old; loc_t loc;
    bool existe = (dir_find(d, nm, &old, &loc) == 0);
    if (existe && (old.attr & ATTR_DIR)) return FS_EISDIR;

    uint32_t first = 0, prev = 0, left = n;
    const uint8_t *src = buf;
    while (left) {
        uint32_t c = alloc_cluster();
        if (!c) { free_chain(first); return FS_EFULL; }
        if (prev) { if (fat_set(prev, c)) { free_chain(first); return FS_EIO; } } else first = c;
        prev = c;
        for (uint32_t s = 0; s < fs.spc; s++) {
            const void *p;
            if (left >= SEC)  { p = src; src += SEC; left -= SEC; }
            else if (left)    { memset(tmp, 0, SEC); memcpy(tmp, src, left); p = tmp; left = 0; }
            else              { p = zero; }
            if (wr(clba(c) + s, p)) { free_chain(first); return FS_EIO; }
        }
    }
    if (existe) free_chain(ecl(&old));

    dirent_t ne;
    if (existe) ne = old; else { memset(&ne, 0, sizeof ne); memcpy(ne.nome, nm, 11); ne.attr = 0x20; }
    ne.hi = first >> 16; ne.lo = first & 0xFFFF; ne.tam = n;
    if (existe) return ent_update(loc, &ne);
    r = dir_add(d, &ne);
    if (r) free_chain(first);
    return r;
}

int fs_mkdir(const char *path) {
    NEEDFS;
    uint32_t d; char last[13];
    int r = resolve_parent(path, &d, last); if (r) return r;
    if (!last[0]) return FS_ENAME;
    uint8_t nm[11]; r = nome83(last, nm); if (r) return r;
    if (nm[0] == '.') return FS_ENAME;
    dirent_t t; if (dir_find(d, nm, &t, 0) == 0) return FS_EEXIST;

    uint32_t c = alloc_cluster(); if (!c) return FS_EFULL;
    if (zero_cluster(c)) { free_chain(c); return FS_EIO; }
    dirent_t dot, dd;
    memset(&dot, 0, 32); memset(dot.nome, ' ', 11); dot.nome[0] = '.';
    dot.attr = ATTR_DIR; dot.hi = c >> 16; dot.lo = c & 0xFFFF;
    dd = dot; dd.nome[1] = '.';
    uint32_t pc = (d == fs.root) ? 0 : d; dd.hi = pc >> 16; dd.lo = pc & 0xFFFF;
    memset(dbuf, 0, SEC); memcpy(dbuf, &dot, 32); memcpy(dbuf + 32, &dd, 32);
    if (wr(clba(c), dbuf)) { free_chain(c); return FS_EIO; }

    dirent_t ne; memset(&ne, 0, 32); memcpy(ne.nome, nm, 11);
    ne.attr = ATTR_DIR; ne.hi = c >> 16; ne.lo = c & 0xFFFF;
    r = dir_add(d, &ne);
    if (r) free_chain(c);
    return r;
}

static int cb_count(dirent_t *e, void *p) {
    if (valida(e) && e->nome[0] != '.') (*(int *)p)++;
    return S_CONT;
}
int fs_remove(const char *path) {
    NEEDFS;
    uint32_t d; char last[13];
    int r = resolve_parent(path, &d, last); if (r) return r;
    if (!last[0]) return FS_ENAME;
    uint8_t nm[11]; r = nome83(last, nm); if (r) return r;
    if (nm[0] == '.') return FS_ENAME;
    dirent_t e; loc_t loc;
    r = dir_find(d, nm, &e, &loc); if (r) return r;
    if (e.attr & ATTR_DIR) {
        int cnt = 0;
        if (dir_scan(ecl(&e), cb_count, &cnt) < 0) return FS_EIO;
        if (cnt) return FS_ENOTEMPTY;
    }
    free_chain(ecl(&e));
    e.nome[0] = 0xE5;
    return ent_update(loc, &e);
}

int fs_info(uint32_t *total_kb, uint32_t *livre_kb) {
    NEEDFS;
    static uint32_t t[128];
    uint32_t livre = 0, nsect = (fs.clusters + 2 + 127) / 128;
    for (uint32_t s = 0; s < nsect; s++) {
        if (rd(fs.fat_lba + s, t)) return FS_EIO;
        for (uint32_t i = 0; i < 128; i++) {
            uint32_t idx = s * 128 + i;
            if (idx >= 2 && idx < fs.clusters + 2 && !(t[i] & 0x0FFFFFFF)) livre++;
        }
    }
    *total_kb = fs.clusters * fs.spc / 2; *livre_kb = livre * fs.spc / 2;
    return 0;
}
