#include <stdio.h>
#include <string.h>
#include "gd32f30x.h"
#include "bsp_board.h"
#include "bsp_uart.h"
#include "bsp_spi.h"
#include "bsp_lcd.h"
#include "bsp_flash.h"
#include "boot_metadata.h"
#include "boot_policy.h"
#include "boot_jump.h"
#include "protocol.h"
#include "crc.h"

#define BOOT_IDLE_TIMEOUT_MS        60000u
#define BOOT_DATA_TIMEOUT_MS        30000u
#define ESP_BAUDRATE                115200u

/* 默认只允许同版本重刷或升级，拒绝降级；需要演示 OTA 降级时用
   python tools/build.py --allow-downgrade boot 重新编译 Boot。 */
#ifndef BOOT_ALLOW_DOWNGRADE
#define BOOT_ALLOW_DOWNGRADE        0
#endif

#if !BOOT_ALLOW_DOWNGRADE
/* 返回 true 表示这是"严格降级"（新版本 < 当前版本）。同版本返回 false，
   以便 --force-bad-crc 故障注入和 A/B 反复重刷仍能进行。 */
static bool version_is_downgrade(uint16_t new_major, uint16_t new_minor,
                                 uint16_t cur_major, uint16_t cur_minor)
{
    if (new_major != cur_major) return new_major < cur_major;
    return new_minor < cur_minor;
}

/* 从 Flash 重新读元数据，取已确认槽 / 待确认槽里的最高版本。
   不依赖 context.metadata —— 它在升级过程中会被 mark_target_invalid() 改写。 */
static void installed_version(uint16_t *major, uint16_t *minor)
{
    boot_metadata_t meta;
    uint16_t m = 0u, n = 0u;
    unsigned i;

    boot_metadata_load(&meta);
    for (i = 0u; i < 2u; ++i) {
        const slot_info_t *s = &meta.slots[i];
        if ((s->state != SLOT_STATE_CONFIRMED) &&
            (s->state != SLOT_STATE_PENDING)) {
            continue;
        }
        if ((s->version_major > m) ||
            ((s->version_major == m) && (s->version_minor > n))) {
            m = s->version_major;
            n = s->version_minor;
        }
    }
    *major = m;
    *minor = n;
}
#endif

typedef enum {
    RX_WAIT_HEADER = 0,
    RX_RECEIVE_DATA,
    RX_ERROR
} rx_state_t;

typedef struct {
    rx_state_t rx_state;
    uint8_t target_slot;
    uint16_t expected_sequence;
    uint32_t received_bytes;
    uint32_t last_activity;
    uint8_t last_percent;
    FirmwareHeader_t header;
    boot_metadata_t metadata;
} boot_context_t;

static proto_parser_t parser;
static boot_context_t context;

static void send_frame(uint8_t command, const uint8_t *payload, uint16_t length)
{
    uint8_t frame[PROTO_MAX_PAYLOAD + 7u];
    size_t size = proto_pack_frame(command, payload, length, frame, sizeof(frame));
    if (size > 0) {
        bsp_esp_uart_write(frame, size);
    }
}

static void send_ack(uint16_t sequence, uint8_t status)
{
    uint8_t payload[3];
    payload[0] = (uint8_t)sequence;
    payload[1] = (uint8_t)(sequence >> 8);
    payload[2] = status;
    send_frame(CMD_PACKET_ACK, payload, sizeof(payload));
}

