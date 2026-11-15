#ifndef VGA_H
#define VGA_H
#include <stdint.h>
#include <stdbool.h>
#define VGA_COLS 80
#define VGA_ROWS 25
enum { VGA_PRETO, VGA_AZUL, VGA_VERDE, VGA_CIANO, VGA_VERMELHO, VGA_MAGENTA,
       VGA_MARROM, VGA_CINZA, VGA_CINZA_ESC, VGA_AZUL_CL, VGA_VERDE_CL,
       VGA_CIANO_CL, VGA_VERMELHO_CL, VGA_MAGENTA_CL, VGA_AMARELO, VGA_BRANCO };
#define VGA_COR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

void vga_init(void);
void vga_clear(uint8_t cor);
void vga_setcolor(uint8_t cor);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_set_cursor(int x, int y);
void vga_cursor_show(bool ligado);
void vga_put_at(int x, int y, char c, uint8_t cor);
void vga_puts_at(int x, int y, const char *s, uint8_t cor);
void vga_fill(int x, int y, int w, int h, char c, uint8_t cor);
void vga_mute(bool m);
#endif
