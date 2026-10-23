#ifndef FALLBACK_UART_H
#define FALLBACK_UART_H
#include <stddef.h>
#include <stdint.h>
void fallback_uart_init(void);
int fallback_uart_get(uint8_t *byte, uint32_t *arrival);
int fallback_uart_fault(void);
int fallback_uart_write(const uint8_t *bytes, size_t count);
#endif
