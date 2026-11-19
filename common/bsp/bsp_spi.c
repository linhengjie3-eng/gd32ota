#include "bsp_spi.h"
#include "board_config.h"
#include "gd32f30x.h"

static void spi_set_prescale(uint32_t prescale)
{
    spi_parameter_struct config;

    spi_disable(BOARD_SPI_PERIPH);
    spi_struct_para_init(&config);
    config.device_mode          = SPI_MASTER;
    config.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    config.frame_size           = SPI_FRAMESIZE_8BIT;
    config.nss                  = SPI_NSS_SOFT;
    config.endian               = SPI_ENDIAN_MSB;
    config.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE; /* mode 0 */
    config.prescale             = prescale;
    spi_init(BOARD_SPI_PERIPH, &config);
    spi_enable(BOARD_SPI_PERIPH);
}

void bsp_spi_init(void)
{
    /* SPI0 在 PA5/PA6/PA7; LCD 控制线 PB0/PB1/PC4/PC5; 触摸走独立软件 SPI */
    rcu_periph_clock_enable(BOARD_SPI_GPIO_RCU);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(BOARD_SPI_RCU);

    gpio_init(BOARD_SPI_GPIO, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ,
              BOARD_SPI_SCK_PIN | BOARD_SPI_MOSI_PIN);
    gpio_init(BOARD_SPI_GPIO, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ,
              BOARD_SPI_MISO_PIN);

    /* 先输出无效电平, 再配置方向, 避免上电瞬间误选中/误复位 */
    gpio_bit_set(LCD_CS_GPIO_PORT,  LCD_CS_GPIO_PIN);
    gpio_bit_set(LCD_RST_GPIO_PORT, LCD_RST_GPIO_PIN);
    gpio_bit_reset(LCD_DC_GPIO_PORT, LCD_DC_GPIO_PIN);
    gpio_bit_reset(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN); /* 背光先关 */

    gpio_init(LCD_CS_GPIO_PORT,  GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_CS_GPIO_PIN);
    gpio_init(LCD_DC_GPIO_PORT,  GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_DC_GPIO_PIN);
    gpio_init(LCD_RST_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ,  LCD_RST_GPIO_PIN);
    gpio_init(LCD_BL_GPIO_PORT,  GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ,  LCD_BL_GPIO_PIN);

    bsp_spi_lcd_mode();
}

void bsp_spi_lcd_mode(void)
{
    /* 120MHz PCLK2 / 8 = 15MHz, ILI9341 写显存典型上限约 20MHz */
    spi_set_prescale(LCD_SPI_PRESCALER);
}

void bsp_spi_touch_mode(void)
{
    /* XPT2046 要求 SCK 不能太快, 约 1MHz 以内 */
    spi_set_prescale(TOUCH_SPI_PRESCALER);
}

uint8_t bsp_spi_transfer(uint8_t data)
{
    while (spi_i2s_flag_get(BOARD_SPI_PERIPH, SPI_FLAG_TBE) == RESET) {}
    spi_i2s_data_transmit(BOARD_SPI_PERIPH, data);
    while (spi_i2s_flag_get(BOARD_SPI_PERIPH, SPI_FLAG_RBNE) == RESET) {}
    data = (uint8_t)spi_i2s_data_receive(BOARD_SPI_PERIPH);
    while (spi_i2s_flag_get(BOARD_SPI_PERIPH, SPI_FLAG_TRANS) != RESET) {}
    return data;
}

void bsp_spi_write(uint8_t data)
{
    (void)bsp_spi_transfer(data);
}

void bsp_spi_write_buffer(const uint8_t *data, uint32_t length)
{
    uint32_t i;
    for (i = 0u; i < length; ++i) {
        bsp_spi_write(data[i]);
    }
}