static void send_handshake_response(void)
{
    uint8_t payload[12];
    uint8_t active = context.metadata.active_slot;
    uint16_t major = 0;
    uint16_t minor = 0;

    if (active <= SLOT_ID_B) {
        major = context.metadata.slots[active].version_major;
        minor = context.metadata.slots[active].version_minor;
    }
    payload[0] = (uint8_t)PROTOCOL_VERSION;
    payload[1] = (uint8_t)(PROTOCOL_VERSION >> 8);
    payload[2] = (context.rx_state == RX_RECEIVE_DATA) ? DEVICE_STATE_BOOT_RECEIVE
                                                       : DEVICE_STATE_BOOT_WAIT;
    payload[3] = context.target_slot;
    payload[4] = (uint8_t)major;
    payload[5] = (uint8_t)(major >> 8);
    payload[6] = (uint8_t)minor;
    payload[7] = (uint8_t)(minor >> 8);
    payload[8] = BOOT_VERSION_MAJOR;
    payload[9] = 0;
    payload[10] = BOOT_VERSION_MINOR;
    payload[11] = 0;
    send_frame(CMD_HANDSHAKE_RESP, payload, sizeof(payload));
}

static void update_progress(void)
{
    uint8_t percent = 0;
    if (context.header.firmware_size > 0) {
        percent = (uint8_t)((context.received_bytes * 100u) /
                            context.header.firmware_size);
    }
    if (percent != context.last_percent) {
        context.last_percent = percent;
        bsp_lcd_progress(20, 220, 200, 18, percent, LCD_COLOR_GREEN);
        printf("OTA progress: %u%%\r\n", (unsigned)percent);
    }
}

static void mark_target_invalid(void)
{
    boot_metadata_load(&context.metadata);
    context.metadata.slots[context.target_slot].state = SLOT_STATE_INVALID;
    context.metadata.pending_slot = SLOT_ID_NONE;
    context.metadata.request_upgrade = 1;
    context.metadata.trial_count = 0;
    boot_metadata_save(&context.metadata);
}

static void handle_header(const uint8_t *payload, uint16_t length)
{
    FirmwareHeader_t header;
    uint32_t base;

    if ((context.rx_state != RX_WAIT_HEADER) && (context.rx_state != RX_ERROR)) {
        send_ack(PROTO_HEADER_ACK_SEQ, ACK_STATE_ERROR);
        return;
    }
    if (length != sizeof(FirmwareHeader_t)) {
        send_ack(PROTO_HEADER_ACK_SEQ, ACK_LENGTH_ERROR);
        return;
    }
    memcpy(&header, payload, sizeof(header));
    if ((header.magic != FW_MAGIC) || (header.firmware_size < 8u) ||
        (header.firmware_size > slot_capacity(context.target_slot))) {
        send_ack(PROTO_HEADER_ACK_SEQ, ACK_LENGTH_ERROR);
        return;
    }

    context.header = header;
    context.received_bytes = 0;
    context.expected_sequence = 0;
    context.last_percent = 0xFFu;
    context.target_slot = boot_metadata_choose_download_slot(&context.metadata);
    if (header.firmware_size > slot_capacity(context.target_slot)) {
        send_ack(PROTO_HEADER_ACK_SEQ, ACK_LENGTH_ERROR);
        return;
    }

#if !BOOT_ALLOW_DOWNGRADE
    /* 擦除目标槽之前拦截降级：一旦擦了槽，旧固件就只剩另一个槽这一份了。 */
    {
        uint16_t cur_major, cur_minor;
        installed_version(&cur_major, &cur_minor);
        if (version_is_downgrade(header.version_major, header.version_minor,
                                 cur_major, cur_minor)) {
            printf("Reject v%u.%u (downgrade from installed v%u.%u)\r\n",
                   (unsigned)header.version_major,
                   (unsigned)header.version_minor,
                   (unsigned)cur_major, (unsigned)cur_minor);
            bsp_lcd_draw_text(20, 170, "DOWNGRADE BLOCKED", LCD_COLOR_RED,
                              LCD_COLOR_BLACK, 2);
            send_ack(PROTO_HEADER_ACK_SEQ, ACK_VERSION_ERROR);
            context.rx_state = RX_WAIT_HEADER;
            return;
        }
    }
#endif

    bsp_lcd_draw_text(20, 170, "ERASING SLOT ...", LCD_COLOR_YELLOW,
                      LCD_COLOR_BLACK, 2);
    printf("Erase slot %c, %lu bytes\r\n", 'A' + context.target_slot,
           (unsigned long)header.firmware_size);
    mark_target_invalid();
    base = slot_base_address(context.target_slot);
    if (!bsp_flash_erase(base, header.firmware_size)) {
        context.rx_state = RX_ERROR;
        send_ack(PROTO_HEADER_ACK_SEQ, ACK_FLASH_ERROR);
        bsp_buzzer_failure();
        return;
    }

    context.rx_state = RX_RECEIVE_DATA;
    bsp_lcd_draw_text(20, 170, "RECEIVING ...    ", LCD_COLOR_WHITE,
                      LCD_COLOR_BLACK, 2);
    update_progress();
    send_ack(PROTO_HEADER_ACK_SEQ, ACK_OK);
}

