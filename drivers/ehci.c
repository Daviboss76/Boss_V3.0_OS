#include "kernel.h"
#include "pci.h"
#include "paging.h"
#include "timer.h"
#include "usbhc.h"

enum { CMD = 0, STS = 1, INTR = 2, CTRLDS = 4, PERLIST = 5, ASYNC = 6, CONFIG = 16, PORTSC = 17 };
enum { Q_HEAD = 0, Q_CTL = 1, Q_BULK0 = 2, N_QH = 10 };

#define QH(i)  ((volatile uint32_t *)(dma + (i) * 128))
#define QTD(i) ((volatile uint32_t *)(dma + 1280 + (i) * 64))
#define SETUP_OFF 1792
#define CTL_OFF   1856
#define CMD_OFF   2432
#define BUF_OFF   4096

static volatile uint8_t *cap;
static volatile uint32_t *op;
static uint8_t *dma;
static int nports, npipes;
static bool ativo, ctl_valido;
static hc_alvo_t ctl_alvo;

static inline uint32_t pa(volatile void *p) { return (uint32_t)p; }

static int espera(volatile uint32_t *r, uint32_t mascara, uint32_t valor, int ms) {
    for (int i = 0; i < ms; i++) {
        if ((*r & mascara) == valor) return 0;
        delay_ms(1);
    }
    return (*r & mascara) == valor ? 0 : -1;
}
static void async_pausa(void) { op[CMD] &= ~(1u << 5); espera(&op[STS], 1u << 15, 0, 20); }
static void async_volta(void) { op[CMD] |= (1u << 5);  espera(&op[STS], 1u << 15, 1u << 15, 20); }

static void qh_idle(volatile uint32_t *q) { q[4] = 1; q[5] = 1; q[6] &= 0x80000000u; }

static void qtd_set(int i, int nx, uint32_t pid, uint32_t buf, uint32_t len, uint32_t dt, uint32_t ioc) {
    volatile uint32_t *t = QTD(i);
    t[0] = nx >= 0 ? pa(QTD(nx)) : 1;
    t[1] = 1;
    t[2] = (dt << 31) | (len << 16) | (ioc << 15) | (3u << 10) | (pid << 8) | 0x80;
    t[3] = buf;
    for (int k = 1; k < 5; k++) t[3 + k] = (buf & ~0xFFFu) + (uint32_t)k * 4096;
    for (int k = 8; k < 13; k++) t[k] = 0;
}

/* executa a cadeia de qTDs na fila qi; 0 ok, -1 tempo esgotado, -2 erro/STALL */
static int exec(int qi, int n, int ms) {
    volatile uint32_t *q = QH(qi), *ult = QTD(n - 1);
    qh_idle(q);
    __asm__ volatile ("" ::: "memory");
    q[4] = pa(QTD(0));
    int r = -1;
    for (int t = 0; t < ms; t++) {
        if (!(ult[2] & 0x80)) { r = 0; break; }
        if (q[6] & 0x40) { r = -2; break; }
        delay_ms(1);
    }
    if (!r) for (int i = 0; i < n; i++) if (QTD(i)[2] & 0x78) r = -2;
    if (!r) for (int i = 0; i < 5 && (q[6] & 0x80); i++) delay_ms(1);
    if (r) { async_pausa(); qh_idle(q); async_volta(); }
    return r;
}

static void ctl_dest(const hc_alvo_t *a) {
    uint32_t hs = a->vel == 2;
    async_pausa();
    volatile uint32_t *q = QH(Q_CTL);
    q[1] = a->addr | ((uint32_t)a->vel << 12) | (1u << 14) | ((uint32_t)a->mps0 << 16) |
           (hs ? (4u << 28) : (1u << 27));
    q[2] = (1u << 30) | (hs ? 0 : (((uint32_t)a->porta << 23) | ((uint32_t)a->hub << 16)));
    qh_idle(q);
    async_volta();
    ctl_alvo = *a; ctl_valido = true;
}

int hc_ctl_ms(const hc_alvo_t *a, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx, uint16_t len,
              void *dados, uint32_t ms) {
    if (!ativo || len > 512) return -1;
    if (!ctl_valido || memcmp(&ctl_alvo, a, sizeof *a)) ctl_dest(a);
    uint8_t *s = dma + SETUP_OFF;
    s[0] = bm; s[1] = req; s[2] = val; s[3] = val >> 8; s[4] = idx; s[5] = idx >> 8; s[6] = len; s[7] = len >> 8;
    bool in = bm & 0x80;
    if (len && !in) memcpy(dma + CTL_OFF, dados, len);
    int n = 1;
    qtd_set(0, 1, 2, pa(s), 8, 0, 0);
    if (len) { qtd_set(1, 2, in ? 1 : 0, pa(dma + CTL_OFF), len, 1, 0); n = 2; }
    qtd_set(n, -1, (len && in) ? 0 : 1, 0, 0, 1, 1);
    if (len) QTD(1)[1] = pa(QTD(2));                  /* pacote curto: segue para o status */
    n++;
    int r = exec(Q_CTL, n, (int)ms);
    if (!r && len && in) memcpy(dados, dma + CTL_OFF, len);
    return r;
}
int hc_ctl(const hc_alvo_t *a, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, void *dados) {
    return hc_ctl_ms(a, bm, req, val, idx, len, dados, 500);
}

void hc_pipes_liberar(void) {
    npipes = 0; ctl_valido = false;
    if (!ativo) return;
    async_pausa();
    for (int i = Q_BULK0; i < N_QH; i++) { QH(i)[6] = 0; QH(i)[4] = 1; QH(i)[5] = 1; }
    async_volta();
}

