#include "boot_jump.h"
#include "memory_map.h"
#include "gd32f30x.h"

typedef void (*application_entry_t)(void);

/*
 * 入口地址必须放在静态存储区: 一旦执行 __set_MSP() 切换到 App 的栈,
 * 原 Boot 栈上的局部变量就不能再访问 (否则 -O0 下会读到新栈的随机内容,
 * 典型现象是跳转瞬间 HardFault)。
 */
static application_entry_t app_entry;

int boot_jump_to(uint32_t application_address, uint32_t application_size)
{
    uint32_t stack = *(const volatile uint32_t *)application_address;
    uint32_t reset = *(const volatile uint32_t *)(application_address + 4u);
    unsigned i;

    if ((stack < SRAM_BASE_ADDR) || (stack > SRAM_END_ADDR) ||
        ((stack & 7u) != 0u) || ((reset & 1u) == 0u) ||
        ((reset & ~1u) < application_address) ||
        ((reset & ~1u) >= (application_address + application_size))) {
        return -1;
    }

    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    for (i = 0u; i < 8u; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }

    /* 复位 Boot 用过外设, 避免残留中断/ DMA 状态带进 App */
    usart_deinit(USART0);
    usart_deinit(USART1);
    spi_i2s_deinit(SPI0);

    /* FWDGT 一旦使能无法用软件停止: 参考工程默认不开启; 若你启用了,
       App 必须接管喂狗, 不要在这里尝试"停看门狗"。 */

    SCB->VTOR = application_address;
    __DSB();
    __ISB();

    app_entry = (application_entry_t)reset;
    __set_MSP(stack);
    /*
     * 必须恢复全局中断。Boot 里 __disable_irq() 把 PRIMASK 置 1,
     * 若带着 PRIMASK=1 跳进 App, USART1 RX 中断、SysTick、PendSV
     * 都会被屏蔽, FreeRTOS 无法调度 —— 表现为"界面出来了但触摸/串口全死"。
     */
    __enable_irq();
    app_entry();
    return -2; /* 正常情况不会返回 */
}
