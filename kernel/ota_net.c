#include "ota_net.h"
#include "e1000.h"
#include "heal.h"
#include "kernel.h"

static int str_len_s(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static int str_eq_ci_n(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return 1;
}

static uint32_t parse_u32_dec(const char *p, const char *end) {
    uint32_t v = 0;
    while (p < end && *p == ' ') p++;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
    }
    return v;
}

static uint32_t parse_u32_hex(const char *p, const char *end) {
    uint32_t v = 0;
    while (p < end && *p == ' ') p++;
    if (p + 2 <= end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while (p < end) {
        char c = *p++;
        uint32_t d = 0;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (uint32_t)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (uint32_t)(c - 'A');
        else break;
        v = (v << 4) | d;
    }
    return v;
}

static int find_header_end(const char *buf, int len) {
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return i + 4;
        }
    }
    return -1;
}

static void parse_headers(const char *hdr, int hdr_len,
                          uint32_t *content_len, uint32_t *crc32_out, int *http_code) {
    *content_len = 0;
    *crc32_out = 0;
    *http_code = 0;

    int i = 0;
    while (i < hdr_len && hdr[i] != ' ') i++;
    while (i < hdr_len && hdr[i] == ' ') i++;
    if (i + 3 <= hdr_len) {
        *http_code = (int)parse_u32_dec(hdr + i, hdr + hdr_len);
    }

    int line_start = 0;
    for (int pos = 0; pos < hdr_len; pos++) {
        if (hdr[pos] == '\n') {
            int line_end = pos;
            if (line_end > line_start && hdr[line_end - 1] == '\r') line_end--;
            const char *line = hdr + line_start;
            int llen = line_end - line_start;

            const char *k_len = "Content-Length:";
            int n_len = str_len_s(k_len);
            if (llen > n_len && str_eq_ci_n(line, k_len, n_len)) {
                *content_len = parse_u32_dec(line + n_len, line + llen);
            }

            const char *k_crc = "X-Kernel-CRC32:";
            int n_crc = str_len_s(k_crc);
            if (llen > n_crc && str_eq_ci_n(line, k_crc, n_crc)) {
                *crc32_out = parse_u32_hex(line + n_crc, line + llen);
            }

            line_start = pos + 1;
        }
    }
}

ota_status_t ota_baixar_e_aplicar(const ota_http_cfg_t *cfg, bool reiniciar_ao_final) {
    if (!cfg || !cfg->download_buf || cfg->buf_cap == 0) {
        return OTA_ERR_REDE;
    }
    if (!e1000_ativo()) {
        heal_log("ota_net: driver intel e1000 nao ativo");
        return OTA_ERR_REDE;
    }

    const char *path = cfg->path ? cfg->path : "/KERNEL.BIN";
    const char *host = cfg->host_header ? cfg->host_header : "10.0.2.2:8080";

    char req[256];
    int n = 0;
    const char *p1 = "GET ";
    for (int i = 0; p1[i] && n < (int)sizeof(req) - 1; i++) req[n++] = p1[i];
    for (int i = 0; path[i] && n < (int)sizeof(req) - 1; i++) req[n++] = path[i];
    const char *p2 = " HTTP/1.1\r\nHost: ";
    for (int i = 0; p2[i] && n < (int)sizeof(req) - 1; i++) req[n++] = p2[i];
    for (int i = 0; host[i] && n < (int)sizeof(req) - 1; i++) req[n++] = host[i];
    const char *p3 = "\r\nConnection: close\r\n\r\n";
    for (int i = 0; p3[i] && n < (int)sizeof(req) - 1; i++) req[n++] = p3[i];
    req[n] = 0;

    if (e1000_send(req, (uint16_t)n) < 0) {
        heal_log("ota_net: falha ao enviar requisicao via intel e1000");
        return OTA_ERR_REDE;
    }

    char hdr_acc[1024];
    int hdr_used = 0;
    int hdr_end = -1;

    uint32_t expected_size = 0;
    uint32_t expected_crc = 0;
    uint32_t body_received = 0;

    uint32_t max_iter = cfg->max_poll_iter ? cfg->max_poll_iter : 200000u;

    for (uint32_t it = 0; it < max_iter; it++) {
        uint8_t chunk[512];
        int got = e1000_poll_rx(chunk, (uint16_t)sizeof(chunk));
        if (got < 0) {
            heal_log("ota_net: erro de recepcao no intel e1000");
            return OTA_ERR_REDE;
        }
        if (got == 0) {
            if (expected_size > 0 && body_received >= expected_size) {
                break;
            }
            continue;
        }

        int idx = 0;
        if (hdr_end < 0) {
            while (idx < got && hdr_used < (int)sizeof(hdr_acc) - 1) {
                hdr_acc[hdr_used++] = (char)chunk[idx++];
                hdr_acc[hdr_used] = 0;
                hdr_end = find_header_end(hdr_acc, hdr_used);
                if (hdr_end >= 0) {
                    int http_code = 0;
                    parse_headers(hdr_acc, hdr_end, &expected_size, &expected_crc, &http_code);
                    if (http_code != 200) {
                        heal_log("ota_net: resposta HTTP inesperada (%d)", http_code);
                        return OTA_ERR_REDE;
                    }
                    if (expected_size == 0 || expected_size > cfg->buf_cap) {
                        heal_log("ota_net: tamanho invalido no header (%u)", expected_size);
                        return OTA_ERR_TAMANHO;
                    }
                    break;
                }
            }
            if (hdr_end < 0 && hdr_used >= (int)sizeof(hdr_acc) - 1) {
                heal_log("ota_net: cabecalho HTTP grande demais");
                return OTA_ERR_REDE;
            }
        }

        if (hdr_end >= 0) {
            while (idx < got && body_received < expected_size) {
                cfg->download_buf[body_received++] = chunk[idx++];
            }
            if (body_received >= expected_size) {
                break;
            }
        }
    }

    if (hdr_end < 0 || body_received < expected_size) {
        heal_log("ota_net: download incompleto (%u/%u bytes)", body_received, expected_size);
        return OTA_ERR_REDE;
    }

    return ota_aplicar_imagem(cfg->download_buf, body_received, expected_crc, reiniciar_ao_final);
}

