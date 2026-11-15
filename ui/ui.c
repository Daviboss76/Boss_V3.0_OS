#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "rtc.h"
#include "pci.h"
#include "ui.h"

#define COR_FUNDO  VGA_COR(VGA_BRANCO, VGA_AZUL)
#define COR_JANELA VGA_COR(VGA_PRETO, VGA_CINZA)
#define COR_BARRA  VGA_COR(VGA_PRETO, VGA_CINZA)

static void janela(int x, int y, int w, int h, const char *titulo, uint8_t c) {
    vga_fill(x, y, w, h, ' ', c);
    for (int i = 1; i < w - 1; i++) {
        vga_put_at(x + i, y, (char)0xCD, c); vga_put_at(x + i, y + h - 1, (char)0xCD, c);
    }
    for (int j = 1; j < h - 1; j++) {
        vga_put_at(x, y + j, (char)0xBA, c); vga_put_at(x + w - 1, y + j, (char)0xBA, c);
    }
    vga_put_at(x, y, (char)0xC9, c);         vga_put_at(x + w - 1, y, (char)0xBB, c);
    vga_put_at(x, y + h - 1, (char)0xC8, c); vga_put_at(x + w - 1, y + h - 1, (char)0xBC, c);
    vga_puts_at(x + 2, y, titulo, c);
}

static void relogio(void) {
    rtc_hora_t t; rtc_ler(&t);
    char s[9] = { '0' + t.h / 10, '0' + t.h % 10, ':', '0' + t.m / 10, '0' + t.m % 10,
                  ':', '0' + t.s / 10, '0' + t.s % 10, 0 };
    vga_puts_at(VGA_COLS - 9, VGA_ROWS - 1, s, COR_BARRA);
}

void ui_run(void) {
    vga_cursor_show(false);
    vga_clear(COR_FUNDO);
    janela(8, 2, 64, 16, " BossOS - Dispositivos PCI ", COR_JANELA);

    vga_setcolor(COR_JANELA);
    int n = pci_count(); if (n > 12) n = 12;
    for (int i = 0; i < n; i++) {
        pci_dev_t *d = pci_get(i);
        vga_set_cursor(10, 4 + i);
        put_hex(d->bus, 2); vga_putc(':'); put_hex(d->slot, 2); vga_putc('.'); put_dec(d->func);
        vga_putc(' '); put_hex(d->vendor, 4); vga_putc(':'); put_hex(d->device, 4);
        vga_putc(' '); vga_puts(pci_class_name(d->class_code));
        if (d->driver) { vga_puts(" ["); vga_puts(d->driver); vga_putc(']'); }
    }

    vga_fill(0, VGA_ROWS - 1, VGA_COLS, 1, ' ', COR_BARRA);
    vga_puts_at(1, VGA_ROWS - 1, "BossOS | [ESC] voltar ao menu", COR_BARRA);

    for (;;) {
        relogio();
        if (kb_poll() == KEY_ESC) break;
    }
    vga_setcolor(VGA_COR(VGA_CINZA, VGA_PRETO));
    vga_cursor_show(true);
}
