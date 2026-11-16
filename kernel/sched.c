#include "kernel.h"
#include "vga.h"
#include "arch.h"
#include "paging.h"
#include "timer.h"
#include "keyboard.h"
#include "rtc.h"
#include "fs.h"
#include "heal.h"
#include "uapi.h"
#include "sched.h"
#include "ring3.h"

#define MAX_PROC 8
#define MAX_FR   96
#define SLICE    2                       /* fatia de 20 ms */

enum { E_LIVRE, E_PRONTO, E_RODANDO, E_DORME, E_TECLA, E_FILHO, E_ZUMBI };

typedef struct {
    int estado, saida, filho;
    char nome[16];
    uint32_t pt, kstack, kesp, acorda, brk, nf, cpu;
    uint32_t frames[MAX_FR];
} proc_t;

typedef struct __attribute__((packed)) {
    uint8_t id[16]; uint16_t tipo, maq; uint32_t ver, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} ehdr_t;
typedef struct { uint32_t tipo, off, vaddr, paddr, filesz, memsz, flags, align; } phdr_t;

static proc_t procs[MAX_PROC];           /* 0 = thread do kernel (shell) */
static int cur, slice;

static uint32_t pframe(proc_t *p) {
    if (p->nf >= MAX_FR) return 0;
    uint32_t f = frame_alloc();
    if (f) p->frames[p->nf++] = f;
    return f;
}
static void libera(int pid) {
    proc_t *p = &procs[pid];
    for (uint32_t i = 0; i < p->nf; i++) frame_free(p->frames[i]);
    if (p->kstack) { frame_free(p->kstack); frame_free(p->kstack + 4096); }
    memset(p, 0, sizeof *p);
}

void sched_init(void) {
    memset(procs, 0, sizeof procs);
    procs[0].estado = E_RODANDO;
    strcpy(procs[0].nome, "kernel");
    cur = 0;
}
int sched_cur_pid(void) { return cur; }

/* ---------- escalonamento (round-robin) ---------- */
static int proximo(void) {
    for (int k = 1; k <= MAX_PROC; k++) {
        int i = (cur + k) % MAX_PROC;
        if (procs[i].estado == E_PRONTO) return i;
    }
    return procs[cur].estado == E_RODANDO ? cur : 0;
}

/* r != 0: salva o contexto atual; r == 0: o processo atual morreu */
static regs_t *troca(int prox, regs_t *r) {
    if (r) {
        procs[cur].kesp = (uint32_t)r;
        if (procs[cur].estado == E_RODANDO) procs[cur].estado = E_PRONTO;
    }
    cur = prox; slice = 0;
    procs[cur].estado = E_RODANDO;
    if (cur == 0) { tss_set_esp0(tss_esp0_padrao()); paging_user_set(0, 0); }
    else          { tss_set_esp0(procs[cur].kstack + 8192); paging_user_set(procs[cur].pt, 0); }
    return (regs_t *)procs[cur].kesp;
}

static regs_t *mata(int pid, int codigo) {
    procs[pid].estado = E_ZUMBI;
    procs[pid].saida = codigo;
    for (int i = 1; i < MAX_PROC; i++)
        if (procs[i].estado == E_FILHO && procs[i].filho == pid) procs[i].estado = E_PRONTO;
    return pid == cur ? troca(proximo(), 0) : 0;
}

regs_t *sched_tick(regs_t *r) {
    for (int i = 1; i < MAX_PROC; i++)
        if (procs[i].estado == E_DORME && (int32_t)(ticks - procs[i].acorda) >= 0) procs[i].estado = E_PRONTO;
    if (cur > 0 && (r->cs & 3) == 3) {           /* so preempta codigo de usuario */
        procs[cur].cpu++;
        if (++slice >= SLICE) {
            int n = proximo();
            if (n != cur) return troca(n, r);
            slice = 0;
        }
    }
    return r;
}

regs_t *sched_yield_isr(regs_t *r) {
    if (cur != 0) return r;
    int n = proximo();
    return n != cur ? troca(n, r) : r;
}

void sched_idle(void) {
    for (int i = 1; i < MAX_PROC; i++)
        if (procs[i].estado == E_PRONTO) { __asm__ volatile ("int $0x81"); return; }
    __asm__ volatile ("sti; hlt");
}

