#include "motor_hal.h"
#include "board_link_config.h"
#include "encoder_edge_filter.h"
#include "delay.h"
#include "stm32f10x.h"
#include <stddef.h>

#if LINK_STM_MOTOR_PWM_TIMER != 4 || LINK_STM_DRIVER_INHIBIT_PIN != 15
#error The lower-motor HAL implements TIM4 and PB15 STBY only
#endif

#define STBY_MASK ((uint16_t)(1U << LINK_STM_DRIVER_INHIBIT_PIN))
#define ENCODER_EXTI_MASK (1UL << 10)
#define DWT_CYCCNT_REG (*(volatile uint32_t *)0xE0001004UL)
#define M4_FILTER_US 20U
#define M4_IRQS_PER_MS 32U

static const uint8_t pwm_pins[4] = LINK_STM_MOTOR_PWM_PINS;
static const uint8_t in1_pins[4] = LINK_STM_MOTOR_IN1_PINS;
static const uint8_t in2_pins[4] = LINK_STM_MOTOR_IN2_PINS;
static const uint8_t encoder_ports[4] = LINK_STM_ENCODER_PORTS;
static const uint8_t encoder_a[4] = LINK_STM_ENCODER_A_PINS;
static const uint8_t encoder_b[4] = LINK_STM_ENCODER_B_PINS;
static const uint8_t encoder_timers[4] = LINK_STM_ENCODER_TIMERS;
static TIM_TypeDef *const encoder_timer[3] = {TIM2, TIM3, TIM1};
static volatile uint8_t motor_ready;
static volatile uint8_t motor_enabled;
/* Keep diagnostics in RAM for SWD; none of these counters clear on STOP. */
static encoder_edge_filter_t m4_filter;
static volatile uint32_t m4_raw_irqs;
static volatile uint8_t m4_overrun, m4_started;
static encoder_irq_budget_t m4_budget;
/* First overrun context; retained until reboot, including across STOP/CLEAR. */
static volatile uint32_t m4_window_ms, m4_fault_ms, m4_fault_window_ms;
static volatile uint32_t m4_fault_cycles, m4_fault_pins;
static uint16_t previous_count[3];
static uint32_t accumulated_count[3];
static uint16_t direction_mask;
static uint16_t pwm_period;

/* This repository's old GNU CMSIS only declares __get/set_PRIMASK. Local
 * inline helpers avoid adding core_cm3.c to every historical build target. */
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

static uint8_t pin_layout_valid(void)
{
    static const uint8_t expected_ports[4] = {0, 0, 0, 1};
    static const uint8_t expected_a[4] = {0, 6, 8, 10};
    static const uint8_t expected_b[4] = {1, 7, 9, 11};
    static const uint8_t expected_timers[4] = {2, 3, 1, 0};
    uint16_t occupied = STBY_MASK | GPIO_Pin_10 | GPIO_Pin_11;
    unsigned i;
    for (i = 0; i < 4; ++i) {
        if (pwm_pins[i] != 6U + i || encoder_ports[i] != expected_ports[i] ||
            encoder_a[i] != expected_a[i] || encoder_b[i] != expected_b[i] ||
            encoder_timers[i] != expected_timers[i]) return 0;
        occupied |= (uint16_t)(1U << pwm_pins[i]);
    }
    for (i = 0; i < 4; ++i) {
        uint16_t a, b;
        if (in1_pins[i] > 15U || in2_pins[i] > 15U) return 0;
        a = (uint16_t)(1U << in1_pins[i]);
        b = (uint16_t)(1U << in2_pins[i]);
        if (a == b || ((a | b) & occupied) != 0U) return 0;
        occupied |= a | b;
    }
    return 1;
}

void lower_motor_emergency_stop(void)
{
    /* Also safe before normal initialization and from fault/SysTick handlers.
     * Never wait for a timer update or use read-modify-write on the GPIO ODR. */
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    (void)RCC->APB2ENR;
    GPIOB->BRR = STBY_MASK;
    motor_enabled = 0;
    __DSB();
}

static void encoder_timer_init(TIM_TypeDef *timer)
{
    TIM_DeInit(timer);
    timer->PSC = 0;
    timer->ARR = 0xffffU;
    /* Both direct inputs, each filtered for eight samples at the timer clock.
     * Encoder TI12 itself counts both edges; CC1P/CC2P stay non-inverted. */
    timer->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0 |
                   (3U << 4) | (3U << 12);
    timer->CCMR2 = 0;
    timer->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
    timer->SMCR = TIM_EncoderMode_TI12;
    timer->DIER = 0;
    if (timer == TIM1) timer->BDTR = 0; /* No complementary outputs or break. */
    timer->EGR = TIM_EGR_UG;
    timer->SR = 0;
    timer->CNT = 0;
    timer->CR1 = TIM_CR1_CEN;
}

