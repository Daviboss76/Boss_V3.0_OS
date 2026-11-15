#ifndef KERNEL_H
#define KERNEL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

static inline void outb(uint16_t p, uint8_t v)  { __asm__ volatile ("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile ("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile ("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t  inb(uint16_t p) { uint8_t v;  __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile ("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile ("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void insw(uint16_t port, void *buf, uint32_t n) {
    __asm__ volatile ("rep insw" : "+D"(buf), "+c"(n) : "d"(port) : "memory");
}
static inline void outsw(uint16_t port, const void *buf, uint32_t n) {
    __asm__ volatile ("rep outsw" : "+S"(buf), "+c"(n) : "d"(port) : "memory");
}

/* setjmp/longjmp do kernel (arch/arch.asm) */
typedef struct { uint32_t ebx, esi, edi, ebp, esp, eip; } kjmp_t;
int  ksetjmp(kjmp_t *b) __attribute__((returns_twice));
void klongjmp(kjmp_t *b, int v) __attribute__((noreturn));

void  *memset(void *d, int c, size_t n);
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
char  *strcpy(char *d, const char *s);

void put_hex(uint32_t v, int digitos);
void put_dec(uint32_t v);
int  kvsnprintf(char *out, int max, const char *fmt, va_list ap);
int  ksnprintf(char *out, int max, const char *fmt, ...);
#endif