void sched_wake_tecla(void) {
    for (int i = 1; i < MAX_PROC; i++) if (procs[i].estado == E_TECLA) procs[i].estado = E_PRONTO;
}

void sched_recover(void) {
    if (cur != 0) { procs[cur].estado = E_ZUMBI; procs[cur].saida = -12; }
    cur = 0; slice = 0;
    procs[0].estado = E_RODANDO;
    tss_set_esp0(tss_esp0_padrao());
    paging_user_set(0, 0);
}

regs_t *sched_fault(regs_t *r) {
    char m[72];
    ksnprintf(m, sizeof m, "proc %s (pid %u): %s eip=%x cr2=%x", procs[cur].nome, (uint32_t)cur,
              exc_nome(r->int_no), r->eip, paging_cr2());
    heal_log("%s", m);
    vga_puts("\n[kernel] "); vga_puts(m); vga_puts(" -> processo encerrado\n");
    return mata(cur, -11);
}

/* ---------- carregador ELF ---------- */
static int elf_carrega(proc_t *p, const uint8_t *img, uint32_t sz, uint32_t *entry, uint32_t *fim) {
    if (sz < sizeof(ehdr_t)) return -1;
    const ehdr_t *e = (const ehdr_t *)img;
    if (memcmp(e->id, "\x7f" "ELF", 4) || e->id[4] != 1 || e->id[5] != 1 || e->tipo != 2 ||
        e->maq != 3 || e->phentsize != 32 || e->phnum == 0 || e->phnum > 8) return -1;
    if (e->phoff > sz || e->phnum * 32u > sz - e->phoff) return -1;
    if (e->entry < USER_BASE || e->entry >= USER_HEAP_MAX) return -1;

    uint32_t *pt = (uint32_t *)p->pt, maxfim = USER_BASE;
    for (uint32_t i = 0; i < e->phnum; i++) {
        const phdr_t *h = (const phdr_t *)(img + e->phoff + i * 32);
        if (h->tipo != 1) continue;
        if (h->vaddr < USER_BASE || h->memsz > USER_HEAP_MAX - USER_BASE ||
            h->vaddr > USER_HEAP_MAX - h->memsz || h->filesz > h->memsz ||
            h->off > sz || h->filesz > sz - h->off) return -1;
        uint32_t ini = h->vaddr & ~0xFFFu, fimv = (h->vaddr + h->memsz + 0xFFF) & ~0xFFFu;
        for (uint32_t va = ini; va < fimv; va += 4096) {
            uint32_t idx = (va - USER_BASE) >> 12, f;
            if (pt[idx] & PTE_P) f = pt[idx] & ~0xFFFu;
            else { f = pframe(p); if (!f) return -1; }
            pt[idx] = f | PTE_P | PTE_US | ((h->flags & 2) ? PTE_RW : 0) | (pt[idx] & PTE_RW);
            uint32_t a = va > h->vaddr ? va : h->vaddr;
            uint32_t fe = h->vaddr + h->filesz, b = (va + 4096 < fe) ? va + 4096 : fe;
            if (a < b) memcpy((void *)(f + (a - va)), img + h->off + (a - h->vaddr), b - a);
        }
        if (h->vaddr + h->memsz > maxfim) maxfim = h->vaddr + h->memsz;
    }
    *entry = e->entry;
    *fim = (maxfim + 0xFFF) & ~0xFFFu;
    return 0;
}

