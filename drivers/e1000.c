#include "e1000.h"

/* Registradores MMIO do Intel 82540EM */
#define E1000_CTRL     0x0000u
#define E1000_STATUS   0x0008u
#define E1000_EERD     0x0014u
#define E1000_ICR      0x00C0u
#define E1000_IMS      0x00D0u
#define E1000_IMC      0x00D8u
#define E1000_RCTL     0x0100u
#define E1000_TCTL     0x0400u
#define E1000_TIPG     0x0410u
#define E1000_RDBAL    0x2800u
#define E1000_RDBAH    0x2804u
#define E1000_RDLEN    0x2808u
#define E1000_RDH      0x2810u
#define E1000_RDT      0x2818u
#define E1000_TDBAL    0x3800u
#define E1000_TDBAH    0x3804u
#define E1000_TDLEN    0x3808u
#define E1000_TDH      0x3810u
#define E1000_TDT      0x3818u
#define E1000_MTA      0x5200u
#define E1000_RA       0x5400u

/* Bits de controle e status */
#define E1000_CTRL_SLU      (1u << 6)   /* Set Link Up */
#define E1000_CTRL_RST      (1u << 26)  /* Device Reset */

#define E1000_STATUS_LU     (1u << 1)   /* Link Up */

#define E1000_RCTL_EN       (1u << 1)   /* Receiver Enable */
#define E1000_RCTL_UPE      (1u << 3)   /* Unicast Promiscuous Enable */
#define E1000_RCTL_MPE      (1u << 4)   /* Multicast Promiscuous Enable */
#define E1000_RCTL_BAM      (1u << 15)  /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2K (0u << 16)  /* Buffer Size = 2048 bytes */
#define E1000_RCTL_SECRC    (1u << 26)  /* Strip Ethernet CRC */

#define E1000_TCTL_EN       (1u << 1)   /* Transmit Enable */
#define E1000_TCTL_PSP      (1u << 3)   /* Pad Short Packets */
#define E1000_TCTL_CT_SHIFT 4u          /* Collision Threshold */
#define E1000_TCTL_COLD_SHF 12u         /* Collision Distance */

#define E1000_TX_CMD_EOP    (1u << 0)   /* End of Packet */
#define E1000_TX_CMD_IFCS   (1u << 1)   /* Insert FCS/CRC */
#define E1000_TX_CMD_RS     (1u << 3)   /* Report Status */

#define E1000_TX_STA_DD     (1u << 0)   /* Descriptor Done */
#define E1000_RX_STA_DD     (1u << 0)   /* Descriptor Done */
#define E1000_RX_STA_EOP    (1u << 1)   /* End of Packet */

#define E1000_NUM_DESCS     32u
#define E1000_PKT_BUF_SIZE  2048u
#define E1000_DESC_RING_RES 4096u       /* Reserva inicial para os descritores no buffer */

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

static volatile uint8_t *mmio = 0;
static e1000_rx_desc_t *rx_ring = 0;
static e1000_tx_desc_t *tx_ring = 0;
static uint32_t rx_buf_base = 0;
static uint32_t tx_buf_base = 0;
static uint32_t rx_idx = 0;
static uint32_t tx_idx = 0;

static uint32_t stat_tx_pkts = 0;
static uint32_t stat_rx_pkts = 0;
static uint32_t stat_tx_errs = 0;
static uint32_t stat_rx_errs = 0;

static bool pronto = false;
static uint8_t mac_addr[6] = {0};

static inline uint32_t mmio_read(uint16_t reg) {
    return *(volatile uint32_t *)(mmio + reg);
}

static inline void mmio_write(uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(mmio + reg) = val;
}

static void atraso_curto(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++) {
        __asm__ volatile("nop");
    }
}

static uint16_t eeprom_read16(uint8_t addr) {
    uint32_t val = 0;
    mmio_write(E1000_EERD, ((uint32_t)addr << 8) | 1u);
    for (uint32_t i = 0; i < 100000u; i++) {
        val = mmio_read(E1000_EERD);
        if (val & (1u << 4)) {
            return (uint16_t)((val >> 16) & 0xFFFFu);
        }
    }
    return 0;
}

