#include "delay.h"
#include "motor_hal.h"
#include "stm32f10x.h"

/* This target owns a periodic SysTick. The historical delay.c implementation
 * must not be linked: it reprograms SysTick for every blocking delay. */
#define DWT_CTRL_REG (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT_REG (*(volatile uint32_t *)0xE0001004UL)
#define IWDG_KEY_ACCESS 0x5555U
#define IWDG_KEY_RELOAD 0xAAAAU
#define IWDG_KEY_START 0xCCCCU

static volatile uint32_t milliseconds;
static volatile uint32_t last_feed_ms;
static volatile uint8_t watchdog_latched = 1;
static volatile uint8_t timing_ready;
static volatile uint8_t iwdg_ready;
static volatile uint8_t iwdg_started;
static uint32_t cycles_per_us;

static uint32_t irq_save(void)
{
    uint32_t prior;
    __asm volatile ("mrs %0, primask\n\tcpsid i" : "=r" (prior) :: "memory");
    return prior;
}

static void irq_restore(uint32_t prior)
{
    __asm volatile ("msr primask, %0" :: "r" (prior) : "memory");
}

static uint8_t in_thread_context(void)
{
    uint32_t exception;
    __asm volatile ("mrs %0, ipsr" : "=r" (exception));
    return (uint8_t)(exception == 0U);
}

static uint8_t iwdg_init(void)
{
    uint32_t start;
    /* LSI, not the PLL/APB clocks, clocks the independent backup watchdog. */
    RCC->CSR |= RCC_CSR_LSION;
    start = DWT_CYCCNT_REG;
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0U) {
        if ((uint32_t)(DWT_CYCCNT_REG - start) >= cycles_per_us * 20000U) return 0;
    }
    /* Start the IWDG clock domain before waiting for PR/RLR synchronization,
     * as in ST's STM32F1 HAL_IWDG_Init sequence. */
    DBGMCU->CR &= ~DBGMCU_CR_DBG_IWDG_STOP;
    IWDG->KR = IWDG_KEY_START;
    iwdg_started = 1;
    IWDG->KR = IWDG_KEY_ACCESS;
    IWDG->PR = 3U; /* Divide LSI by 32. */
    IWDG->RLR = 249U; /* 8000 LSI periods: nominal 200 ms at 40 kHz. */
    start = DWT_CYCCNT_REG;
    while (IWDG->SR != 0U) {
        if ((uint32_t)(DWT_CYCCNT_REG - start) >= cycles_per_us * 20000U) return 0;
    }
    if (IWDG->PR != 3U || IWDG->RLR != 249U) return 0;
    IWDG->KR = IWDG_KEY_RELOAD;
    /* No debug-freeze: a stopped CPU must not leave STBY asserted. The exact
     * reset interval follows the actual LSI frequency, not a calibrated 200 ms. */
    return 1;
}

void delay_init(void)
{
    uint32_t ticks;
    lower_motor_emergency_stop();
    watchdog_latched = 1;
    if (timing_ready) return; /* Never silently erase a running watchdog fault. */
    SystemCoreClockUpdate();
    cycles_per_us = SystemCoreClock / 1000000U;
    ticks = SystemCoreClock / 1000U;
    if (cycles_per_us == 0U || ticks == 0U) return;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT_CYCCNT_REG = 0;
    DWT_CTRL_REG |= 1U;
    milliseconds = 0;
    last_feed_ms = 0;
    /* Four preemption-priority bits: SysTick=0, USART2=1, EXTI10=2.
     * This is an isolated bare-metal target; no RTOS uses SysTick. */
    NVIC_SetPriorityGrouping(3U);
    if (SysTick_Config(ticks) != 0U) return;
    NVIC_SetPriority(SysTick_IRQn, 0U);
    /* Communication uses SysTick even if the independent watchdog fails.
     * watchdog_latched remains set until IWDG configuration is verified. */
    timing_ready = 1;
    if (!iwdg_init()) return;
    iwdg_ready = 1;
    last_feed_ms = milliseconds;
    watchdog_latched = 0;
}

void SysTick_Handler(void)
{
    uint32_t now = milliseconds + 1U;
    milliseconds = now;
    if (timing_ready && (uint32_t)(now - last_feed_ms) >= LOWER_MOTOR_WATCHDOG_MS) {
        watchdog_latched = 1;
        lower_motor_emergency_stop();
    }
}

uint32_t delay_millis(void)
{
    /* An aligned 32-bit load is atomic on Cortex-M3, including from an ISR. */
    return milliseconds;
}

void delay_us(uint16_t us)
{
    uint32_t start, cycles;
    if (us == 0U || cycles_per_us == 0U) return;
    cycles = (uint32_t)us * cycles_per_us;
    start = DWT_CYCCNT_REG;
    while ((uint32_t)(DWT_CYCCNT_REG - start) < cycles) {
        __NOP();
    }
}

void delay_ms(uint16_t ms)
{
    uint32_t start = delay_millis();
    /* Main context with interrupts enabled only. Deliberately does not feed
     * either watchdog; a blocking operation must not sustain motor output. */
    while ((uint32_t)(delay_millis() - start) < (uint32_t)ms) __WFI();
}

void lower_motor_watchdog_feed(void)
{
    uint32_t prior, now;
    if (!in_thread_context() || !timing_ready) return;
    prior = irq_save();
    if (prior != 0U) {
        /* A main loop that keeps PRIMASK set would otherwise keep feeding the
         * IWDG while SysTick/command deadlines can never advance. */
        watchdog_latched = 1;
        lower_motor_emergency_stop();
        irq_restore(prior);
        return;
    }
    now = milliseconds;
    /* Even a delayed main-loop feed cannot hide the elapsed software deadline. */
    if ((uint32_t)(now - last_feed_ms) >= LOWER_MOTOR_WATCHDOG_MS) {
        watchdog_latched = 1;
        lower_motor_emergency_stop();
    }
    last_feed_ms = now;
    /* Keep diagnostic communication alive if a started IWDG failed setup.
     * iwdg_ready stays false: neither feeding nor CLEAR can enable outputs. */
    if (iwdg_started) IWDG->KR = IWDG_KEY_RELOAD;
    irq_restore(prior);
}

uint8_t lower_motor_watchdog_fault(void)
{
    return (uint8_t)(!timing_ready || !iwdg_ready || watchdog_latched);
}

void lower_motor_watchdog_clear(void)
{
    uint32_t prior;
    if (!in_thread_context() || !timing_ready || !iwdg_ready) return;
    prior = irq_save();
    if (prior == 0U && !lower_motor_is_enabled()) {
        /* Explicit owner reset only, after disarming/resetting its command
         * state. No watchdog feed or motor write calls this function. */
        last_feed_ms = milliseconds;
        watchdog_latched = 0;
    }
    irq_restore(prior);
}
