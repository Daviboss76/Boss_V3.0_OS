#ifndef BLK_H
#define BLK_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { char nome[8]; char modelo[41]; uint32_t setores; } blk_info_t;

void blk_init(void);
void blk_rescan(void);
const char *blk_backend(void);
uint32_t blk_erros(void);

int  blk_count(void);
const blk_info_t *blk_info(int i);
int  blk_read(int i, uint32_t lba, uint32_t n, void *buf);
int  blk_write(int i, uint32_t lba, uint32_t n, const void *buf);
bool blk_vol_info(int i, int *disco, uint32_t *ini, uint32_t *tam);
void blk_prefer(int disco, uint32_t ini);

int  disk_count(void);
const blk_info_t *disk_info(int d);
bool disk_is_usb(int d);
int  disk_read(int d, uint32_t lba, uint32_t n, void *buf);
int  disk_write(int d, uint32_t lba, uint32_t n, const void *buf);
void disk_arm(int d, uint32_t lba, uint32_t n);
void disk_disarm(void);
#endif
