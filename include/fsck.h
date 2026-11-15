#ifndef FSCK_H
#define FSCK_H
#include <stdint.h>
#include <stdbool.h>
int fsck_run(bool reparar, uint32_t *achados, uint32_t *reparados);
#endif
