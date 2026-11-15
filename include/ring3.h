#ifndef RING3_H
#define RING3_H
#include <stdint.h>
#include <stdbool.h>
#include "abi.h"
#include "arch.h"

enum { R3_OK = 0, R3_FAULT, R3_TIMEOUT, R3_BADSYS };
extern uint32_t r3_fault_no, r3_fault_eip, r3_fault_err;

int  ring3_run(int slot, uint32_t orcamento_ticks);
void ring3_tick(regs_t *r);
void ring3_fault(regs_t *r);
void ring3_syscall(regs_t *r);
void ring3_reset(void);
bool ring3_ativa(void);
#endif
