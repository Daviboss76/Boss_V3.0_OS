#include "ulib.h"

#define MOUSE_PORT     0x60
#define MOUSE_STATUS   0x64
#define MOUSE_CMD_WR   0x64
#define MOUSE_DATA     0x60

static void mouse_wait_input(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(MOUSE_STATUS) & 2) == 0) return;
    }
}

static void mouse_wait_output(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(MOUSE_STATUS) & 1) == 1) return;
    }
}

static void mouse_write(uint8_t val) {
    mouse_wait_input();
    outb(MOUSE_CMD_WR, 0xD4); // Informa o controlador 8042 que o byte vai para o rato
    mouse_wait_input();
    outb(MOUSE_DATA, val);
}

static uint8_t mouse_read(void) {
    mouse_wait_output();
    return inb(MOUSE_DATA);
}

static void drv(volatile mbox_t *m) {
    // Opção 1: Inicializar o Rato PS/2
    if (m->op == 1) {
        // 1. Ativar porta auxiliar do rato
        mouse_wait_input();
        outb(MOUSE_CMD_WR, 0xA8);

        // 2. Configurar interrupção no controlador 8042 (ativar bit IRQ12)
        mouse_wait_input();
        outb(MOUSE_CMD_WR, 0x20);
        mouse_wait_output();
        uint8_t status = inb(MOUSE_DATA) | 2;

        mouse_wait_input();
        outb(MOUSE_CMD_WR, 0x60);
        mouse_wait_input();
        outb(MOUSE_DATA, status);

        // 3. Padrões de fábrica e ativação de streaming
        mouse_write(0xF6);
        mouse_read(); // ACK (0xFA)

        mouse_write(0xF4);
        mouse_read(); // ACK (0xFA)

        m->ret = 0;
        return;
    }
    
    // Opção 2: Ler estado/pacote do rato (Polling direto ou leitura de buffer)
    else if (m->op == 2) {
        uint8_t status = inb(MOUSE_STATUS);
        
        // Verifica se existem dados prontos na porta do rato (bit 5 do status)
        if (status & 0x20) {
            uint8_t b1 = inb(MOUSE_DATA);
            uint8_t b2 = inb(MOUSE_DATA);
            uint8_t b3 = inb(MOUSE_DATA);

            // Devolve os dados através dos argumentos da mailbox:
            // arg[0] = Botões (Esquerdo, Direito, Meio)
            // arg[1] = Delta X (Movimento horizontal)
            // arg[2] = Delta Y (Movimento vertical)
            m->arg[0] = (uint32_t)(b1 & 0x07);
            m->arg[1] = (uint32_t)((int8_t)b2);
            m->arg[2] = (uint32_t)((int8_t)b3);
            m->ret = 1; // Sucesso: dados recolhidos
        } else {
            m->ret = 0; // Sem novos dados do rato no momento
        }
        return;
    }

    // Operação inválida
    m->ret = -1;
}

DRIVER(drv)

