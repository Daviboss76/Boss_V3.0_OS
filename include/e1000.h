#ifndef BOSS_E1000_H
#define BOSS_E1000_H

#include <stdint.h>
#include <stdbool.h>

#define E1000_MAX_FRAME 1518u

int  e1000_init(uint32_t mmio_phys, uint32_t rx_phys, uint32_t tx_phys);
bool e1000_ativo(void);
bool e1000_link_up(void);
void e1000_get_mac(uint8_t mac[6]);
int  e1000_send(const void *frame, uint16_t len);
int  e1000_poll_rx(void *buf_out, uint16_t max_len);
void e1000_stats(uint32_t *tx_pkts, uint32_t *rx_pkts, uint32_t *tx_errs, uint32_t *rx_errs);

#endif