static void carregar_mac(void) {
    uint32_t ral = mmio_read(E1000_RA);
    uint32_t rah = mmio_read(E1000_RA + 4u);

    mac_addr[0] = (uint8_t)(ral & 0xFFu);
    mac_addr[1] = (uint8_t)((ral >> 8) & 0xFFu);
    mac_addr[2] = (uint8_t)((ral >> 16) & 0xFFu);
    mac_addr[3] = (uint8_t)((ral >> 24) & 0xFFu);
    mac_addr[4] = (uint8_t)(rah & 0xFFu);
    mac_addr[5] = (uint8_t)((rah >> 8) & 0xFFu);

    /* Se o RAL/RAH vier zerado, tenta ler da EEPROM */
    bool zerado = true;
    for (int i = 0; i < 6; i++) {
        if (mac_addr[i] != 0) {
            zerado = false;
            break;
        }
    }
    if (zerado) {
        uint16_t w0 = eeprom_read16(0);
        uint16_t w1 = eeprom_read16(1);
        uint16_t w2 = eeprom_read16(2);
        mac_addr[0] = (uint8_t)(w0 & 0xFFu);
        mac_addr[1] = (uint8_t)((w0 >> 8) & 0xFFu);
        mac_addr[2] = (uint8_t)(w1 & 0xFFu);
        mac_addr[3] = (uint8_t)((w1 >> 8) & 0xFFu);
        mac_addr[4] = (uint8_t)(w2 & 0xFFu);
        mac_addr[5] = (uint8_t)((w2 >> 8) & 0xFFu);

        ral = ((uint32_t)mac_addr[0]) |
              ((uint32_t)mac_addr[1] << 8) |
              ((uint32_t)mac_addr[2] << 16) |
              ((uint32_t)mac_addr[3] << 24);
        rah = ((uint32_t)mac_addr[4]) |
              ((uint32_t)mac_addr[5] << 8) |
              (1u << 31); /* AV: Address Valid */
        mmio_write(E1000_RA, ral);
        mmio_write(E1000_RA + 4u, rah);
    }
}

static void inicializar_rx(uint32_t rx_phys) {
    rx_ring = (e1000_rx_desc_t *)(uintptr_t)rx_phys;
    rx_buf_base = rx_phys + E1000_DESC_RING_RES;

    for (uint32_t i = 0; i < E1000_NUM_DESCS; i++) {
        rx_ring[i].addr = (uint64_t)(rx_buf_base + i * E1000_PKT_BUF_SIZE);
        rx_ring[i].length = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status = 0;
        rx_ring[i].errors = 0;
        rx_ring[i].special = 0;
    }

    rx_idx = 0;
    mmio_write(E1000_RDBAL, rx_phys);
    mmio_write(E1000_RDBAH, 0);
    mmio_write(E1000_RDLEN, E1000_NUM_DESCS * (uint32_t)sizeof(e1000_rx_desc_t));
    mmio_write(E1000_RDH, 0);
    mmio_write(E1000_RDT, E1000_NUM_DESCS - 1u);

    uint32_t rctl = E1000_RCTL_EN |
                    E1000_RCTL_BAM |
                    E1000_RCTL_BSIZE_2K |
                    E1000_RCTL_SECRC;
    mmio_write(E1000_RCTL, rctl);
}

static void inicializar_tx(uint32_t tx_phys) {
    tx_ring = (e1000_tx_desc_t *)(uintptr_t)tx_phys;
    tx_buf_base = tx_phys + E1000_DESC_RING_RES;

    for (uint32_t i = 0; i < E1000_NUM_DESCS; i++) {
        tx_ring[i].addr = (uint64_t)(tx_buf_base + i * E1000_PKT_BUF_SIZE);
        tx_ring[i].length = 0;
        tx_ring[i].cso = 0;
        tx_ring[i].cmd = 0;
        tx_ring[i].status = E1000_TX_STA_DD;
        tx_ring[i].css = 0;
        tx_ring[i].special = 0;
    }

    tx_idx = 0;
    mmio_write(E1000_TDBAL, tx_phys);
    mmio_write(E1000_TDBAH, 0);
    mmio_write(E1000_TDLEN, E1000_NUM_DESCS * (uint32_t)sizeof(e1000_tx_desc_t));
    mmio_write(E1000_TDH, 0);
    mmio_write(E1000_TDT, 0);

    /* IPG padrão para 1000BASE-T / Ethernet */
    mmio_write(E1000_TIPG, 0x0060200Au);

    uint32_t tctl = E1000_TCTL_EN |
                    E1000_TCTL_PSP |
                    (15u << E1000_TCTL_CT_SHIFT) |
                    (64u << E1000_TCTL_COLD_SHF);
    mmio_write(E1000_TCTL, tctl);
}

