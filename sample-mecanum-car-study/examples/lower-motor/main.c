#include "stm32f10x.h"
#include "delay.h"
#include "board_link.h"
#include "motor_control.h"
#include "speed_control.h"
#include "actuation_guard.h"
#include "motor_hal.h"

#if LINK_PWM_LIMIT_PERMILLE != MOTOR_MAX_PERMILLE || LINK_PWM_LIMIT_PERMILLE != LOWER_MOTOR_PWM_LIMIT
#error PWM limits must agree across protocol, control and hardware
#endif

#if LINK_STM_UART != 2 || LINK_STM_TX_PIN != 2 || LINK_STM_RX_PIN != 3
#error This transport implements USART2 on PA2/PA3; update transport for another mapping
#endif
#define RX_RING_SIZE 128U
static volatile uint8_t rx_bytes[RX_RING_SIZE], rx_head, rx_tail, rx_fault;
static volatile uint32_t rx_ms[RX_RING_SIZE];
static link_node_t controller;
static motor_control_t motors;
static speed_control_t speeds;
static actuation_guard_t actuation_guard; /* Retained across STOP/new session. */
static uint8_t watchdog_fault_seen;
volatile uint32_t board_link_tx_frames, board_link_transport_errors;
volatile uint8_t board_link_led_pin_high;
volatile uint8_t board_link_outputs_inhibited=1;

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
            rx_bytes[rx_head]=byte; rx_ms[rx_head]=delay_millis();
            __DMB(); rx_head=next;
        }
    }
}
static void transport_fault(uint32_t now)
{
    uint32_t mask;
    lower_motor_emergency_stop();
    now=delay_millis(); actuation_guard_stop(&actuation_guard,now);
    __asm volatile ("mrs %0, primask\n\tcpsid i" : "=r" (mask) :: "memory");
    rx_tail=rx_head; rx_fault=0;
    __asm volatile ("msr primask, %0" :: "r" (mask) : "memory");
    link_node_transport_error(&controller,now); board_link_transport_errors++;
    (void)motor_control_update(&motors,now,0,0,now,0);
    speed_control_stop(&speeds,now);
    board_link_outputs_inhibited=1;
    link_node_set_motor_state(&controller,1,1);
    led_apply();
}
static uint8_t reported_fault(void)
{
    return lower_motor_watchdog_fault()?SPEED_FAULT_WATCHDOG:speeds.fault;
}
static void check_watchdog(uint32_t now)
{
    uint8_t fault=lower_motor_watchdog_fault();
    /* Revoke an old motion lease ONCE on the fault edge, then let HELLO,
     * telemetry, LED and stopped-only configuration continue. Persistent
     * hardware failure must not erase the receive queue every owner pass. */
    if(fault && !watchdog_fault_seen) transport_fault(now);
    watchdog_fault_seen=fault;
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
    int32_t initial_counts[4];
    uint32_t now, seed;
    lower_motor_init(); led_init(); delay_init();
    /* UID + cycle timing is a generation seed, NOT cryptographic entropy.
     * HELLO also mixes in the ESP's boot id. No flash is written. */
    seed=*(const uint32_t *)0x1ffff7e8UL ^ *(const uint32_t *)0xe0001004UL;
    link_node_init(&controller,LINK_CONTROLLER,seed,delay_millis());
    link_node_set_motor_state(&controller,1,1);
    motor_control_init(&motors,delay_millis());
    lower_encoder_sample(initial_counts);
    speed_control_init(&speeds,delay_millis(),initial_counts);
    uart_init(); lower_motor_watchdog_feed();
    while(1) {
        size_t length; unsigned budget; int32_t counts[4]; uint8_t enabled, encoder_mask, old_fault;
        now=delay_millis(); link_node_tick(&controller,now);
        if(rx_fault) transport_fault(now);
        check_watchdog(now);
        for(budget=0;budget<RX_RING_SIZE && rx_tail!=rx_head;budget++) {
            uint8_t tail=rx_tail, byte=rx_bytes[tail]; uint32_t arrival=rx_ms[tail];
            uint32_t accepted=controller.accepted_controls;
            uint32_t session=controller.token;
            __DMB(); rx_tail=(uint8_t)((tail+1U)%RX_RING_SIZE);
            now=delay_millis(); /* ISR may have queued this byte after loop entry. */
            if((uint32_t)(now-arrival)>=LINK_FRAME_TIMEOUT_MS) {
                transport_fault(now); break;
            }
            link_node_receive(&controller,byte,now);
            if(controller.token!=session) {
                lower_motor_emergency_stop();
                now=delay_millis(); actuation_guard_stop(&actuation_guard,now);
                (void)motor_control_update(&motors,now,0,0,now,0);
                speed_control_stop(&speeds,now);
            }
            /* Buffer residence consumes the lease; it is not fresh input. */
            if(controller.accepted_controls!=accepted) {
                unsigned wheel; uint8_t any=0;
                controller.command_ms=arrival;
                for(wheel=0;wheel<4;wheel++)
                    if((controller.requested_mode==LINK_PWM?controller.requested_pwm[wheel]:controller.requested_mm_s[wheel])!=0) any=1;
                if(!controller.requested_enable || !any) {
                    /* Even a zero velocity heartbeat must reach hardware before
                     * processing later input. Explicit STOP also revokes token. */
                    lower_motor_emergency_stop();
                    now=delay_millis(); actuation_guard_stop(&actuation_guard,now);
                    (void)motor_control_update(&motors,now,0,0,now,0);
                    speed_control_stop(&speeds,now);
                }
            }
        }
        now=delay_millis(); link_node_tick(&controller,now);
        encoder_mask=lower_encoder_sample(counts);
        old_fault=speeds.fault;
        enabled=!lower_motor_watchdog_fault() && controller.connected && controller.command_valid &&
            controller.requested_enable && controller.requested_mode==LINK_CONTROL &&
            actuation_guard_allows(&actuation_guard,LINK_CONTROL,now);
        (void)speed_control_update(&speeds,now,counts,encoder_mask,controller.requested_mm_s,
                                  enabled,controller.command_ms,controller.command_ttl_ms);
        enabled=!lower_motor_watchdog_fault() && controller.connected && controller.command_valid &&
            controller.requested_enable && controller.requested_mode==LINK_PWM && !speeds.fault &&
            actuation_guard_allows(&actuation_guard,LINK_PWM,now);
        (void)motor_control_update(&motors,now,controller.requested_pwm,enabled,
                                  controller.command_ms,controller.command_ttl_ms);
        if(motors.fault || (speeds.fault && speeds.fault!=old_fault) || rx_fault) transport_fault(now);
        check_watchdog(now);
        if(controller.config_received) {
            uint8_t result=1;
            /* Counts are sampled above even when disarmed. Never configure
             * while a command/output is enabled or measured wheels move. */
            if(!controller.requested_enable && !motors.active && !speeds.active &&
               actuation_guard_idle(&actuation_guard,now)) {
                if(controller.config_wheel==255) {
                    result=2;
                    if(speed_control_clear_fault(&speeds)) {
                        lower_motor_watchdog_clear();
                        if(!lower_motor_watchdog_fault()) result=0;
                    }
                }
                else {
                    const uint8_t *p=controller.config_payload; speed_config_t cfg;
                    cfg.motor_sign=(p[1]&1)?-1:1; cfg.encoder_sign=(p[1]&2)?-1:1;
                    cfg.confirmed=(p[1]&4)?1:0; cfg.diameter_tenth_mm=link_get_u16(p+2);
                    cfg.counts_per_rev=link_get_u32(p+4); cfg.kp_milli=link_get_u16(p+8);
                    cfg.ki_milli=link_get_u16(p+10); cfg.max_mm_s=link_get_u16(p+12);
                    cfg.accel_mm_s2=link_get_u16(p+14);
                    result=speed_control_configure(&speeds,controller.config_wheel,&cfg)?0:2;
                }
            }
            link_node_finish_config(&controller,result,speeds.ready_mask,reported_fault());
        }
        if(controller.requested_mode==LINK_CONTROL) lower_motor_write(speeds.pwm_permille,speeds.active);
        else lower_motor_write(motors.output_permille,motors.active);
        board_link_outputs_inhibited=lower_motor_is_enabled()?0U:1U;
        if(!board_link_outputs_inhibited) actuation_guard_record(&actuation_guard,controller.requested_mode,delay_millis());
        else actuation_guard_stop(&actuation_guard,delay_millis());
        link_node_set_motor_state(&controller,1,board_link_outputs_inhibited);
        link_node_publish_encoders(&controller,counts,encoder_mask,now);
        link_node_publish_speed(&controller,speeds.measured_mm_s,speeds.ramped_mm_s,
                                speeds.feedback_valid_mask,speeds.ready_mask,reported_fault(),now);
        led_apply();
        length=link_node_transmit(&controller,now,wire,sizeof(wire));
        if(length) {
            if(uart_write(wire,length)) board_link_tx_frames++;
            else transport_fault(delay_millis());
        }
        lower_motor_watchdog_feed(); /* Only after one complete bounded owner-loop pass. */
        delay_ms(1);
    }
}
