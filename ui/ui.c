#include "kernel.h"
#include "rtc.h"
#include "pci.h"
#include "ui.h"
#include "keyboard.h"
#include "fs.h"
#include "drv.h"
#include "sched.h"

// Definições do modo texto VGA (80 colunas x 25 linhas)
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

// Cores VGA (Fundo << 4 | Letra)
#define COR_FUNDO_AZUL      0x1F  // Azul escuro com texto branco brilhante
#define COR_BARRA_TOPO      0x70  // Cinzento claro com texto preto (Título)
#define COR_BARRA_STATUS    0x30  // Azul claro com texto preto (Barra inferior)
#define COR_MENU_BOX        0x07  // Fundo cinzento claro com texto preto (Menu)
#define COR_DESTAQUE        0x3F  // Azul claro com texto branco (Item selecionado no menu)
#define COR_LOGO            0x1E  // Azul com amarelo brilhante para o logótipo Boss-OS

// Lista de programas ELF encontrados no disco
#define MAX_PROGRAMS 16
static char prog_list[MAX_PROGRAMS][13];
static int prog_count = 0;
static int selected_index = 0;

// Ponteiro direto para a memória de vídeo VGA
static volatile uint16_t *vga_buffer = (volatile uint16_t*) 0xB8000;

// Escrever caractere no ecrã VGA
static void vga_put_char(int x, int y, char c, uint8_t color) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        vga_buffer[y * VGA_WIDTH + x] = ((uint16_t)color << 8) | (uint8_t)c;
    }
}

// Escrever string no ecrã VGA
static void vga_print_string(int x, int y, const char *str, uint8_t color) {
    int i = 0;
    while (str[i] != '\0' && (x + i) < VGA_WIDTH) {
        vga_put_char(x + i, y, str[i], color);
        i++;
    }
}

// Preencher uma linha inteira com uma cor e caractere
static void vga_fill_line(int y, char c, uint8_t color) {
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_put_char(x, y, c, color);
    }
}

// Callback para detetar ficheiros .elf no FAT32
static void scan_elf_callback(const char *name, bool is_dir, uint32_t size) {
    (void)size;
    if (!is_dir && prog_count < MAX_PROGRAMS) {
        int len = 0;
        while (name[len] != '\0') len++;
        
        if (len >= 4) {
            char ext[5];
            for (int i = 0; i < 4; i++) {
                char c = name[len - 4 + i];
                if (c >= 'A' && c <= 'Z') c += 32;
                ext[i] = c;
            }
            ext[4] = '\0';

            if (!strcmp(ext, ".elf")) {
                int j = 0;
                while (name[j] != '\0' && j < 12) {
                    prog_list[prog_count][j] = name[j];
                    j++;
                }
                prog_list[prog_count][j] = '\0';
                prog_count++;
            }
        }
    }
}

// Atualizar lista de programas a partir do diretório atual do sistema de ficheiros
static void refresh_program_list(void) {
    prog_count = 0;
    selected_index = 0;
    fs_list(fs_cwd(), scan_elf_callback);
}

// Desenhar o papel de parede clássico com o logótipo central
static void desenhar_papel_de_parede(void) {
    // 1. Preencher fundo azul
    for (int y = 1; y < 24; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_put_char(x, y, ' ', COR_FUNDO_AZUL);
        }
    }

    // 2. Barra Superior
    vga_fill_line(0, ' ', COR_BARRA_TOPO);
    vga_print_string(2, 0, " Boss-OS - Versao 3.0 (Text-Mode Classic) ", COR_BARRA_TOPO);

    // 3. Logótipo centralizado
    vga_print_string(32, 8,  "  ____             ___  ____  ", COR_LOGO);
    vga_print_string(32, 9,  " | __ )  ___  ___ / _ \\/ ___| ", COR_LOGO);
    vga_print_string(32, 10, " |  _ \\ / _ \\/ __| | | \\___ \\ ", COR_LOGO);
    vga_print_string(32, 11, " | |_) | (_) \\__ \\ |_| |___) |", COR_LOGO);
    vga_print_string(32, 12, " |____/ \\___/|___/\\___/ |____/ ", COR_LOGO);
}

// Atualizar a barra de status inferior
static void atualizar_barra_status(void) {
    vga_fill_line(24, ' ', COR_BARRA_STATUS);
    vga_print_string(2, 24, " [I] Inicia / Menu    [ESC] Sair ", 0x3F);
    vga_print_string(VGA_WIDTH - 28, 24, " Dir: ", 0x30);
    vga_print_string(VGA_WIDTH - 22, 24, fs_cwd(), 0x3F);
}

