#ifndef CRC_H
#define CRC_H

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_ccitt_false(const uint8_t *data, size_t length);
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length);
uint32_t crc32_compute(const uint8_t *data, size_t length);

#endif
