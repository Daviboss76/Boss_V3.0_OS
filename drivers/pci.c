#include "kernel.h"
#include "vga.h"
#include "pci.h"

static pci_dev_t devs[PCI_MAX];
static int ndev;

uint32_t pci_read32(uint8_t b, uint8_t s, uint8_t f, uint8_t off) {
    outl(0xCF8, 0x80000000u | ((uint32_t)b << 16) | ((uint32_t)s << 11) |
                ((uint32_t)f << 8) | (off & 0xFC));
    return inl(0xCFC);
}
void pci_write32(uint8_t b, uint8_t s, uint8_t f, uint8_t off, uint32_t v) {
    outl(0xCF8, 0x80000000u | ((uint32_t)b << 16) | ((uint32_t)s << 11) |
                ((uint32_t)f << 8) | (off & 0xFC));
    outl(0xCFC, v);
}
void pci_enable_busmaster(pci_dev_t *d) {
    uint32_t cmd = pci_read32(d->bus, d->slot, d->func, 0x04) & 0xFFFF;
    pci_write32(d->bus, d->slot, d->func, 0x04, cmd | 0x07); /* IO + MEM + BM */
}

static void adicionar(uint8_t b, uint8_t s, uint8_t f) {
    uint32_t id = pci_read32(b, s, f, 0x00);
    if ((id & 0xFFFF) == 0xFFFF || ndev >= PCI_MAX) return;
    pci_dev_t *d = &devs[ndev++];
    d->bus = b; d->slot = s; d->func = f;
    d->vendor = id & 0xFFFF; d->device = id >> 16;
    uint32_t cl = pci_read32(b, s, f, 0x08);
    d->revision = cl & 0xFF; d->prog_if = (cl >> 8) & 0xFF;
    d->subclass = (cl >> 16) & 0xFF; d->class_code = cl >> 24;
    uint8_t ht = (pci_read32(b, s, f, 0x0C) >> 16) & 0x7F;
    for (int i = 0; i < 6; i++)
        d->bar[i] = (ht == 0) ? pci_read32(b, s, f, 0x10 + i * 4) : 0;
    d->irq = pci_read32(b, s, f, 0x3C) & 0xFF;
    d->driver = 0;
}

static bool casa(const pci_driver_t *r, const pci_dev_t *d) {
    return (r->vendor     == PCI_ANY16 || r->vendor     == d->vendor)     &&
           (r->device     == PCI_ANY16 || r->device     == d->device)     &&
           (r->class_code == PCI_ANY8  || r->class_code == d->class_code) &&
           (r->subclass   == PCI_ANY8  || r->subclass   == d->subclass);
}

void pci_init(void) {
    ndev = 0;
    for (int b = 0; b < 256; b++)
        for (int s = 0; s < 32; s++) {
            if ((pci_read32(b, s, 0, 0) & 0xFFFF) == 0xFFFF) continue;
            bool multi = (pci_read32(b, s, 0, 0x0C) >> 16) & 0x80;
            for (int f = 0; f < (multi ? 8 : 1); f++) adicionar(b, s, f);
        }

    for (int i = 0; i < ndev; i++)
        for (int k = 0; pci_drivers[k]; k++)
            if (casa(pci_drivers[k], &devs[i]) && pci_drivers[k]->probe(&devs[i]) == 0) {
                devs[i].driver = pci_drivers[k]->nome;
                break;
            }
}
int pci_count(void) { return ndev; }
pci_dev_t *pci_get(int i) { return (i >= 0 && i < ndev) ? &devs[i] : 0; }

const char *pci_class_name(uint8_t c) {
    switch (c) {
    case 0x01: return "Armazenamento"; case 0x02: return "Rede";
    case 0x03: return "Video";         case 0x04: return "Multimidia";
    case 0x05: return "Memoria";       case 0x06: return "Bridge";
    case 0x07: return "Comunicacao";   case 0x08: return "Sistema";
    case 0x09: return "Entrada";       case 0x0C: return "Serial bus";
    default:   return "Outro";
    }
}
