#ifndef KEYBOARD_H
#define KEYBOARD_H
#define KEY_ESC   27
#define KEY_CIMA  0x101
#define KEY_BAIXO 0x102
#define KEY_ESQ   0x103
#define KEY_DIR   0x104
#define KEY_DEL   0x105
void kb_init(void);   /* liga a IRQ1 */
int  kb_poll(void);   /* -1 se nao ha tecla */
int  kb_getc(void);   /* bloqueante (dorme com hlt e roda a guarda de autorreparo) */
#endif
