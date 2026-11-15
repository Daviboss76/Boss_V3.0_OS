#include "kernel.h"
#include "vga.h"
#include "arch.h"
#include "timer.h"
#include "ring3.h"

static kjmp_t ctx;
static volatile bool ativa;
static volatile int status;
static volatile uint32_t deadline;
static uint32_t base_atual;
uint32_t r3_fault_no, r3_fault_eip, r3_fault_err;

bool ring3_ativa(void) { return ativa; }
void ring3_reset(void) { ativa = false; }

/* entra em ring 3 no slot e so volta por EXIT, falha ou timeout */
int ring3_run(int slot, uint32_t orcamento) {
    base_atual = SLOT_BASE + (uint32_t)slot * SLOT_SIZE;
    gdt_set_user(base_atual, SLOT_SIZE - 1);
    status = R3_FAULT;
    deadline = ticks + orcamento;
    ativa = true;
    if (ksetjmp(&ctx) == 0)
        enter_user(0, USER_STACK);
    ativa = false;
    __asm__ volatile ("cld; sti" ::: "memory");
    return status;
}

static void termina(int st) {
    status = st;
    klongjmp(&ctx, 1);
}

void ring3_tick(regs_t *r) {                    /* watchdog */
    if (ativa && (r->cs & 3) == 3 && (int32_t)(ticks - deadline) > 0) {
        r3_fault_eip = r->eip;
        termina(R3_TIMEOUT);
    }
}

void ring3_fault(regs_t *r) {
    if (!ativa) return;
    r3_fault_no = r->int_no; r3_fault_eip = r->eip; r3_fault_err = r->err;
    termina(R3_FAULT);
}

void ring3_syscall(regs_t *r) {
    if (!ativa || (r->cs & 3) != 3) { r->eax = (uint32_t)-1; return; }
    switch (r->eax) {
    case SYS_EXIT:
        termina(R3_OK);
        break;
    case SYS_WRITE: {
        uint32_t p = r->ebx, n = r->ecx;
        if (n > 256 || p >= SLOT_SIZE || p + n > SLOT_SIZE) { r3_fault_eip = r->eip; termina(R3_BADSYS); }
        const char *s = (const char *)(base_atual + p);
        for (uint32_t i = 0; i < n; i++) vga_putc(s[i]);
        r->eax = n;
        break; }
    case SYS_TICKS:
        r->eax = ticks;
        break;
    default:
        r3_fault_eip = r->eip;
        termina(R3_BADSYS);
    }
}