void lower_motor_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_ClocksTypeDef clocks;
    uint32_t timer_clock, periods, prior;
    uint16_t pwm_mask = 0, encoder_mask = 0;
    unsigned i;

    /* STBY must become a driven low before any PWM or direction is changed. */
    lower_motor_emergency_stop();
    motor_ready = 0;
    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Pin = STBY_MASK;
    GPIO_Init(GPIOB, &gpio);
    if (!pin_layout_valid()) return;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                          RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3 |
                          RCC_APB1Periph_TIM4, ENABLE);
    /* No timer remaps: CH3/4 of TIM2/3 remain disabled, leaving USART2 and
     * direction GPIOs free. Only JTAG is released; PA13/PA14 retain SWD. */
    AFIO->MAPR = (AFIO->MAPR & ~(AFIO_MAPR_TIM1_REMAP | AFIO_MAPR_TIM2_REMAP |
                    AFIO_MAPR_TIM3_REMAP | AFIO_MAPR_TIM4_REMAP |
                    AFIO_MAPR_USART2_REMAP | AFIO_MAPR_SWJ_CFG)) |
                    AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

    direction_mask = 0;
    for (i = 0; i < 4; ++i) {
        pwm_mask |= (uint16_t)(1U << pwm_pins[i]);
        direction_mask |= (uint16_t)((1U << in1_pins[i]) | (1U << in2_pins[i]));
    }
    GPIOB->BRR = (uint16_t)(pwm_mask | direction_mask | STBY_MASK);
    gpio.GPIO_Pin = (uint16_t)(pwm_mask | direction_mask | STBY_MASK);
    GPIO_Init(GPIOB, &gpio);

    RCC_GetClocksFreq(&clocks);
    timer_clock = clocks.PCLK1_Frequency;
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) timer_clock *= 2U;
    periods = timer_clock / LOWER_MOTOR_PWM_HZ;
    if (periods < 2U || periods > 65535U) return;
    pwm_period = (uint16_t)periods;
    TIM_DeInit(TIM4);
    TIM4->PSC = 0;
    TIM4->ARR = periods - 1U;
    /* PWM1, high polarity, all four CCRs preload together under STBY=0. */
    TIM4->CCMR1 = (6U << 4) | TIM_CCMR1_OC1PE | (6U << 12) | TIM_CCMR1_OC2PE;
    TIM4->CCMR2 = (6U << 4) | TIM_CCMR2_OC3PE | (6U << 12) | TIM_CCMR2_OC4PE;
    TIM4->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM4->CCR1 = TIM4->CCR2 = TIM4->CCR3 = TIM4->CCR4 = 0;
    TIM4->EGR = TIM_EGR_UG;
    TIM4->SR = 0;
    TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = pwm_mask;
    GPIO_Init(GPIOB, &gpio);

    /* Encoder pins alone become inputs: PA2/PA3 are deliberately untouched. */
    for (i = 0; i < 3; ++i)
        encoder_mask |= (uint16_t)((1U << encoder_a[i]) | (1U << encoder_b[i]));
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Pin = encoder_mask;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_Init(GPIOB, &gpio);
    for (i = 0; i < 3; ++i) {
        encoder_timer_init(encoder_timer[i]);
        previous_count[i] = 0;
        accumulated_count[i] = 0;
    }
    prior = irq_save();
    m4_started=0; m4_overrun=0; m4_raw_irqs=0;
    m4_window_ms=m4_fault_ms=m4_fault_window_ms=0;
    m4_fault_cycles=m4_fault_pins=0;
    EXTI->IMR &= ~ENCODER_EXTI_MASK;
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource10);
    EXTI->RTSR |= ENCODER_EXTI_MASK;
    EXTI->FTSR |= ENCODER_EXTI_MASK;
    EXTI->EMR &= ~ENCODER_EXTI_MASK;
    EXTI->PR = ENCODER_EXTI_MASK;
    NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    NVIC_SetPriority(EXTI15_10_IRQn, 2U);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
    /* delay_init() starts/resets DWT later. Do not timestamp edges until then. */
    motor_ready = 1;
    irq_restore(prior);
}

void lower_encoder_start(void)
{
    uint32_t prior=irq_save(), pins=GPIOB->IDR;
    if(motor_ready && !m4_started && SystemCoreClock>=1000000U) {
        encoder_edge_filter_init(&m4_filter,(uint8_t)!!(pins&GPIO_Pin_10),
                                 (SystemCoreClock/1000000U)*M4_FILTER_US);
        encoder_irq_budget_init(&m4_budget,DWT_CYCCNT_REG,
                                SystemCoreClock/1000U,M4_IRQS_PER_MS);
        m4_window_ms=delay_millis();
        EXTI->PR=ENCODER_EXTI_MASK;
        NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
        m4_started=1;
        EXTI->IMR|=ENCODER_EXTI_MASK;
    }
    irq_restore(prior);
}

