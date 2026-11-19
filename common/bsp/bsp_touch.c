#include "bsp_touch.h"
#include "bsp_lcd.h"
#include "board_config.h"
#include "gd32f30x.h"

/*
 * XPT2046 使用独立的软件 SPI (bit-bang): T_CLK/T_DIN/T_OUT 各占一个引脚,
 * 不与 LCD 的 PA5/PA7/PA6 共用, 接线时一根线对应一个排针。
 */
#define TOUCH_CS_LOW()      gpio_bit_reset(TOUCH_CS_GPIO_PORT, TOUCH_CS_GPIO_PIN)
#define TOUCH_CS_HIGH()     gpio_bit_set(TOUCH_CS_GPIO_PORT, TOUCH_CS_GPIO_PIN)
#define TOUCH_CLK_HIGH()    gpio_bit_set(TOUCH_CLK_GPIO_PORT, TOUCH_CLK_GPIO_PIN)
#define TOUCH_CLK_LOW()     gpio_bit_reset(TOUCH_CLK_GPIO_PORT, TOUCH_CLK_GPIO_PIN)
#define TOUCH_DIN_HIGH()    gpio_bit_set(TOUCH_DIN_GPIO_PORT, TOUCH_DIN_GPIO_PIN)
#define TOUCH_DIN_LOW()     gpio_bit_reset(TOUCH_DIN_GPIO_PORT, TOUCH_DIN_GPIO_PIN)
#define TOUCH_OUT_READ()    gpio_input_bit_get(TOUCH_OUT_GPIO_PORT, TOUCH_OUT_GPIO_PIN)
#define TOUCH_IRQ_ACTIVE()  (gpio_input_bit_get(TOUCH_IRQ_GPIO_PORT, \
                                                TOUCH_IRQ_GPIO_PIN) == RESET)

void bsp_touch_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOE);

    /* 先输出无效电平, 避免上电瞬间误发时钟/选中 */
    TOUCH_CS_HIGH();
    TOUCH_CLK_LOW();
    TOUCH_DIN_LOW();

    gpio_init(TOUCH_CS_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, TOUCH_CS_GPIO_PIN);
    gpio_init(TOUCH_CLK_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, TOUCH_CLK_GPIO_PIN);
    gpio_init(TOUCH_DIN_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, TOUCH_DIN_GPIO_PIN);
    gpio_init(TOUCH_OUT_GPIO_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, TOUCH_OUT_GPIO_PIN);
    /*
     * PENIRQ 低有效。XPT2046 的 PENIRQ 多为开漏输出, 用 GD32 内部上拉输入
     * (GPIO_MODE_IPU) 即可省掉外部 4.7k 电阻; 若仍误触发可外接上拉。
     */
    gpio_init(TOUCH_IRQ_GPIO_PORT, GPIO_MODE_IPU,
              GPIO_OSPEED_2MHZ, TOUCH_IRQ_GPIO_PIN);
}

static void touch_delay(void)
{
    __NOP();
    __NOP();
    __NOP();
    __NOP();
}

static uint8_t touch_xfer(uint8_t out)
{
    uint8_t in = 0u;
    unsigned bit;

    for (bit = 0u; bit < 8u; ++bit) {
        if ((out & 0x80u) != 0u) {
            TOUCH_DIN_HIGH();
        } else {
            TOUCH_DIN_LOW();
        }
        out <<= 1;
        touch_delay();
        TOUCH_CLK_HIGH();
        touch_delay();
        in <<= 1;
        if (TOUCH_OUT_READ() != RESET) {
            in |= 1u;
        }
        TOUCH_CLK_LOW();
        touch_delay();
    }
    return in;
}

static uint16_t touch_channel(uint8_t command)
{
    uint16_t value;

    (void)touch_xfer(command);
    value = (uint16_t)touch_xfer(0u) << 8;
    value |= touch_xfer(0u);
    return (uint16_t)((value >> 3) & 0x0FFFu);
}

static uint16_t scale_axis(uint16_t raw, int raw_min, int raw_max,
                           uint16_t output_max, int invert)
{
    uint32_t value;
    if (raw <= raw_min) raw = (uint16_t)raw_min;
    if (raw >= raw_max) raw = (uint16_t)raw_max;
    value = (uint32_t)(raw - raw_min) * output_max / (uint32_t)(raw_max - raw_min);
    if (invert) value = output_max - value;
    return (uint16_t)value;
}

bool bsp_touch_raw_read(uint16_t *raw_x, uint16_t *raw_y, bool *pressed)
{
    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    unsigned i;

    *pressed = false;
    if (!TOUCH_IRQ_ACTIVE()) {
        return true;   /* 未按下 */
    }
    TOUCH_CS_LOW();
    (void)touch_channel(0xD0);
    for (i = 0; i < 5u; ++i) {
        sum_x += touch_channel(0xD0);
        sum_y += touch_channel(0x90);
    }
    TOUCH_CS_HIGH();
    *raw_x = (uint16_t)(sum_x / 5u);
    *raw_y = (uint16_t)(sum_y / 5u);
#if TOUCH_SWAP_XY
    {
        uint16_t temporary = *raw_x;
        *raw_x = *raw_y;
        *raw_y = temporary;
    }
#endif
    *pressed = true;
    return true;
}

bool bsp_touch_read(uint16_t *x, uint16_t *y, bool *pressed)
{
    uint16_t raw_x = 0;
    uint16_t raw_y = 0;

    if (!bsp_touch_raw_read(&raw_x, &raw_y, pressed)) {
        return false;
    }
    if (!*pressed) {
        return true;
    }
    *x = scale_axis(raw_x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX,
                    LCD_WIDTH - 1u, TOUCH_INVERT_X);
    *y = scale_axis(raw_y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX,
                    LCD_HEIGHT - 1u, TOUCH_INVERT_Y);
    return true;
}
