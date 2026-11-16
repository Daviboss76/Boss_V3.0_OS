#include "plib.h"

ENTRADA {
    puts_("Ola do ring 3! Sou o processo ");
    putnum_(getpid_());
    puts_(" (paginacao + escalonador)\n");
    exit_(0);
}