// Desenhar a caixa do menu "(Inicia)" com os programas .ELF detetados
static void desenhar_menu_inicia(void) {
    int start_x = 2;
    int start_y = 11;
    int box_w = 32;
    int box_h = 6 + (prog_count > 0 ? prog_count : 1);
    if (box_h > 12) box_h = 12;

    // Desenhar fundo da caixa do menu
    for (int y = start_y; y < start_y + box_h; y++) {
        for (int x = start_x; x < start_x + box_w; x++) {
            vga_put_char(x, y, ' ', COR_MENU_BOX);
        }
    }

    // Bordas e Título do Menu
    vga_print_string(start_x, start_y,     "+-- [ Inicia - Programas ELF ] --+", 0x08);
    vga_print_string(start_x, start_y + box_h - 1, "+--------------------------------+", 0x08);

    if (prog_count == 0) {
        vga_print_string(start_x + 2, start_y + 2, "Nenhum .ELF no diretorio!", 0x04);
    } else {
        for (int i = 0; i < prog_count && i < (box_h - 2); i++) {
            uint8_t cor_item = (i == selected_index) ? COR_DESTAQUE : COR_MENU_BOX;
            
            // Limpar linha do item
            for (int x = start_x + 1; x < start_x + box_w - 1; x++) {
                vga_put_char(x, start_y + 2 + i, ' ', cor_item);
            }

            char linha[30];
            int idx = 0;
            linha[idx++] = (i == selected_index) ? '>' : ' ';
            linha[idx++] = ' ';
            linha[idx++] = '[';
            linha[idx++] = '1' + i;
            linha[idx++] = ']';
            linha[idx++] = ' ';

            int c = 0;
            while (prog_list[i][c] != '\0' && idx < 28) {
                linha[idx++] = prog_list[i][c++];
            }
            linha[idx] = '\0';

            vga_print_string(start_x + 2, start_y + 2 + i, linha, cor_item);
        }
    }
}

// Executar o programa selecionado usando o método run (proc_spawn_file / proc_wait)
static void executar_programa_selecionado(void) {
    if (prog_count == 0 || selected_index < 0 || selected_index >= prog_count) return;

    // Limpar ecrã para ecrã inteiro do programa
    for (int y = 0; y < VGA_HEIGHT; y++) vga_fill_line(y, ' ', 0x07);

    char path_buf[128];
    const char *cwd = fs_cwd();
    if (!strcmp(cwd, "/")) {
        path_buf[0] = '/';
        strcpy(path_buf + 1, prog_list[selected_index]);
    } else {
        strcpy(path_buf, cwd);
        int l = strlen(path_buf);
        path_buf[l] = '/';
        strcpy(path_buf + l + 1, prog_list[selected_index]);
    }

    vga_print_string(2, 2, "A executar: ", 0x0F);
    vga_print_string(14, 2, path_buf, 0x0A);
    vga_print_string(2, 4, "----------------------------------------", 0x07);

    // --- Execução utilizando a tua lógica de processos ---
    int pid = proc_spawn_file(path_buf);
    if (pid < 0) {
        vga_print_string(2, 6, "[ERRO] Falha ao iniciar o processo ELF!", 0x0C);
    } else {
        int cod = 0;
        proc_wait(pid, &cod);
        vga_print_string(2, 6, "[Processo terminado com sucesso]", 0x0A);
    }

    vga_print_string(2, 8, "Pressione qualquer tecla para voltar ao menu...", 0x0E);
    while (kb_poll() == 0);
}

void ui_run(void) {
    bool menu_aberto = false;

    // Carregar lista inicial de programas .elf do disco
    refresh_program_list();

    // Desenhar interface base
    desenhar_papel_de_parede();
    atualizar_barra_status();

    for (;;) {
        int key = kb_poll();

        // Atualizar status inferior
        atualizar_barra_status();

        if (key == KEY_ESC) {
            if (menu_aberto) {
                menu_aberto = false;
                desenhar_papel_de_parede();
            } else {
                break; // Sair para o menu anterior do sistema
            }
        }

        // Tecla 'I' ou 'i' para abrir/fechar o menu Inicia
        if (key == 'i' || key == 'I') {
            menu_aberto = !menu_aberto;
            if (menu_aberto) {
                refresh_program_list();
                desenhar_menu_inicia();
            } else {
                desenhar_papel_de_parede();
            }
        }

        // Se o menu estiver aberto, navegar com W e S e executar com ENTER
        if (menu_aberto) {
            if ((key == 'w' || key == 'W') && selected_index > 0) {
                selected_index--;
                desenhar_menu_inicia();
            } else if ((key == 's' || key == 'S') && selected_index < prog_count - 1) {
                selected_index++;
                desenhar_menu_inicia();
            } else if (key == '\n' || key == 0x1C || key == 13) { // ENTER
                menu_aberto = false;
                executar_programa_selecionado();
                refresh_program_list();
                desenhar_papel_de_parede();
            }
        }

        // Pausa leve para evitar consumo total do CPU
        for (volatile int delay = 0; delay < 100000; delay++);
    }

    // Limpar o ecrã ao sair
    for (int y = 0; y < VGA_HEIGHT; y++) vga_fill_line(y, ' ', 0x07);
}

