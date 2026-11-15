#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "arch.h"
#include "pci.h"
#include "blk.h"
#include "fs.h"
#include "fsck.h"
#include "drv.h"
#include "heal.h"
#include "splash.h"
#include "ui.h"
#include "terminal.h"

typedef struct { const char *nome; void (*run)(void); } frontend_t;

static const frontend_t frontends[] = {
    { "Interface (modo texto)", ui_run },
    { "Terminal",               terminal_run },
};
#define N_FRONT ((int)(sizeof(frontends) / sizeof(frontends[0])))

static int fs_res;
static int32_t rtc_ring3 = -1;

static void init_nucleo(void) {
    gdt_init();
    idt_init();
    pic_init();
    timer_init(100);
    kb_init();
    heal_init();
    __asm__ volatile ("sti");
}
static void detecta_hw(void) { pci_init(); blk_init(); }
static void montar_fs(void) {
    fs_res = fs_mount();
    if (fs_res == 0) {
        uint32_t a, r;
        if (fsck_run(true, &a, &r) == 0 && a) {
            heal_log("fsck: %u problema(s), %u setor(es) da FAT reparado(s)", a, r);
            fs_res = fs_mount();
        }
    }
}
static void init_drivers(void) { drv_call("rtc", 1, 0, 0, 0, 0, &rtc_ring3); }

static void dois(uint32_t v) { vga_putc('0' + v / 10); vga_putc('0' + v % 10); }

static void resumo(void) {
    vga_puts("BossOS 32 bits - kernel em C\n");
    vga_puts("[ok] GDT/TSS, IDT, PIC, timer 100 Hz, teclado por IRQ\n");
    vga_puts("[ok] pci: "); put_dec(pci_count()); vga_puts(" dispositivo(s)\n");
    vga_puts("[ok] blocos: backend "); vga_puts(blk_backend()); vga_putc('\n');
    for (int i = 0; i < blk_count(); i++) {
        const blk_info_t *d = blk_info(i);
        vga_puts("     "); vga_puts(d->nome); vga_puts(": "); vga_puts(d->modelo);
        vga_putc(' '); put_dec(d->setores / 2048); vga_puts(" MB\n");
    }
    if (fs_res == 0) {
        uint32_t t, l; fs_info(&t, &l);
        vga_puts("[ok] FAT32: "); put_dec(t / 1024); vga_puts(" MB, ");
        put_dec(l / 1024); vga_puts(" MB livres\n");
    } else {
        vga_puts("[--] sem FAT32: "); vga_puts(fs_strerror(fs_res)); vga_putc('\n');
    }
    if (rtc_ring3 >= 0) {
        vga_puts("[ok] drivers em ring 3: rtc respondeu ");
        dois((rtc_ring3 >> 16) & 0xFF); vga_putc(':');
        dois((rtc_ring3 >> 8) & 0xFF);  vga_putc(':');
        dois(rtc_ring3 & 0xFF);         vga_putc('\n');
    } else {
        vga_puts("[--] driver rtc de ring 3 nao respondeu\n");
    }
}

static int menu(void) {
    vga_puts("\nEscolha o modo de inicializacao:\n");
    for (int i = 0; i < N_FRONT; i++) {
        vga_puts("  ["); vga_putc('1' + i); vga_puts("] ");
        vga_puts(frontends[i].nome); vga_putc('\n');
    }
    for (;;) {
        int c = kb_getc();
        if (c >= '1' && c < '1' + N_FRONT) return c - '1';
    }
}

void kmain(uint32_t boot_drive) {
    (void)boot_drive;
    vga_init();

    splash_init();
    splash_step("Nucleo: GDT, TSS, IDT, PIC e timer...", 20, init_nucleo);
    splash_step("Detectando PCI e discos (IDE/AHCI)...", 50, detecta_hw);
    splash_step("Montando e verificando o FAT32...", 75, montar_fs);
    splash_step("Iniciando drivers em ring 3...", 100, init_drivers);
    splash_end();

    vga_clear(VGA_COR(VGA_CINZA, VGA_PRETO));
    vga_cursor_show(true);
    resumo();

    kjmp_t chk;
    for (;;) {
        if (ksetjmp(&chk) == 0) {
            oops_ctx = &chk;
            int sel = menu();
            frontends[sel].run();
        } else {
            __asm__ volatile ("cld; sti" ::: "memory");
            vga_setcolor(VGA_COR(VGA_CINZA, VGA_PRETO));
            vga_puts("[heal] frontend abortado; voltando ao menu\n");
        }
        oops_ctx = 0;
        vga_clear(VGA_COR(VGA_CINZA, VGA_PRETO));
    }
}
