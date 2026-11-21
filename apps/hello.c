#include "plib.h"

ENTRADA {
    puts_("=== Processo Ring 3 (Ola) Interativo ===\n");
    puts_("Digite comandos. Escreva 'exit' para fechar.\n");

    char buf[32];
    int idx = 0;

    for (;;) {
        puts_("ola> ");
        idx = 0;

        for (;;) {
            char c = getchar_();

            // Tratar Enter ('\n' ou '\r')
            if (c == '\n' || c == '\r') {
                buf[idx] = '\0';
                puts_("\n");
                break;
            } 
            // Tratar Backspace ('\b' ou 127)
            else if (c == '\b' || c == 127) {
                if (idx > 0) {
                    idx--;
                    puts_("\b \b");
                }
            } 
            // Caractere normal
            else if (idx < (int)sizeof(buf) - 1) {
                buf[idx++] = c;
                char temp[2] = {c, '\0'};
                puts_(temp);
            }
        }

        // Verificar se o utilizador digitou "exit"
        if (buf[0] == 'e' && buf[1] == 'x' && buf[2] == 'i' && buf[3] == 't' && buf[4] == '\0') {
            puts_("A sair do processo...\n");
            break;
        }

        puts_("Comando desconhecido: ");
        puts_(buf);
        puts_("\n");
    }

    exit_(0);
}

