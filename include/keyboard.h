#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdbool.h>
#define KEY_ESC   27
#define KEY_CIMA  0x101
#define KEY_BAIXO 0x102
#define KEY_ESQ   0x103
#define KEY_DIR   0x104
#define KEY_DEL   0x105
void kb_init(void);
int  kb_poll(void);            /* -1 se nao ha tecla (tambem consulta teclados USB) */
int  kb_getc(void);
bool kb_intr_take(void);
void kb_push(int c);           /* usado pelo driver de teclado USB */
void kb_intr_set(void);        /* Ctrl+C vindo do USB */
#endif
