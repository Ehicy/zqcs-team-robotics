/* 仅用于电脑测试：模拟引脚及比较寄存器，不模拟真实 PWM 波形/电流。 */
#include "mock_hardware.h"
#include "delay.h"
#include <assert.h>
#include <string.h>

uint16_t mock_ccr[8];
uint16_t mock_gpio_a;
uint16_t mock_gpio_b;
uint16_t mock_gpio_c;
uint32_t mock_apb2;
uint16_t mock_af_a, mock_af_b;
uint16_t mock_period[2];
uint16_t mock_prescaler[2];
uint32_t mock_millis;
static uint8_t incoming[9];
static uint8_t outgoing[9];
static size_t input_count;
static size_t read_bit;
static size_t sent_bit;

void mock_frame(const uint8_t *bytes, size_t count)
{
    assert(count <= sizeof incoming);
    memset(incoming, 0xFF, sizeof incoming);
    memcpy(incoming, bytes, count);
    memset(outgoing, 0, sizeof outgoing);
    input_count = count;
    read_bit = 0;
    sent_bit = 0;
}

void mock_assert_poll_complete(void)
{
    static const uint8_t request[9] = {1, 0x42, 0, 0, 0, 0, 0, 0, 0};
    assert(read_bit == 72 && sent_bit == 72);
    assert(memcmp(outgoing, request, sizeof request) == 0);
    assert((mock_gpio_b & (GPIO_Pin_15 | GPIO_Pin_12 | GPIO_Pin_13)) ==
           (GPIO_Pin_15 | GPIO_Pin_12 | GPIO_Pin_13));
}

