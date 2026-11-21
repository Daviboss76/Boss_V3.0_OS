#include "plib.h"

ENTRADA {
    uint32_t pid = getpid_();
    puts_("[proc "); putnum_(pid); puts_("] Iniciado. Digite 'exit' para fechar.\n");

    uint32_t i = 0;
    char buf[32];

    for (;;) {
        // Mostra a contagem de forma contínua
        puts_("[proc "); putnum_(pid); puts_("] contagem "); putnum_(i++); puts_("\n");
        sleep_(30 + pid * 7);

        // (Opcional) Se a tua plib suportar leitura não bloqueante (ex: kbhit_), 
        // podes verificar se o utilizador escreveu algo para sair.
        // Caso contrário, podes adaptar para ler diretamente se a API bloquear.
    }

    exit_((int)pid);
}

