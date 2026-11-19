#include <stdio.h>
#include "lvgl.h"
#include "gd32f30x.h"
#include "board_config.h"
#include "bsp_board.h"
#include "bsp_touch.h"
#include "app_ui.h"
#include "app_state.h"
#include "memory_map.h"
#include "boot_metadata.h"

static lv_obj_t *main_screen;
static lv_obj_t *update_screen;
static lv_obj_t *network_label;
static lv_obj_t *latest_label;
static lv_obj_t *update_status_label;

#if TOUCH_CALIBRATION_MODE
static lv_obj_t *cal_raw_label;
static lv_obj_t *cal_point_labels[4];
static uint8_t cal_index;
static uint32_t cal_last_ms;
static bool cal_was_pressed;

static lv_obj_t *app_ui_create_calibration(void)
{
    lv_obj_t *screen = lv_obj_create(0);
    lv_obj_t *label;
    unsigned i;

    label = lv_label_create(screen);
    lv_label_set_text(label, "TOUCH CALIBRATION");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);

    label = lv_label_create(screen);
    lv_label_set_text(label, "Tap corners in order:\nTL   TR   BL   BR");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 40);

    cal_raw_label = lv_label_create(screen);
    lv_label_set_text(cal_raw_label, "raw X=---- Y=----");
    lv_obj_set_style_text_font(cal_raw_label, &lv_font_montserrat_20, 0);
    lv_obj_align(cal_raw_label, LV_ALIGN_TOP_MID, 0, 96);

    for (i = 0; i < 4u; ++i) {
        cal_point_labels[i] = lv_label_create(screen);
        lv_label_set_text(cal_point_labels[i], "----  X=---- Y=----");
        lv_obj_align(cal_point_labels[i], LV_ALIGN_TOP_LEFT, 20,
                     (lv_coord_t)(146 + 34 * i));
    }
    return screen;
}

static void app_ui_update_calibration(void)
{
    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    bool pressed = false;
    char text[32];

    bsp_touch_raw_read(&raw_x, &raw_y, &pressed);
    if (pressed) {
        snprintf(text, sizeof(text), "raw X=%u Y=%u", raw_x, raw_y);
        lv_label_set_text(cal_raw_label, text);
        if (!cal_was_pressed && (cal_index < 4u) &&
            ((uint32_t)(bsp_millis() - cal_last_ms) >= 300u)) {
            snprintf(text, sizeof(text), "%u  X=%u Y=%u",
                     (unsigned)(cal_index + 1u), raw_x, raw_y);
            lv_label_set_text(cal_point_labels[cal_index], text);
            cal_index++;
            cal_last_ms = bsp_millis();
        }
    }
    cal_was_pressed = pressed;
}
#endif

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             lv_coord_t y, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 200, 42);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void show_update_screen(lv_event_t *event)
{
    (void)event;
    lv_scr_load(update_screen);
}

static void show_main_screen(lv_event_t *event)
{
    (void)event;
    lv_scr_load(main_screen);
}

static void check_update(lv_event_t *event)
{
    (void)event;
    app_check_update_requested = 1;
    lv_label_set_text(update_status_label, "Checking via ESP32-S3...");
}

static void enter_upgrade(lv_event_t *event)
{
    (void)event;
    lv_label_set_text(update_status_label, "Saving upgrade request...");
    lv_task_handler();
    if (boot_metadata_request_upgrade()) {
        bsp_buzzer_beep();
        NVIC_SystemReset();
    } else {
        lv_label_set_text(update_status_label, "Metadata write failed. Try again.");
    }
}

static void rollback_firmware(lv_event_t *event)
{
    (void)event;
    lv_label_set_text(update_status_label, "Looking for older firmware...");
    lv_task_handler();
    if (boot_metadata_request_rollback(APP_SLOT_ID, APP_VERSION_MAJOR,
                                       APP_VERSION_MINOR)) {
        bsp_buzzer_beep();
        NVIC_SystemReset();
    } else {
        lv_label_set_text(update_status_label,
                          "No older firmware in backup slot.");
    }
}

void app_ui_create(void)
{
    char text[64];
    lv_obj_t *label;

    main_screen = lv_obj_create(0);
    update_screen = lv_obj_create(0);

    label = lv_label_create(main_screen);
    lv_label_set_text(label, "GD32F303 TERMINAL");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);

    snprintf(text, sizeof(text), "Application v%u.%u  Slot %c",
             APP_VERSION_MAJOR, APP_VERSION_MINOR, 'A' + APP_SLOT_ID);
    label = lv_label_create(main_screen);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 54);

    network_label = lv_label_create(main_screen);
    lv_label_set_text(network_label, "ESP32-S3: OFFLINE");
    lv_obj_align(network_label, LV_ALIGN_TOP_MID, 0, 82);

    label = lv_label_create(main_screen);
    lv_label_set_text(label, "Business application\nplaceholder\n\nFreeRTOS + LVGL is running.");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 124);
    make_button(main_screen, "Firmware update", 250, show_update_screen);

    label = lv_label_create(update_screen);
    lv_label_set_text(label, "FIRMWARE UPDATE");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);

    snprintf(text, sizeof(text), "Current: v%u.%u (%c)",
             APP_VERSION_MAJOR, APP_VERSION_MINOR, 'A' + APP_SLOT_ID);
    label = lv_label_create(update_screen);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 48);

    latest_label = lv_label_create(update_screen);
    lv_label_set_text(latest_label, "Latest: not checked");
    lv_obj_align(latest_label, LV_ALIGN_TOP_MID, 0, 76);
    make_button(update_screen, "Check update", 104, check_update);
    make_button(update_screen, "Enter upgrade mode", 148, enter_upgrade);
    make_button(update_screen, "Rollback firmware", 192, rollback_firmware);
    make_button(update_screen, "Back", 236, show_main_screen);

    update_status_label = lv_label_create(update_screen);
    lv_label_set_text(update_status_label, "A/B rollback protects the old image.");
    lv_obj_set_width(update_status_label, 220);
    lv_obj_set_style_text_align(update_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(update_status_label, LV_ALIGN_TOP_MID, 0, 282);

    /* 默认活动屏是 LVGL 自动建的空屏, 必须显式加载主界面, 否则上电白屏 */
    lv_scr_load(main_screen);
#if TOUCH_CALIBRATION_MODE
    lv_scr_load(app_ui_create_calibration());
#endif
}

void app_ui_update(void)
{
    app_state_t state;
    char text[64];
#if TOUCH_CALIBRATION_MODE
    app_ui_update_calibration();
#endif
    app_state_snapshot(&state);
    lv_label_set_text(network_label, state.esp_online ? "ESP32-S3: ONLINE" : "ESP32-S3: OFFLINE");
    if (state.update_checked) {
        snprintf(text, sizeof(text), "Latest: v%u.%u%s", state.latest_major,
                 state.latest_minor, state.update_available ? " (available)" : "");
        lv_label_set_text(latest_label, text);
        if (app_check_update_requested == 0) {
            lv_label_set_text(update_status_label,
                              state.update_available ? "Update available." : "No newer version.");
        }
    }
}
