
#include "delay.h"
#include "stm32f10x.h"

/* 本项目独占 SysTick 做阻塞延时，不能同时拿它作 RTOS/毫秒中断时基。
 * 相对上游修改：从实际时钟计算计数数，处理 us=0，纠正 LOAD 的减一。
 */
static uint32_t ticks_per_us;
/* 仓库的旧CMSIS未声明DWT结构，直接使用Cortex-M3固定寄存器地址。
 * 测试可替换这两个寄存器，以验证计数回绕和不同系统时钟。
 */
#ifndef DELAY_DWT_CTRL
#define DELAY_DWT_CTRL (*(volatile uint32_t *)0xE0001000UL)
#endif
#ifndef DELAY_DWT_CYCCNT
#define DELAY_DWT_CYCCNT (*(volatile uint32_t *)0xE0001004UL)
#endif
static uint32_t cycles_per_ms;
static uint32_t last_cycles;
static uint32_t remainder_cycles;
static uint32_t elapsed_ms;

void delay_init(void)
{
    SystemCoreClockUpdate();
    ticks_per_us = SystemCoreClock / 1000000U;
    /* STM32F103 本项目使用 72 MHz，HSE 启动失败回到 HSI 时为 8 MHz。 */
    SysTick->CTRL = 0;
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DELAY_DWT_CYCCNT = 0;
    DELAY_DWT_CTRL |= 1U; /* CYCCNTENA */
    cycles_per_ms = SystemCoreClock / 1000U;
    last_cycles = remainder_cycles = elapsed_ms = 0;
}

uint32_t delay_millis(void)
{
    uint32_t cycles = DELAY_DWT_CYCCNT;
    uint32_t delta = cycles - last_cycles; /* unsigned减法允许硬件计数回绕 */
    last_cycles = cycles;
    if (cycles_per_ms == 0U) return 0U;
    /* 分别除/取余，避免delta+余数在接近UINT32_MAX时溢出。 */
    elapsed_ms += delta / cycles_per_ms;
    remainder_cycles += delta % cycles_per_ms;
    if (remainder_cycles >= cycles_per_ms)
    {
        remainder_cycles -= cycles_per_ms;
        elapsed_ms++;
    }
    return elapsed_ms;
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
