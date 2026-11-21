#ifndef PLIB_H
#define PLIB_H
#include <stdint.h>
#include "uapi.h"

static inline uint32_t sc(uint32_t n, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

static inline void puts_(const char *s) { uint32_t n = 0; while (s[n]) n++; sc(SYS_WRITE, (uint32_t)s, n, 0); }
static inline void putnum_(uint32_t v) {
    char b[12]; int i = 11; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    puts_(b + i);
}
static inline void sleep_(uint32_t t)  { sc(SYS_SLEEP, t, 0, 0); }
static inline uint32_t getpid_(void)   { return sc(SYS_GETPID, 0, 0, 0); }
static inline void exit_(int c)        { sc(SYS_EXIT, (uint32_t)c, 0, 0); for (;;) ; }

// --- Funções de leitura de teclado corrigidas para SYS_FREAD ---
static inline int read_(int fd, void *buf, uint32_t count) {
    return (int)sc(SYS_FREAD, (uint32_t)fd, (uint32_t)buf, count);
}

static inline char getchar_(void) {
    char c = 0;
    while (read_(0, &c, 1) <= 0) {
        sleep_(1);
    }
    return c;
}

#define ENTRADA void __attribute__((section(".text.start"), noreturn)) _start(void)
#endif

