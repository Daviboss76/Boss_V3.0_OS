#include "ulib.h"

static uint32_t crc = 0xFFFFFFFFu;

static void drv(volatile mbox_t *m) {
    switch (m->op) {
    case 1: crc = 0xFFFFFFFFu; m->ret = 0; break;
    case 2:
        for (uint32_t i = 0; i < m->len && i < 256; i++) {
            crc ^= m->buf[i];
            for (int k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
        }
        m->ret = 0; break;
    case 3: m->ret = (int32_t)~crc; break;
    default: m->ret = -1;
    }
}
DRIVER(drv)
