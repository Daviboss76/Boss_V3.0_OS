#ifndef ULIB_H
#define ULIB_H
#include <stdint.h>
#include "abi.h"

static inline uint32_t sys3(uint32_t n, uint32_t a, uint32_t b) {
    uint32_t r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b) : "memory");
    return r;
}
static inline uint32_t sys_ticks(void) { return sys3(SYS_TICKS, 0, 0); }
static inline void sys_write(const char *s, uint32_t n) { sys3(SYS_WRITE, (uint32_t)s, n); }

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile ("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

#define MBOX ((volatile mbox_t *)MBOX_OFF)
#define DRIVER(fn) \
    void __attribute__((section(".text.start"), noreturn)) _start(void) { \
        fn(MBOX); sys3(SYS_EXIT, 0, 0); for (;;) ; }
#endif
