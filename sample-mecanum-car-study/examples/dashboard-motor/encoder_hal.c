/* PA PWM owns TIM2. M1/M2 use TIM3 partial remap/TIM4; M3/M4 use
 * A-only x2 decoding. UART3 PB10/11 and SWD PA13/14 remain untouched. */
#include "motor_hal.h"
#include "encoder_edge_filter.h"
#include "delay.h"
#include "stm32f10x.h"
#include <stddef.h>

#define SOFT_LINES (GPIO_Pin_0 | GPIO_Pin_8)
#define CYCLES (*(volatile uint32_t *)0xE0001004UL)
static TIM_TypeDef *const timers[2] = {TIM3, TIM4};
static const uint8_t a_pin[2] = {0, 8}, b_pin[2] = {1, 9};
static encoder_edge_filter_t filter[2];
static encoder_irq_budget_t budget[2];
static volatile uint8_t started, failed_mask;
static uint16_t previous[2];
static uint32_t accumulated[2];
volatile uint32_t dashboard_encoder_irqs[2];

static uint32_t lock_irqs(void)
{
    uint32_t old;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(old) :: "memory");
    return old;
}
static void unlock_irqs(uint32_t old)
{
    __asm volatile("msr primask, %0" :: "r"(old) : "memory");
}
static int32_t signed_count(uint32_t v)
{
    return v <= INT32_MAX ? (int32_t)v : -(int32_t)(UINT32_MAX-v)-1;
}
static void timer_init(TIM_TypeDef *timer)
{
    TIM_DeInit(timer);
    timer->PSC=0; timer->ARR=0xffffU;
    timer->CCMR1=TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0 | (3U<<4) | (3U<<12);
    timer->CCMR2=0; timer->CCER=TIM_CCER_CC1E | TIM_CCER_CC2E;
    timer->SMCR=TIM_EncoderMode_TI12; timer->DIER=0;
    timer->EGR=TIM_EGR_UG; timer->SR=0; timer->CNT=0;
    timer->CR1=TIM_CR1_CEN;
}
void lower_encoder_start(void)
{
    GPIO_InitTypeDef g;
    uint32_t pins, clock_us=SystemCoreClock/1000000U;
    unsigned i;
    if(started || lower_motor_is_enabled() || !clock_us) return;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO,ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3 | RCC_APB1Periph_TIM4,ENABLE);
    /* TIM3 CH1/2 -> PB4/5. Only JTAG is disabled; SWD stays enabled. */
    AFIO->MAPR=(AFIO->MAPR & ~(AFIO_MAPR_TIM3_REMAP | AFIO_MAPR_TIM4_REMAP |
                              AFIO_MAPR_SWJ_CFG)) |
                AFIO_MAPR_TIM3_REMAP_PARTIALREMAP | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    GPIO_StructInit(&g); g.GPIO_Mode=GPIO_Mode_IPU; g.GPIO_Speed=GPIO_Speed_2MHz;
    g.GPIO_Pin=GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_4 | GPIO_Pin_5 |
               GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOB,&g);
    for(i=0;i<2;i++) timer_init(timers[i]);
    EXTI->IMR &= ~SOFT_LINES;
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB,GPIO_PinSource0);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB,GPIO_PinSource8);
    EXTI->RTSR |= SOFT_LINES; EXTI->FTSR |= SOFT_LINES;
    EXTI->EMR &= ~SOFT_LINES; EXTI->PR=SOFT_LINES;
    pins=GPIOB->IDR;
    for(i=0;i<2;i++) {
        encoder_edge_filter_init(&filter[i],(uint8_t)((pins>>a_pin[i])&1U),clock_us*20U);
        encoder_irq_budget_init(&budget[i],CYCLES,clock_us*1000U,32U);
    }
    NVIC_ClearPendingIRQ(EXTI0_IRQn); NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
    NVIC_SetPriority(EXTI0_IRQn,2U); NVIC_SetPriority(EXTI9_5_IRQn,2U);
    NVIC_EnableIRQ(EXTI0_IRQn); NVIC_EnableIRQ(EXTI9_5_IRQn);
    started=1; EXTI->IMR |= SOFT_LINES;
}
static void encoder_irq(unsigned i)
{
    uint32_t line=1UL<<a_pin[i], now, pins;
    if(!(EXTI->PR & line)) return;
    EXTI->PR=line;
    if(!started || (failed_mask & (1U<<i))) return;
    now=CYCLES; dashboard_encoder_irqs[i]++;
    if(!encoder_irq_budget_allow(&budget[i],now)) {
        failed_mask |= (uint8_t)(1U<<i);
        EXTI->IMR &= ~line;
        /* Bound interrupt load, then retry from the owner loop. Sampling
         * noise must not permanently latch off unrelated manual PWM. */
        return;
    }
    pins=GPIOB->IDR;
    encoder_edge_filter_irq(&filter[i],now,(uint8_t)((pins>>a_pin[i])&1U),
                            (uint8_t)((pins>>b_pin[i])&1U));
}
void EXTI0_IRQHandler(void) { encoder_irq(0); }
void EXTI9_5_IRQHandler(void) { encoder_irq(1); }
uint8_t lower_encoder_sample(int32_t counts[4])
{
    uint32_t old, pins, soft[2];
    uint16_t current[2];
    uint8_t valid;
    unsigned i;
    if(!counts) return 0;
    if(!started) { for(i=0;i<4;i++) counts[i]=0; return 0; }
    old=lock_irqs(); pins=GPIOB->IDR;
    for(i=0;i<2;i++) {
        current[i]=(uint16_t)timers[i]->CNT;
        if(!(failed_mask & (1U<<i)))
            encoder_edge_filter_poll(&filter[i],CYCLES,(uint8_t)((pins>>a_pin[i])&1U),
                                     (uint8_t)((pins>>b_pin[i])&1U));
        soft[i]=filter[i].count;
    }
    valid=(uint8_t)(15U & ~((uint32_t)failed_mask<<2));
    for(i=0;i<2;i++) if((failed_mask&(1U<<i)) &&
        (uint32_t)(CYCLES-budget[i].start)>=budget[i].window_ticks) {
        filter[i].stable_a=(uint8_t)((pins>>a_pin[i])&1U);
        filter[i].pending=0;
        encoder_irq_budget_init(&budget[i],CYCLES,SystemCoreClock/1000U,32U);
        EXTI->PR=1UL<<a_pin[i];
        failed_mask&=(uint8_t)~(1U<<i);
        EXTI->IMR|=1UL<<a_pin[i];
    }
    unlock_irqs(old);
    for(i=0;i<2;i++) {
        uint16_t difference=(uint16_t)(current[i]-previous[i]);
        int32_t delta=difference<=32767U ? (int32_t)difference : (int32_t)difference-65536;
        previous[i]=current[i]; accumulated[i]+=(uint32_t)delta;
        counts[i]=signed_count(accumulated[i]); counts[i+2]=signed_count(soft[i]);
    }
    /* Valid means the sampler is healthy, not proof that a cable is present. */
    return valid;
}
