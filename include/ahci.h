#ifndef AHCI_H
#define AHCI_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool present, atapi;
    uint32_t sectors;
    char model[41];
} ahci_info_t;

const ahci_info_t *ahci_info(int porta);               /* 0..3 */
int ahci_read(int porta, uint32_t lba, uint32_t n, void *buf);
int ahci_write(int porta, uint32_t lba, uint32_t n, const void *buf);
#endif
