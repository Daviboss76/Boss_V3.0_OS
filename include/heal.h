#ifndef HEAL_H
#define HEAL_H
#include <stdint.h>
#include <stdbool.h>
#include "kernel.h"
#include "arch.h"

extern volatile bool heal_due;
extern kjmp_t *oops_ctx;

void heal_init(void);
void heal_log(const char *fmt, ...);
void heal_poll(void);
int  heal_verify(void);          /* 0 ok, 1 reparou, -1 falhou */
void heal_print(void);
void heal_canario_danificar(void);
void oops_recuperado(regs_t *r);
void kpanic(regs_t *r) __attribute__((noreturn));
#endif
