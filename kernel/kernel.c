#include "kernel.h"
#include "e1000.h"
#include "net.h"
#include "drv.h"
#include "heal.h"
#include "pci.h"
#include "blk.h"
#include "keyboard.h"
#include "splash.h"
#include "vga.h"

void ui_run(void);
void terminal_run(void);
void disktool_run(void);
void usbtool_run(void);

void gdt_init(void);
void idt_init(void);
void pic_init(void);
void timer_init(uint32_t hz);
void kb_init(void);
void paging_init(void);
void sched_init(void);
void pci_init(void);
void blk_init(void);
int pci_count(void);
void paging_map_mmio(uint32_t fisico, uint32_t tam);
void paging_allow_user(uint32_t addr, uint32_t len);
int fsck_run(bool reparar, uint32_t *problemas, uint32_t *reparados);
int fs_mount(void);
void fs_info(uint32_t *total_kb, uint32_t *livre_kb);
const char *blk_backend(void);
int disk_count(void);
int usb_ndev(void);
int usb_msd_count(void);
void vga_puts(const char *s);
void vga_putc(char c);
void put_dec(uint32_t v);
int kbd_getc_blocking(void);

#ifndef E1000_RING_BYTES
#define E1000_RING_BYTES (4096u + 32u * 2048u)
#endif

typedef struct { const char *nome; void (*run)(void); } frontend_t;

static const frontend_t frontends[] = {
    { "Interface (modo texto)",           ui_run },
    { "Terminal",                         terminal_run },
    { "Discos, particoes e instalador",   disktool_run },
    { "USB e pendrive",                   usbtool_run },
};
#define N_FRONT ((int)(sizeof(frontends) / sizeof(frontends[0])))

static int fs_res = -1;
static int net_res = -1;
static int32_t rtc_ring3 = -1;

static uint8_t e1000_rx_mem[E1000_RING_BYTES] __attribute__((aligned(4096)));
static uint8_t e1000_tx_mem[E1000_RING_BYTES] __attribute__((aligned(4096)));

static void init_nucleo(void) {
    gdt_init();
    idt_init();
    pic_init();
    timer_init(100);
    kb_init();
    heal_init();
    paging_init();
    sched_init();
    __asm__ volatile ("sti");
}

static void detecta_hw(void) {
    pci_init();
    blk_init();
}

static void montar_fs(void) {
    uint32_t a = 0, r = 0;
    fs_res = fs_mount();
    if (fs_res != 0) {
        if (fsck_run(true, &a, &r) == 0 && a) {
            heal_log("fsck: %u problema(s), %u setor(es) da FAT reparado(s)", a, r);
            fs_res = fs_mount();
        }
    }
}

static void init_drivers(void) {
    drv_call("rtc", 1, 0, 0, 0, 0, &rtc_ring3);

    for (int i = 0; i < pci_count(); i++) {
        pci_dev_t *d = pci_get(i);
        if (!d) continue;
        if (d->class_code == 0x02 && d->vendor == 0x8086 && (d->bar[0] & 1u) == 0) {
            uint32_t mmio = d->bar[0] & ~0xFu;
            if (mmio) {
                pci_enable_busmaster(d);
                paging_map_mmio(mmio, 0x20000);

                if (e1000_init(mmio,
                               (uint32_t)(uintptr_t)e1000_rx_mem,
                               (uint32_t)(uintptr_t)e1000_tx_mem) == 0) {
                    heal_log("e1000: ring 0 inicializado (link=%s)",
                             e1000_link_up() ? "up" : "down");
                } else {
                    heal_log("e1000: falha ao inicializar em ring 0");
                }

                paging_allow_user((uint32_t)(uintptr_t)e1000_rx_mem, sizeof(e1000_rx_mem));
                paging_allow_user((uint32_t)(uintptr_t)e1000_tx_mem, sizeof(e1000_tx_mem));
                net_res = net_init(mmio,
                                   (uint32_t)(uintptr_t)e1000_rx_mem,
                                   (uint32_t)(uintptr_t)e1000_tx_mem);
            }
            break;
        }
    }
}

static void dois(uint32_t v) {
    vga_putc((char)('0' + (v / 10u) % 10u));
    vga_putc((char)('0' + v % 10u));
}

static void resumo(void) {
    vga_puts("BossOS 32 bits - kernel em C\n");
    vga_puts("[ok] GDT/TSS, IDT, PIC, timer, paginacao, escalonador\n");
    vga_puts("[ok] pci: "); put_dec((uint32_t)pci_count()); vga_puts(" dispositivo(s)\n");
    vga_puts("[ok] USB: "); put_dec((uint32_t)usb_ndev()); vga_puts(" dispositivo(s), ");
    put_dec((uint32_t)usb_msd_count()); vga_puts(" armazenamento(s) (detalhes: modo USB e pendrive)\n");
    vga_puts("[ok] discos ("); vga_puts(blk_backend()); vga_puts("): "); put_dec((uint32_t)disk_count()); vga_putc('\n');
    for (int i = 0; i < disk_count(); i++) {
        const blk_info_t *d = disk_info(i);
        if (!d) continue;
        vga_puts("     "); vga_puts(d->nome); vga_puts(disk_is_usb(i) ? " [USB] " : " ");
        vga_puts(d->modelo); vga_putc(' '); put_dec(d->setores / 2048u); vga_puts(" MB\n");
    }
    if (fs_res == 0) {
        uint32_t t = 0, l = 0;
        fs_info(&t, &l);
        vga_puts("[ok] volume BOSSOS: "); put_dec(t / 1024u); vga_puts(" MB, ");
        put_dec(l / 1024u); vga_puts(" MB livres\n");
    } else {
        vga_puts("[--] nenhum volume BOSSOS montado (use 'install' ou 'USB e pendrive')\n");
    }
    if (net_res == 0) {
        vga_puts("[ok] rede intel_e1000 inicializada\n");
    } else {
        vga_puts("[--] rede intel_e1000 nao iniciada\n");
    }
    if (rtc_ring3 >= 0) {
        vga_puts("[ok] driver rtc em ring 3: ");
        dois(((uint32_t)rtc_ring3 >> 16) & 0xFFu); vga_putc(':');
        dois(((uint32_t)rtc_ring3 >> 8) & 0xFFu);  vga_putc(':');
        dois((uint32_t)rtc_ring3 & 0xFFu);         vga_putc('\n');
    }
}

static int menu(void) {
    vga_puts("\nEscolha o modo de inicializacao:\n");
    for (int i = 0; i < N_FRONT; i++) {
        vga_puts("  ["); vga_putc((char)('1' + i)); vga_puts("] ");
        vga_puts(frontends[i].nome); vga_putc('\n');
    }
    for (;;) {
    int c = kb_getc();
    if (c >= '1' && c < '1' + N_FRONT) {
        return c - '1';
        }
    }
}

void kmain(void) {
    splash_init();
    splash_step("Inicializando nucleo...", 25, init_nucleo);
    splash_step("Detectando hardware...", 50, detecta_hw);
    splash_step("Montando sistema de arquivos...", 75, montar_fs);
    splash_step("Inicializando drivers...", 100, init_drivers);
    splash_end();
    vga_clear(0x07);
    vga_set_cursor(0, 0);

    resumo();
    for (;;) {
        int idx = menu();
        if (frontends[idx].run) {
            frontends[idx].run();
        }
    }
}

