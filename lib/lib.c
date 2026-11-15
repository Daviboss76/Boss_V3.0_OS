#include "kernel.h"
#include "vga.h"

void *memset(void *d, int c, size_t n) {
    uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d;
}
void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d;
}
void *memmove(void *d, const void *s, size_t n) {
    uint8_t *a = d; const uint8_t *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}
char *strcpy(char *d, const char *s) {
    char *r = d; while ((*d++ = *s++)) ; return r;
}

void put_hex(uint32_t v, int digitos) {
    static const char h[] = "0123456789ABCDEF";
    for (int i = (digitos - 1) * 4; i >= 0; i -= 4) vga_putc(h[(v >> i) & 0xF]);
}
void put_dec(uint32_t v) {
    char b[11]; int i = 0;
    if (!v) { vga_putc('0'); return; }
    while (v) { b[i++] = '0' + v % 10; v /= 10; }
    while (i) vga_putc(b[--i]);
}

/* printf minimo: %s %d %u %x %% */
static void emite(char *o, int max, int *n, char c) { if (*n < max - 1) o[*n] = c; (*n)++; }
static void emite_num(char *o, int max, int *n, uint32_t v, uint32_t base) {
    char t[12]; int i = 0;
    if (!v) t[i++] = '0';
    while (v) { t[i++] = "0123456789abcdef"[v % base]; v /= base; }
    while (i) emite(o, max, n, t[--i]);
}
int kvsnprintf(char *o, int max, const char *f, va_list ap) {
    int n = 0;
    for (; *f; f++) {
        if (*f != '%') { emite(o, max, &n, *f); continue; }
        f++; if (!*f) break;
        switch (*f) {
        case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(nulo)";
                    while (*s) emite(o, max, &n, *s++); break; }
        case 'd': { int32_t v = va_arg(ap, int32_t);
                    if (v < 0) { emite(o, max, &n, '-'); emite_num(o, max, &n, (uint32_t)(-(v + 1)) + 1u, 10); }
                    else emite_num(o, max, &n, (uint32_t)v, 10);
                    break; }
        case 'u': emite_num(o, max, &n, va_arg(ap, uint32_t), 10); break;
        case 'x': emite_num(o, max, &n, va_arg(ap, uint32_t), 16); break;
        case '%': emite(o, max, &n, '%'); break;
        default:  emite(o, max, &n, '%'); emite(o, max, &n, *f); break;
        }
    }
    if (max > 0) o[n < max ? n : max - 1] = 0;
    return n;
}
int ksnprintf(char *o, int max, const char *f, ...) {
    va_list ap; va_start(ap, f);
    int r = kvsnprintf(o, max, f, ap);
    va_end(ap); return r;
}
