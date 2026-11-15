#include "kernel.h"
#include "vga.h"

static volatile uint16_t *const vram = (volatile uint16_t *)0xB8000;
static int cx, cy;
static uint8_t cor_atual = VGA_COR(VGA_CINZA, VGA_PRETO);
static bool mudo;

void vga_mute(bool m) { mudo = m; }

static void cursor_hw(void) {
    uint16_t pos = cy * VGA_COLS + cx;
    outb(0x3D4, 14); outb(0x3D5, pos >> 8);
    outb(0x3D4, 15); outb(0x3D5, pos & 0xFF);
}
void vga_init(void) { vga_clear(cor_atual); }

void vga_clear(uint8_t cor) {
    cor_atual = cor;
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) vram[i] = (cor << 8) | ' ';
    cx = cy = 0; cursor_hw();
}
void vga_setcolor(uint8_t cor) { cor_atual = cor; }

static void rolar(void) {
    for (int i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++) vram[i] = vram[i + VGA_COLS];
    for (int x = 0; x < VGA_COLS; x++)
        vram[(VGA_ROWS - 1) * VGA_COLS + x] = (cor_atual << 8) | ' ';
    cy = VGA_ROWS - 1;
}
void vga_putc(char c) {
    if (mudo) return;
    if (c == '\n')      { cx = 0; cy++; }
    else if (c == '\r') { cx = 0; }
    else if (c == '\t') { cx = (cx + 4) & ~3; }
    else if (c == '\b') {
        if (cx > 0) { cx--; vram[cy * VGA_COLS + cx] = (cor_atual << 8) | ' '; }
    } else {
        vram[cy * VGA_COLS + cx++] = (cor_atual << 8) | (uint8_t)c;
    }
    if (cx >= VGA_COLS) { cx = 0; cy++; }
    if (cy >= VGA_ROWS) rolar();
    cursor_hw();
}
void vga_puts(const char *s) { while (*s) vga_putc(*s++); }

void vga_set_cursor(int x, int y) { cx = x; cy = y; cursor_hw(); }
void vga_cursor_show(bool on) {
    outb(0x3D4, 0x0A); outb(0x3D5, on ? 14 : 0x20);
    outb(0x3D4, 0x0B); outb(0x3D5, 15);
}
void vga_put_at(int x, int y, char c, uint8_t cor) {
    vram[y * VGA_COLS + x] = (cor << 8) | (uint8_t)c;
}
void vga_puts_at(int x, int y, const char *s, uint8_t cor) {
    while (*s && x < VGA_COLS) vga_put_at(x++, y, *s++, cor);
}
void vga_fill(int x, int y, int w, int h, char c, uint8_t cor) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) vga_put_at(x + i, y + j, c, cor);
}
