#include "lvgl.h"
#include "lv_port_indev.h"
#include "bsp_touch.h"

static lv_indev_drv_t input_driver;

static void touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;
    (void)driver;
    if (bsp_touch_read(&x, &y, &pressed)) {
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
        data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void lv_port_indev_init(void)
{
    lv_indev_drv_init(&input_driver);
    input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = touch_read;
    lv_indev_drv_register(&input_driver);
}
