#include "kernel.h"
#include "vga.h"
#include "arch.h"
#include "ring3.h"
#include "sched.h"
#include "paging.h"
#include "timer.h"
#include "heal.h"

#define BACKUP_ADDR 0x200000u
#define LOG_N 16
#define LOG_W 76

extern uint8_t __kernel_start[], __guard_end[];

static char logbuf[LOG_N][LOG_W];
static uint32_t logt[LOG_N], lognum;
static uint32_t hash_ok, guard_len, verificacoes, reparos, oops_n;
volatile bool heal_due;
kjmp_t *oops_ctx;

static uint32_t fnv(const uint8_t *p, uint32_t n) {
    uint32_t h = 2166136261u;
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

void heal_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    uint32_t i = lognum++ % LOG_N;
    kvsnprintf(logbuf[i], LOG_W, fmt, ap);
    logt[i] = ticks;
    va_end(ap);
}

void heal_init(void) {
    guard_len = (uint32_t)(__guard_end - __kernel_start);
    memcpy((void *)BACKUP_ADDR, __kernel_start, guard_len);
    hash_ok = fnv(__kernel_start, guard_len);
    heal_log("guarda do kernel: %u bytes, hash %x", guard_len, hash_ok);
}

int heal_verify(void) {
    verificacoes++;
    if (fnv(__kernel_start, guard_len) == hash_ok) return 0;
    paging_wp(false);                                   /* o texto e somente leitura */
    memcpy(__kernel_start, (void *)BACKUP_ADDR, guard_len);
    paging_wp(true);
    if (fnv(__kernel_start, guard_len) != hash_ok) {
        heal_log("FALHA: nao consegui restaurar o texto do kernel");
        return -1;
    }
    reparos++;
    heal_log("texto do kernel corrompido: restaurado do backup");
    return 1;
}

void heal_poll(void) {
    if (heal_due) { heal_due = false; heal_verify(); }
}

__attribute__((noinline, used)) void heal_canario(void) {
    __asm__ volatile (".rept 32\n\tnop\n\t.endr");
}
void heal_canario_danificar(void) {                     /* simula um bit-flip de hardware */
    paging_wp(false);
    ((volatile uint8_t *)heal_canario)[8] = 0xCC;
    paging_wp(true);
}

void oops_recuperado(regs_t *r) {
    char m[LOG_W];
    oops_n++;
    ring3_reset();
    sched_recover();
    vga_mute(false);
    ksnprintf(m, sizeof m, "oops %s eip=%x cr2=%x: comando abortado", exc_nome(r->int_no), r->eip, paging_cr2());
    heal_log("%s", m);
    vga_setcolor(VGA_COR(VGA_VERMELHO_CL, VGA_PRETO));
    vga_puts("\n[oops] "); vga_puts(m); vga_putc('\n');
}

void heal_print(void) {
    vga_puts("guarda: "); put_dec(verificacoes); vga_puts(" verificacoes, ");
    put_dec(reparos); vga_puts(" reparo(s); oops recuperados: "); put_dec(oops_n);
    vga_putc('\n');
    uint32_t n = lognum < LOG_N ? lognum : LOG_N, ini = lognum - n;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = (ini + k) % LOG_N;
        vga_putc('['); put_dec(logt[i] / 100); vga_puts("s] ");
        vga_puts(logbuf[i]); vga_putc('\n');
    }
}

void kpanic(regs_t *r) {
    __asm__ volatile ("cli");
    vga_mute(false);
    vga_clear(VGA_COR(VGA_BRANCO, VGA_VERMELHO));
    vga_puts("*** KERNEL PANIC ***\n\n");
    vga_puts(exc_nome(r->int_no)); vga_puts("\n\n");
    vga_puts("eip="); put_hex(r->eip, 8); vga_puts("  cs="); put_hex(r->cs, 4);
    vga_puts("  err="); put_hex(r->err, 8); vga_puts("  cr2="); put_hex(paging_cr2(), 8); vga_puts("\n");
    vga_puts("eflags="); put_hex(r->eflags, 8); vga_puts("\n");
    vga_puts("eax="); put_hex(r->eax, 8); vga_puts(" ebx="); put_hex(r->ebx, 8);
    vga_puts(" ecx="); put_hex(r->ecx, 8); vga_puts(" edx="); put_hex(r->edx, 8); vga_puts("\n");
    for (;;) __asm__ volatile ("hlt");
}