int e1000_init(uint32_t mmio_phys, uint32_t rx_phys, uint32_t tx_phys) {
    pronto = false;
    stat_tx_pkts = 0;
    stat_rx_pkts = 0;
    stat_tx_errs = 0;
    stat_rx_errs = 0;

    if (!mmio_phys || !rx_phys || !tx_phys) {
        return -1;
    }

    mmio = (volatile uint8_t *)(uintptr_t)mmio_phys;
    if (mmio_read(E1000_STATUS) == 0xFFFFFFFFu) {
        mmio = 0;
        return -1;
    }

    /* Desativa interrupções enquanto configura */
    mmio_write(E1000_IMC, 0xFFFFFFFFu);
    (void)mmio_read(E1000_ICR);

    /* Reset global do controlador */
    uint32_t ctrl = mmio_read(E1000_CTRL);
    mmio_write(E1000_CTRL, ctrl | E1000_CTRL_RST);
    atraso_curto(200000u);

    for (uint32_t i = 0; i < 100000u; i++) {
        if (!(mmio_read(E1000_CTRL) & E1000_CTRL_RST)) {
            break;
        }
        atraso_curto(50u);
    }

    mmio_write(E1000_IMC, 0xFFFFFFFFu);
    (void)mmio_read(E1000_ICR);

    /* Sobe o link */
    ctrl = mmio_read(E1000_CTRL);
    ctrl |= E1000_CTRL_SLU;
    mmio_write(E1000_CTRL, ctrl);

    /* Limpa tabela multicast */
    for (uint32_t i = 0; i < 128u; i++) {
        mmio_write((uint16_t)(E1000_MTA + i * 4u), 0);
    }

    carregar_mac();
    inicializar_rx(rx_phys);
    inicializar_tx(tx_phys);

    pronto = true;
    return 0;
}

bool e1000_ativo(void) {
    return pronto;
}

bool e1000_link_up(void) {
    if (!pronto || !mmio) return false;
    return (mmio_read(E1000_STATUS) & E1000_STATUS_LU) != 0;
}

void e1000_get_mac(uint8_t mac[6]) {
    if (!mac) return;
    for (int i = 0; i < 6; i++) {
        mac[i] = mac_addr[i];
    }
}

int e1000_send(const void *frame, uint16_t len) {
    if (!pronto || !tx_ring || !frame || len == 0 || len > E1000_MAX_FRAME) {
        stat_tx_errs++;
        return -1;
    }

    e1000_tx_desc_t *desc = &tx_ring[tx_idx];

    /* Espera o descritor atual terminar se ainda estiver ocupado */
    uint32_t spin = 0;
    while (!(desc->status & E1000_TX_STA_DD)) {
        spin++;
        if (spin > 500000u) {
            stat_tx_errs++;
            return -1;
        }
        atraso_curto(10u);
    }

    uint32_t pkt_addr = tx_buf_base + tx_idx * E1000_PKT_BUF_SIZE;
    uint8_t *dst = (uint8_t *)(uintptr_t)pkt_addr;
    const uint8_t *src = (const uint8_t *)frame;

    for (uint16_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }

    desc->addr = (uint64_t)pkt_addr;
    desc->length = len;
    desc->cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    desc->status = 0;

    tx_idx = (tx_idx + 1u) % E1000_NUM_DESCS;
    mmio_write(E1000_TDT, tx_idx);

    stat_tx_pkts++;
    return 0;
}

int e1000_poll_rx(void *buf_out, uint16_t max_len) {
    if (!pronto || !rx_ring || !buf_out || max_len == 0) {
        return -1;
    }

    e1000_rx_desc_t *desc = &rx_ring[rx_idx];
    if (!(desc->status & E1000_RX_STA_DD)) {
        return 0;
    }

    if (desc->errors != 0) {
        stat_rx_errs++;
        desc->status = 0;
        desc->errors = 0;
        uint32_t done_idx = rx_idx;
        rx_idx = (rx_idx + 1u) % E1000_NUM_DESCS;
        mmio_write(E1000_RDT, done_idx);
        return -1;
    }

    uint16_t pkt_len = desc->length;
    if (pkt_len > E1000_PKT_BUF_SIZE) {
        pkt_len = E1000_PKT_BUF_SIZE;
    }

    uint16_t copy_len = (pkt_len > max_len) ? max_len : pkt_len;
    const uint8_t *src = (const uint8_t *)(uintptr_t)(uint32_t)desc->addr;
    uint8_t *dst = (uint8_t *)buf_out;

    for (uint16_t i = 0; i < copy_len; i++) {
        dst[i] = src[i];
    }

    desc->status = 0;
    desc->errors = 0;
    desc->length = 0;

    uint32_t done_idx = rx_idx;
    rx_idx = (rx_idx + 1u) % E1000_NUM_DESCS;
    mmio_write(E1000_RDT, done_idx);

    stat_rx_pkts++;
    return (int)copy_len;
}

void e1000_stats(uint32_t *tx_pkts, uint32_t *rx_pkts, uint32_t *tx_errs, uint32_t *rx_errs) {
    if (tx_pkts) *tx_pkts = stat_tx_pkts;
    if (rx_pkts) *rx_pkts = stat_rx_pkts;
    if (tx_errs) *tx_errs = stat_tx_errs;
    if (rx_errs) *rx_errs = stat_rx_errs;
}

