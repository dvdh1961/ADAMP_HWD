#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init_1ms(void);
uint32_t timer_millis(void);
void timer_delay_ms(uint16_t delay_ms);

#endif

