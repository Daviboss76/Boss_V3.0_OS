#include "net.h"
#include "e1000.h"
#include "drv.h"
#include "heal.h"
#include "kernel.h"

typedef struct {
    uint32_t op;
    uint32_t a0;
    uint32_t a1;
    uint32_t len;
    uint8_t  buf[256];
} net_mbox_t;

static net_if_t ifaces[NET_MAX_IFACES];
static int n_ifaces = 0;
static int ativa_idx = -1;

static const char *candidatos[] = {
    "intel_e1000",
};

static bool str_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a++ != *b++) return false;
    }
    return *a == *b;
}

static void mem_zero(void *dst, uint32_t len) {
    uint8_t *p = (uint8_t *)dst;
    for (uint32_t i = 0; i < len; i++) p[i] = 0;
}

static void mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int net_rpc(const char *drv_nome, uint32_t op,
                   uint32_t a0, uint32_t a1,
                   const void *in_buf, uint32_t in_len,
                   void *out_buf, uint32_t out_cap, uint32_t *out_len,
                   int32_t *ret_out) {
    net_mbox_t mb;
    mem_zero(&mb, sizeof(mb));
    mb.op = op;
    mb.a0 = a0;
    mb.a1 = a1;

    if (in_buf && in_len > 0) {
        if (in_len > sizeof(mb.buf)) in_len = (uint32_t)sizeof(mb.buf);
        mem_copy(mb.buf, in_buf, in_len);
        mb.len = in_len;
    }

    int rc = drv_call(drv_nome, op, a0, a1, &mb, (uint32_t)sizeof(mb), ret_out);
    if (rc != DRV_OK) return rc;

    if (out_buf && out_cap > 0 && mb.len > 0) {
        uint32_t n = (mb.len > out_cap) ? out_cap : mb.len;
        mem_copy(out_buf, mb.buf, n);
        if (out_len) *out_len = n;
    } else if (out_len) {
        *out_len = 0;
    }

    return DRV_OK;
}

static int registrar_driver(const char *nome, uint32_t mmio_phys,
                            uint32_t rx_phys, uint32_t tx_phys) {
    if (n_ifaces >= NET_MAX_IFACES) return -1;

    if (str_eq(nome, "intel_e1000") && e1000_ativo()) {
        net_if_t *nif = &ifaces[n_ifaces];
        mem_zero(nif, sizeof(*nif));
        nif->drv_nome = nome;
        nif->ativa = true;
        e1000_get_mac(nif->mac);

        int idx = n_ifaces++;
        if (ativa_idx < 0) ativa_idx = idx;
        heal_log("net: interface [%d] registrada com driver %s (ring 0)", idx, nome);
        return idx;
    }

    int32_t ret = -1;
    int rc = net_rpc(nome, NET_OP_INIT, mmio_phys, rx_phys,
                     &tx_phys, sizeof(tx_phys),
                     0, 0, 0, &ret);
    if (rc != DRV_OK || ret != 0) {
        return -1;
    }

    net_if_t *nif = &ifaces[n_ifaces];
    mem_zero(nif, sizeof(*nif));
    nif->drv_nome = nome;
    nif->ativa = true;

    uint8_t mac_tmp[6] = {0};
    uint32_t mac_len = 0;
    int32_t mac_ret = -1;
    if (net_rpc(nome, NET_OP_GET_MAC, 0, 0, 0, 0,
                mac_tmp, sizeof(mac_tmp), &mac_len, &mac_ret) == DRV_OK &&
        mac_ret == 0 && mac_len >= 6) {
        mem_copy(nif->mac, mac_tmp, 6);
    }

    int idx = n_ifaces++;
    if (ativa_idx < 0) ativa_idx = idx;
    heal_log("net: interface [%d] registrada com driver %s", idx, nome);
    return idx;
}

int net_init(uint32_t mmio_phys, uint32_t rx_phys, uint32_t tx_phys) {
    n_ifaces = 0;
    ativa_idx = -1;

    int n_cand = (int)(sizeof(candidatos) / sizeof(candidatos[0]));
    for (int i = 0; i < n_cand; i++) {
        registrar_driver(candidatos[i], mmio_phys, rx_phys, tx_phys);
    }

    if (ativa_idx < 0) {
        heal_log("net: nenhum driver de rede inicializado");
        return -1;
    }
    return 0;
}

bool net_tem_rede(void) {
    return (ativa_idx >= 0 && ativa_idx < n_ifaces && ifaces[ativa_idx].ativa);
}

const net_if_t *net_iface_ativa(void) {
    if (!net_tem_rede()) return 0;
    return &ifaces[ativa_idx];
}

int net_selecionar_driver(const char *drv_nome) {
    for (int i = 0; i < n_ifaces; i++) {
        if (str_eq(ifaces[i].drv_nome, drv_nome) && ifaces[i].ativa) {
            ativa_idx = i;
            heal_log("net: rota padrao trocada para driver %s", drv_nome);
            return 0;
        }
    }
    return -1;
}

int net_get_mac(uint8_t mac_out[6]) {
    if (!net_tem_rede() || !mac_out) return -1;
    mem_copy(mac_out, ifaces[ativa_idx].mac, 6);
    return 0;
}

int net_send_frame(const void *frame, uint16_t len) {
    if (!net_tem_rede() || !frame || len == 0 || len > NET_MAX_FRAME) return -1;

    net_if_t *nif = &ifaces[ativa_idx];
    if (str_eq(nif->drv_nome, "intel_e1000") && e1000_ativo()) {
        if (e1000_send(frame, len) != 0) {
            nif->erros++;
            return -1;
        }
        nif->tx_ok++;
        return 0;
    }

    int32_t ret = -1;
    int rc = net_rpc(nif->drv_nome, NET_OP_SEND, len, 0,
                     frame, len, 0, 0, 0, &ret);
    if (rc != DRV_OK || ret != 0) {
        nif->erros++;
        return -1;
    }
    nif->tx_ok++;
    return 0;
}

int net_poll_rx(void *buf_out, uint16_t max_len) {
    if (!net_tem_rede() || !buf_out || max_len == 0) return -1;

    net_if_t *nif = &ifaces[ativa_idx];
    if (str_eq(nif->drv_nome, "intel_e1000") && e1000_ativo()) {
        int n = e1000_poll_rx(buf_out, max_len);
        if (n < 0) {
            nif->erros++;
            return -1;
        }
        if (n == 0) return 0;
        nif->rx_ok++;
        return n;
    }

    uint16_t pedir = (max_len > sizeof(((net_mbox_t *)0)->buf))
                   ? (uint16_t)sizeof(((net_mbox_t *)0)->buf)
                   : max_len;

    uint32_t rx_len = 0;
    int32_t ret = -1;
    int rc = net_rpc(nif->drv_nome, NET_OP_POLL_RX, pedir, 0,
                     0, 0, buf_out, max_len, &rx_len, &ret);
    if (rc != DRV_OK) {
        nif->erros++;
        return -1;
    }
    if (ret <= 0) return 0;

    nif->rx_ok++;
    return (int)rx_len;
}

