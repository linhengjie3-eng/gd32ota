#include "ota_protocol.h"
#include <string.h>

uint16_t ota_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    unsigned bit;

    for (i = 0u; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t ota_pack_frame(uint8_t command, const uint8_t *payload,
                      uint16_t length, uint8_t *output, size_t capacity)
{
    size_t frame_size = (size_t)length + PROTO_FRAME_OVERHEAD;
    uint16_t crc;
    uint16_t i;

    if ((length > PROTO_MAX_PAYLOAD) || (capacity < frame_size) ||
        ((length > 0u) && (payload == NULL))) {
        return 0u;
    }
    output[0] = PROTO_SOF0;
    output[1] = PROTO_SOF1;
    output[2] = command;
    output[3] = (uint8_t)length;
    output[4] = (uint8_t)(length >> 8);
    for (i = 0u; i < length; ++i) {
        output[5u + i] = payload[i];
    }
    crc = ota_crc16_ccitt_false(&output[2], (size_t)length + 3u);
    output[5u + length] = (uint8_t)crc;
    output[6u + length] = (uint8_t)(crc >> 8);
    return frame_size;
}

enum {
    PARSE_SOF0 = 0,
    PARSE_SOF1,
    PARSE_COMMAND,
    PARSE_LEN_L,
    PARSE_LEN_H,
    PARSE_PAYLOAD,
    PARSE_CRC_L,
    PARSE_CRC_H
};

void ota_parser_init(ota_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
    parser->state = PARSE_SOF0;
}

static void deliver(ota_parser_t *parser, ota_frame_callback_t callback,
                    void *context, size_t *delivered)
{
    uint8_t check[3u + PROTO_MAX_PAYLOAD];
    ota_frame_t frame;
    uint16_t crc;

    check[0] = parser->command;
    check[1] = (uint8_t)parser->length;
    check[2] = (uint8_t)(parser->length >> 8);
    if (parser->length > 0u) {
        memcpy(&check[3], parser->payload, parser->length);
    }
    crc = ota_crc16_ccitt_false(check, (size_t)parser->length + 3u);
    if (crc == parser->received_crc && callback != NULL) {
        frame.command = parser->command;
        frame.length = parser->length;
        if (parser->length > 0u) {
            memcpy(frame.payload, parser->payload, parser->length);
        }
        callback(&frame, context);
        (*delivered)++;
    }
    ota_parser_init(parser);
}

size_t ota_parser_feed(ota_parser_t *parser, const uint8_t *data,
                       size_t length, ota_frame_callback_t callback,
                       void *context)
{
    size_t i;
    size_t delivered = 0u;

    for (i = 0u; i < length; ++i) {
        uint8_t byte = data[i];
        switch (parser->state) {
        case PARSE_SOF0:
            if (byte == PROTO_SOF0) parser->state = PARSE_SOF1;
            break;
        case PARSE_SOF1:
            parser->state = (byte == PROTO_SOF1) ? PARSE_COMMAND : PARSE_SOF0;
            if (byte == PROTO_SOF0 && parser->state == PARSE_SOF0) {
                parser->state = PARSE_SOF1; /* 连续帧头, 保留一个 SOF0 */
            }
            break;
        case PARSE_COMMAND:
            parser->command = byte;
            parser->state = PARSE_LEN_L;
            break;
        case PARSE_LEN_L:
            parser->length = byte;
            parser->state = PARSE_LEN_H;
            break;
        case PARSE_LEN_H:
            parser->length |= (uint16_t)byte << 8;
            parser->index = 0u;
            if (parser->length > PROTO_MAX_PAYLOAD) {
                ota_parser_init(parser);
            } else {
                parser->state = (parser->length == 0u) ? PARSE_CRC_L
                                                       : PARSE_PAYLOAD;
            }
            break;
        case PARSE_PAYLOAD:
            parser->payload[parser->index++] = byte;
            if (parser->index >= parser->length) parser->state = PARSE_CRC_L;
            break;
        case PARSE_CRC_L:
            parser->received_crc = byte;
            parser->state = PARSE_CRC_H;
            break;
        case PARSE_CRC_H:
            parser->received_crc |= (uint16_t)byte << 8;
            deliver(parser, callback, context, &delivered);
            break;
        default:
            ota_parser_init(parser);
            break;
        }
    }
    return delivered;
}
