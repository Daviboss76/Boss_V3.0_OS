#include "kernel.h"
#include "vga.h"
#include "pci.h"

static uint8_t mac[6];

static int probe(pci_dev_t *d) {
    if (!(d->bar[0] & 1)) return -1;            /* BAR0 deve ser de I/O */
    uint16_t io = d->bar[0] & ~3u;
    pci_enable_busmaster(d);
    outb(io + 0x52, 0x00);                      /* liga o chip */
    outb(io + 0x37, 0x10);                      /* reset */
    for (int i = 0; i < 100000 && (inb(io + 0x37) & 0x10); i++) ;
    for (int i = 0; i < 6; i++) mac[i] = inb(io + i);

    vga_puts("[pci] rtl8139 MAC ");
    for (int i = 0; i < 6; i++) { put_hex(mac[i], 2); if (i < 5) vga_putc(':'); }
    vga_putc('\n');
    return 0;
}
pci_driver_t drv_rtl8139 = { "rtl8139", 0x10EC, 0x8139, PCI_ANY8, PCI_ANY8, probe };
