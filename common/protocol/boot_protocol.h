#ifndef BOOT_PROTOCOL_H
#define BOOT_PROTOCOL_H

#include <stdint.h>

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
#define ACK_VERSION_ERROR           7u   /* 固件头版本低于当前活动槽，拒绝降级 */

#define VERIFY_OK                   0u
#define VERIFY_CRC_FAILED           1u
#define VERIFY_VECTOR_FAILED        2u
#define VERIFY_FLASH_FAILED         3u
#define VERIFY_SIZE_FAILED          4u

#define DEVICE_STATE_APP            0u
#define DEVICE_STATE_BOOT_WAIT      1u
#define DEVICE_STATE_BOOT_RECEIVE   2u
#define DEVICE_STATE_BOOT_ERROR     3u

#endif