int proc_spawn(const char *nome, const uint8_t *elf, uint32_t tam) {
    int pid = 0;
    for (int t = 0; t < 2 && !pid; t++) {
        for (int i = 1; i < MAX_PROC; i++) if (procs[i].estado == E_LIVRE) { pid = i; break; }
        if (!pid && t == 0)
            for (int i = 1; i < MAX_PROC; i++) if (procs[i].estado == E_ZUMBI && i != cur) libera(i);
    }
    if (!pid) return PE_SLOT;

    proc_t *p = &procs[pid];
    memset(p, 0, sizeof *p);                     /* continua E_LIVRE ate ficar pronto */
    p->kstack = frame_alloc_contig(2);           /* pilha de kernel de 8 KB */
    if (!p->kstack) { libera(pid); return PE_MEM; }
    p->pt = pframe(p);
    if (!p->pt) { libera(pid); return PE_MEM; }

    uint32_t entry, fim;
    if (elf_carrega(p, elf, tam, &entry, &fim) < 0) { libera(pid); return PE_ELF; }
    uint32_t *pt = (uint32_t *)p->pt;
    for (int k = 0; k < 2; k++) {                /* pilha de usuario: 2 paginas no topo */
        uint32_t f = pframe(p);
        if (!f) { libera(pid); return PE_MEM; }
        pt[1020 + k] = f | PTE_P | PTE_RW | PTE_US;
    }
    p->brk = fim;
    for (int n = 0; nome[n] && n < 15; n++) p->nome[n] = nome[n];

    regs_t *f = (regs_t *)(p->kstack + 8192 - sizeof(regs_t));   /* contexto inicial */
    memset(f, 0, sizeof *f);
    f->gs = f->fs = f->es = f->ds = 0x3B;
    f->eip = entry; f->cs = 0x33; f->eflags = 0x202;
    f->useresp = USER_STACK_TOP; f->ss = 0x3B;
    p->kesp = (uint32_t)f;
    p->estado = E_PRONTO;
    return pid;
}

int proc_spawn_file(const char *caminho) {
    bool dir; uint32_t tam;
    int r = fs_stat(caminho, &dir, &tam);
    if (r) return r;
    if (dir) return FS_EISDIR;
    if (tam < 64 || tam > 256 * 1024) return PE_ELF;
    uint32_t n = (tam + 4095) / 4096, buf = frame_alloc_contig(n);
    if (!buf) return PE_MEM;
    uint32_t lido = 0;
    r = fs_read(caminho, (void *)buf, tam, &lido);
    const char *nome = caminho;
    for (const char *c = caminho; *c; c++) if (*c == '/') nome = c + 1;
    int pid = r ? r : proc_spawn(nome, (const uint8_t *)buf, lido);
    for (uint32_t i = 0; i < n; i++) frame_free(buf + i * 4096);
    return pid;
}

/* ---------- chamadas de sistema dos processos ---------- */
static bool user_ok(proc_t *p, uint32_t a, uint32_t n, bool escrita) {
    if (a < USER_BASE || a >= USER_BASE + 0x400000u) return false;
    if (!n) return true;
    uint32_t fim = a + n;
    if (fim < a || fim > USER_BASE + 0x400000u) return false;
    uint32_t *pt = (uint32_t *)p->pt;
    for (uint32_t pg = (a - USER_BASE) >> 12; pg <= (fim - 1 - USER_BASE) >> 12; pg++) {
        uint32_t e = pt[pg];
        if (!(e & PTE_P) || !(e & PTE_US) || (escrita && !(e & PTE_RW))) return false;
    }
    return true;
}
static int copia_str(proc_t *p, uint32_t src, char *dst, int max) {
    for (int i = 0; i < max - 1; i++) {
        if (!user_ok(p, src + i, 1, false)) return -1;
        dst[i] = *(const char *)(src + i);
        if (!dst[i]) return i;
    }
    return -1;
}

