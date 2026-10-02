#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "stm32f10x.h"

static SysTick_Type test_systick;
static CoreDebug_Type test_core_debug;
static volatile uint32_t test_dwt_ctrl, test_dwt_cycles;
#undef SysTick
#define SysTick (&test_systick)
#undef CoreDebug
#define CoreDebug (&test_core_debug)
#define DELAY_DWT_CTRL test_dwt_ctrl
#define DELAY_DWT_CYCCNT test_dwt_cycles
uint32_t SystemCoreClock = 72000000U;
void SystemCoreClockUpdate(void) {}
void SysTick_CLKSourceConfig(uint32_t source)
{ assert(source == SysTick_CLKSource_HCLK); }
#include "../hardware/Src/delay.c"

int main(void)
{
    uint64_t reference;
    delay_init();
    assert(test_core_debug.DEMCR & CoreDebug_DEMCR_TRCENA_Msk);
    assert(test_dwt_ctrl & 1U);
    test_dwt_cycles = 36000; assert(delay_millis() == 0);
    test_dwt_cycles = 72000; assert(delay_millis() == 1);
    assert(delay_millis() == 1); /* 同一时刻重复读取不重复累计 */
    test_dwt_cycles += 71999; assert(delay_millis() == 1);
    reference = (uint64_t)72000 + 71999 + UINT32_MAX;
    test_dwt_cycles += UINT32_MAX;
    assert(delay_millis() == reference / 72000U);
    test_dwt_cycles += 72000;
    assert(delay_millis() == reference / 72000U + 1);
    delay_us(0);

    SystemCoreClock = 8000000U; delay_init();
    test_dwt_cycles = 7999; assert(delay_millis() == 0);
    test_dwt_cycles++; assert(delay_millis() == 1);
    test_dwt_cycles += 8000 * 17; assert(delay_millis() == 18);
    puts("PASS: actual DWT clock code, fractional milliseconds, cycle wrap, 72/8 MHz");
    return 0;
}
