#ifndef BOSS_NET_H
#define BOSS_NET_H

#include <stdint.h>
#include <stdbool.h>

#define NET_MAX_IFACES 4
#define NET_MAX_FRAME  1518

enum {
    NET_OP_INIT    = 1,
    NET_OP_SEND    = 2,
    NET_OP_GET_MAC = 3,
    NET_OP_POLL_RX = 4
};

typedef struct {
    const char *drv_nome;
    uint8_t mac[6];
    bool ativa;
    uint32_t tx_ok;
    uint32_t rx_ok;
    uint32_t erros;
} net_if_t;

int  net_init(uint32_t mmio_phys, uint32_t rx_phys, uint32_t tx_phys);
bool net_tem_rede(void);
const net_if_t *net_iface_ativa(void);
int  net_selecionar_driver(const char *drv_nome);
int  net_get_mac(uint8_t mac_out[6]);
int  net_send_frame(const void *frame, uint16_t len);
int  net_poll_rx(void *buf_out, uint16_t max_len);

#endif

