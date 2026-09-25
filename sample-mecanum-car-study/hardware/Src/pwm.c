#include "pwm.h"
#include "stm32f10x.h"

#define MOTOR_PA_PINS (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | \
                       GPIO_Pin_6 | GPIO_Pin_7)
#define MOTOR_PB_PINS (GPIO_Pin_0 | GPIO_Pin_1)

static void timer_init(TIM_TypeDef *tim)
{
    TIM_TimeBaseInitTypeDef timer;
    TIM_OCInitTypeDef channel;
    /* 复位同时关闭 CCR 预装载；停车/换向时写零立即生效。 */
    TIM_DeInit(tim);
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_Period = PWM_PERIOD_COUNTS - 1U;
    timer.TIM_Prescaler = PWM_PRESCALER_DIV - 1U;
    TIM_InternalClockConfig(tim);
    TIM_TimeBaseInit(tim, &timer);
    TIM_OCStructInit(&channel);
    channel.TIM_OCMode = TIM_OCMode_PWM1;
    channel.TIM_OCPolarity = TIM_OCPolarity_High;
    channel.TIM_OutputState = TIM_OutputState_Enable;
    channel.TIM_Pulse = 0;
    TIM_OC1Init(tim, &channel);
    TIM_OC2Init(tim, &channel);
    TIM_OC3Init(tim, &channel);
    TIM_OC4Init(tim, &channel);
}

void pwm_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    /* 先将全部电机输入保持低，再配置定时器和复用输出。 */
    GPIO_ResetBits(GPIOA, MOTOR_PA_PINS);
    GPIO_ResetBits(GPIOB, MOTOR_PB_PINS);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = MOTOR_PA_PINS;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = MOTOR_PB_PINS;
    GPIO_Init(GPIOB, &gpio);
    timer_init(TIM2);
    timer_init(TIM3);
    /* 默认映射：TIM2 CH1..4=PA0..3，TIM3 CH1..4=PA6/PA7/PB0/PB1。 */
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = MOTOR_PA_PINS;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = MOTOR_PB_PINS;
    GPIO_Init(GPIOB, &gpio);
    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

static uint16_t duty_to_compare(float duty)
{
    if (!(duty > 0.0f)) return 0;
    if (duty >= 1.0f) return PWM_PERIOD_COUNTS;
    /* CCR=0 -> 0%，CCR=ARR+1 -> 100%。 */
    return (uint16_t)(duty * PWM_PERIOD_COUNTS);
}

/* 一个 H 桥占用相邻两个通道。无预装载，先清反向再加正向，
 * 正反切换不会短暂进入 1/1 制动；不提供机械反转等待或电流限制。
 */
static void set_pair(TIM_TypeDef *tim, uint8_t first_pair, float duty)
{
    uint16_t compare = duty_to_compare(duty < 0.0f ? -duty : duty);
    if (first_pair)
    {
        if (duty > 0.0f)
        {
            TIM_SetCompare2(tim, 0);
            TIM_SetCompare1(tim, compare);
        }
        else
        {
            TIM_SetCompare1(tim, 0);
            TIM_SetCompare2(tim, compare);
        }
    }
    else
    {
        if (duty > 0.0f)
        {
            TIM_SetCompare4(tim, 0);
            TIM_SetCompare3(tim, compare);
        }
        else
        {
            TIM_SetCompare3(tim, 0);
            TIM_SetCompare4(tim, compare);
        }
    }
}

void pwm_set1(float duty) { set_pair(TIM2, 1, duty); }
void pwm_set2(float duty) { set_pair(TIM2, 0, duty); }
void pwm_set3(float duty) { set_pair(TIM3, 1, duty); }
void pwm_set4(float duty) { set_pair(TIM3, 0, duty); }

void pwm_stop_all(void)
{
    pwm_set1(0.0f);
    pwm_set2(0.0f);
    pwm_set3(0.0f);
    pwm_set4(0.0f);
}
