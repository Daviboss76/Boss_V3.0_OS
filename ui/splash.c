#include "kernel.h"
#include "vga.h"
#include "timer.h"
#include "splash.h"

#define BLOCO  ((char)0xDB)
#define SOMBRA ((char)0xB0)
#define PRETO(fg) VGA_COR(fg, VGA_PRETO)

static const uint8_t paleta[6] = { VGA_VERMELHO_CL, VGA_AMARELO, VGA_VERDE_CL,
                                   VGA_CIANO_CL, VGA_AZUL_CL, VGA_MAGENTA_CL };
static const char *const G_B[5] = { "####.", "#...#", "####.", "#...#", "####." };
static const char *const G_O[5] = { ".###.", "#...#", "#...#", "#...#", ".###." };
static const char *const G_S[5] = { ".####", "#....", ".###.", "....#", "####." };
static const char logo[] = "BOSS OS";
static int pct, frame;

static const char *const *glifo(char c) {
    return c == 'B' ? G_B : c == 'O' ? G_O : G_S;
}

static void desenha_logo(void) {
    int x = 5, y = 7;
    for (const char *p = logo; *p; p++) {
        if (*p == ' ') { x += 5; continue; }
        const char *const *g = glifo(*p);
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 5; c++)
                for (int k = 0; k < 2; k++) {
                    int cx = x + c * 2 + k;
                    uint8_t cor = paleta[((cx - 5) / 2 + frame) % 6];
                    vga_put_at(cx, y + r, g[r][c] == '#' ? BLOCO : ' ', PRETO(cor));
                }
        x += 11;
    }
}

static void desenha_barra(void) {
    int cheio = pct * 50 / 100;
    vga_put_at(14, 16, '[', PRETO(VGA_CINZA));
    vga_put_at(65, 16, ']', PRETO(VGA_CINZA));
    for (int i = 0; i < 50; i++) {
        if (i < cheio) vga_put_at(15 + i, 16, BLOCO, PRETO(paleta[(i / 4 + frame) % 6]));
        else           vga_put_at(15 + i, 16, SOMBRA, PRETO(VGA_CINZA_ESC));
    }
    char t[5] = { pct >= 100 ? '1' : ' ', pct >= 10 ? '0' + (pct / 10) % 10 : ' ',
                  '0' + pct % 10, '%', 0 };
    vga_puts_at(38, 17, t, PRETO(VGA_BRANCO));
}

static void centro(int y, const char *s, uint8_t cor) {
    vga_fill(0, y, VGA_COLS, 1, ' ', cor);
    vga_puts_at((VGA_COLS - (int)strlen(s)) / 2, y, s, cor);
}

static void quadro(void) { desenha_logo(); desenha_barra(); frame++; }

void splash_init(void) {
    vga_cursor_show(false);
    vga_clear(PRETO(VGA_BRANCO));
    pct = 0; frame = 0;
    centro(13, "kernel em C  -  32 bits", PRETO(VGA_CINZA));
    quadro();
}

void splash_step(const char *msg, int alvo, void (*fn)(void)) {
    centro(19, msg, PRETO(VGA_CINZA));
    if (fn) { vga_mute(true); fn(); vga_mute(false); }
    while (pct < alvo) { pct++; quadro(); delay_ms(18); }
}

void splash_end(void) {
    centro(19, "Bem-vindo ao BossOS!", PRETO(VGA_AMARELO));
    for (int i = 0; i < 30; i++) { quadro(); delay_ms(30); }
}
