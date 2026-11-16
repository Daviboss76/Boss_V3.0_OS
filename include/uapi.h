#ifndef UAPI_H
#define UAPI_H
#include <stdint.h>

#define USER_BASE      0x10000000u
#define USER_HEAP_MAX  0x103C0000u
#define USER_STACK_TOP 0x103FE000u

enum { SYS_EXIT = 0, SYS_WRITE = 1, SYS_TICKS = 2, SYS_GETKEY = 3, SYS_WAITKEY = 4,
       SYS_SLEEP = 5, SYS_YIELD = 6, SYS_GETPID = 7, SYS_TIME = 8, SYS_SBRK = 9,
       SYS_FREAD = 10, SYS_FWRITE = 11, SYS_SPAWN = 12, SYS_WAIT = 13 };
#endif
