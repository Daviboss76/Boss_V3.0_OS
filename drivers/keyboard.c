#include "kernel.h"
#include "arch.h"
#include "heal.h"
#include "sched.h"
#include "keyboard.h"

static const char normal[58] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0,'*', 0,' ' };
static const char shifted[58] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0,'*', 0,' ' };

#define KB_N 64
static volatile int fila[KB_N];
static volatile uint8_t ini, fim;
static volatile bool intr;
static bool shift, ctrl, e0;

static void empilha(int c) {
    uint8_t nf = (fim + 1) % KB_N;
    if (nf == ini) return;
    fila[fim] = c; fim = nf;
}

static void decodifica(uint8_t sc) {
    if (sc == 0xE0) { e0 = true; return; }
    bool solta = sc & 0x80;
    sc &= 0x7F;
    if (sc == 0x1D) { ctrl = !solta; e0 = false; return; }       /* Ctrl esq/dir */
    if (e0) {
        e0 = false;
        if (solta) return;
        switch (sc) {
        case 0x48: empilha(KEY_CIMA);  break;
        case 0x50: empilha(KEY_BAIXO); break;
        case 0x4B: empilha(KEY_ESQ);   break;
        case 0x4D: empilha(KEY_DIR);   break;
        case 0x53: empilha(KEY_DEL);   break;
        }
        return;
    }
    if (sc == 0x2A || sc == 0x36) { shift = !solta; return; }
    if (ctrl && sc == 0x2E) { if (!solta) intr = true; return; } /* Ctrl+C */
    if (solta || sc >= 58) return;
    char c = shift ? shifted[sc] : normal[sc];
    if (c) empilha((uint8_t)c);
}

static void irq1(regs_t *r) {
    (void)r;
    if (inb(0x64) & 1) decodifica(inb(0x60));
    sched_wake_tecla();
}

void kb_init(void) {
    while (inb(0x64) & 1) inb(0x60);
    irq_register(1, irq1);
}
int kb_poll(void) {
    if (ini == fim) return -1;
    int c = fila[ini];
    ini = (ini + 1) % KB_N;
    return c;
}
bool kb_intr_take(void) { bool v = intr; intr = false; return v; }

int kb_getc(void) {
    int c;
    while ((c = kb_poll()) < 0) {
        heal_poll();
        sched_idle();
    }
    return c;
}
