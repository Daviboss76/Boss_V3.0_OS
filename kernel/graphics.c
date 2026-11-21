#include "kernel.h"

// Estrutura para armazenar as propriedades do VBE e do Back Buffer
typedef struct {
    uint32_t phys_base;     // Endereço físico do framebuffer (Front Buffer)
    uint32_t width;         // Largura do ecrã (ex: 800)
    uint32_t height;        // Altura do ecrã (ex: 600)
    uint32_t pitch;         // Bytes por linha (pitch)
    uint32_t bpp;           // Bits por píxel (ex: 32)
    uint32_t *back_buffer;  // Ponteiro para o buffer oculto na RAM
    uint32_t size_in_bytes; // Tamanho total do buffer em bytes
} graphics_context_t;

static graphics_context_t gfx;

// Alocador simples ou estático para o Back Buffer (podes ajustar ao teu gestor de memória do kernel)
static uint32_t static_back_buffer[800 * 600]; // Suporta até 800x600x32bit nativamente

int graphics_init(void) {
    // 1. Ler os parâmetros guardados pelo Stage2 em 0x500
    uint32_t *boot_params = (uint32_t*) 0x500;
    gfx.phys_base = boot_params[0];
    gfx.width     = boot_params[1];
    gfx.height    = boot_params[2];
    gfx.pitch     = boot_params[3];
    gfx.bpp       = boot_params[4];

    // Validação de segurança básica dos dados do VBE
    if (gfx.phys_base == 0 || gfx.width == 0 || gfx.height == 0) {
        return -1; // Dados VBE inválidos ou não encontrados
    }

    // 2. Configurar o Back Buffer
    // Se a resolução padrão for 800x600, usamos o buffer estático. 
    // Para resoluções dinâmicas maiores, deves usar o teu alocador de memória do kernel (ex: kmalloc).
    if (gfx.width <= 800 && gfx.height <= 600) {
        gfx.back_buffer = static_back_buffer;
    } else {
        // Exemplo caso uses alocação dinâmica: gfx.back_buffer = (uint32_t*) kmalloc(gfx.width * gfx.height * sizeof(uint32_t));
        return -2; // Resolução excede o buffer estático pré-alocado
    }

    gfx.size_in_bytes = gfx.width * gfx.height * sizeof(uint32_t);

    // Limpar o back buffer com preto inicialmente
    for (uint32_t i = 0; i < (gfx.width * gfx.height); i++) {
        gfx.back_buffer[i] = 0x00000000;
    }

    return 0;
}

// Desenha um píxel diretamente no Back Buffer (Invisível ao utilizador até fazer o swap)
inline void gfx_draw_pixel(int x, int y, uint32_t color) {
    if (x < 0 || (uint32_t)x >= gfx.width || y < 0 || (uint32_t)y >= gfx.height) {
        return;
    }
    gfx.back_buffer[y * gfx.width + x] = color;
}

// Desenha um retângulo preenchido no Back Buffer
void gfx_draw_rect(int x, int y, int w, int h, uint32_t color) {
    int x2 = x + w;
    int y2 = y + h;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)x2 > gfx.width) x2 = gfx.width;
    if ((uint32_t)y2 > gfx.height) y2 = gfx.height;

    for (int cy = y; cy < y2; cy++) {
        uint32_t row_offset = cy * gfx.width;
        for (int cx = x; cx < x2; cx++) {
            gfx.back_buffer[row_offset + cx] = color;
        }
    }
}

// Executa o Swap (Copia o Back Buffer para o Front Buffer físico de uma só vez)
void gfx_swap_buffers(void) {
    uint32_t *front_buffer = (uint32_t*) gfx.phys_base;
    uint32_t total_pixels = gfx.width * gfx.height;

    // Cópia otimizada bloco a bloco (equivalente a um memcpy rápido de 32 bits)
    #if defined(__i386__) || defined(_M_IX86)
    __asm__ volatile (
        "cld\n\t"
        "rep movsd\n\t"
        :
        : "S" (gfx.back_buffer), "D" (front_buffer), "c" (total_pixels)
        : "memory", "cc"
    );
    #else
    // Fallback caso seja compilado noutra arquitetura
    for (uint32_t i = 0; i < total_pixels; i++) {
        front_buffer[i] = gfx.back_buffer[i];
    }
    #endif
}

