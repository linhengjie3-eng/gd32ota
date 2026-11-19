#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdint.h>

void bsp_board_init(void);
void bsp_time_init(void);
void bsp_delay_ms(uint32_t milliseconds);
uint32_t bsp_millis(void);
void bsp_buzzer_init(void);
void bsp_buzzer_set(int enabled);
void bsp_buzzer_success(void);
void bsp_buzzer_failure(void);
void bsp_buzzer_beep(void);

#endif
