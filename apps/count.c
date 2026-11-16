#include "plib.h"

ENTRADA {
    uint32_t pid = getpid_();
    for (uint32_t i = 0; i < 8; i++) {
        puts_("[proc "); putnum_(pid); puts_("] contagem "); putnum_(i); puts_("\n");
        sleep_(30 + pid * 7);
    }
    exit_((int)pid);
}
