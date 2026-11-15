/* rtc.h */
#ifndef RTC_H
#define RTC_H
#include <stdint.h>
typedef struct { uint8_t h, m, s; } rtc_hora_t;
void rtc_ler(rtc_hora_t *t);
#endif
