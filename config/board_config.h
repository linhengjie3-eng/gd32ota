#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/*
 * ============================================================================
 * GD32F303VGT6 (LQFP100) 板级配置 —— 本项目的引脚由本文件统一定义
 * ----------------------------------------------------------------------------
 * 芯片: GD32F303VGT6, Cortex-M4 @120MHz, Flash 1MiB, SRAM 96KiB
 * 逻辑电平: 全部 3.3V; ESP32-S3 与 GD32 必须共地、TX/RX 交叉
 *
 * 引脚总表 (LQFP100):
 *   PA9  / USART0_TX   调试日志输出 -> USB-TTL RX
 *   PA10 / USART0_RX   调试日志输入 <- USB-TTL TX
 *   PA2  / USART1_TX   -> ESP32-S3 RX (交叉)
 *   PA3  / USART1_RX   <- ESP32-S3 TX (交叉)
 *   PA5  / SPI0_SCK    -> LCD SCK
 *   PA6  / SPI0_MISO   不接 (LCD 只写不读; 触摸已改为独立软件 SPI)
 *   PA7  / SPI0_MOSI   -> LCD SDI
 *   PB0  / GPIO        LCD CS   (低有效)
 *   PB1  / GPIO        LCD D/C  (0=命令, 1=数据)
 *   PC4  / GPIO        LCD RESET(低有效); 若模块无 RST 引脚可悬空
 *   PC5  / GPIO        LCD LED/背光 (高电平亮)
 *   PC6  / GPIO        TOUCH CS (低有效)
 *   PC7  / GPIO        TOUCH PENIRQ (低有效, 建议外部 4.7k 上拉到 3.3V)
 *   PD5  / GPIO        TOUCH T_CLK (软件 SPI)
 *   PD7  / GPIO        TOUCH T_DIN (软件 SPI)
 *   PE0  / GPIO        TOUCH T_OUT (软件 SPI)
 *   PB8  / GPIO        蜂鸣器控制 (0=响, 1=停, 低电平触发)
 *   PA13 / SWDIO       保留给调试器, 严禁复用
 *   PA14 / SWCLK       保留给调试器, 严禁复用
 *   BOOT0(PB? 见数据手册) 保持 0 (从主 Flash 启动)
 *
 * 注意: KMRTM28028-SPI 屏幕若排针顺序与上面不同, 只需改本文件宏,
 *       不要改各 .c 里的逻辑。触摸 IC 默认按 XPT2046(兼容 TSC2046) 实现。
 * ============================================================================
 */

/* ---------------------------------------------------------------- 系统时钟 */
#define BOARD_MCU_NAME                  "GD32F303VGT6"
#define BOARD_HXTAL_HZ                  8000000u    /* 板上外部晶振 8MHz */
#define BOARD_SYSCLK_HZ                 120000000u  /* system_gd32f30x.c 配置 */
#define BOARD_DEBUG_UART_BAUD           115200u
#define BOARD_ESP_UART_BAUD             115200u

/* ------------------------------------------------------------------ UART */
/* 宏只做文本替换; 使用它们的 .c 必须先 #include "gd32f30x.h" */
#define DEBUG_UART_PERIPH               USART0
#define DEBUG_UART_RCU                  RCU_USART0
#define DEBUG_UART_GPIO                 GPIOA
#define DEBUG_UART_GPIO_RCU             RCU_GPIOA
#define DEBUG_UART_TX_PIN               GPIO_PIN_9
#define DEBUG_UART_RX_PIN               GPIO_PIN_10

#define ESP_UART_PERIPH                 USART1
#define ESP_UART_RCU                    RCU_USART1
#define ESP_UART_IRQ                    USART1_IRQn
#define ESP_UART_GPIO                   GPIOA
#define ESP_UART_GPIO_RCU               RCU_GPIOA
#define ESP_UART_TX_PIN                 GPIO_PIN_2
#define ESP_UART_RX_PIN                 GPIO_PIN_3

