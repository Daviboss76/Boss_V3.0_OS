#ifndef USB_H
#define USB_H
#include <stdint.h>
#include <stdbool.h>

enum { UT_OUTRO, UT_HUB, UT_MSD, UT_TECLADO, UT_MOUSE, UT_AUDIO };

typedef struct {
    uint8_t addr, vel, classe, tipo;      /* vel: 0 full, 1 low, 2 high */
    uint16_t vid, pid;
    char nome[28];
    uint8_t hub, porta;                   /* endereco do hub pai e numero da porta */
} usb_dev_t;

void usb_scan(void);
const char *usb_log(void);
int  usb_ndev(void);
const usb_dev_t *usb_dev(int i);
void usb_poll(void);

typedef struct { bool present; uint32_t sectors; char model[41]; } usb_info_t;
int  usb_msd_count(void);
const usb_info_t *usb_info(int i);
int  usb_read(int i, uint32_t lba, uint32_t n, void *buf);
int  usb_write(int i, uint32_t lba, uint32_t n, const void *buf);

void usb_mouse(int *x, int *y, int *botoes);
int  usb_audio_count(void);
int  usb_audio_info(int i, char *out, int max);
int  usb_audio_volume(int i, int percentual);       /* 0..100 define; <0 le */
int  usb_audio_mudo(int i, bool mudo);
#endif
