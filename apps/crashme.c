#include "plib.h"

ENTRADA {
    puts_("vou escrever no endereco 0 (nao mapeado)...\n");
    *(volatile int *)0 = 1;
    puts_("isto nao deveria aparecer\n");
    exit_(0);
}
