#ifndef PCI_H
#define PCI_H
#include <stdint.h>

#define PCI_ANY16 0xFFFF
#define PCI_ANY8  0xFF
#define PCI_MAX   64

typedef struct {
    uint8_t  bus, slot, func;
    uint16_t vendor, device;
    uint8_t  class_code, subclass, prog_if, revision, irq;
    uint32_t bar[6];
    const char *driver;            /* nome do driver ligado, ou 0 */
} pci_dev_t;

typedef struct {
    const char *nome;
    uint16_t vendor, device;       /* PCI_ANY16 = coringa */
    uint8_t  class_code, subclass; /* PCI_ANY8  = coringa */
    int (*probe)(pci_dev_t *dev);  /* 0 = assumiu o dispositivo */
} pci_driver_t;

extern pci_driver_t *const pci_drivers[];   /* terminado em 0 (drivers.c) */

uint32_t pci_read32(uint8_t b, uint8_t s, uint8_t f, uint8_t off);
void     pci_write32(uint8_t b, uint8_t s, uint8_t f, uint8_t off, uint32_t v);
void     pci_init(void);
int      pci_count(void);
pci_dev_t *pci_get(int i);
void     pci_enable_busmaster(pci_dev_t *d);
const char *pci_class_name(uint8_t c);
#endif
