#ifndef ARCH_H
#define ARCH_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_pusha, ebx, edx, ecx, eax;
    uint32_t int_no, err;
    uint32_t eip, cs, eflags, useresp, ss;
} regs_t;

typedef void (*irq_handler_t)(regs_t *r);

void gdt_init(void);
void gdt_set_user(uint32_t base, uint32_t limite);
void tss_io_deny_all(void);
void tss_io_allow(uint16_t porta);
void tss_set_esp0(uint32_t esp0);
uint32_t tss_esp0_padrao(void);
void idt_init(void);
void irq_register(int irq, irq_handler_t h);
const char *exc_nome(uint32_t n);
void pic_init(void);
void pic_mask(int irq);
void pic_unmask(int irq);
void pic_eoi(int irq);
bool pic_spurious(int irq);
void enter_user(uint32_t eip, uint32_t esp) __attribute__((noreturn));
#endif
