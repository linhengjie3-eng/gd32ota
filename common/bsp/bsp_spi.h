#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stdint.h>

void bsp_spi_init(void);
void bsp_spi_lcd_mode(void);
void bsp_spi_touch_mode(void);
void bsp_spi_write(uint8_t data);
uint8_t bsp_spi_transfer(uint8_t data);
void bsp_spi_write_buffer(const uint8_t *data, uint32_t length);

#endif
