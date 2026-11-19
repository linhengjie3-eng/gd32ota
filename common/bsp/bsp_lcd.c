/*
 * ILI9341 240x320 SPI TFT 裸机驱动 (Bootloader 与 App 共用)
 *
 * 与 ST7789 的关键差异:
 *   - ILI9341 必须走完整的电源/伽马/帧率初始化表, 仅发 SWRESET+SLPOUT
 *     +DISPON 往往点不亮或颜色异常;
 *   - ILI9341 有 RESET 引脚, 上电应先硬件复位再软件复位;
 *   - 竖屏使用 MADCTL 0x48 (MX|BGR), 列地址 0..239 / 页地址 0..319;
 *   - 数据依然是 RGB565, 所以 LVGL 侧 LV_COLOR_16_SWAP 必须为 0。
 *
 * 传输约定:
 *   bsp_lcd_set_window() 发完 0x2C (RAMWR) 后保持 CS=0 / DC=1,
 *   调用方随后必须写像素流, 并以 bsp_lcd_write_pixels() 内部的
 *   LCD_CS_HIGH() 或显式的 CS 拉高结束本次传输。
 */
#include "bsp_lcd.h"
#include "bsp_spi.h"
#include "bsp_board.h"
#include "board_config.h"
#include "gd32f30x.h"
#include <stddef.h>

#define LCD_CS_LOW()        gpio_bit_reset(LCD_CS_GPIO_PORT, LCD_CS_GPIO_PIN)
#define LCD_CS_HIGH()       gpio_bit_set(LCD_CS_GPIO_PORT, LCD_CS_GPIO_PIN)
#define LCD_DC_COMMAND()    gpio_bit_reset(LCD_DC_GPIO_PORT, LCD_DC_GPIO_PIN)
#define LCD_DC_DATA()       gpio_bit_set(LCD_DC_GPIO_PORT, LCD_DC_GPIO_PIN)

#if LCD_BL_ACTIVE_HIGH
#define LCD_BL_ON()         gpio_bit_set(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN)
#define LCD_BL_OFF()        gpio_bit_reset(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN)
#else
#define LCD_BL_ON()         gpio_bit_reset(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN)
#define LCD_BL_OFF()        gpio_bit_set(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN)
#endif

static void lcd_write_command(uint8_t command)
{
    LCD_CS_LOW();
    LCD_DC_COMMAND();
    bsp_spi_write(command);
    LCD_CS_HIGH();
}

static void lcd_write_command_data(uint8_t command, const uint8_t *data,
                                   uint8_t length)
{
    uint8_t i;
    LCD_CS_LOW();
    LCD_DC_COMMAND();
    bsp_spi_write(command);
    if (length > 0u) {
        LCD_DC_DATA();
        for (i = 0u; i < length; ++i) {
            bsp_spi_write(data[i]);
        }
    }
    LCD_CS_HIGH();
}

/* ILI9341 初始化表 (Adafruit/官方参考流程, 适用于绝大多数 SPI 模块) */
typedef struct {
    uint8_t command;
    uint8_t length;
    uint8_t data[15];
    uint8_t delay_ms;
} lcd_init_cmd_t;

static const lcd_init_cmd_t ili9341_init_sequence[] = {
    {0xEF, 3, {0x03, 0x80, 0x02},                                   0},
    {0xCF, 3, {0x00, 0xC1, 0x30},                                   0},
    {0xED, 4, {0x64, 0x03, 0x12, 0x81},                             0},
    {0xE8, 3, {0x85, 0x00, 0x78},                                   0},
    {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02},                       0},
    {0xF7, 1, {0x20},                                                0},
    {0xEA, 2, {0x00, 0x00},                                          0},
    {0xC0, 1, {0x23},                                                0}, /* PWCTR1  */
    {0xC1, 1, {0x10},                                                0}, /* PWCTR2  */
    {0xC5, 2, {0x3E, 0x28},                                          0}, /* VMCTR1  */
    {0xC7, 1, {0x86},                                                0}, /* VMCTR2  */
    {0x36, 1, {LCD_MADCTL},                                          0}, /* MADCTL  */
    {0x3A, 1, {LCD_PIXEL_FORMAT},                                    0}, /* PIXFMT  */
    {0xB1, 2, {0x00, 0x18},                                          0}, /* FRMCTR1 */
    {0xB6, 3, {0x08, 0x82, 0x27},                                   0}, /* DFUNCTR */
    {0xF2, 1, {0x00},                                                0}, /* ENABLE3G */
    {0x26, 1, {0x01},                                                0}, /* GAMMASET */
    {0xE0, 15, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00},          0}, /* PGAMMA */
    {0xE1, 15, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F},          0}, /* NGAMMA */
    {0x11, 0, {0},                                                 120}, /* SLPOUT  */
    {0x29, 0, {0},                                                  20}  /* DISPON  */
};