int hc_bulk_abre(hc_pipe_t *p, const hc_alvo_t *a, uint8_t ep, uint16_t mps) {
    if (!ativo || npipes >= N_QH - Q_BULK0) return -1;
    int qi = Q_BULK0 + npipes++;
    uint32_t hs = a->vel == 2;
    async_pausa();
    volatile uint32_t *q = QH(qi);
    q[1] = a->addr | ((uint32_t)ep << 8) | ((uint32_t)a->vel << 12) | ((uint32_t)mps << 16) | (hs ? (4u << 28) : 0);
    q[2] = (1u << 30) | (hs ? 0 : (((uint32_t)a->porta << 23) | ((uint32_t)a->hub << 16)));
    q[6] = 0; q[4] = 1; q[5] = 1;
    async_volta();
    p->alvo = *a; p->ep = ep; p->mps = mps; p->qh = qi;
    return 0;
}

int hc_bulk(hc_pipe_t *p, bool in, void *buf, uint32_t len, uint32_t ms, uint32_t *feito) {
    if (!ativo || len > 16384) return -1;
    qtd_set(0, -1, in ? 1 : 0, pa(buf), len, 0, 1);
    int r = exec(p->qh, 1, (int)ms);
    if (feito) *feito = len - ((QTD(0)[2] >> 16) & 0x7FFF);
    return r;
}
void hc_pipe_reset(hc_pipe_t *p) {
    async_pausa();
    QH(p->qh)[6] = 0; QH(p->qh)[4] = 1; QH(p->qh)[5] = 1;
    async_volta();
}

uint8_t *hc_buf(void) { return dma + BUF_OFF; }
uint8_t *hc_cmd(void) { return dma + CMD_OFF; }

/* ---------- portas ---------- */
bool hc_ativo(void) { return ativo; }
int  hc_nportas(void) { return ativo ? nports : 0; }
bool hc_porta_conectada(int p) { return ativo && (op[PORTSC + p] & 1); }

int hc_porta_reset(int p) {
    volatile uint32_t *ps = &op[PORTSC + p];
    uint32_t v = *ps;
    if (!(v & 1)) return -1;
    if (((v >> 10) & 3) == 1) { *ps = (v & ~0x2Au) | (1u << 13); return 0; }      /* low-speed: companheiro */
    *ps = (v & ~0x2Eu) | (1u << 8);                                               /* reset, porta desabilitada */
    delay_ms(60);
    *ps = (*ps & ~0x2Au) & ~(1u << 8);
    espera(ps, 1u << 8, 0, 20);
    delay_ms(20);
    v = *ps;
    if (!(v & 4)) { *ps = (v & ~0x2Au) | (1u << 13); return 0; }                  /* full-speed: companheiro */
    return 2;
}

/* ---------- inicializacao (via registro PCI) ---------- */
static int probe(pci_dev_t *d) {
    if (d->prog_if != 0x20 || ativo) return -1;
    uint32_t bar = d->bar[0] & ~0xFu;
    if (!bar) return -1;
    paging_map_mmio(bar, 0x1000);
    pci_enable_busmaster(d);
    cap = (volatile uint8_t *)bar;
    op = (volatile uint32_t *)(cap + cap[0]);
    uint32_t hcs = *(volatile uint32_t *)(cap + 4), hcc = *(volatile uint32_t *)(cap + 8);
    nports = hcs & 0xF;
    if (nports > 15) nports = 15;

    uint32_t eecp = (hcc >> 8) & 0xFF;                   /* BIOS entrega o controlador */
    if (eecp >= 0x40) {
        uint32_t v = pci_read32(d->bus, d->slot, d->func, eecp);
        if ((v & 0xFF) == 1) {
            pci_write32(d->bus, d->slot, d->func, eecp, v | (1u << 24));
            for (int i = 0; i < 100 && (pci_read32(d->bus, d->slot, d->func, eecp) & (1u << 16)); i++) delay_ms(10);
            pci_write32(d->bus, d->slot, d->func, eecp + 4, 0);
        }
    }
    op[CMD] &= ~1u;
    espera(&op[STS], 1u << 12, 1u << 12, 100);
    op[CMD] |= 2;
    if (espera(&op[CMD], 2, 0, 200)) return -1;

    dma = (uint8_t *)frame_alloc_contig(5);
    if (!dma) return -1;
    for (int i = 0; i < N_QH; i++) {
        QH(i)[0] = pa(QH((i + 1) % N_QH)) | 2;
        QH(i)[1] = (2u << 12) | (64u << 16) | (4u << 28);
        QH(i)[2] = 1u << 30;
        QH(i)[4] = 1; QH(i)[5] = 1; QH(i)[6] = 0;
    }
    QH(Q_HEAD)[1] = 1u << 15;                             /* cabeca da lista, parada (halted) */
    QH(Q_HEAD)[2] = 0;
    QH(Q_HEAD)[6] = 0x40;

    op[CTRLDS] = 0;
    op[INTR] = 0;
    op[ASYNC] = pa(QH(Q_HEAD));
    op[CMD] = (8u << 16) | (1u << 5) | 1u;                /* async ligada, executando */
    op[CONFIG] = 1;                                       /* portas para o EHCI */
    if (espera(&op[STS], 1u << 12, 0, 100)) return -1;
    if (hcs & 0x10) for (int p = 0; p < nports; p++) op[PORTSC + p] |= (1u << 12);
    delay_ms(100);
    ativo = true;
    return 0;
}
pci_driver_t drv_ehci = { "ehci", PCI_ANY16, PCI_ANY16, 0x0C, 0x03, probe };
