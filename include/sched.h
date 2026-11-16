#ifndef SCHED_H
#define SCHED_H
#include <stdint.h>
#include <stdbool.h>
#include "arch.h"

enum { PE_ELF = -20, PE_MEM = -21, PE_SLOT = -22 };

void sched_init(void);
int  sched_cur_pid(void);
void sched_idle(void);               /* chamado pelo kernel nas esperas: cede CPU ou dorme */
void sched_wake_tecla(void);
void sched_recover(void);            /* apos um oops em ring 0 */
regs_t *sched_tick(regs_t *r);
regs_t *sched_fault(regs_t *r);
regs_t *sched_syscall(regs_t *r);
regs_t *sched_yield_isr(regs_t *r);

int  proc_spawn(const char *nome, const uint8_t *elf, uint32_t tam);
int  proc_spawn_file(const char *caminho);        /* pid ou erro (<0) */
int  proc_wait(int pid, int *codigo);             /* kernel thread; Ctrl+C mata o processo */
void proc_kill(int pid);
int  proc_info(int i, char *nome, int *estado, uint32_t *cpu, uint32_t *kb);
const char *proc_strerror(int e);
#endif
