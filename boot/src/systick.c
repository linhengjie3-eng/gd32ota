#include "bsp_board.h"

void SysTick_Handler(void)
{
    extern volatile uint32_t g_bsp_milliseconds;
    g_bsp_milliseconds++;
}
