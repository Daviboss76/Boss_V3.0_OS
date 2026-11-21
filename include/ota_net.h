#ifndef BOSS_OTA_NET_H
#define BOSS_OTA_NET_H

#include <stdint.h>
#include <stdbool.h>
#include "ota.h"

/* Funcao que envia bytes crus pela sua pilha TCP/UDP/rede e retorna qtd enviada ou < 0 */
typedef int (*ota_tx_fn)(const void *data, uint32_t len);

/* Funcao que le bytes crus disponiveis e retorna qtd lida (0 = sem dados agora, < 0 = erro) */
typedef int (*ota_rx_fn)(void *buf, uint32_t max_len);

typedef struct {
    const char *host_header;   /* Ex.: "10.0.2.2:8080" ou "192.168.1.10:8080" */
    const char *path;          /* Ex.: "/KERNEL.BIN" */
    ota_tx_fn   tx;
    ota_rx_fn   rx;
    uint8_t    *download_buf;  /* Buffer reservado para montar o KERNEL.BIN */
    uint32_t    buf_cap;       /* Capacidade maxima do buffer (ex.: OTA_MAX_KERNEL_SIZE) */
    uint32_t    max_poll_iter; /* Limite de iteracoes de leitura para nao travar */
} ota_http_cfg_t;

ota_status_t ota_baixar_e_aplicar(const ota_http_cfg_t *cfg, bool reiniciar_ao_final);

#endif

