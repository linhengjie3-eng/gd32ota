#ifndef BSP_LCD_H
#define BSP_LCD_H

#include <stdint.h>

/*
 * ILI9341 240x320 竖屏, RGB565。
 * 颜色宏是标准 RGB565; ILI9341 的 MADCTL BGR 位用于适配面板子像素
 * 排列, 不改变 MCU 侧数据格式, 因此 LVGL 的 LV_COLOR_16_SWAP 必须为 0。
 * 若实测红蓝互换, 优先改 config/board_config.h 的 LCD_MADCTL(BGR 位),
 * 不要改这里的颜色宏。
 */

#define LCD_WIDTH                   240u
#define LCD_HEIGHT                  320u
#define LCD_COLOR_BLACK             0x0000u
#define LCD_COLOR_WHITE             0xFFFFu
#define LCD_COLOR_BLUE              0x001Fu
#define LCD_COLOR_GREEN             0x07E0u
#define LCD_COLOR_RED               0xF800u
#define LCD_COLOR_YELLOW            0xFFE0u
#define LCD_COLOR_GRAY              0x8410u

void bsp_lcd_init(void);
void bsp_lcd_fill(uint16_t color);
void bsp_lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                       uint16_t height, uint16_t color);
void bsp_lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void bsp_lcd_write_pixels(const uint16_t *pixels, uint32_t count);
void bsp_lcd_draw_char(uint16_t x, uint16_t y, char ch,
                       uint16_t foreground, uint16_t background, uint8_t scale);
void bsp_lcd_draw_text(uint16_t x, uint16_t y, const char *text,
                       uint16_t foreground, uint16_t background, uint8_t scale);
void bsp_lcd_progress(uint16_t x, uint16_t y, uint16_t width,
                      uint16_t height, uint8_t percent, uint16_t color);

#endif
