#include "ota.h"
#include "fs.h"
#include "heal.h"
#include "kernel.h"

static ota_fs_ops_t g_ops;

static void default_reboot(void) {
    outb(0x64, 0xFE);
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

static int fat32_write_cb(const char *path, const void *data, uint32_t size) {
    return fs_write(path, data, size);
}

static int fat32_remove_cb(const char *path) {
    return fs_remove(path);
}

static int fat32_exists_cb(const char *path) {
    bool is_dir = false;
    uint32_t tam = 0;
    return (fs_stat(path, &is_dir, &tam) == 0) ? 1 : 0;
}

static int fat32_flush_cb(void) {
    return 0;
}

void ota_init(const ota_fs_ops_t *ops) {
    memset(&g_ops, 0, sizeof(g_ops));
    if (ops) {
        g_ops = *ops;
    }
    if (!g_ops.fs_write_file)  g_ops.fs_write_file  = fat32_write_cb;
    if (!g_ops.fs_remove_file) g_ops.fs_remove_file = fat32_remove_cb;
    if (!g_ops.fs_exists)      g_ops.fs_exists      = fat32_exists_cb;
    if (!g_ops.fs_flush)       g_ops.fs_flush       = fat32_flush_cb;
    if (!g_ops.hw_reboot)      g_ops.hw_reboot      = default_reboot;
}

void ota_bind_fat32(void) {
    ota_fs_ops_t ops = {
        .fs_write_file  = fat32_write_cb,
        .fs_remove_file = fat32_remove_cb,
        .fs_exists      = fat32_exists_cb,
        .fs_flush       = fat32_flush_cb,
        .hw_reboot      = default_reboot
    };
    ota_init(&ops);
}

uint32_t ota_crc32(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

ota_status_t ota_aplicar_imagem(const uint8_t *img, uint32_t size,
                                uint32_t expected_crc32, bool reiniciar_ao_final) {
    if (!img || size == 0 || size > OTA_MAX_KERNEL_SIZE) {
        heal_log("ota: tamanho de imagem invalido (%u bytes)", size);
        return OTA_ERR_TAMANHO;
    }

    uint32_t calc_crc = ota_crc32(img, size);
    if (expected_crc32 != 0 && calc_crc != expected_crc32) {
        heal_log("ota: falha de CRC32 (esperado=%x, calculado=%x)", expected_crc32, calc_crc);
        return OTA_ERR_CRC;
    }

    heal_log("ota: gravando imagem temporaria %s (%u bytes, crc=%x)",
             OTA_TEMP_PATH, size, calc_crc);

    if (g_ops.fs_write_file(OTA_TEMP_PATH, img, size) != 0) {
        heal_log("ota: erro ao escrever %s", OTA_TEMP_PATH);
        return OTA_ERR_FS_WRITE;
    }

    if (g_ops.fs_exists && g_ops.fs_exists(OTA_KERNEL_PATH)) {
        if (g_ops.fs_remove_file(OTA_KERNEL_PATH) != 0) {
            heal_log("ota: erro ao remover %s antigo", OTA_KERNEL_PATH);
            return OTA_ERR_FS_RM;
        }
    }

    if (g_ops.fs_write_file(OTA_KERNEL_PATH, img, size) != 0) {
        heal_log("ota: erro critico ao gravar novo %s", OTA_KERNEL_PATH);
        return OTA_ERR_FS_WRITE;
    }

    if (g_ops.fs_exists && g_ops.fs_exists(OTA_TEMP_PATH)) {
        g_ops.fs_remove_file(OTA_TEMP_PATH);
    }

    if (g_ops.fs_flush) {
        if (g_ops.fs_flush() != 0) {
            heal_log("ota: falha no flush do sistema de arquivos");
            return OTA_ERR_FS_FLUSH;
        }
    }

    heal_log("ota: atualizacao concluida com sucesso");
    if (reiniciar_ao_final) {
        ota_reiniciar();
    }
    return OTA_OK;
}

void ota_reiniciar(void) {
    heal_log("ota: reiniciando sistema para carregar novo kernel...");
    if (g_ops.fs_flush) {
        g_ops.fs_flush();
    }
    g_ops.hw_reboot();
}

