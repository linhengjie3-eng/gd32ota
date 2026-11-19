#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool esp_online;
    bool update_checked;
    bool update_available;
    uint16_t latest_major;
    uint16_t latest_minor;
} app_state_t;

extern volatile int app_check_update_requested;

void app_state_init(void);
void app_state_set_online(bool online);
void app_state_set_update(uint16_t major, uint16_t minor, bool available);
void app_state_snapshot(app_state_t *snapshot);

#endif
