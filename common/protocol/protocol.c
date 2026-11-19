#include "protocol.h"
#include "crc.h"

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

void proto_parser_init(proto_parser_t *parser)
{
    parser->state = PARSE_SOF0;
    parser->command = 0;
    parser->length = 0;
    parser->index = 0;
    parser->received_crc = 0;
}

static bool check_and_deliver(proto_parser_t *parser,
                              proto_frame_callback_t callback,
                              void *context)
{
    uint8_t check[3 + PROTO_MAX_PAYLOAD];
    uint16_t crc;
    bool valid = false;

    check[0] = parser->command;
    check[1] = (uint8_t)parser->length;
    check[2] = (uint8_t)(parser->length >> 8);
    if (parser->length > 0) {
        uint16_t i;
        for (i = 0; i < parser->length; ++i) {
            check[3 + i] = parser->payload[i];
        }
    }
    crc = crc16_ccitt_false(check, (size_t)parser->length + 3u);
    if (crc == parser->received_crc) {
        if (callback != 0) {
            callback(parser->command, parser->payload, parser->length, context);
        }
        valid = true;
    }
    proto_parser_init(parser);
    return valid;
}

bool proto_parser_feed(proto_parser_t *parser,
                       const uint8_t *data,
                       size_t length,
                       proto_frame_callback_t callback,
                       void *context)
{
    bool any_valid = false;
    size_t i;

    for (i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        switch (parser->state) {
        case PARSE_SOF0:
            if (byte == PROTO_SOF0) {
                parser->state = PARSE_SOF1;
            }
            break;
        case PARSE_SOF1:
            parser->state = (byte == PROTO_SOF1) ? PARSE_COMMAND : PARSE_SOF0;
            if ((byte == PROTO_SOF0) && (parser->state == PARSE_SOF0)) {
                parser->state = PARSE_SOF1;
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
            parser->index = 0;
            if (parser->length > PROTO_MAX_PAYLOAD) {
                proto_parser_init(parser);
            } else {
                parser->state = (parser->length == 0) ? PARSE_CRC_L : PARSE_PAYLOAD;
            }
            break;
        case PARSE_PAYLOAD:
            parser->payload[parser->index++] = byte;
            if (parser->index >= parser->length) {
                parser->state = PARSE_CRC_L;
            }
            break;
        case PARSE_CRC_L:
            parser->received_crc = byte;
            parser->state = PARSE_CRC_H;
            break;
        case PARSE_CRC_H:
            parser->received_crc |= (uint16_t)byte << 8;
            if (check_and_deliver(parser, callback, context)) {
                any_valid = true;
            }
            break;
        default:
            proto_parser_init(parser);
            break;
        }
    }
    return any_valid;
}

size_t proto_pack_frame(uint8_t command,
                        const uint8_t *payload,
                        uint16_t length,
                        uint8_t *output,
                        size_t output_capacity)
{
    size_t frame_size = (size_t)length + 7u;
    uint16_t crc;
    uint16_t i;

    if ((length > PROTO_MAX_PAYLOAD) || (output_capacity < frame_size) ||
        ((length > 0) && (payload == 0))) {
        return 0;
    }
    output[0] = PROTO_SOF0;
    output[1] = PROTO_SOF1;
    output[2] = command;
    output[3] = (uint8_t)length;
    output[4] = (uint8_t)(length >> 8);
    for (i = 0; i < length; ++i) {
        output[5 + i] = payload[i];
    }
    crc = crc16_ccitt_false(&output[2], (size_t)length + 3u);
    output[5 + length] = (uint8_t)crc;
    output[6 + length] = (uint8_t)(crc >> 8);
    return frame_size;
}
