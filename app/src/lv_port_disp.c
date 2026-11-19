#include "lvgl.h"
#include "lv_port_disp.h"
#include "bsp_lcd.h"

static lv_disp_draw_buf_t draw_buffer;
static lv_color_t draw_pixels[LCD_WIDTH * 20u];
static lv_disp_drv_t display_driver;

static void display_flush(lv_disp_drv_t *driver, const lv_area_t *area,
                          lv_color_t *colors)
{
    uint32_t count = (uint32_t)(area->x2 - area->x1 + 1) *
                     (uint32_t)(area->y2 - area->y1 + 1);
    bsp_lcd_set_window((uint16_t)area->x1, (uint16_t)area->y1,
                       (uint16_t)area->x2, (uint16_t)area->y2);
    bsp_lcd_write_pixels((const uint16_t *)colors, count);
    lv_disp_flush_ready(driver);
}

void lv_port_disp_init(void)
{
    lv_disp_draw_buf_init(&draw_buffer, draw_pixels, 0, LCD_WIDTH * 20u);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = LCD_WIDTH;
    display_driver.ver_res = LCD_HEIGHT;
    display_driver.flush_cb = display_flush;
    display_driver.draw_buf = &draw_buffer;
    lv_disp_drv_register(&display_driver);
}
