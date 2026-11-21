#ifndef USBHC_H
#define USBHC_H
#include <stdint.h>
#include <stdbool.h>

/* vel: 0 full, 1 low, 2 high. hub/porta: hub USB 2.0 que faz a traducao (0 se o dispositivo e high-speed) */
typedef struct { uint8_t addr, vel, hub, porta; uint16_t mps0; } hc_alvo_t;
typedef struct { hc_alvo_t alvo; uint8_t ep; uint16_t mps; int qh; } hc_pipe_t;

bool hc_ativo(void);
int  hc_nportas(void);
bool hc_porta_conectada(int p);
int  hc_porta_reset(int p);          /* 2 = high-speed pronto; 0 = cedido ao companheiro; <0 erro */
int  hc_ctl(const hc_alvo_t *a, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, void *dados);
int  hc_ctl_ms(const hc_alvo_t *a, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, void *dados, uint32_t ms);
void hc_pipes_liberar(void);
int  hc_bulk_abre(hc_pipe_t *p, const hc_alvo_t *a, uint8_t ep, uint16_t mps);
int  hc_bulk(hc_pipe_t *p, bool in, void *buf, uint32_t len, uint32_t ms, uint32_t *feito);
void hc_pipe_reset(hc_pipe_t *p);    /* toggle volta a DATA0 */
uint8_t *hc_buf(void);               /* 16 KB de DMA para dados bulk */
uint8_t *hc_cmd(void);               /* 128 bytes de DMA (CBW/CSW) */
#endif
