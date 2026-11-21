#include "ulib.h"

#define E1000_CTRL    0x0000
#define E1000_STATUS  0x0008
#define E1000_IMS     0x00D0
#define E1000_RCTL    0x0100
#define E1000_TCTL    0x0400
#define E1000_RDBAL   0x2800
#define E1000_RDBAH   0x2804
#define E1000_RDLEN   0x2808
#define E1000_RDH     0x2810
#define E1000_RDT     0x2818
#define E1000_TDBAL   0x3800
#define E1000_TDBAH   0x3804
#define E1000_TDLEN   0x3808
#define E1000_TDH     0x3810
#define E1000_TDT     0x3818
#define E1000_RA      0x5400

#define E1000_TX_CMD_EOP  (1u << 0)
#define E1000_TX_CMD_IFCS (1u << 1)
#define E1000_TX_CMD_RS   (1u << 3)
#define E1000_TX_STA_DD   (1u << 0)
#define E1000_RX_STA_DD   (1u << 0)

#define NUM_DESCS 32

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

static volatile uint8_t *e1000_mmio = 0;
static struct e1000_rx_desc *rx_descs = 0;
static struct e1000_tx_desc *tx_descs = 0;
static uint32_t tx_pkt_base = 0;
static uint32_t rx_cur = 0;

static uint32_t e1000_read(uint16_t reg) {
    return *(volatile uint32_t *)(e1000_mmio + reg);
}

static void e1000_write(uint16_t reg, uint32_t data) {
    *(volatile uint32_t *)(e1000_mmio + reg) = data;
}

static void e1000_get_mac(uint8_t *mac) {
    uint32_t low  = e1000_read(E1000_RA);
    uint32_t high = e1000_read(E1000_RA + 4);
    mac[0] = (uint8_t)(low & 0xFF);
    mac[1] = (uint8_t)((low >> 8) & 0xFF);
    mac[2] = (uint8_t)((low >> 16) & 0xFF);
    mac[3] = (uint8_t)((low >> 24) & 0xFF);
    mac[4] = (uint8_t)(high & 0xFF);
    mac[5] = (uint8_t)((high >> 8) & 0xFF);
}

static int e1000_init(uint32_t mmio_addr, uint32_t rx_buf_addr, uint32_t tx_buf_addr) {
    if (!mmio_addr || !rx_buf_addr || !tx_buf_addr) return -1;

    e1000_mmio = (volatile uint8_t *)mmio_addr;
    if (e1000_read(E1000_STATUS) == 0xFFFFFFFF) return -1;

    uint32_t ctrl = e1000_read(E1000_CTRL);
    e1000_write(E1000_CTRL, ctrl | (1u << 26));
    for (volatile int i = 0; i < 100000; i++) {}

    ctrl = e1000_read(E1000_CTRL);
    ctrl |= (1u << 6);
    ctrl &= ~(1u << 3);
    e1000_write(E1000_CTRL, ctrl);

    for (int i = 0; i < 128; i++) {
        e1000_write((uint16_t)(0x5200 + i * 4), 0);
    }

    rx_descs = (struct e1000_rx_desc *)rx_buf_addr;
    for (int i = 0; i < NUM_DESCS; i++) {
        rx_descs[i].addr = (uint64_t)(rx_buf_addr + 4096u + (uint32_t)i * 2048u);
        rx_descs[i].status = 0;
        rx_descs[i].errors = 0;
    }
    rx_cur = 0;

    e1000_write(E1000_RDBAL, rx_buf_addr);
    e1000_write(E1000_RDBAH, 0);
    e1000_write(E1000_RDLEN, NUM_DESCS * (uint32_t)sizeof(struct e1000_rx_desc));
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, NUM_DESCS - 1);

    uint32_t rctl = e1000_read(E1000_RCTL);
    rctl |= (1u << 1);
    rctl |= (1u << 15);
    rctl |= (2u << 16);
    e1000_write(E1000_RCTL, rctl);

    tx_descs = (struct e1000_tx_desc *)tx_buf_addr;
    tx_pkt_base = tx_buf_addr + 4096u;
    for (int i = 0; i < NUM_DESCS; i++) {
        tx_descs[i].addr = 0;
        tx_descs[i].length = 0;
        tx_descs[i].cmd = 0;
        tx_descs[i].status = E1000_TX_STA_DD;
    }

    e1000_write(E1000_TDBAL, tx_buf_addr);
    e1000_write(E1000_TDBAH, 0);
    e1000_write(E1000_TDLEN, NUM_DESCS * (uint32_t)sizeof(struct e1000_tx_desc));
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);

    uint32_t tctl = e1000_read(E1000_TCTL);
    tctl |= (1u << 1);
    tctl |= (1u << 3);
    tctl |= (0x10u << 4);
    tctl |= (0x40u << 12);
    e1000_write(E1000_TCTL, tctl);

    e1000_write(E1000_IMS, 0);
    return 0;
}