static void lcd_hardware_reset(void)
{
    /* ILI9341 要求复位低电平 >= 10us, 复位后 >= 120ms 才能发命令 */
    gpio_bit_reset(LCD_RST_GPIO_PORT, LCD_RST_GPIO_PIN);
    bsp_delay_ms(20);
    gpio_bit_set(LCD_RST_GPIO_PORT, LCD_RST_GPIO_PIN);
    bsp_delay_ms(120);
}

void bsp_lcd_init(void)
{
    unsigned i;
    const unsigned count = sizeof(ili9341_init_sequence) /
                           sizeof(ili9341_init_sequence[0]);

    LCD_BL_OFF();
    lcd_hardware_reset();
    bsp_spi_lcd_mode();

    lcd_write_command(0x01);            /* SWRESET */
    bsp_delay_ms(150);

    for (i = 0u; i < count; ++i) {
        const lcd_init_cmd_t *entry = &ili9341_init_sequence[i];
        lcd_write_command_data(entry->command, entry->data, entry->length);
        if (entry->delay_ms > 0u) {
            bsp_delay_ms(entry->delay_ms);
        }
    }
    lcd_write_command(LCD_INVERT_COLORS ? 0x21u : 0x20u); /* INVON / INVOFF */

    bsp_lcd_fill(LCD_COLOR_BLACK);
    LCD_BL_ON();
}

void bsp_lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    x0 += LCD_X_OFFSET;
    x1 += LCD_X_OFFSET;
    y0 += LCD_Y_OFFSET;
    y1 += LCD_Y_OFFSET;

    /* CASET: 列地址 (0..239) */
    LCD_CS_LOW();
    LCD_DC_COMMAND();
    bsp_spi_write(0x2A);
    LCD_DC_DATA();
    data[0] = (uint8_t)(x0 >> 8); data[1] = (uint8_t)x0;
    data[2] = (uint8_t)(x1 >> 8); data[3] = (uint8_t)x1;
    bsp_spi_write_buffer(data, 4);
    LCD_CS_HIGH();

    /* PASET: 页地址 (0..319) */
    LCD_CS_LOW();
    LCD_DC_COMMAND();
    bsp_spi_write(0x2B);
    LCD_DC_DATA();
    data[0] = (uint8_t)(y0 >> 8); data[1] = (uint8_t)y0;
    data[2] = (uint8_t)(y1 >> 8); data[3] = (uint8_t)y1;
    bsp_spi_write_buffer(data, 4);
    LCD_CS_HIGH();

    /* RAMWR: 之后保持 CS=0 / DC=1, 由像素流结束本次传输 */
    LCD_CS_LOW();
    LCD_DC_COMMAND();
    bsp_spi_write(0x2C);
    LCD_DC_DATA();
}

/* 不操作 CS, 供需要分批写像素的调用方复用 */
static void lcd_stream_pixels(const uint16_t *pixels, uint32_t count)
{
    uint32_t i;
    for (i = 0u; i < count; ++i) {
        bsp_spi_write((uint8_t)(pixels[i] >> 8));
        bsp_spi_write((uint8_t)pixels[i]);
    }
}

void bsp_lcd_write_pixels(const uint16_t *pixels, uint32_t count)
{
    lcd_stream_pixels(pixels, count);
    LCD_CS_HIGH();
}

