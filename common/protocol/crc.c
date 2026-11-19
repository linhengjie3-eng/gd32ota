#include "crc.h"

uint16_t crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    unsigned bit;

    for (i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    size_t i;
    unsigned bit;

    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return crc;
}

uint32_t crc32_compute(const uint8_t *data, size_t length)
{
    return crc32_update(0xFFFFFFFFu, data, length) ^ 0xFFFFFFFFu;
}
