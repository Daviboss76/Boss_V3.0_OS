#include "plib.h"

ENTRADA {
    puts_("=== Teste de Crash / Seguranca (Ring 3) ===\n");
    puts_("Digite 'crash' ou 'exit'.\n");

    char buf[32];
    int idx = 0;

    for (;;) {
        puts_("crashme> ");
        idx = 0;

        for (;;) {
            char c = (char)getchar_();
            if (c == '\n' || c == '\r') {
                buf[idx] = '\0';
                puts_("\n");
                break;
            } else if (idx < (int)sizeof(buf) - 1) {
                buf[idx++] = c;
                char temp[2] = {c, '\0'};
                puts_(temp);
            }
        }

        if (buf[0] == 'e' && buf[1] == 'x' && buf[2] == 'i' && buf[3] == 't' && buf[4] == '\0') {
            puts_("A fechar programa com seguranca...\n");
            break;
        }
        if (buf[0] == 'c' && buf[1] == 'r' && buf[2] == 'a' && buf[3] == 's' && buf[4] == 'h' && buf[5] == '\0') {
            puts_("Vou escrever no endereco 0 (nao mapeado)...\n");
            *(volatile int *)0 = 1;
            break;
        }

        puts_("Use 'crash' ou 'exit'.\n");
    }

    exit_(0);
}

