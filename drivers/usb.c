#include "kernel.h"
#include "pci.h"
#include "timer.h"
#include "keyboard.h"
#include "usb.h"
#include "usbhc.h"

#define MAXDEV 16

typedef struct {
    bool ok; hc_alvo_t alvo; hc_pipe_t in, out;
    uint8_t iface, in_ep, out_ep; uint32_t tag; usb_info_t info;
} msd_t;
typedef struct { bool ok; hc_alvo_t alvo; uint8_t iface, proto, falhas, ant[8]; } hid_t;
typedef struct {
    bool ok; hc_alvo_t alvo; uint8_t ctl_if, unit, bm0, bm1, bm2, canais, bits, nrates, iso_ep;
    uint32_t rates[4];
} aud_t;
typedef struct {
    int msd_if, hid_if, hid_proto, aud_if;
    uint8_t msd_in, msd_out, unit, ctrlsz, bm0, bm1, bm2, canais, bits, nrates, iso_ep;
    uint16_t msd_in_mps, msd_out_mps;
    uint32_t rates[4];
} cfg_t;

static usb_dev_t devs[MAXDEV];
static msd_t msd[4];
static hid_t hid[4];
static aud_t aud[2];
static int ndev, nmsd, nhid, naud, hid_rr, mx, my, mbt;
static uint32_t prox_end;
static char ulog[1400];
static int ulen;