static void handle_data(const uint8_t *payload, uint16_t length)
{
    uint16_t sequence;
    uint16_t data_length;
    uint32_t offset;
    uint32_t base;

    if (context.rx_state != RX_RECEIVE_DATA) {
        send_ack(PROTO_HEADER_ACK_SEQ, ACK_STATE_ERROR);
        return;
    }
    if (length < 3u) {
        send_ack(PROTO_HEADER_ACK_SEQ, ACK_LENGTH_ERROR);
        return;
    }
    sequence = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    data_length = length - 2u;
    if (data_length > PROTO_DATA_CHUNK) {
        send_ack(sequence, ACK_LENGTH_ERROR);
        return;
    }

    if ((context.expected_sequence > 0) &&
        (sequence == (uint16_t)(context.expected_sequence - 1u))) {
        send_ack(sequence, ACK_OK); /* Lost ACK: acknowledge the duplicate. */
        return;
    }
    if (sequence != context.expected_sequence) {
        send_ack(sequence, ACK_SEQUENCE_ERROR);
        return;
    }
    offset = (uint32_t)sequence * PROTO_DATA_CHUNK;
    if ((offset != context.received_bytes) ||
        (offset + data_length > context.header.firmware_size) ||
        ((offset + data_length < context.header.firmware_size) &&
         (data_length != PROTO_DATA_CHUNK))) {
        send_ack(sequence, ACK_LENGTH_ERROR);
        return;
    }

    base = slot_base_address(context.target_slot);
    if (!bsp_flash_write(base + offset, &payload[2], data_length)) {
        context.rx_state = RX_ERROR;
        send_ack(sequence, ACK_FLASH_ERROR);
        return;
    }
    context.received_bytes += data_length;
    context.expected_sequence++;
    update_progress();
    send_ack(sequence, ACK_OK);
}

static void send_verify_result(uint8_t status)
{
    uint8_t payload[6];
    payload[0] = status;
    payload[1] = context.target_slot;
    payload[2] = (uint8_t)context.header.version_major;
    payload[3] = (uint8_t)(context.header.version_major >> 8);
    payload[4] = (uint8_t)context.header.version_minor;
    payload[5] = (uint8_t)(context.header.version_minor >> 8);
    send_frame(CMD_VERIFY_RESULT, payload, sizeof(payload));
}

