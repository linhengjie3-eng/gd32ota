#include "bsp_board.h"
#include "board_config.h"
#include "gd32f30x.h"

volatile uint32_t g_bsp_milliseconds;

void bsp_board_init(void)
{
    SystemCoreClockUpdate();
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_AF);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void bsp_time_init(void)
{
    g_bsp_milliseconds = 0;
    SysTick_Config(SystemCoreClock / 1000u);
}

uint32_t bsp_millis(void)
{
    return g_bsp_milliseconds;
}

void bsp_delay_ms(uint32_t delay)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = (SystemCoreClock / 1000u) * delay;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {}
}

void bsp_buzzer_init(void)
{
    /* 低电平触发: 上电先输出高电平(停), 再配置方向, 避免误响 */
    gpio_bit_set(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN);
    gpio_init(BUZZER_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ,
              BUZZER_GPIO_PIN);
}

void bsp_buzzer_set(int enabled)
{
#if BUZZER_ACTIVE_LOW
    if (enabled) {
        gpio_bit_reset(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN);
    } else {
        gpio_bit_set(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN);
    }
#else
    if (enabled) {
        gpio_bit_set(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN);
    } else {
        gpio_bit_reset(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN);
    }
#endif
}

void bsp_buzzer_success(void)
{
    bsp_buzzer_set(1);
    bsp_delay_ms(90);
    bsp_buzzer_set(0);
    bsp_delay_ms(90);
    bsp_buzzer_set(1);
    bsp_delay_ms(90);
    bsp_buzzer_set(0);
}

void bsp_buzzer_failure(void)
{
    bsp_buzzer_set(1);
    bsp_delay_ms(650);
    bsp_buzzer_set(0);
}

void bsp_buzzer_beep(void)
{
    bsp_buzzer_set(1);
    bsp_delay_ms(50);
    bsp_buzzer_set(0);
}
