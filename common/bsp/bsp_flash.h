#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint32_t bsp_flash_page_size(uint32_t address);
bool bsp_flash_range_valid(uint32_t address, size_t length);
bool bsp_flash_erase(uint32_t address, size_t length);
bool bsp_flash_write(uint32_t address, const void *data, size_t length);

#endif
