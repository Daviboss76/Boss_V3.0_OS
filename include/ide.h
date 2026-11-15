#ifndef IDE_H
#define IDE_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool present, atapi, lba48;
    uint32_t sectors;
    char model[41];
} ide_info_t;

const ide_info_t *ide_info(int drive);                 /* 0..3 */
int ide_read (int drive, uint32_t lba, uint32_t n, void *buf);        /* 0 = ok */
int ide_write(int drive, uint32_t lba, uint32_t n, const void *buf);
#endif