static void handle_end(void)
{
    slot_info_t *slot;
    uint8_t result = VERIFY_OK;

    if ((context.rx_state != RX_RECEIVE_DATA) ||
        (context.received_bytes != context.header.firmware_size)) {
        send_verify_result(VERIFY_SIZE_FAILED);
        return;
    }
    slot = &context.metadata.slots[context.target_slot];
    slot->size = context.header.firmware_size;
    slot->crc32 = context.header.crc32;
    slot->version_major = context.header.version_major;
    slot->version_minor = context.header.version_minor;
    slot->build_time = context.header.build_time;
    slot->state = SLOT_STATE_PENDING;
    memset(slot->reserved, 0, sizeof(slot->reserved));

    if (!boot_image_validate(context.target_slot, slot)) {
        uint32_t crc = crc32_compute((const void *)slot_base_address(context.target_slot),
                                     slot->size);
        result = (crc == slot->crc32) ? VERIFY_VECTOR_FAILED : VERIFY_CRC_FAILED;
    }
    if (result == VERIFY_OK) {
        context.metadata.active_slot = (context.target_slot == SLOT_ID_A) ? SLOT_ID_B : SLOT_ID_A;
        context.metadata.pending_slot = context.target_slot;
        context.metadata.request_upgrade = 0;
        context.metadata.trial_count = 0;
        if (!boot_metadata_save(&context.metadata)) {
            result = VERIFY_FLASH_FAILED;
        }
    }
    if (result != VERIFY_OK) {
        mark_target_invalid();
        context.rx_state = RX_ERROR;
        send_verify_result(result);
        bsp_buzzer_failure();
        bsp_lcd_draw_text(20, 170, "VERIFY FAILED    ", LCD_COLOR_RED,
                          LCD_COLOR_BLACK, 2);
        return;
    }

    send_verify_result(VERIFY_OK);
    bsp_lcd_draw_text(20, 170, "VERIFY OK        ", LCD_COLOR_GREEN,
                      LCD_COLOR_BLACK, 2);
    bsp_buzzer_success();
    printf("OTA success, reset into trial slot %c\r\n", 'A' + context.target_slot);
    bsp_delay_ms(300);
    NVIC_SystemReset();
}

static void frame_received(uint8_t command, const uint8_t *payload,
                           uint16_t length, void *user)
{
    (void)user;
    /* 只有升级帧才算“有进展”。ESP32 每 2s 发一次 CMD_VERSION_QUERY，
       若它也刷新计时，Boot 的保底超时会被永远喂活，收不到固件也不会回跳 App。 */
    if ((command == CMD_FIRMWARE_HEADER) || (command == CMD_FIRMWARE_DATA) ||
        (command == CMD_FIRMWARE_END)) {
        context.last_activity = bsp_millis();
    }
    switch (command) {
    case CMD_HANDSHAKE:
    case CMD_VERSION_QUERY:
        send_handshake_response();
        break;
    case CMD_FIRMWARE_HEADER:
        handle_header(payload, length);
        break;
    case CMD_FIRMWARE_DATA:
        handle_data(payload, length);
        break;
    case CMD_FIRMWARE_END:
        handle_end();
        break;
    default:
        break;
    }
}

static void show_boot_screen(void)
{
    char line[40];
    snprintf(line, sizeof(line), "BOOT v%u.%u", BOOT_VERSION_MAJOR, BOOT_VERSION_MINOR);
    bsp_lcd_fill(LCD_COLOR_BLACK);
    bsp_lcd_draw_text(36, 28, "GD32F303 A/B OTA", LCD_COLOR_BLUE, LCD_COLOR_BLACK, 2);
    bsp_lcd_draw_text(60, 62, line, LCD_COLOR_WHITE, LCD_COLOR_BLACK, 2);
    bsp_lcd_draw_text(20, 170, "WAITING FIRMWARE", LCD_COLOR_YELLOW, LCD_COLOR_BLACK, 2);
    bsp_lcd_progress(20, 220, 200, 18, 0, LCD_COLOR_GREEN);
}

