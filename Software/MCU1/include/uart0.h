#ifndef UART0_H
#define UART0_H

#include <stdbool.h>
#include <stdint.h>

void uart0_init_adamnet(void);
void uart0_write_byte(uint8_t value);
bool uart0_read_byte(uint8_t *value);
void uart0_clear_rx(void);
bool uart0_rx_overflowed(void);
bool uart0_received_ever(void);

#endif
