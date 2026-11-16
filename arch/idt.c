#include "kernel.h"
#include "arch.h"
#include "ring3.h"
#include "sched.h"
#include "heal.h"

typedef struct __attribute__((packed)) { uint16_t lo, sel; uint8_t zero, flags; uint16_t hi; } idt_ent_t;
typedef struct __attribute__((packed)) { uint16_t limite; uint32_t base; } idtr_t;

extern const uint32_t isr_table[48];
extern void isr128(void);
extern void isr129(void);
extern void idt_load(idtr_t *p);

static idt_ent_t idt[256];
static irq_handler_t irqs[16];

static const char *const nomes[32] = {
    "#DE divisao por zero", "#DB debug", "NMI", "#BP breakpoint", "#OF overflow",
    "#BR limite", "#UD opcode invalido", "#NM sem FPU", "#DF double fault",
    "FPU overrun", "#TS TSS invalido", "#NP segmento ausente", "#SS pilha",
    "#GP protecao geral", "#PF page fault", "reservado", "#MF FPU",
    "#AC alinhamento", "#MC machine check", "#XM SIMD", "#VE virtualizacao",
    "#CP controle", "reservado", "reservado", "reservado", "reservado",
    "reservado", "reservado", "reservado", "reservado", "#SX seguranca", "reservado" };

const char *exc_nome(uint32_t n) { return n < 32 ? nomes[n] : "interrupcao"; }

static void gate(int n, uint32_t h, uint8_t flags) {
    idt[n].lo = h & 0xFFFF; idt[n].sel = 0x08; idt[n].zero = 0;
    idt[n].flags = flags;   idt[n].hi = h >> 16;
}

void idt_init(void) {
    memset(idt, 0, sizeof idt);
    for (int i = 0; i < 48; i++) gate(i, isr_table[i], 0x8E);
    gate(0x80, (uint32_t)isr128, 0xEE);          /* syscall (DPL 3) */
    gate(0x81, (uint32_t)isr129, 0x8E);          /* yield do kernel (DPL 0) */
    idtr_t p = { sizeof idt - 1, (uint32_t)idt };
    idt_load(&p);
}

void irq_register(int irq, irq_handler_t h) {
    irqs[irq] = h;
    pic_unmask(irq);
}

static regs_t *excecao(regs_t *r) {
    if (r->int_no == 8) kpanic(r);
    if ((r->cs & 3) == 3) {
        if (sched_cur_pid() > 0) return sched_fault(r);   /* processo: mata so ele */
        ring3_fault(r);                                    /* driver sincrono */
        kpanic(r);
    }
    if (oops_ctx) {                                        /* ring 0: aborta o comando */
        oops_recuperado(r);
        klongjmp(oops_ctx, 1);
    }
    kpanic(r);
}

/* devolve o contexto a retomar (outro processo, se o escalonador decidir) */
regs_t *isr_handler(regs_t *r) {
    uint32_t n = r->int_no;
    if (n == 0x80) return sched_syscall(r);
    if (n == 0x81) return sched_yield_isr(r);
    if (n >= 32 && n < 48) {
        int irq = n - 32;
        if (pic_spurious(irq)) return r;
        pic_eoi(irq);
        if (irqs[irq]) irqs[irq](r);
        return irq == 0 ? sched_tick(r) : r;
    }
    if (n < 32) return excecao(r);
    return r;
}