static int try_boot_application(void)
{
    uint8_t selected;

    if ((context.metadata.pending_slot <= SLOT_ID_B) &&
        (context.metadata.trial_count >= BOOT_MAX_TRIALS)) {
        uint8_t failed = context.metadata.pending_slot;
        context.metadata.slots[failed].state = SLOT_STATE_INVALID;
        context.metadata.pending_slot = SLOT_ID_NONE;
        context.metadata.trial_count = 0;
        boot_metadata_save(&context.metadata);
        bsp_buzzer_failure();
        printf("Trial limit reached, rollback to slot %c\r\n",
               'A' + context.metadata.active_slot);
    }

    for (;;) {
        selected = boot_policy_select(&context.metadata);
        if (selected == SLOT_ID_NONE) {
            return -1;
        }
        if (!boot_image_validate(selected, &context.metadata.slots[selected])) {
            printf("Slot %c image invalid\r\n", 'A' + selected);
            context.metadata.slots[selected].state = SLOT_STATE_INVALID;
            if (context.metadata.pending_slot == selected) {
                context.metadata.pending_slot = SLOT_ID_NONE;
                context.metadata.trial_count = 0;
            }
            boot_metadata_save(&context.metadata);
            continue;
        }
        break;
    }

    if (context.metadata.pending_slot == selected) {
        context.metadata.trial_count++;
        if (!boot_metadata_save(&context.metadata)) {
            return -1;
        }
    }
    printf("Jump slot %c, v%u.%u, %lu bytes\r\n", 'A' + selected,
           (unsigned)context.metadata.slots[selected].version_major,
           (unsigned)context.metadata.slots[selected].version_minor,
           (unsigned long)context.metadata.slots[selected].size);
    return boot_jump_to(slot_base_address(selected), context.metadata.slots[selected].size);
}

int main(void)
{
    uint8_t input[64];
    size_t count;

    bsp_board_init();
    bsp_time_init();
    bsp_debug_uart_init(115200);
    bsp_esp_uart_init(ESP_BAUDRATE);
    bsp_buzzer_init();
    bsp_spi_init();
    bsp_lcd_init();
    proto_parser_init(&parser);
    boot_metadata_load(&context.metadata);
    context.target_slot = boot_metadata_choose_download_slot(&context.metadata);
    context.rx_state = RX_WAIT_HEADER;
    context.last_activity = bsp_millis();

    printf("\r\nGD32F303 Boot %u.%u, A/B OTA\r\n", BOOT_VERSION_MAJOR, BOOT_VERSION_MINOR);
    if (!context.metadata.request_upgrade) {
        if (try_boot_application() == -2) {
            printf("Application returned unexpectedly\r\n");
        }
    }

    show_boot_screen();
    bsp_buzzer_beep();
    for (;;) {
        count = bsp_esp_uart_read(input, sizeof(input));
        if (count > 0) {
            proto_parser_feed(&parser, input, count, frame_received, 0);
        }
        if ((context.rx_state == RX_WAIT_HEADER) || (context.rx_state == RX_ERROR)) {
            if ((uint32_t)(bsp_millis() - context.last_activity) > BOOT_IDLE_TIMEOUT_MS) {
                if ((context.metadata.active_slot <= SLOT_ID_B) &&
                    boot_image_validate(context.metadata.active_slot,
                                        &context.metadata.slots[context.metadata.active_slot])) {
                    context.metadata.request_upgrade = 0;
                    boot_metadata_save(&context.metadata);
                    printf("Upgrade timeout, return to previous application\r\n");
                    NVIC_SystemReset();
                }
                context.last_activity = bsp_millis();
            }
        } else if (context.rx_state == RX_RECEIVE_DATA) {
            /* 传输中途 ESP32 掉线/断网：不能无限期停在 RECEIVING。 */
            if ((uint32_t)(bsp_millis() - context.last_activity) > BOOT_DATA_TIMEOUT_MS) {
                printf("Transfer timeout, invalidate target slot %c\r\n",
                       'A' + context.target_slot);
                mark_target_invalid();
                context.rx_state = RX_WAIT_HEADER;
                context.last_activity = bsp_millis();
                bsp_lcd_draw_text(20, 170, "TRANSFER TIMEOUT", LCD_COLOR_RED,
                                  LCD_COLOR_BLACK, 2);
            }
        }
    }
}