static void ulogf(const char *f, ...) {
    va_list ap; va_start(ap, f);
    int resto = (int)sizeof ulog - ulen;
    if (resto > 1) { int n = kvsnprintf(ulog + ulen, resto, f, ap); ulen += n < resto ? n : resto - 1; }
    va_end(ap);
}
const char *usb_log(void) { return ulog; }
int usb_ndev(void) { return ndev; }
const usb_dev_t *usb_dev(int i) { return (i >= 0 && i < ndev) ? &devs[i] : 0; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* ================= armazenamento em massa (BOT + SCSI) ================= */
static int bulk(msd_t *m, bool in, void *buf, uint32_t len, uint32_t ms, uint32_t *feito) {
    return hc_bulk(in ? &m->in : &m->out, in, buf, len, ms, feito);
}
static void limpa_halt(msd_t *m, bool in) {
    hc_ctl(&m->alvo, 0x02, 1, 0, in ? m->in_ep : m->out_ep, 0, 0);
    hc_pipe_reset(in ? &m->in : &m->out);
}
static void recupera(msd_t *m) {
    hc_ctl(&m->alvo, 0x21, 0xFF, 0, m->iface, 0, 0);
    limpa_halt(m, true); limpa_halt(m, false);
}

/* devolve o status SCSI (0 = ok) ou -1 em erro de transporte */
static int bot(msd_t *m, const uint8_t *cdb, int cblen, uint32_t len, bool in, uint32_t *feito) {
    uint8_t *c = hc_cmd(), *s = hc_cmd() + 64;
    memset(c, 0, 31);
    memcpy(c, "USBC", 4);
    uint32_t tag = ++m->tag;
    memcpy(c + 4, &tag, 4); memcpy(c + 8, &len, 4);
    c[12] = in ? 0x80 : 0; c[14] = cblen;
    memcpy(c + 15, cdb, cblen);
    if (bulk(m, false, c, 31, 1000, 0)) { recupera(m); return -1; }
    uint32_t fe = 0;
    if (len) {
        int r = bulk(m, in, hc_buf(), len, 8000, &fe);
        if (r == -2) limpa_halt(m, in);
        else if (r) { recupera(m); return -1; }
    }
    memset(s, 0, 13);
    int r = bulk(m, true, s, 13, 3000, 0);
    if (r) { limpa_halt(m, true); r = bulk(m, true, s, 13, 3000, 0); }
    uint32_t st; memcpy(&st, s + 4, 4);
    if (r || memcmp(s, "USBS", 4) || st != tag) { recupera(m); return -1; }
    if (feito) *feito = fe;
    return s[12];
}
static int scsi(msd_t *m, const uint8_t *cdb, int n, uint32_t len, bool in, void *buf) {
    if (!in && len) memcpy(hc_buf(), buf, len);
    int r = bot(m, cdb, n, len, in, 0);
    if (!r && in && len) memcpy(buf, hc_buf(), len);
    return r;
}

static int msd_init(msd_t *m) {
    uint8_t cdb[16], b[36];
    bool pronto = false;
    uint8_t lun;
    hc_ctl(&m->alvo, 0xA1, 0xFE, 0, m->iface, 1, &lun);            /* GET_MAX_LUN (pode falhar) */
    for (int t = 0; t < 15 && !pronto; t++) {
        memset(cdb, 0, 16);
        if (scsi(m, cdb, 6, 0, true, 0) == 0) { pronto = true; break; }
        memset(cdb, 0, 16); cdb[0] = 3; cdb[4] = 18;                 /* REQUEST SENSE */
        scsi(m, cdb, 6, 18, true, b);
        delay_ms(100);
    }
    if (!pronto) return -1;
    strcpy(m->info.model, "USB");
    memset(cdb, 0, 16); cdb[0] = 0x12; cdb[4] = 36;                  /* INQUIRY */
    if (scsi(m, cdb, 6, 36, true, b) == 0) {
        int k = 0;
        for (int i = 8; i < 32 && k < 40; i++) m->info.model[k++] = (b[i] >= 32 && b[i] < 127) ? b[i] : ' ';
        while (k > 0 && m->info.model[k - 1] == ' ') k--;
        m->info.model[k] = 0;
    }
    uint8_t cap[8];
    memset(cdb, 0, 16); cdb[0] = 0x25;                               /* READ CAPACITY(10) */
    if (scsi(m, cdb, 10, 8, true, cap)) return -1;
    if (be32(cap + 4) != 512) { ulogf("  armazenamento com setor de %u bytes: nao suportado\n", be32(cap + 4)); return -1; }
    m->info.sectors = be32(cap) + 1;
    m->info.present = true;
    return 0;
}

static int rw(msd_t *m, uint32_t lba, uint32_t n, void *buf, bool wr) {
    uint8_t *p = buf, cdb[16];
    while (n) {
        uint32_t q = n > 32 ? 32 : n;
        memset(cdb, 0, 16);
        cdb[0] = wr ? 0x2A : 0x28;
        cdb[2] = lba >> 24; cdb[3] = lba >> 16; cdb[4] = lba >> 8; cdb[5] = lba;
        cdb[7] = q >> 8; cdb[8] = q;
        int r = -1;
        for (int t = 0; t < 3 && r; t++) r = scsi(m, cdb, 10, q * 512, !wr, p);
        if (r) return -1;
        lba += q; n -= q; p += q * 512;
    }
    if (wr) { memset(cdb, 0, 16); cdb[0] = 0x35; scsi(m, cdb, 10, 0, true, 0); }     /* SYNCHRONIZE CACHE */
    return 0;
}
int usb_msd_count(void) { return nmsd; }
const usb_info_t *usb_info(int i) { return (i >= 0 && i < nmsd) ? &msd[i].info : 0; }
int usb_read(int i, uint32_t lba, uint32_t n, void *buf) {
    if (i < 0 || i >= nmsd || lba + n > msd[i].info.sectors || lba + n < lba) return -1;
    return rw(&msd[i], lba, n, buf, false);
}
int usb_write(int i, uint32_t lba, uint32_t n, const void *buf) {
    if (i < 0 || i >= nmsd || lba + n > msd[i].info.sectors || lba + n < lba) return -1;
    return rw(&msd[i], lba, n, (void *)buf, true);
}

/* ================= teclado e mouse (HID boot, consulta por GET_REPORT) ================= */
static const char hn[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;'`,./";
static const char hs[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|~:\"~<>?";

static int hid_ascii(uint8_t k, bool sh) {
    if (k >= 0x04 && k <= 0x38) return (uint8_t)(sh ? hs[k - 4] : hn[k - 4]);
    switch (k) {
    case 0x4F: return KEY_DIR;  case 0x50: return KEY_ESQ;
    case 0x51: return KEY_BAIXO; case 0x52: return KEY_CIMA;
    case 0x4C: return KEY_DEL;
    }
    return 0;
}

void usb_poll(void) {
    static uint32_t ultimo;
    extern volatile uint32_t ticks;
    if (!nhid || (int32_t)(ticks - ultimo) < 2) return;
    ultimo = ticks;
    hid_t *h = &hid[hid_rr++ % nhid];
    if (!h->ok) return;
    uint8_t r[8];
    if (hc_ctl_ms(&h->alvo, 0xA1, 0x01, 0x0100, h->iface, h->proto == 1 ? 8 : 4, r, 15)) {
        if (++h->falhas >= 3) { h->ok = false; ulogf("hid: dispositivo desativado (sem resposta)\n"); }
        return;
    }
    h->falhas = 0;
    if (h->proto == 2) {                                   /* mouse */
        mbt = r[0] & 7;
        mx += (int8_t)r[1]; my += (int8_t)r[2];
        if (mx < 0) mx = 0; if (my < 0) my = 0; if (mx > 4095) mx = 4095; if (my > 4095) my = 4095;
        return;
    }
    bool sh = r[0] & 0x22, ct = r[0] & 0x11;               /* teclado */
    for (int i = 2; i < 8; i++) {
        uint8_t k = r[i];
        if (k < 4) continue;
        bool novo = true;
        for (int j = 2; j < 8; j++) if (h->ant[j] == k) novo = false;
        if (!novo) continue;
        int c = hid_ascii(k, sh);
        if (ct && (c == 'c' || c == 'C')) kb_intr_set();
        else if (c > 0) kb_push(c);
    }
    memcpy(h->ant, r, 8);
}
void usb_mouse(int *x, int *y, int *botoes) { *x = mx; *y = my; *botoes = mbt; }

/* ================= audio (classe 1, UAC1): controles de volume e mudo ================= */
int usb_audio_count(void) { return naud; }

static int aud_ctl(aud_t *a, uint8_t req, uint8_t sel, uint8_t ch, void *dados, uint16_t len, bool in) {
    return hc_ctl(&a->alvo, in ? 0xA1 : 0x21, req, ((uint16_t)sel << 8) | ch,
                  ((uint16_t)a->unit << 8) | a->ctl_if, len, dados);
}
int usb_audio_volume(int i, int pct) {
    if (i < 0 || i >= naud || !aud[i].unit) return -2;
    aud_t *a = &aud[i];
    uint8_t bm[3] = { a->bm0, a->bm1, a->bm2 };
    int feito = 0;
    for (uint8_t ch = 0; ch < 3; ch++) {
        if (!(bm[ch] & 2)) continue;
        uint8_t b[2];
        if (aud_ctl(a, 0x82, 2, ch, b, 2, true)) continue;
        int32_t mn = (int16_t)(b[0] | (b[1] << 8));
        if (aud_ctl(a, 0x83, 2, ch, b, 2, true)) continue;
        int32_t mxv = (int16_t)(b[0] | (b[1] << 8));
        if (pct < 0) {
            if (aud_ctl(a, 0x81, 2, ch, b, 2, true) || mxv == mn) return -1;
            int32_t cur = (int16_t)(b[0] | (b[1] << 8));
            return (int)((cur - mn) * 100 / (mxv - mn));
        }
        if (pct > 100) pct = 100;
        int32_t v = mn + (mxv - mn) * pct / 100;
        b[0] = v & 0xFF; b[1] = (v >> 8) & 0xFF;
        if (!aud_ctl(a, 0x01, 2, ch, b, 2, false)) feito++;
        if (ch == 0 && feito) break;
    }
    return feito ? 0 : -1;
}
int usb_audio_mudo(int i, bool mudo) {
    if (i < 0 || i >= naud || !aud[i].unit) return -2;
    aud_t *a = &aud[i];
    uint8_t bm[3] = { a->bm0, a->bm1, a->bm2 }, b = mudo ? 1 : 0;
    int feito = 0;
    for (uint8_t ch = 0; ch < 3; ch++) {
        if (!(bm[ch] & 1)) continue;
        if (!aud_ctl(a, 0x01, 1, ch, &b, 1, false)) feito++;
        if (ch == 0 && feito) break;
    }
    return feito ? 0 : -1;
}
int usb_audio_info(int i, char *out, int max) {
    if (i < 0 || i >= naud) return -1;
    aud_t *a = &aud[i];
    return ksnprintf(out, max, "%u canais, %u bits, %u taxa(s)%s, mudo:%s volume:%s, saida isocrona: %s",
                     (uint32_t)a->canais, (uint32_t)a->bits, (uint32_t)a->nrates,
                     a->nrates ? "" : " (continua)", (a->bm0 | a->bm1 | a->bm2) & 1 ? "sim" : "nao",
                     (a->bm0 | a->bm1 | a->bm2) & 2 ? "sim" : "nao", a->iso_ep ? "sim (envio de som: proxima etapa)" : "nao");
}

/* ================= enumeracao ================= */
static void parse_cfg(const uint8_t *c, uint32_t tot, cfg_t *r) {
    int cls = -1, sub = 0, pro = 0, ifn = 0;
    memset(r, 0, sizeof *r);
    r->msd_if = r->hid_if = r->aud_if = -1;
    for (uint32_t p = 0; p + 2 <= tot && c[p] >= 2; p += c[p]) {
        const uint8_t *d = c + p; uint32_t len = c[p];
        if (p + len > tot) break;
        if (d[1] == 4 && len >= 9) {
            ifn = d[2]; cls = d[5]; sub = d[6]; pro = d[7];
            if (d[3] == 0) {
                if (cls == 8 && sub == 6 && pro == 0x50 && r->msd_if < 0) r->msd_if = ifn;
                else if (cls == 3 && sub == 1 && (pro == 1 || pro == 2) && r->hid_if < 0) { r->hid_if = ifn; r->hid_proto = pro; }
                else if (cls == 1 && sub == 1 && r->aud_if < 0) r->aud_if = ifn;
            }
        } else if (d[1] == 5 && len >= 7) {
            uint8_t a = d[2], at = d[3] & 3; uint16_t m = d[4] | (d[5] << 8);
            if (cls == 8 && ifn == r->msd_if && at == 2) {
                if (a & 0x80) { r->msd_in = a; r->msd_in_mps = m; } else { r->msd_out = a; r->msd_out_mps = m; }
            } else if (cls == 1 && sub == 2 && at == 1 && !(a & 0x80) && !r->iso_ep) r->iso_ep = a;
        } else if (d[1] == 0x24 && cls == 1) {
            if (sub == 1 && d[2] == 6 && len >= 7 && !r->unit) {
                r->unit = d[3]; r->ctrlsz = d[5] ? d[5] : 1;
                r->bm0 = d[6];
                if (len >= 6u + 2u * r->ctrlsz) { r->bm1 = d[6 + r->ctrlsz]; if (len >= 6u + 3u * r->ctrlsz) r->bm2 = d[6 + 2 * r->ctrlsz]; }
            } else if (sub == 2 && d[2] == 2 && len >= 8 && !r->canais) {
                r->canais = d[4]; r->bits = d[6];
                uint32_t nr = d[7];
                for (uint32_t k = 0; k < nr && k < 4 && 8 + 3 * k + 3 <= len; k++)
                    r->rates[r->nrates++] = d[8 + 3 * k] | (d[9 + 3 * k] << 8) | (d[10 + 3 * k] << 16);
            }
        }
    }
}

static void str_desc(const hc_alvo_t *a, uint8_t idx, char *out, int max) {
    uint8_t b[64];
    out[0] = 0;
    if (!idx || hc_ctl(a, 0x80, 6, 0x0300 | idx, 0x0409, 62, b) || b[1] != 3) return;
    int n = (b[0] - 2) / 2, k = 0;
    for (int i = 0; i < n && k < max - 1 && 2 + i * 2 < 62; i++) { uint8_t c = b[2 + i * 2]; out[k++] = (c >= 32 && c < 127) ? c : '?'; }
    out[k] = 0;
}

static void enumera(hc_alvo_t al, int nivel, uint8_t pai, uint8_t porta);

static void hub(hc_alvo_t al, int nivel) {
    uint8_t h[8];
    if (nivel >= 3 || hc_ctl(&al, 0xA0, 6, 0x2900, 0, 8, h)) { ulogf("  hub: sem descritor\n"); return; }
    int np = h[2] > 7 ? 7 : h[2];
    ulogf("  hub com %u porta(s)\n", (uint32_t)h[2]);
    for (int p = 1; p <= np; p++) hc_ctl(&al, 0x23, 3, 8, p, 0, 0);               /* liga a energia */
    delay_ms(h[5] * 2 + 100);
    for (int p = 1; p <= np; p++) {
        uint8_t st[4];
        if (hc_ctl(&al, 0xA3, 0, 0, p, 4, st) || !(st[0] & 1)) continue;
        hc_ctl(&al, 0x23, 3, 4, p, 0, 0);                                         /* reset da porta */
        delay_ms(50);
        for (int t = 0; t < 20; t++) {
            if (hc_ctl(&al, 0xA3, 0, 0, p, 4, st) || !(st[0] & 0x10)) break;
            delay_ms(10);
        }
        hc_ctl(&al, 0x23, 1, 20, p, 0, 0);
        delay_ms(10);
        if (hc_ctl(&al, 0xA3, 0, 0, p, 4, st) || !(st[0] & 2)) continue;
        hc_alvo_t f; memset(&f, 0, sizeof f);
        f.vel = (st[1] & 4) ? 2 : (st[1] & 2) ? 1 : 0;
        if (f.vel != 2) {
            if (al.vel == 2) { f.hub = al.addr; f.porta = (uint8_t)p; }
            else { f.hub = al.hub; f.porta = al.porta; }
        }
        ulogf("  porta %u do hub: %s\n", (uint32_t)p, f.vel == 2 ? "high-speed" : f.vel == 1 ? "low-speed" : "full-speed");
        enumera(f, nivel + 1, al.addr, (uint8_t)p);
    }
}

static void enumera(hc_alvo_t al, int nivel, uint8_t pai, uint8_t porta) {
    uint8_t d[18], cfg[255];
    al.addr = 0;
    al.mps0 = al.vel == 2 ? 64 : 8;
    if (hc_ctl(&al, 0x80, 6, 0x0100, 0, 8, d)) { ulogf("  falha ao ler o descritor\n"); return; }
    al.mps0 = d[7] ? d[7] : 8;
    uint8_t addr = (uint8_t)prox_end++;
    if (hc_ctl(&al, 0x00, 5, addr, 0, 0, 0)) { ulogf("  falha no SET_ADDRESS\n"); return; }
    delay_ms(10);
    al.addr = addr;
    if (hc_ctl(&al, 0x80, 6, 0x0100, 0, 18, d)) { ulogf("  falha no descritor completo\n"); return; }
    char nome[28];
    str_desc(&al, d[15], nome, sizeof nome);
    uint16_t vid = d[8] | (d[9] << 8), pid = d[10] | (d[11] << 8);
    ulogf("  dispositivo %x:%x classe %u \"%s\" (end. %u)\n", (uint32_t)vid, (uint32_t)pid, (uint32_t)d[4], nome, (uint32_t)addr);

    usb_dev_t *dv = 0;
    if (ndev < MAXDEV) {
        dv = &devs[ndev++];
        memset(dv, 0, sizeof *dv);
        dv->addr = addr; dv->vel = al.vel; dv->classe = d[4]; dv->vid = vid; dv->pid = pid; dv->hub = pai; dv->porta = porta;
        strcpy(dv->nome, nome[0] ? nome : "(sem nome)");
    }
    if (hc_ctl(&al, 0x80, 6, 0x0200, 0, 9, cfg)) return;
    uint32_t tot = cfg[2] | (cfg[3] << 8);
    if (tot < 9) return;
    if (tot > sizeof cfg) tot = sizeof cfg;
    if (hc_ctl(&al, 0x80, 6, 0x0200, 0, tot, cfg)) return;
    if (hc_ctl(&al, 0x00, 9, cfg[5], 0, 0, 0)) { ulogf("  falha no SET_CONFIGURATION\n"); return; }

    if (d[4] == 9) { if (dv) dv->tipo = UT_HUB; hub(al, nivel); return; }
    cfg_t r;
    parse_cfg(cfg, tot, &r);

    if (r.msd_if >= 0 && r.msd_in && r.msd_out && nmsd < 4) {
        msd_t *m = &msd[nmsd];
        memset(m, 0, sizeof *m);
        m->alvo = al; m->iface = (uint8_t)r.msd_if; m->in_ep = r.msd_in; m->out_ep = r.msd_out;
        if (!hc_bulk_abre(&m->in, &al, r.msd_in & 0xF, r.msd_in_mps) &&
            !hc_bulk_abre(&m->out, &al, r.msd_out & 0xF, r.msd_out_mps) && msd_init(m) == 0) {
            m->ok = true; nmsd++;
            if (dv) { dv->tipo = UT_MSD; ksnprintf(dv->nome, sizeof dv->nome, "%s", m->info.model); }
            ulogf("  armazenamento: %s, %u MB\n", m->info.model, m->info.sectors / 2048);
        } else ulogf("  armazenamento nao respondeu\n");
    }
    if (r.hid_if >= 0 && nhid < 4) {
        hid_t *h = &hid[nhid++];
        memset(h, 0, sizeof *h);
        h->alvo = al; h->iface = (uint8_t)r.hid_if; h->proto = (uint8_t)r.hid_proto; h->ok = true;
        hc_ctl(&al, 0x21, 0x0B, 0, r.hid_if, 0, 0);                      /* SET_PROTOCOL(boot) */
        hc_ctl(&al, 0x21, 0x0A, 0, r.hid_if, 0, 0);                      /* SET_IDLE */
        if (dv && dv->tipo == UT_OUTRO) dv->tipo = r.hid_proto == 1 ? UT_TECLADO : UT_MOUSE;
        ulogf("  %s HID\n", r.hid_proto == 1 ? "teclado" : "mouse");
    }
    if (r.aud_if >= 0 && naud < 2) {
        aud_t *a = &aud[naud++];
        memset(a, 0, sizeof *a);
        a->alvo = al; a->ctl_if = (uint8_t)r.aud_if; a->unit = r.unit; a->bm0 = r.bm0; a->bm1 = r.bm1; a->bm2 = r.bm2;
        a->canais = r.canais; a->bits = r.bits; a->nrates = r.nrates; a->iso_ep = r.iso_ep;
        for (int k = 0; k < 4; k++) a->rates[k] = r.rates[k];
        a->ok = true;
        if (dv && dv->tipo != UT_MSD) dv->tipo = UT_AUDIO;
        ulogf("  audio USB: %u canais, %u bits\n", (uint32_t)r.canais, (uint32_t)r.bits);
    }
}

void usb_scan(void) {
    ulen = 0; ulog[0] = 0;
    ndev = nmsd = nhid = naud = hid_rr = 0;
    prox_end = 1;
    for (int i = 0; i < pci_count(); i++) {
        pci_dev_t *d = pci_get(i);
        if (d->class_code != 0x0C || d->subclass != 0x03) continue;
        ulogf("controlador %x:%x %s\n", (uint32_t)d->vendor, (uint32_t)d->device,
              d->prog_if == 0x20 ? "EHCI (USB 2.0)" :
              d->prog_if == 0x30 ? "xHCI (USB 3) - sem driver" :
              d->prog_if == 0x10 ? "OHCI (USB 1.1) - sem driver" :
              d->prog_if == 0x00 ? "UHCI (USB 1.1) - sem driver" : "outro");
    }
    if (!hc_ativo()) { ulogf("nenhum EHCI ativo: dispositivos USB nao serao vistos\n"); return; }
    hc_pipes_liberar();
    for (int p = 0; p < hc_nportas(); p++) {
        if (!hc_porta_conectada(p)) continue;
        ulogf("porta %u: dispositivo conectado\n", (uint32_t)p + 1);
        int v = hc_porta_reset(p);
        if (v == 2) { hc_alvo_t raiz; memset(&raiz, 0, sizeof raiz); raiz.vel = 2; enumera(raiz, 0, 0, (uint8_t)(p + 1)); }
        else if (v == 0) ulogf("  USB 1.1 ligado direto na porta: cedido ao companheiro (UHCI/OHCI), sem driver\n");
        else ulogf("  falha no reset\n");
    }
    ulogf("%u dispositivo(s), %u armazenamento(s), %u hid, %u audio\n", (uint32_t)ndev, (uint32_t)nmsd, (uint32_t)nhid, (uint32_t)naud);
}
