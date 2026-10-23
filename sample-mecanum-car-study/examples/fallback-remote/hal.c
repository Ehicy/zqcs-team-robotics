#include "hal.h"
#include "fallback.h"
#include "stm32f10x.h"
/* The timing module latches its watchdog flag before calling this hook. The
 * owner freezes trajectories on that flag; hardware holds its last PWM until
 * reset. This board has no bridge STBY and must not drop a gravity-loaded arm
 * merely because a software task stalled. Reset still disables every PWM. */
void lower_motor_emergency_stop(void)
{
    /* Hold the last commanded position; 1500us is not a generic stop.
     * This cannot guarantee physical stopping; support the mechanism first. */
}
uint8_t lower_motor_is_enabled(void) { return 0; }
void remote_led(uint8_t on)
{
    if (on)
        GPIOC->BRR = GPIO_Pin_13;
    else
        GPIOC->BSRR = GPIO_Pin_13;
}
void remote_hal_init(void)
{
    GPIO_InitTypeDef g;
    RCC_ClocksTypeDef c;
    uint32_t clock;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    GPIOB->BRR = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8;
    GPIOC->BSRR = GPIO_Pin_13;
    GPIO_StructInit(&g);
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOC, &g);
    RCC_GetClocksFreq(&c);
    clock = c.PCLK1_Frequency;
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
        clock *= 2;
    TIM_DeInit(TIM4);
    TIM4->PSC = clock / 1000000U - 1;
    TIM4->ARR = 19999;
    TIM4->CCMR1 = (6U << 4) | TIM_CCMR1_OC1PE | (6U << 12) | TIM_CCMR1_OC2PE;
    TIM4->CCMR2 = (6U << 4) | TIM_CCMR2_OC3PE;
    TIM4->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E;
    TIM4->CCR1 = TIM4->CCR2 = TIM4->CCR3 = 0;
    TIM4->EGR = TIM_EGR_UG;
    TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    g.GPIO_Mode = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8;
    GPIO_Init(GPIOB, &g);
}
int remote_servo_write(unsigned ch, uint16_t us)
{
    if (ch >= 3 || (us && (us < 500 || us > 2500)))
        return 0;
    if (ch == 0)
        TIM4->CCR1 = us;
    else if (ch == 1)
        TIM4->CCR2 = us;
    else
        TIM4->CCR3 = us;
    return 1; /* Preload takes effect at the next 20ms frame, no timer reset. */
}
