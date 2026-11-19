#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void bsp_debug_uart_init(uint32_t baudrate);
void bsp_debug_uart_write(const uint8_t *data, size_t length);
void bsp_esp_uart_init(uint32_t baudrate);
void bsp_esp_uart_write(const uint8_t *data, size_t length);
size_t bsp_esp_uart_read(uint8_t *data, size_t capacity);
int bsp_debug_putchar(int ch);

#endif
