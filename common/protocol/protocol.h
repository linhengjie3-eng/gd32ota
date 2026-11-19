#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boot_protocol.h"

typedef void (*proto_frame_callback_t)(uint8_t command,
                                       const uint8_t *payload,
                                       uint16_t length,
                                       void *context);

typedef struct {
    uint8_t state;
    uint8_t command;
    uint16_t length;
    uint16_t index;
    uint16_t received_crc;
    uint8_t payload[PROTO_MAX_PAYLOAD];
} proto_parser_t;

void proto_parser_init(proto_parser_t *parser);
bool proto_parser_feed(proto_parser_t *parser,
                       const uint8_t *data,
                       size_t length,
                       proto_frame_callback_t callback,
                       void *context);
size_t proto_pack_frame(uint8_t command,
                        const uint8_t *payload,
                        uint16_t length,
                        uint8_t *output,
                        size_t output_capacity);

#endif
