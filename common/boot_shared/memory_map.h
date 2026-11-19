#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>

#define FLASH_BASE_ADDR             0x08000000u
#define FLASH_TOTAL_SIZE            0x00100000u
#define FLASH_END_ADDR              (FLASH_BASE_ADDR + FLASH_TOTAL_SIZE)

#define SRAM_BASE_ADDR              0x20000000u
#define SRAM_TOTAL_SIZE             0x00018000u
#define SRAM_END_ADDR               (SRAM_BASE_ADDR + SRAM_TOTAL_SIZE)

#define BOOT_BASE_ADDR              0x08000000u
#define BOOT_MAX_SIZE               0x00004000u

#define META_PAGE_A_ADDR            0x08004000u
#define META_PAGE_B_ADDR            0x08004800u
#define META_PAGE_BYTES             0x00000800u
#define META_REGION_SIZE            0x00001000u

#define SLOT_A_ADDR                 0x08005000u
#define SLOT_A_SIZE                 0x0007B000u
#define SLOT_B_ADDR                 0x08080000u
#define SLOT_B_SIZE                 0x00080000u

#define BANK0_END_ADDR              0x08080000u
#define BANK0_PAGE_SIZE             0x00000800u
#define BANK1_PAGE_SIZE             0x00001000u

#define SLOT_ID_A                   0u
#define SLOT_ID_B                   1u
#define SLOT_ID_NONE                0xFFu

#define PROTOCOL_VERSION            0x0100u
#define BOOT_VERSION_MAJOR          1u
#define BOOT_VERSION_MINOR          0u

#if defined(APP_SLOT_B)
#define APP_SLOT_ID                 SLOT_ID_B
#define APP_BASE_ADDR               SLOT_B_ADDR
#define APP_REGION_SIZE             SLOT_B_SIZE
#define APP_VECTOR_OFFSET           (SLOT_B_ADDR - FLASH_BASE_ADDR)
#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR           1u
#endif
#ifndef APP_VERSION_MINOR
#define APP_VERSION_MINOR           0u
#endif
#else
#define APP_SLOT_ID                 SLOT_ID_A
#define APP_BASE_ADDR               SLOT_A_ADDR
#define APP_REGION_SIZE             SLOT_A_SIZE
#define APP_VECTOR_OFFSET           (SLOT_A_ADDR - FLASH_BASE_ADDR)
#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR           1u
#endif
#ifndef APP_VERSION_MINOR
#define APP_VERSION_MINOR           0u
#endif
#endif

static inline uint32_t slot_base_address(uint8_t slot)
{
    return (slot == SLOT_ID_B) ? SLOT_B_ADDR : SLOT_A_ADDR;
}

static inline uint32_t slot_capacity(uint8_t slot)
{
    return (slot == SLOT_ID_B) ? SLOT_B_SIZE : SLOT_A_SIZE;
}

#endif
