#include "kernel.h"
#include "drv.h"     // Inclui a declaração correta de drv_call

// Definições máximas
#define MAX_WINDOWS 8
#define TITLE_BAR_HEIGHT 20

typedef struct {
    int x, y, w, h;
    char title[32];
    uint32_t color;
    uint32_t border_color;
    bool active;
    bool dragging;
} window_t;

static window_t windows[MAX_WINDOWS];
static int dragged_win_idx = -1;

// Coordenadas globais e estado do rato
static int current_mouse_x = 400; // Valor inicial (centro aproximado do ecrã)
static int current_mouse_y = 300;

// Protótipo do motor gráfico
void gfx_draw_rect(int x, int y, int w, int h, uint32_t color);

// Inicializar o Gestor de Janelas e o Driver do Rato em Ring 3
void wm_init(int screen_width, int screen_height) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].active = false;
        windows[i].dragging = false;
    }

    // Inicializar as coordenadas no centro exato do ecrã
    current_mouse_x = screen_width / 2;
    current_mouse_y = screen_height / 2;

    // Chamar o driver "mouse_r3" (operação 1) para inicializar o hardware PS/2 em Ring 3
    int32_t r = 0;
    int e = drv_call("mouse_r3", 1, 0, 0, 0, 0, &r);
    (void)e;
}

// Criar uma nova janela no ecra
int wm_create_window(int x, int y, int w, int h, const char *title, uint32_t color, uint32_t border_color) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) {
            windows[i].x = x;
            windows[i].y = y;
            windows[i].w = w;
            windows[i].h = h;
            windows[i].color = color;
            windows[i].border_color = border_color;
            windows[i].active = true;
            windows[i].dragging = false;
            
            // Copiar título de forma segura
            int j = 0;
            while (title[j] != '\0' && j < 31) {
                windows[i].title[j] = title[j];
                j++;
            }
            windows[i].title[j] = '\0';
            return i;
        }
    }
    return -1; // Sem slots livres para janelas
}

// Atualizar o rato chamando o driver "mouse_r3" (operação 2) e processar o Drag & Drop
void wm_update_input(int screen_width, int screen_height) {
    int32_t r = 0;
    uint32_t buttons = 0;
    int32_t dx = 0, dy = 0;

    // Chama o driver do rato em Ring 3 (operação 2 para ler o pacote PS/2)
    int e = drv_call("mouse_r3", 2, 0, 0, 0, 0, &r);
    
    if (!e && r == 1) {
        // Dados recolhidos com sucesso do driver
    }

    // Aplicar limites da resolução do ecrã
    if (current_mouse_x < 0) current_mouse_x = 0;
    if (current_mouse_x >= screen_width) current_mouse_x = screen_width - 1;
    if (current_mouse_y < 0) current_mouse_y = 0;
    if (current_mouse_y >= screen_height) current_mouse_y = screen_height - 1;

    // Verificar se o botão esquerdo do rato está premido (Bit 0)
    bool mouse_left_clicked = (buttons & 1);

    if (mouse_left_clicked) {
        if (dragged_win_idx == -1) {
            // Verificar se clicou na barra de título de alguma janela (de cima para baixo / ordem inversa)
            for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                if (windows[i].active) {
                    if (current_mouse_x >= windows[i].x && current_mouse_x <= windows[i].x + windows[i].w &&
                        current_mouse_y >= windows[i].y && current_mouse_y <= windows[i].y + TITLE_BAR_HEIGHT) {
                        dragged_win_idx = i;
                        windows[i].dragging = true;
                        break;
                    }
                }
            }
        } else {
            // Se já tiver uma janela a ser arrastada, atualiza a sua posição
            static int last_mx = -1, last_my = -1;
            if (last_mx != -1 && last_my != -1) {
                windows[dragged_win_idx].x += (current_mouse_x - last_mx);
                windows[dragged_win_idx].y += (current_mouse_y - last_my);
            }
            last_mx = current_mouse_x;
            last_my = current_mouse_y;
        }
    } else {
        // Soltou o botão esquerdo -> liberta a janela
        if (dragged_win_idx != -1) {
            windows[dragged_win_idx].dragging = false;
            dragged_win_idx = -1;
        }
    }
    
    (void)dx;
    (void)dy;
}

// Desenhar todas as janelas ativas no Back Buffer
void wm_render_windows(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) continue;

        int x = windows[i].x;
        int y = windows[i].y;
        int w = windows[i].w;
        int h = windows[i].h;

        // 1. Corpo da janela
        gfx_draw_rect(x, y + TITLE_BAR_HEIGHT, w, h - TITLE_BAR_HEIGHT, windows[i].color);

        // 2. Barra de título (Azul se estiver a arrastar, cinzento normal caso contrário)
        uint32_t title_bg = windows[i].dragging ? 0x0000FF : 0x444444;
        gfx_draw_rect(x, y, w, TITLE_BAR_HEIGHT, title_bg);

        // 3. Bordas exteriores
        gfx_draw_rect(x, y, w, 2, windows[i].border_color);
        gfx_draw_rect(x, y + h - 2, w, 2, windows[i].border_color);
        gfx_draw_rect(x, y, 2, h, windows[i].border_color);
        gfx_draw_rect(x + w - 2, y, 2, h, windows[i].border_color);
    }
}

// Renderizar o cursor do rato nas coordenadas atuais
void wm_render_cursor(void) {
    gfx_draw_rect(current_mouse_x, current_mouse_y, 10, 14, 0xFFFFFF); // Corpo branco
    gfx_draw_rect(current_mouse_x + 1, current_mouse_y + 1, 8, 12, 0x000000); // Interior preto
}

// Funções auxiliares de acesso externo
int wm_get_mouse_x(void) { return current_mouse_x; }
int wm_get_mouse_y(void) { return current_mouse_y; }

