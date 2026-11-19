#include "app_state.h"
#include "FreeRTOS.h"
#include "task.h"

static app_state_t state;
volatile int app_check_update_requested;

void app_state_init(void)
{
    state.esp_online = false;
    state.update_checked = false;
    state.update_available = false;
    state.latest_major = 0;
    state.latest_minor = 0;
    app_check_update_requested = 0;
}

void app_state_set_online(bool online)
{
    taskENTER_CRITICAL();
    state.esp_online = online;
    taskEXIT_CRITICAL();
}

void app_state_set_update(uint16_t major, uint16_t minor, bool available)
{
    taskENTER_CRITICAL();
    state.latest_major = major;
    state.latest_minor = minor;
    state.update_available = available;
    state.update_checked = true;
    taskEXIT_CRITICAL();
}

void app_state_snapshot(app_state_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = state;
    taskEXIT_CRITICAL();
}