static int e1000_send_bytes(const uint8_t *frame, uint16_t length) {
    if (!tx_descs || !tx_pkt_base || !frame || length == 0 || length > 1518) return -1;

    uint32_t tdt = e1000_read(E1000_TDT);
    uint32_t slot = tdt % NUM_DESCS;
    uint32_t pkt_addr = tx_pkt_base + slot * 2048u;

    uint8_t *dst = (uint8_t *)pkt_addr;
    for (uint16_t i = 0; i < length; i++) dst[i] = frame[i];

    tx_descs[slot].addr = (uint64_t)pkt_addr;
    tx_descs[slot].length = length;
    tx_descs[slot].cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    tx_descs[slot].status = 0;

    uint32_t next_tdt = (tdt + 1) % NUM_DESCS;
    e1000_write(E1000_TDT, next_tdt);
    return 0;
}

static int e1000_poll_rx_copy(uint8_t *out, uint16_t max_len) {
    if (!rx_descs || !out || max_len == 0) return 0;

    if (!(rx_descs[rx_cur].status & E1000_RX_STA_DD)) {
        return 0;
    }

    uint16_t pkt_len = rx_descs[rx_cur].length;
    const uint8_t *src = (const uint8_t *)(uint32_t)rx_descs[rx_cur].addr;
    uint16_t copy_len = (pkt_len > max_len) ? max_len : pkt_len;

    for (uint16_t i = 0; i < copy_len; i++) out[i] = src[i];

    rx_descs[rx_cur].status = 0;
    uint32_t prev = rx_cur;
    rx_cur = (rx_cur + 1) % NUM_DESCS;
    e1000_write(E1000_RDT, prev);
    return (int)copy_len;
}

static void drv(volatile mbox_t *m) {
    switch (m->op) {
    case 1: {
        uint32_t tx_phys = (m->len >= sizeof(uint32_t))
                         ? *(const uint32_t *)m->buf
                         : m->arg[2];
        m->ret = e1000_init(m->arg[0], m->arg[1], tx_phys);
        break;
    }
    case 2: {
        uint16_t len = (uint16_t)m->arg[0];
        if (len > m->len) len = (uint16_t)m->len;
        m->ret = e1000_send_bytes((const uint8_t *)m->buf, len);
        break;
    }
    case 3: {
        uint8_t mac[6];
        e1000_get_mac(mac);
        for (int i = 0; i < 6; i++) m->buf[i] = mac[i];
        m->len = 6;
        m->ret = 0;
        break;
    }
    case 4: {
        uint16_t pedir = (uint16_t)m->arg[0];
        if (pedir > sizeof(m->buf)) pedir = (uint16_t)sizeof(m->buf);
        int n = e1000_poll_rx_copy((uint8_t *)m->buf, pedir);
        m->len = (n > 0) ? (uint32_t)n : 0;
        m->ret = n;
        break;
    }
    default:
        m->ret = -1;
        break;
    }
}

DRIVER(drv)