void RCC_APB1PeriphClockCmd(uint32_t periph, FunctionalState state)
{
    assert(periph == (RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3) && state == ENABLE);
}
void RCC_APB2PeriphClockCmd(uint32_t periph, FunctionalState state)
{
    assert(state == ENABLE);
    mock_apb2 |= periph;
}
void GPIO_StructInit(GPIO_InitTypeDef *gpio) { memset(gpio, 0, sizeof *gpio); }
void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio)
{
    if (gpio->GPIO_Mode == GPIO_Mode_AF_PP)
    {
        unsigned i;
        for (i = 0; i < 8; i++) assert(mock_ccr[i] == 0);
        if (port == GPIOA) mock_af_a |= gpio->GPIO_Pin;
        else mock_af_b |= gpio->GPIO_Pin;
    }
    if (port == GPIOA) assert(mock_apb2 & RCC_APB2Periph_GPIOA);
    else if (port == GPIOB) assert(mock_apb2 & RCC_APB2Periph_GPIOB);
    else
    {
        assert(port == GPIOC && (mock_apb2 & RCC_APB2Periph_GPIOC));
        assert(gpio->GPIO_Pin == GPIO_Pin_13 && gpio->GPIO_Mode == GPIO_Mode_Out_PP);
        assert(gpio->GPIO_Speed == GPIO_Speed_2MHz);
        assert(mock_gpio_c & GPIO_Pin_13); /* 切输出前已预置灭灯 */
    }
}
void GPIO_SetBits(GPIO_TypeDef *port, uint16_t pins)
{
    if (port == GPIOA)
    {
        mock_gpio_a |= pins;
    }
    else if (port == GPIOC)
    {
        assert(pins == GPIO_Pin_13);
        mock_gpio_c |= pins;
    }
    else
    {
        assert(port == GPIOB);
        mock_gpio_b |= pins;
    }
}
void GPIO_ResetBits(GPIO_TypeDef *port, uint16_t pins)
{
    if (port == GPIOA)
    {
        mock_gpio_a &= (uint16_t)~pins;
    }
    else if (port == GPIOC)
    {
        assert(pins == GPIO_Pin_13);
        mock_gpio_c &= (uint16_t)~pins;
    }
    else
    {
        assert(port == GPIOB);
        if ((pins & GPIO_Pin_13) && !(mock_gpio_b & GPIO_Pin_12))
        {
            assert(sent_bit < input_count * 8);
            if (mock_gpio_b & GPIO_Pin_15)
                outgoing[sent_bit / 8] |= (uint8_t)(1U << (sent_bit % 8));
            sent_bit++;
        }
        mock_gpio_b &= (uint16_t)~pins;
    }
}
uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef *port, uint16_t pin)
{
    uint8_t value;
    assert(port == GPIOB && pin == GPIO_Pin_14);
    assert(!(mock_gpio_b & GPIO_Pin_12)); /* CS 必须有效 */
    assert(!(mock_gpio_b & GPIO_Pin_13)); /* 原协议在低半周期末采样 */
    assert(read_bit < input_count * 8 && sent_bit == read_bit + 1);
    value = (uint8_t)((incoming[read_bit / 8] >> (read_bit % 8)) & 1U);
    read_bit++;
    return value;
}
static unsigned timer_index(TIM_TypeDef *tim)
{
    assert(tim == TIM2 || tim == TIM3);
    return tim == TIM2 ? 0U : 1U;
}
void TIM_DeInit(TIM_TypeDef *timer)
{
    memset(mock_ccr + timer_index(timer) * 4, 0, 4 * sizeof mock_ccr[0]);
}
void TIM_TimeBaseStructInit(TIM_TimeBaseInitTypeDef *timer)
{ memset(timer, 0, sizeof *timer); }
void TIM_InternalClockConfig(TIM_TypeDef *timer) { (void)timer_index(timer); }
void TIM_TimeBaseInit(TIM_TypeDef *timer, TIM_TimeBaseInitTypeDef *config)
{
    mock_period[timer_index(timer)] = config->TIM_Period;
    mock_prescaler[timer_index(timer)] = config->TIM_Prescaler;
}
void TIM_OCStructInit(TIM_OCInitTypeDef *channel)
{ memset(channel, 0, sizeof *channel); }
static void init_channel(TIM_TypeDef *tim, unsigned ch, TIM_OCInitTypeDef *c)
{
    assert(c->TIM_OCMode == TIM_OCMode_PWM1);
    assert(c->TIM_OCPolarity == TIM_OCPolarity_High);
    assert(c->TIM_OutputState == TIM_OutputState_Enable && c->TIM_Pulse == 0);
    mock_ccr[timer_index(tim) * 4 + ch] = c->TIM_Pulse;
}
void TIM_OC1Init(TIM_TypeDef *t, TIM_OCInitTypeDef *c) { init_channel(t,0,c); }
void TIM_OC2Init(TIM_TypeDef *t, TIM_OCInitTypeDef *c) { init_channel(t,1,c); }
void TIM_OC3Init(TIM_TypeDef *t, TIM_OCInitTypeDef *c) { init_channel(t,2,c); }
void TIM_OC4Init(TIM_TypeDef *t, TIM_OCInitTypeDef *c) { init_channel(t,3,c); }
void TIM_Cmd(TIM_TypeDef *timer, FunctionalState state)
{ (void)timer_index(timer); assert(state == ENABLE); }
static void set_compare(TIM_TypeDef *tim, unsigned ch, uint16_t value)
{
    unsigned index = timer_index(tim) * 4 + ch;
    /* 每次写寄存器后立即验证，包括正反切换的中间步骤。 */
    mock_ccr[index] = value;
    assert(value <= mock_period[timer_index(tim)] + 1U);
    assert(mock_ccr[index] == 0 || mock_ccr[index ^ 1U] == 0);
}
void TIM_SetCompare1(TIM_TypeDef *t, uint16_t v) { set_compare(t,0,v); }
void TIM_SetCompare2(TIM_TypeDef *t, uint16_t v) { set_compare(t,1,v); }
void TIM_SetCompare3(TIM_TypeDef *t, uint16_t v) { set_compare(t,2,v); }
void TIM_SetCompare4(TIM_TypeDef *t, uint16_t v) { set_compare(t,3,v); }
void delay_init(void) {}
void delay_us(uint16_t us) { (void)us; }
void delay_ms(uint16_t ms) { (void)ms; }
uint32_t delay_millis(void) { return mock_millis; }
