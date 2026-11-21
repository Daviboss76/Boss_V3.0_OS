#include "kernel.h"
#include "arch.h"
#include "ring3.h"
#include "timer.h"
#include "heal.h"
#include "drv.h"

extern const uint8_t blob_rtc[],  blob_rtc_end[];
extern const uint8_t blob_beep[], blob_beep_end[];
extern const uint8_t blob_crc[],  blob_crc_end[];
extern const uint8_t blob_bug[],  blob_bug_end[];
extern const uint8_t blob_intel_e1000[], blob_intel_e1000_end[];
extern const uint8_t blob_mouse_r3[], blob_mouse_r3_end[];

typedef struct {
    const char *nome;
    const uint8_t *ini, *fim;
    uint16_t portas[4];
    uint32_t nportas, orcamento;       /* orcamento em ticks (10 ms) */
    bool carregado;
    uint32_t consec, quarentena_ate, chamadas, falhas, reinicios;
} drv_t;

static drv_t drv[] = {
    { "rtc",         blob_rtc,         blob_rtc_end,         { 0x70, 0x71 },       2, 20,  0, {0}, 0, 0, 0, 0, 0 },
    { "beep",        blob_beep,        blob_beep_end,        { 0x42, 0x43, 0x61 }, 3, 300, 0, {0}, 0, 0, 0, 0, 0 },
    { "crc",         blob_crc,         blob_crc_end,         { 0 },                0, 50,  0, {0}, 0, 0, 0, 0, 0 },
    { "bug",         blob_bug,         blob_bug_end,         { 0 },                0, 30,  0, {0}, 0, 0, 0, 0, 0 },
    { "intel_e1000", blob_intel_e1000, blob_intel_e1000_end, { 0 },                0, 100, 0, {0}, 0, 0, 0, 0, 0 },
    { "mouse_r3",    blob_mouse_r3,    blob_mouse_r3_end,    { 0x60, 0x64 },       2, 50,  0, {0}, 0, 0, 0, 0, 0 },
};

#define N_DRV ((int)(sizeof drv / sizeof drv[0]))
_Static_assert(sizeof drv / sizeof drv[0] <= N_SLOTS, "slots insuficientes");

static uint8_t *slot_mem(int i) { return (uint8_t *)(SLOT_BASE + (uint32_t)i * SLOT_SIZE); }

static void carrega(int i) {                 /* imagem limpa, direto da copia protegida */
    uint32_t n = (uint32_t)(drv[i].fim - drv[i].ini);
    if (n > MBOX_OFF - 0x1000) n = MBOX_OFF - 0x1000;
    memset(slot_mem(i), 0, SLOT_SIZE);
    memcpy(slot_mem(i), drv[i].ini, n);
    drv[i].carregado = true;
}

static const char *motivo(int st) {
    switch (st) {
    case R3_FAULT:   return exc_nome(r3_fault_no);
    case R3_TIMEOUT: return "travou (watchdog)";
    case R3_BADSYS:  return "syscall invalida";
    default:         return "desconhecido";
    }
}

int drv_count(void) { return N_DRV; }

int drv_stat(int i, drv_stat_t *s) {
    if (i < 0 || i >= N_DRV) return -1;
    drv_t *d = &drv[i];
    s->nome = d->nome; s->chamadas = d->chamadas;
    s->falhas = d->falhas; s->reinicios = d->reinicios;
    bool q = d->quarentena_ate && (int32_t)(ticks - d->quarentena_ate) < 0;
    s->estado = q ? 2 : (d->carregado ? 1 : 0);
    return 0;
}

int drv_call(const char *nome, uint32_t op, uint32_t a0, uint32_t a1,
             const void *buf, uint32_t len, int32_t *ret) {
    int i;
    for (i = 0; i < N_DRV; i++) if (!strcmp(drv[i].nome, nome)) break;
    if (i == N_DRV) return DRV_ENOENT;
    drv_t *d = &drv[i];

    if (d->quarentena_ate) {
        if ((int32_t)(ticks - d->quarentena_ate) < 0) return DRV_EQUAR;
        d->quarentena_ate = 0;
        heal_log("drv %s: saiu da quarentena", d->nome);
    }
    d->chamadas++;
    mbox_t *m = (mbox_t *)(slot_mem(i) + MBOX_OFF);

    for (int t = 0; t < 2; t++) {            /* tentativa + 1 reinicio */
        if (!d->carregado) {
            carrega(i);
            if (t) d->reinicios++;
        }
        memset(m, 0, sizeof *m);
        m->op = op; m->arg[0] = a0; m->arg[1] = a1;
        if (buf && len) {
            if (len > sizeof m->buf) len = sizeof m->buf;
            memcpy(m->buf, buf, len); m->len = len;
        }
        tss_io_deny_all();
        for (uint32_t p = 0; p < d->nportas; p++) tss_io_allow(d->portas[p]);
        int st = ring3_run(i, d->orcamento);
        tss_io_deny_all();

        if (st == R3_OK) {
            d->consec = 0;
            if (ret) *ret = m->ret;
            return DRV_OK;
        }
        d->falhas++; d->consec++; d->carregado = false;
        heal_log("drv %s: %s em eip=%x, imagem recarregada", d->nome, motivo(st), r3_fault_eip);
        if (d->consec >= 3) {
            d->consec = 0;
            d->quarentena_ate = ticks + 1000;       /* 10 s */
            heal_log("drv %s: 3 falhas seguidas, quarentena de 10 s", d->nome);
            return DRV_EQUAR;
        }
    }
    return DRV_EFALHA;
}
