#include "pci.h"
extern pci_driver_t drv_rtl8139, drv_ide, drv_ahci;
pci_driver_t *const pci_drivers[] = { &drv_rtl8139, &drv_ide, &drv_ahci, 0 };
