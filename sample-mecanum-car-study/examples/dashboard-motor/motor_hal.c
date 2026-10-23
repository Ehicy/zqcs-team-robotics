/* Dashboard profile: retain the verified PA-side PWM/direction wiring.
 * Encoder inputs are initialized separately by encoder_hal.c. */
#include "fallback_pins.h"
#include "motor_hal.h"
#include "stm32f10x.h"
static uint16_t period, directions;
static uint8_t ready, enabled;
static const uint8_t in1[4] = FB_IN1_PINS, in2[4] = FB_IN2_PINS;
void lower_motor_emergency_stop(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    (void)RCC->APB2ENR;
    GPIOB->BRR = GPIO_Pin_15;
    enabled = 0;
    __DSB();
}
uint8_t lower_motor_is_enabled(void) { return enabled && (GPIOB->ODR & GPIO_Pin_15) != 0; }
void lower_motor_init(void)
{
    GPIO_InitTypeDef g;
    RCC_ClocksTypeDef c;
    uint32_t clock;
    unsigned i;
    lower_motor_emergency_stop();
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO,
                           ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    AFIO->MAPR =
        (AFIO->MAPR & ~(AFIO_MAPR_SWJ_CFG | AFIO_MAPR_TIM2_REMAP)) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    directions = 0;
    for (i = 0; i < 4; i++)
        directions |= (uint16_t)((1U << in1[i]) | (1U << in2[i]));
    GPIOB->BRR = GPIO_Pin_15;
    GPIOA->BRR = directions | GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_StructInit(&g);
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Pin = GPIO_Pin_15;
    GPIO_Init(GPIOB, &g);
    g.GPIO_Pin = directions;
    GPIO_Init(GPIOA, &g);
    RCC_GetClocksFreq(&c);
    clock = c.PCLK1_Frequency;
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
        clock *= 2;
    period = (uint16_t)(clock / 20000U);
    if (period < 2)
        return;
    TIM_DeInit(TIM2);
    TIM2->PSC = 0;
    TIM2->ARR = period - 1;
    TIM2->CCMR1 = (6U << 4) | TIM_CCMR1_OC1PE | (6U << 12) | TIM_CCMR1_OC2PE;
    TIM2->CCMR2 = (6U << 4) | TIM_CCMR2_OC3PE | (6U << 12) | TIM_CCMR2_OC4PE;
    TIM2->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM2->CCR1 = TIM2->CCR2 = TIM2->CCR3 = TIM2->CCR4 = 0;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    g.GPIO_Mode = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_Init(GPIOA, &g);
    /* TIM2 is PWM here, NOT an encoder. No EXTI/encoder GPIO is initialized. */
    ready = 1;
}
void lower_motor_write_braked(const int16_t pwm[4], uint8_t active, uint8_t short_brake_mask)
{
    uint16_t c[4], set = 0;
    unsigned i;
    int any = 0;
    uint32_t mask;
    if (!active || !ready || !pwm || short_brake_mask>15U || lower_motor_watchdog_fault())
    {
        lower_motor_emergency_stop();
        return;
    }
    for (i = 0; i < 4; i++)
    {
        int v = pwm[i];
        if (v < -1000 || v > 1000)
        {
            lower_motor_emergency_stop();
            return;
        }
        if (short_brake_mask & (1U<<i))
        {
            if(v) { lower_motor_emergency_stop(); return; }
            any=1;
            set |= (uint16_t)((1U<<in1[i]) | (1U<<in2[i]));
            c[i]=period; /* IN1=IN2=HIGH, PWM=HIGH => short brake, no reverse. */
        }
        else if (v)
        {
            any = 1;
            set |= (uint16_t)(1U << (v > 0 ? in1[i] : in2[i]));
            if (v < 0)
                v = -v;
            c[i] = (uint16_t)((uint32_t)v * period / 1000);
        }
        else
            c[i] = period; /* IN1=IN2=0 and PWM high => coast, not short brake. */
    }
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(mask)::"memory");
    lower_motor_emergency_stop();
    if (!mask && any && !lower_motor_watchdog_fault())
    {
        GPIOA->BRR = directions;
        GPIOA->BSRR = set;
        TIM2->CCR1 = c[0];
        TIM2->CCR2 = c[1];
        TIM2->CCR3 = c[2];
        TIM2->CCR4 = c[3];
        TIM2->EGR = TIM_EGR_UG;
        TIM2->SR = 0;
        __DSB();
        enabled = 1;
        GPIOB->BSRR = GPIO_Pin_15;
    }
    __asm volatile("msr primask, %0" ::"r"(mask) : "memory");
}
void lower_motor_write(const int16_t pwm[4],uint8_t active)
{ lower_motor_write_braked(pwm,active,0); }
