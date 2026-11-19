#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 与 GD32 侧 common/protocol/boot_protocol.h 保持一致 */
#define PROTO_SOF0                  0xA5u
#define PROTO_SOF1                  0x5Au
#define PROTO_MAX_PAYLOAD           300u
#define PROTO_DATA_CHUNK            256u
#define PROTO_HEADER_ACK_SEQ        0xFFFFu

#define CMD_HANDSHAKE               0x01u
#define CMD_HANDSHAKE_RESP          0x02u
#define CMD_FIRMWARE_HEADER         0x10u
#define CMD_FIRMWARE_DATA           0x11u
#define CMD_PACKET_ACK              0x12u
#define CMD_FIRMWARE_END            0x13u
#define CMD_VERIFY_RESULT           0x14u
#define CMD_VERSION_QUERY           0x20u
#define CMD_UPDATE_CHECK            0x21u
#define CMD_UPDATE_CHECK_RESP       0x22u

#define ACK_OK                      0u
#define ACK_CRC_ERROR               1u
#define ACK_SEQUENCE_ERROR          2u
#define ACK_STATE_ERROR             3u
#define ACK_FLASH_ERROR             4u
#define ACK_BUSY                    5u
#define ACK_LENGTH_ERROR            6u
#define ACK_VERSION_ERROR           7u
#define DEVICE_STATE_APP            0u
#define DEVICE_STATE_BOOT_WAIT      1u
#define DEVICE_STATE_BOOT_RECEIVE   2u
#define DEVICE_STATE_BOOT_ERROR     3u

/* 帧头 2B + CMD 1B + LEN 2B + CRC16 2B = 7B */
#define PROTO_FRAME_OVERHEAD        7u
#define PROTO_FRAME_MAX_SIZE        (PROTO_MAX_PAYLOAD + PROTO_FRAME_OVERHEAD)

typedef struct {
    uint8_t command;
    uint16_t length;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} ota_frame_t;

typedef void (*ota_frame_callback_t)(const ota_frame_t *frame, void *context);

typedef struct {
    uint8_t state;
    uint8_t command;
    uint16_t length;
    uint16_t index;
    uint16_t received_crc;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} ota_parser_t;

uint16_t ota_crc16_ccitt_false(const uint8_t *data, size_t length);
size_t ota_pack_frame(uint8_t command, const uint8_t *payload,
                      uint16_t length, uint8_t *output, size_t capacity);
void ota_parser_init(ota_parser_t *parser);
/* 返回成功解析出的帧数 */
size_t ota_parser_feed(ota_parser_t *parser, const uint8_t *data,
                       size_t length, ota_frame_callback_t callback,
                       void *context);

#endif
