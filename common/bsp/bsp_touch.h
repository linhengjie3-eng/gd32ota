#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

void bsp_touch_init(void);
bool bsp_touch_read(uint16_t *x, uint16_t *y, bool *pressed);
/* 读取未经 TOUCH_RAW_* 缩放的原始坐标, 仅用于触摸校准 */
bool bsp_touch_raw_read(uint16_t *raw_x, uint16_t *raw_y, bool *pressed);

#endif
