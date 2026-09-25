
#include "delay.h"
#include "stm32f10x.h"

/* 本项目独占 SysTick 做阻塞延时，不能同时拿它作 RTOS/毫秒中断时基。
 * 相对上游修改：从实际时钟计算计数数，处理 us=0，纠正 LOAD 的减一。
 */
static uint32_t ticks_per_us;

void delay_init(void)
{
    SystemCoreClockUpdate();
    ticks_per_us = SystemCoreClock / 1000000U;
    /* STM32F103 本项目使用 72 MHz，HSE 启动失败回到 HSI 时为 8 MHz。 */
    SysTick->CTRL = 0;
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK);
}

void delay_us(uint16_t us)
{
    if (us == 0)
    {
        return;
    }
    /* 最大 65535 us，在 72 MHz 下也不超过 24 位 LOAD 范围。 */
    SysTick->LOAD = ticks_per_us * us - 1U;
    SysTick->VAL = 0;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0)
    {
        /* 等待计数完成。此函数不能在中断中重入调用。 */
    }
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SysTick->VAL = 0;
}

void delay_ms(uint16_t ms)
{
    /* 每次等待 1 ms，避免长延时超出 SysTick 的计数范围。 */
    while (ms > 0)
    {
        delay_us(1000);
        ms--;
    }
}