void lower_motor_write(const int16_t pwm[4], uint8_t active)
{
    uint16_t compare[4], set_pins = 0;
    uint8_t any_motion = 0;
    uint32_t prior;
    unsigned i;
    if (active != 1U || pwm == NULL || !motor_ready) {
        lower_motor_emergency_stop();
        return;
    }
    for (i = 0; i < 4; ++i) {
        int32_t value = pwm[i];
        if (value < -LOWER_MOTOR_PWM_LIMIT || value > LOWER_MOTOR_PWM_LIMIT) {
            lower_motor_emergency_stop();
            return;
        }
        if (value != 0) {
            any_motion = 1;
            set_pins |= (uint16_t)(1U << (value > 0 ? in1_pins[i] : in2_pins[i]));
            if (value < 0) value = -value;
            compare[i] = (uint16_t)((uint32_t)value * pwm_period / 1000U);
        } else {
            /* TB6612: IN1=IN2=0 and PWM=1 is high impedance (coast).
             * PWM=0 would short-brake even with both direction inputs low. */
            compare[i] = pwm_period;
        }
    }
    prior = irq_save();
    lower_motor_emergency_stop();
    if (prior != 0U || !any_motion || !m4_started || m4_overrun || lower_motor_watchdog_fault()) {
        irq_restore(prior);
        return;
    }
    GPIOB->BRR = direction_mask;
    GPIOB->BSRR = set_pins;
    TIM4->CCR1 = compare[0];
    TIM4->CCR2 = compare[1];
    TIM4->CCR3 = compare[2];
    TIM4->CCR4 = compare[3];
    TIM4->EGR = TIM_EGR_UG;
    TIM4->SR = 0;
    __DSB();
    motor_enabled = 1;
    GPIOB->BSRR = STBY_MASK;
    irq_restore(prior);
}

uint8_t lower_motor_is_enabled(void)
{
    return (uint8_t)(motor_enabled && ((GPIOB->ODR & STBY_MASK) != 0U));
}

void EXTI15_10_IRQHandler(void)
{
    if ((EXTI->PR & ENCODER_EXTI_MASK) != 0U) {
        uint32_t pins, now, previous_window;
        uint8_t a, b;
        EXTI->PR = ENCODER_EXTI_MASK; /* W1C before sampling; never clear other lines. */
        if(!m4_started || m4_overrun) return;
        now=DWT_CYCCNT_REG;
        m4_raw_irqs++;
        previous_window=m4_budget.start;
        if(!encoder_irq_budget_allow(&m4_budget,now)) {
            /* Bound interrupt work during a noisy/floating-input storm. Latch
             * invalid feedback and stop, rather than starving UART/main until
             * IWDG resets. Only reboot/reinitialization releases this mask. */
            m4_overrun=1;
            EXTI->IMR&=~ENCODER_EXTI_MASK;
            lower_motor_emergency_stop();
            m4_fault_ms=delay_millis();
            m4_fault_window_ms=m4_fault_ms-m4_window_ms;
            m4_fault_cycles=now-m4_budget.start;
            m4_fault_pins=GPIOB->IDR;
            return;
        }
        if(m4_budget.start!=previous_window) m4_window_ms=delay_millis();
        pins = GPIOB->IDR;
        a = (uint8_t)((pins & GPIO_Pin_10) != 0U);
        b = (uint8_t)((pins & GPIO_Pin_11) != 0U);
        /* Matches non-inverted hardware TI12 direction on each A transition.
         * Multiple edges while EXTI is pending cannot be recovered in software. */
        encoder_edge_filter_irq(&m4_filter,now,a,b);
    }
}

static int32_t signed_wrapping_count(uint32_t value)
{
    if (value <= INT32_MAX) return (int32_t)value;
    return -(int32_t)(UINT32_MAX - value) - 1;
}

uint8_t lower_encoder_sample(int32_t counts[4])
{
    uint16_t current[3];
    uint32_t software_count, prior;
    unsigned i;
    if (counts == NULL) return 0;
    if (!motor_ready) {
        for (i = 0; i < 4; ++i) counts[i] = 0;
        return 0;
    }
    prior = irq_save();
    for (i = 0; i < 3; ++i) current[i] = (uint16_t)encoder_timer[i]->CNT;
    if(m4_started && !m4_overrun) {
        uint32_t pins=GPIOB->IDR;
        /* This poll also commits the final edge after the wheel stops. Keep B
         * from the original edge even if it changed before this later poll. */
        encoder_edge_filter_poll(&m4_filter,DWT_CYCCNT_REG,
                                (uint8_t)!!(pins&GPIO_Pin_10),
                                (uint8_t)!!(pins&GPIO_Pin_11));
    }
    software_count = m4_filter.count;
    irq_restore(prior);
    for (i = 0; i < 3; ++i) {
        uint16_t difference = (uint16_t)(current[i] - previous_count[i]);
        int32_t delta = difference <= 32767U ? (int32_t)difference :
                                              (int32_t)difference - 65536;
        previous_count[i] = current[i];
        accumulated_count[i] += (uint32_t)delta; /* Defined modulo-2^32 arithmetic. */
        counts[i] = signed_wrapping_count(accumulated_count[i]);
    }
    counts[3] = signed_wrapping_count(software_count);
    return (m4_started && !m4_overrun) ? 15U : 7U;
}
