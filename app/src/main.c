#include <stdio.h>
#include <string.h>
#include "gd32f30x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_board.h"
#include "bsp_uart.h"
#include "bsp_spi.h"
#include "bsp_lcd.h"
#include "bsp_touch.h"
#include "protocol.h"
#include "crc.h"
#include "boot_metadata.h"
#include "memory_map.h"
#include "app_state.h"
#include "app_ui.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
/* Keil MDK / ARM Compiler: 符号由 scatter 文件生成, 见 keil/README.md */
extern uint32_t Image$$ER_IROM1$$Limit;
#define APP_IMAGE_END       ((uint32_t)&Image$$ER_IROM1$$Limit)
#else
/* GCC / GNU ld: 在 linker/app.ld 中定义 */
extern uint32_t __app_image_end;
#define APP_IMAGE_END       ((uint32_t)&__app_image_end)
#endif
static proto_parser_t comm_parser;
static TickType_t last_esp_frame;

static void app_send_frame(uint8_t command, const uint8_t *payload, uint16_t length)
{
    uint8_t frame[PROTO_MAX_PAYLOAD + 7u];
    size_t size = proto_pack_frame(command, payload, length, frame, sizeof(frame));
    if (size > 0) {
        bsp_esp_uart_write(frame, size);
    }
}

static void app_send_version(void)
{
    uint8_t payload[12] = {0};
    uint8_t target = (APP_SLOT_ID == SLOT_ID_A) ? SLOT_ID_B : SLOT_ID_A;
    payload[0] = (uint8_t)PROTOCOL_VERSION;
    payload[1] = (uint8_t)(PROTOCOL_VERSION >> 8);
    payload[2] = DEVICE_STATE_APP;
    payload[3] = target;
    payload[4] = APP_VERSION_MAJOR;
    payload[6] = APP_VERSION_MINOR;
    payload[8] = BOOT_VERSION_MAJOR;
    payload[10] = BOOT_VERSION_MINOR;
    app_send_frame(CMD_HANDSHAKE_RESP, payload, sizeof(payload));
}

static void app_frame_received(uint8_t command, const uint8_t *payload,
                               uint16_t length, void *context)
{
    (void)context;
    last_esp_frame = xTaskGetTickCount();
    switch (command) {
    case CMD_HANDSHAKE:
    case CMD_VERSION_QUERY:
        app_send_version();
        break;
    case CMD_UPDATE_CHECK_RESP:
        if (length >= 5u) {
            uint16_t major = (uint16_t)payload[1] | ((uint16_t)payload[2] << 8);
            uint16_t minor = (uint16_t)payload[3] | ((uint16_t)payload[4] << 8);
            app_state_set_update(major, minor, payload[0] != 0u);
        }
        break;
    default:
        break;
    }
}

static void comm_task(void *argument)
{
    uint8_t input[64];
    TickType_t last_wake = xTaskGetTickCount();
    (void)argument;
    proto_parser_init(&comm_parser);
    last_esp_frame = last_wake;
    for (;;) {
        size_t count = bsp_esp_uart_read(input, sizeof(input));
        if (count > 0) {
            proto_parser_feed(&comm_parser, input, count, app_frame_received, 0);
        }
        app_state_set_online((xTaskGetTickCount() - last_esp_frame) < pdMS_TO_TICKS(4000));
        if (app_check_update_requested) {
            app_send_frame(CMD_UPDATE_CHECK, 0, 0);
            app_check_update_requested = 0;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));
    }
}

static void gui_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    (void)argument;
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    app_ui_create();
    for (;;) {
        lv_tick_inc(10);
        app_ui_update();
        lv_task_handler();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}

static void confirm_task(void *argument)
{
    boot_metadata_t metadata;
    uint32_t image_size = APP_IMAGE_END - APP_BASE_ADDR;
    (void)argument;

    /* Delay confirmation until scheduler, UART, LCD, touch and LVGL are alive. */
    vTaskDelay(pdMS_TO_TICKS(5000));
    boot_metadata_load(&metadata);
    if ((metadata.active_slot != APP_SLOT_ID) ||
        (metadata.slots[APP_SLOT_ID].state != SLOT_STATE_CONFIRMED) ||
        (metadata.slots[APP_SLOT_ID].size != image_size)) {
        slot_info_t *slot = &metadata.slots[APP_SLOT_ID];
        memset(slot, 0, sizeof(*slot));
        slot->size = image_size;
        slot->crc32 = crc32_compute((const void *)APP_BASE_ADDR, image_size);
        slot->version_major = APP_VERSION_MAJOR;
        slot->version_minor = APP_VERSION_MINOR;
        slot->state = SLOT_STATE_CONFIRMED;
        metadata.active_slot = APP_SLOT_ID;
        metadata.pending_slot = SLOT_ID_NONE;
        metadata.request_upgrade = 0;
        metadata.trial_count = 0;
    }
    if (boot_metadata_confirm_running(APP_SLOT_ID)) {
        printf("Application slot %c confirmed\r\n", 'A' + APP_SLOT_ID);
    } else {
        printf("Warning: application confirmation failed\r\n");
    }
    vTaskDelete(0);
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    printf("FreeRTOS heap exhausted\r\n");
    for (;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    taskDISABLE_INTERRUPTS();
    printf("Stack overflow: %s\r\n", name);
    for (;;) {}
}

int main(void)
{
    nvic_vector_table_set(NVIC_VECTTAB_FLASH, APP_VECTOR_OFFSET);
    bsp_board_init();
    bsp_debug_uart_init(115200);
    bsp_esp_uart_init(115200);
    bsp_buzzer_init();
    bsp_spi_init();
    bsp_lcd_init();
    bsp_touch_init();
    app_state_init();

    printf("\r\nGD32F303 App %u.%u, slot %c\r\n",
           APP_VERSION_MAJOR, APP_VERSION_MINOR, 'A' + APP_SLOT_ID);
    xTaskCreate(gui_task, "GuiTask", 2048, 0, tskIDLE_PRIORITY + 3, 0);
    xTaskCreate(comm_task, "CommTask", 1024, 0, tskIDLE_PRIORITY + 1, 0);
    /* 确认任务优先级必须高于 GUI: GUI 在 LVGL 刷新耗时长时会一直 taskYIELD,
       低优先级任务会被无限期饿死, 导致新槽永远无法标记为 CONFIRMED */
    xTaskCreate(confirm_task, "Confirm", 512, 0, tskIDLE_PRIORITY + 4, 0);
    vTaskStartScheduler();
    printf("Scheduler failed to start\r\n");
    for (;;) {}
}