static regs_t *proc_syscall(regs_t *r) {
    proc_t *p = &procs[cur];
    switch (r->eax) {
    case SYS_EXIT:
        return mata(cur, (int)r->ebx);
    case SYS_WRITE: {
        uint32_t a = r->ebx, n = r->ecx;
        if (n > 512 || !user_ok(p, a, n, false)) { r->eax = (uint32_t)-1; return r; }
        for (uint32_t i = 0; i < n; i++) vga_putc(((const char *)a)[i]);
        r->eax = n; return r; }
    case SYS_TICKS:   r->eax = ticks; return r;
    case SYS_GETKEY:  r->eax = (uint32_t)kb_poll(); return r;
    case SYS_WAITKEY: {
        int k = kb_poll();
        if (k >= 0) { r->eax = (uint32_t)k; return r; }
        p->estado = E_TECLA; r->eip -= 2;                    /* refaz a chamada ao acordar */
        return troca(proximo(), r); }
    case SYS_SLEEP: {
        uint32_t t = r->ebx > 500 ? 500 : r->ebx;
        r->eax = 0;
        if (!t) return r;
        p->acorda = ticks + t; p->estado = E_DORME;
        return troca(proximo(), r); }
    case SYS_YIELD: {
        r->eax = 0;
        int n = proximo();
        return n != cur ? troca(n, r) : r; }
    case SYS_GETPID:  r->eax = (uint32_t)cur; return r;
    case SYS_TIME: {
        rtc_hora_t t; rtc_ler(&t);
        r->eax = ((uint32_t)t.h << 16) | ((uint32_t)t.m << 8) | t.s;
        return r; }
    case SYS_SBRK: {
        uint32_t old = p->brk, nw = old + r->ebx;
        if (nw < old || nw > USER_HEAP_MAX) { r->eax = (uint32_t)-1; return r; }
        uint32_t *pt = (uint32_t *)p->pt;
        for (uint32_t va = (old + 0xFFF) & ~0xFFFu; va < nw; va += 4096) {
            uint32_t idx = (va - USER_BASE) >> 12;
            if (pt[idx] & PTE_P) continue;
            uint32_t f = pframe(p);
            if (!f) { r->eax = (uint32_t)-1; return r; }
            pt[idx] = f | PTE_P | PTE_RW | PTE_US;
        }
        p->brk = nw; r->eax = old; return r; }
    case SYS_FREAD: {
        char nome[64];
        if (copia_str(p, r->ebx, nome, 64) < 0 || !user_ok(p, r->ecx, r->edx, true)) { r->eax = (uint32_t)-1; return r; }
        uint32_t lido = 0;
        int e = fs_read(nome, (void *)r->ecx, r->edx, &lido);
        r->eax = e ? (uint32_t)e : lido; return r; }
    case SYS_FWRITE: {
        char nome[64];
        if (copia_str(p, r->ebx, nome, 64) < 0 || !user_ok(p, r->ecx, r->edx, false)) { r->eax = (uint32_t)-1; return r; }
        r->eax = (uint32_t)fs_write(nome, (const void *)r->ecx, r->edx); return r; }
    case SYS_SPAWN: {
        char nome[64];
        if (copia_str(p, r->ebx, nome, 64) < 0) { r->eax = (uint32_t)-1; return r; }
        r->eax = (uint32_t)proc_spawn_file(nome); return r; }
    case SYS_WAIT: {
        int pid = (int)r->ebx;
        if (pid <= 0 || pid >= MAX_PROC || pid == cur || procs[pid].estado == E_LIVRE) { r->eax = (uint32_t)-1; return r; }
        if (procs[pid].estado == E_ZUMBI) { r->eax = (uint32_t)procs[pid].saida; libera(pid); return r; }
        p->estado = E_FILHO; p->filho = pid; r->eip -= 2;
        return troca(proximo(), r); }
    default:
        heal_log("proc %s: syscall invalida %u", p->nome, r->eax);
        return mata(cur, -2);
    }
}

regs_t *sched_syscall(regs_t *r) {
    if (cur == 0) { ring3_syscall(r); return r; }       /* driver sincrono do kernel thread */
    return proc_syscall(r);
}

/* ---------- API do kernel thread ---------- */
void proc_kill(int pid) {
    if (pid > 0 && pid < MAX_PROC && procs[pid].estado != E_LIVRE && procs[pid].estado != E_ZUMBI && pid != cur)
        mata(pid, -9);
}

int proc_wait(int pid, int *codigo) {
    if (pid <= 0 || pid >= MAX_PROC) return -1;
    while (procs[pid].estado != E_ZUMBI && procs[pid].estado != E_LIVRE) {
        if (kb_intr_take()) { proc_kill(pid); vga_puts("^C\n"); }
        sched_idle();
    }
    if (codigo) *codigo = procs[pid].saida;
    if (procs[pid].estado == E_ZUMBI) libera(pid);
    return 0;
}

int proc_info(int i, char *nome, int *estado, uint32_t *cpu, uint32_t *kb) {
    if (i < 0 || i >= MAX_PROC || procs[i].estado == E_LIVRE) return -1;
    strcpy(nome, procs[i].nome);
    *estado = procs[i].estado;
    *cpu = procs[i].cpu;
    *kb = procs[i].nf * 4 + (procs[i].kstack ? 8 : 0);
    return 0;
}

const char *proc_strerror(int e) {
    return e == PE_ELF ? "ELF invalido" : e == PE_MEM ? "sem memoria" :
           e == PE_SLOT ? "tabela de processos cheia" : "erro";
}