/* ------------------------------------------------------------- SPI0 总线 */
#define BOARD_SPI_PERIPH                SPI0
#define BOARD_SPI_RCU                   RCU_SPI0
#define BOARD_SPI_GPIO                  GPIOA
#define BOARD_SPI_GPIO_RCU              RCU_GPIOA
#define BOARD_SPI_SCK_PIN               GPIO_PIN_5
#define BOARD_SPI_MISO_PIN              GPIO_PIN_6
#define BOARD_SPI_MOSI_PIN              GPIO_PIN_7
#define LCD_SPI_PRESCALER               SPI_PSC_8    /* 120MHz/8 = 15MHz */
#define TOUCH_SPI_PRESCALER             SPI_PSC_128  /* 约 937.5kHz */

/* ------------------------------------------------------- LCD: ILI9341 */
#define LCD_DRIVER_IC                   "ILI9341"
#define LCD_CS_GPIO_PORT                GPIOB
#define LCD_CS_GPIO_PIN                 GPIO_PIN_0
#define LCD_DC_GPIO_PORT                GPIOB
#define LCD_DC_GPIO_PIN                 GPIO_PIN_1
#define LCD_RST_GPIO_PORT               GPIOC
#define LCD_RST_GPIO_PIN                GPIO_PIN_4
#define LCD_BL_GPIO_PORT                GPIOC
#define LCD_BL_GPIO_PIN                 GPIO_PIN_5

/*
 * ILI9341 MADCTL (0x36) 位定义:
 *   D7 MY=0x80  D6 MX=0x40  D5 MV=0x20  D4 ML=0x10  D3 BGR=0x08  D2 MH=0x04
 * 竖屏 240x320 常用 0x48 (MX | BGR): X 镜像 + 适配 BGR 子像素面板。
 * 若出现左右镜像, 去掉 MX 改用 0x08; 若红蓝互换, 去掉 BGR 改用 0x40
 * (或保留 0x48 并在 bsp_lcd.h 中交换颜色宏)。RGB565 数据顺序始终不变,
 * 因此 LVGL 的 LV_COLOR_16_SWAP 必须保持 0。
 */
#define LCD_X_OFFSET                    0
#define LCD_Y_OFFSET                    0
#define LCD_MADCTL                      0x48u
#define LCD_PIXEL_FORMAT                0x55u       /* MCU 16-bit RGB565 */
/* ILI9341 正常不需要反显; 若画面呈"负片"改为 1 (发送 0x21 INVON) */
#define LCD_INVERT_COLORS               0
#define LCD_RST_ACTIVE_LOW              1
#define LCD_BL_ACTIVE_HIGH              1

/* ------------------------------------------------- 触摸: XPT2046 引脚与校准值 */
/* 触摸使用独立软件 SPI, T_CLK/T_DIN/T_OUT 与 LCD 的 SPI0 完全分开 */
#define TOUCH_CLK_GPIO_PORT             GPIOD
#define TOUCH_CLK_GPIO_PIN              GPIO_PIN_5
#define TOUCH_DIN_GPIO_PORT             GPIOD
#define TOUCH_DIN_GPIO_PIN              GPIO_PIN_7
#define TOUCH_OUT_GPIO_PORT             GPIOE
#define TOUCH_OUT_GPIO_PIN              GPIO_PIN_0
#define TOUCH_CS_GPIO_PORT              GPIOC
#define TOUCH_CS_GPIO_PIN               GPIO_PIN_6
#define TOUCH_IRQ_GPIO_PORT             GPIOC
#define TOUCH_IRQ_GPIO_PIN              GPIO_PIN_7
/* 实测值 (2026-11-19, 本板): TL(358,3822) TR(3868,3863) BL(356,326) BR(3868,314)
 * X 从左到右增大, 不反相; Y 从上到下减小, 需要反相。 */
#define TOUCH_RAW_X_MIN                 345
#define TOUCH_RAW_X_MAX                 3885
#define TOUCH_RAW_Y_MIN                 310
#define TOUCH_RAW_Y_MAX                 3870
#define TOUCH_SWAP_XY                   0
#define TOUCH_INVERT_X                  0
#define TOUCH_INVERT_Y                  1
/* 触摸校准临时开关: 1 = 上电直接进校准界面显示原始坐标; 校准完改回 0 */
#define TOUCH_CALIBRATION_MODE          0

/* ------------------------------------------------------------- 蜂鸣器 */
#define BUZZER_GPIO_PORT                GPIOB
#define BUZZER_GPIO_PIN                 GPIO_PIN_8
#define BUZZER_ACTIVE_LOW               1

#endif /* BOARD_CONFIG_H */