void bsp_lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                       uint16_t height, uint16_t color)
{
    uint32_t remaining;
    uint16_t line[32];
    unsigned i;

    if ((width == 0u) || (height == 0u) ||
        (x >= LCD_WIDTH) || (y >= LCD_HEIGHT)) {
        return;
    }
    if ((uint32_t)x + width > LCD_WIDTH)  width = LCD_WIDTH - x;
    if ((uint32_t)y + height > LCD_HEIGHT) height = LCD_HEIGHT - y;
    remaining = (uint32_t)width * height;
    for (i = 0u; i < 32u; ++i) line[i] = color;

    bsp_lcd_set_window(x, y, (uint16_t)(x + width - 1u),
                       (uint16_t)(y + height - 1u));
    while (remaining > 0u) {
        uint32_t chunk = remaining > 32u ? 32u : remaining;
        lcd_stream_pixels(line, chunk);
        remaining -= chunk;
    }
    LCD_CS_HIGH();
}

void bsp_lcd_fill(uint16_t color)
{
    bsp_lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

static const uint8_t *glyph_for(char ch)
{
    static const uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E}
    };
    static const uint8_t letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
        {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t blank[5];
    static const uint8_t dot[5]     = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t dash[5]    = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t slash[5]   = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t percent[5] = {0x23,0x13,0x08,0x64,0x62};
    static const uint8_t colon[5]   = {0x00,0x36,0x36,0x00,0x00};

    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
    if (ch == '.') return dot;
    if (ch == '-') return dash;
    if (ch == '/') return slash;
    if (ch == '%') return percent;
    if (ch == ':') return colon;
    return blank;
}

void bsp_lcd_draw_char(uint16_t x, uint16_t y, char ch,
                       uint16_t foreground, uint16_t background, uint8_t scale)
{
    const uint8_t *glyph = glyph_for(ch);
    unsigned column, row;

    if (scale == 0u) scale = 1u;
    for (column = 0u; column < 6u; ++column) {
        uint8_t bits = column < 5u ? glyph[column] : 0u;
        for (row = 0u; row < 8u; ++row) {
            uint16_t color = (row < 7u && (bits & (1u << row)))
                             ? foreground : background;
            bsp_lcd_fill_rect((uint16_t)(x + column * scale),
                              (uint16_t)(y + row * scale),
                              scale, scale, color);
        }
    }
}

void bsp_lcd_draw_text(uint16_t x, uint16_t y, const char *text,
                       uint16_t foreground, uint16_t background, uint8_t scale)
{
    if (scale == 0u) scale = 1u;
    while ((*text != '\0') && ((uint32_t)x + 6u * scale <= LCD_WIDTH)) {
        bsp_lcd_draw_char(x, y, *text++, foreground, background, scale);
        x = (uint16_t)(x + 6u * scale);
    }
}

void bsp_lcd_progress(uint16_t x, uint16_t y, uint16_t width,
                      uint16_t height, uint8_t percent, uint16_t color)
{
    char label[5];
    const char *shown;
    uint16_t inner = width - 4u;
    uint16_t filled;

    if (percent > 100u) percent = 100u;
    filled = (uint16_t)((uint32_t)inner * percent / 100u);
    bsp_lcd_fill_rect(x, y, width, 2, LCD_COLOR_WHITE);
    bsp_lcd_fill_rect(x, (uint16_t)(y + height - 2u), width, 2, LCD_COLOR_WHITE);
    bsp_lcd_fill_rect(x, y, 2, height, LCD_COLOR_WHITE);
    bsp_lcd_fill_rect((uint16_t)(x + width - 2u), y, 2, height, LCD_COLOR_WHITE);
    bsp_lcd_fill_rect((uint16_t)(x + 2u), (uint16_t)(y + 2u),
                      inner, (uint16_t)(height - 4u), LCD_COLOR_BLACK);
    if (filled > 0u) {
        bsp_lcd_fill_rect((uint16_t)(x + 2u), (uint16_t)(y + 2u),
                          filled, (uint16_t)(height - 4u), color);
    }
    label[0] = (char)('0' + percent / 100u);
    label[1] = (char)('0' + (percent / 10u) % 10u);
    label[2] = (char)('0' + percent % 10u);
    label[3] = '%';
    label[4] = '\0';
    shown = (percent == 100u) ? label : &label[1];
    bsp_lcd_draw_text((uint16_t)(x + width / 2u - 18u),
                      (uint16_t)(y + height + 8u), shown,
                      LCD_COLOR_WHITE, LCD_COLOR_BLACK, 2);
}
