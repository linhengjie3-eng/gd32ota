#include "bsp_flash.h"
#include "gd32f30x.h"
#include "memory_map.h"
#include <string.h>

uint32_t bsp_flash_page_size(uint32_t address)
{
    return (address >= BANK0_END_ADDR) ? BANK1_PAGE_SIZE : BANK0_PAGE_SIZE;
}

bool bsp_flash_range_valid(uint32_t address, size_t length)
{
    uint32_t end;
    if ((length == 0u) || (address < META_PAGE_A_ADDR) ||
        (address >= FLASH_END_ADDR)) {
        return false;
    }
    end = address + (uint32_t)length;
    return (end >= address) && (end <= FLASH_END_ADDR);
}

bool bsp_flash_erase(uint32_t address, size_t length)
{
    uint32_t end;
    bool result = true;

    if (!bsp_flash_range_valid(address, length) ||
        (address % bsp_flash_page_size(address) != 0u)) {
        return false;
    }
    end = address + (uint32_t)length;
    fmc_bank0_unlock();
    fmc_bank1_unlock();
    while (address < end) {
        uint32_t page = bsp_flash_page_size(address);
        if (fmc_page_erase(address) != FMC_READY) {
            result = false;
            break;
        }
        address += page;
    }
    fmc_bank1_lock();
    fmc_bank0_lock();
    return result;
}

bool bsp_flash_write(uint32_t address, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    bool result = true;
    size_t offset = 0;

    if (!bsp_flash_range_valid(address, length) || ((address & 3u) != 0u)) {
        return false;
    }
    fmc_bank0_unlock();
    fmc_bank1_unlock();
    while (offset < length) {
        uint32_t word = 0xFFFFFFFFu;
        unsigned part;
        for (part = 0; part < 4u && offset + part < length; ++part) {
            word &= ~((uint32_t)0xFFu << (part * 8u));
            word |= (uint32_t)bytes[offset + part] << (part * 8u);
        }
        if (fmc_word_program(address + (uint32_t)offset, word) != FMC_READY) {
            result = false;
            break;
        }
        offset += 4u;
    }
    fmc_bank1_lock();
    fmc_bank0_lock();
    if (result) {
        result = memcmp((const void *)address, data, length) == 0;
    }
    return result;
}
