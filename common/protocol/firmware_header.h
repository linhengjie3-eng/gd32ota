#ifndef FIRMWARE_HEADER_H
#define FIRMWARE_HEADER_H

#include <stdint.h>

#define FW_MAGIC                    0x4641574Cu
#define FW_PACKAGE_HEADER_SIZE      40u

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define FW_PACKED
#elif defined(__GNUC__)
#define FW_PACKED __attribute__((packed))
#else
#define FW_PACKED
#endif

typedef struct FW_PACKED {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t firmware_size;
    uint32_t crc32;
    uint32_t build_time;
    uint8_t reserve[20];
} FirmwareHeader_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

#undef FW_PACKED

#endif
