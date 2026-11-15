#ifndef DRV_H
#define DRV_H
#include <stdint.h>

enum { DRV_OK = 0, DRV_ENOENT = -1, DRV_EQUAR = -2, DRV_EFALHA = -3 };

typedef struct {
    const char *nome;
    int estado;                       /* 0 descarregado, 1 ativo, 2 quarentena */
    uint32_t chamadas, falhas, reinicios;
} drv_stat_t;

int drv_count(void);
int drv_stat(int i, drv_stat_t *s);
int drv_call(const char *nome, uint32_t op, uint32_t a0, uint32_t a1,
             const void *buf, uint32_t len, int32_t *ret);
#endif
