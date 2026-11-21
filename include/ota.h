#ifndef BOSS_OTA_H
#define BOSS_OTA_H

#include <stdint.h>
#include <stdbool.h>

#define OTA_KERNEL_PATH     "KERNEL.BIN"
#define OTA_TEMP_PATH       "KERNEL.TMP"
#define OTA_MAX_KERNEL_SIZE (4u * 1024u * 1024u)

typedef enum {
    OTA_OK = 0,
    OTA_ERR_REDE = -1,
    OTA_ERR_TAMANHO = -2,
    OTA_ERR_CRC = -3,
    OTA_ERR_FS_WRITE = -4,
    OTA_ERR_FS_RM = -5,
    OTA_ERR_FS_FLUSH = -6
} ota_status_t;

typedef struct {
    int  (*fs_write_file)(const char *path, const void *data, uint32_t size);
    int  (*fs_remove_file)(const char *path);
    int  (*fs_exists)(const char *path);
    int  (*fs_flush)(void);
    void (*hw_reboot)(void);
} ota_fs_ops_t;

void         ota_init(const ota_fs_ops_t *ops);
void         ota_bind_fat32(void);
uint32_t     ota_crc32(const uint8_t *data, uint32_t len);
ota_status_t ota_aplicar_imagem(const uint8_t *img, uint32_t size,
                                uint32_t expected_crc32, bool reiniciar_ao_final);
void         ota_reiniciar(void);

#endif

