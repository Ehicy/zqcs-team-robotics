#include "uart.h"
#include "delay.h"
#include "fallback_pins.h"
#include "stm32f10x.h"
#ifdef FB_UART3
#define PORT GPIOB
#define TX_PIN (1U << FB_MOTOR_TX_PIN)
#define RX_PIN (1U << FB_MOTOR_RX_PIN)
#define GPIO_CLOCK RCC_APB2Periph_GPIOB
#define UART_CLOCK RCC_APB1Periph_USART3
#define UART USART3
#define UART_IRQn USART3_IRQn
#define UART_HANDLER USART3_IRQHandler
#else
#define PORT GPIOA
#define TX_PIN (1U << FB_REMOTE_TX_PIN)
#define RX_PIN (1U << FB_REMOTE_RX_PIN)
#define GPIO_CLOCK RCC_APB2Periph_GPIOA
#define UART_CLOCK RCC_APB1Periph_USART2
#define UART USART2
#define UART_IRQn USART2_IRQn
#define UART_HANDLER USART2_IRQHandler
#endif
static volatile uint8_t data[128], head, tail, fault;
static volatile uint32_t times[128];
void fallback_uart_init(void)
{
    GPIO_InitTypeDef g;
    RCC_ClocksTypeDef c;
    RCC_APB2PeriphClockCmd(GPIO_CLOCK, ENABLE);
    RCC_APB1PeriphClockCmd(UART_CLOCK, ENABLE);
    GPIO_StructInit(&g);
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Pin = TX_PIN;
    g.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(PORT, &g);
    g.GPIO_Pin = RX_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(PORT, &g);
    RCC_GetClocksFreq(&c);
    UART->CR1 = UART->CR2 = UART->CR3 = 0;
    UART->BRR = (uint16_t)((c.PCLK1_Frequency + 57600) / 115200);
    NVIC_SetPriority(UART_IRQn, 1);
    NVIC_EnableIRQ(UART_IRQn);
    UART->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
}
void UART_HANDLER(void)
{
    uint32_t status = UART->SR;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE))
    {
        uint8_t b = (uint8_t)UART->DR, next = (uint8_t)((head + 1U) % 128U);
        if (status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE) || next == tail)
            fault = 1;
        else if (status & USART_SR_RXNE)
        {
            data[head] = b;
            times[head] = delay_millis();
            __DMB();
            head = next;
        }
    }
}
int fallback_uart_get(uint8_t *b, uint32_t *arrival)
{
    uint8_t t = tail;
    if (t == head)
        return 0;
    *b = data[t];
    *arrival = times[t];
    __DMB();
    tail = (uint8_t)((t + 1U) % 128U);
    return 1;
}
int fallback_uart_fault(void)
{
    if (!fault)
        return 0;
    __disable_irq();
    tail = head;
    fault = 0;
    __enable_irq();
    return 1;
}
int fallback_uart_write(const uint8_t *b, size_t count)
{
    size_t i;
    uint32_t start = delay_millis();
    for (i = 0; i < count; i++)
    {
        while (!(UART->SR & USART_SR_TXE))
            if (delay_millis() - start >= 10)
                return 0;
        UART->DR = b[i];
    }
    return 1;
}
