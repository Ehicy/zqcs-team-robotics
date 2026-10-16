#include "stm32f10x.h"
#include "delay.h"
#include "board_link.h"

#if LINK_STM_UART != 2 || LINK_STM_TX_PIN != 2 || LINK_STM_RX_PIN != 3
#error This transport implements USART2 on PA2/PA3; update transport for another mapping
#endif
#define RX_RING_SIZE 128U
static volatile uint8_t rx_bytes[RX_RING_SIZE], rx_head, rx_tail, rx_fault;
static volatile uint32_t rx_ms[RX_RING_SIZE], clock_hint_ms;
static link_node_t controller;
volatile uint32_t board_link_tx_frames, board_link_transport_errors;
volatile uint8_t board_link_led_pin_high;
volatile const uint8_t board_link_outputs_inhibited=1;

/* New controller target only. Never initialize the old TIM2/TIM3 PWM driver
 * or PS2, which would conflict with USART2 or PB15. Hardware STBY wiring and
 * pull-down are still required before any later motor firmware is used. */
static void outputs_inhibit(void)
{
    GPIO_InitTypeDef gpio;
    static const uint8_t pwm_pins[4]=LINK_STM_MOTOR_PWM_PINS;
    uint16_t pins=(uint16_t)(1U<<LINK_STM_DRIVER_INHIBIT_PIN); unsigned i;
    for(i=0;i<4;i++) pins|=(uint16_t)(1U<<pwm_pins[i]);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
    GPIO_ResetBits(GPIOB,pins);
    GPIO_StructInit(&gpio); gpio.GPIO_Mode=GPIO_Mode_Out_PP;
    gpio.GPIO_Speed=GPIO_Speed_2MHz;
    gpio.GPIO_Pin=pins;
    GPIO_Init(GPIOB,&gpio);
}
/* PC13 board LED is active-low. This diagnostic never drives motor pins. */
static void led_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC,ENABLE);
    GPIO_SetBits(GPIOC,GPIO_Pin_13); /* Preload OFF before enabling the output. */
    GPIO_StructInit(&gpio); gpio.GPIO_Mode=GPIO_Mode_Out_PP;
    gpio.GPIO_Speed=GPIO_Speed_2MHz; gpio.GPIO_Pin=GPIO_Pin_13;
    GPIO_Init(GPIOC,&gpio);
    board_link_led_pin_high=(GPIOC->ODR&GPIO_Pin_13)?1U:0U;
}
static void led_apply(void)
{
    if(controller.led_on) GPIO_ResetBits(GPIOC,GPIO_Pin_13);
    else GPIO_SetBits(GPIOC,GPIO_Pin_13);
    /* ODR readback for SWD diagnostics; this is not optical feedback. */
    board_link_led_pin_high=(GPIOC->ODR&GPIO_Pin_13)?1U:0U;
}
static void uart_init(void)
{
    GPIO_InitTypeDef gpio; RCC_ClocksTypeDef clocks; NVIC_InitTypeDef irq;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,ENABLE);
    GPIO_StructInit(&gpio); gpio.GPIO_Speed=GPIO_Speed_50MHz;
    gpio.GPIO_Pin=(uint16_t)(1U<<LINK_STM_TX_PIN); gpio.GPIO_Mode=GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA,&gpio);
    gpio.GPIO_Pin=(uint16_t)(1U<<LINK_STM_RX_PIN); gpio.GPIO_Mode=GPIO_Mode_IPU;
    GPIO_Init(GPIOA,&gpio);
    RCC_GetClocksFreq(&clocks);
    USART2->CR1=0; USART2->CR2=0; USART2->CR3=0;
    USART2->BRR=(uint16_t)((clocks.PCLK1_Frequency+LINK_BAUD/2U)/LINK_BAUD);
    irq.NVIC_IRQChannel=USART2_IRQn; irq.NVIC_IRQChannelPreemptionPriority=1;
    irq.NVIC_IRQChannelSubPriority=0; irq.NVIC_IRQChannelCmd=ENABLE;
    NVIC_Init(&irq);
    USART2->CR1=USART_CR1_UE|USART_CR1_TE|USART_CR1_RE|USART_CR1_RXNEIE;
}
void USART2_IRQHandler(void)
{
    uint32_t status=USART2->SR;
    if(status&(USART_SR_RXNE|USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)) {
        uint8_t byte=(uint8_t)USART2->DR; /* SR then DR clears RX/errors */
        uint8_t next=(uint8_t)((rx_head+1U)%RX_RING_SIZE);
        if(status&(USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)) rx_fault=1;
        else if(next==rx_tail) rx_fault=1;
        else if(status&USART_SR_RXNE) {
            rx_bytes[rx_head]=byte; rx_ms[rx_head]=clock_hint_ms;
            __DMB(); rx_head=next;
        }
    }
}
static void transport_fault(uint32_t now)
{
    __disable_irq(); rx_tail=rx_head; rx_fault=0; __enable_irq();
    link_node_transport_error(&controller,now); board_link_transport_errors++;
    led_apply();
}
static int uart_write(const uint8_t *bytes,size_t count)
{
    uint32_t start=delay_millis(); size_t i;
    for(i=0;i<count;i++) {
        while(!(USART2->SR&USART_SR_TXE))
            if((uint32_t)(delay_millis()-start)>=10U) return 0;
        USART2->DR=bytes[i];
    }
    return 1;
}
int main(void)
{
    uint8_t wire[LINK_WIRE_MAX];
    uint32_t now, seed;
    outputs_inhibit(); led_init(); delay_init();
    /* UID + cycle timing is a generation seed, NOT cryptographic entropy.
     * HELLO also mixes in the ESP's boot id. No flash is written. */
    seed=*(const uint32_t *)0x1ffff7e8UL ^ *(const uint32_t *)0xe0001004UL;
    link_node_init(&controller,LINK_CONTROLLER,seed,delay_millis());
    clock_hint_ms=delay_millis(); uart_init();
    while(1) {
        size_t length;
        now=delay_millis(); clock_hint_ms=now; link_node_tick(&controller,now);
        if(rx_fault) transport_fault(now);
        while(rx_tail!=rx_head) {
            uint8_t tail=rx_tail, byte=rx_bytes[tail]; uint32_t arrival=rx_ms[tail];
            __DMB(); rx_tail=(uint8_t)((tail+1U)%RX_RING_SIZE);
            if((uint32_t)(now-arrival)>=LINK_FRAME_TIMEOUT_MS) {
                transport_fault(now); break;
            }
            link_node_receive(&controller,byte,now);
        }
        led_apply();
        length=link_node_transmit(&controller,now,wire,sizeof(wire));
        if(length) {
            if(uart_write(wire,length)) board_link_tx_frames++;
            else transport_fault(delay_millis());
        }
        outputs_inhibit(); /* No packet can release this gate. */
        delay_ms(1);
    }
}
