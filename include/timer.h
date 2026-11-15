#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>
extern volatile uint32_t ticks;       /* incrementa a cada 10 ms */
void timer_init(uint32_t hz);
void delay_ms(uint32_t ms);           /* por polling (PIT canal 2) */
#endif
