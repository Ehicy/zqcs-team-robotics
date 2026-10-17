#ifndef CRTC_BOARD_LINK_CONFIG_H
#define CRTC_BOARD_LINK_CONFIG_H

/* One source of truth. UART1 on ESP32 is GPIO-matrix routed, NOT UART0's
 * TX/RX header. STM32 USART2 conflicts with the historical TT PWM target;
 * the board-link target deliberately excludes that motor driver. */
#define LINK_BAUD                    115200UL
#define LINK_ESP_UART                1
#define LINK_ESP_TX_GPIO              4 /* ESP GPIO4 -> STM32 PA3 RX */
#define LINK_ESP_RX_GPIO              6 /* ESP GPIO6 <- STM32 PA2 TX; measured on board */
#define LINK_STM_UART                2
#define LINK_STM_TX_PIN              2 /* PA2 */
#define LINK_STM_RX_PIN              3 /* PA3 */
#define LINK_STM_DRIVER_INHIBIT_PIN  15 /* PB15, reserved TB6612 STBY */
#define LINK_VERSION                 1U
#define LINK_MAX_PAYLOAD            20U
#define LINK_FRAME_TIMEOUT_MS       50U
#define LINK_PEER_TIMEOUT_MS       300U
#define LINK_COMMAND_MAX_TTL_MS    200U
#define LINK_COMMAND_PERIOD_MS      50U
#define LINK_STATUS_PERIOD_MS      100U
#define LINK_HELLO_PERIOD_MS       100U
#define LINK_TARGET_LIMIT_MM_S    1000
#define LINK_PWM_LIMIT_PERMILLE    250 /* first open-loop bench firmware: 25% */
#define LINK_ACTUATION_ENABLED       0 /* legacy board-link target invariant;
                                       use the separate lower-motor target for PWM */
/* Resource reservation for the confirmed two TB6612 boards and four MG370s.
 * GPIO port index: 0=A, 1=B. board-link keeps outputs inhibited; the separate
 * lower-motor target implements these pins and raw encoder sampling.
 * TIM1/2/3 encoder multipliers=4; M4 A dual-edge EXTI10 multiplier=2.
 * PPR definition, levels, direction and counts/rev require actual verification. */
#define LINK_STM_ENCODER_PORTS      {0,0,0,1}
#define LINK_STM_ENCODER_A_PINS     {0,6,8,10}
#define LINK_STM_ENCODER_B_PINS     {1,7,9,11}
#define LINK_STM_ENCODER_TIMERS     {2,3,1,0}
#define LINK_STM_ENCODER_MULTIPLIERS {4,4,4,2}
#define LINK_STM_MOTOR_PWM_TIMER    4
#define LINK_STM_MOTOR_PWM_PINS     {6,7,8,9} /* all GPIOB */
#define LINK_STM_MOTOR_IN1_PINS     {0,3,5,13} /* all GPIOB */
#define LINK_STM_MOTOR_IN2_PINS     {1,4,12,14} /* all GPIOB */
#define LINK_ENCODER_SAMPLE_MAX_AGE_MS 200U
#define LINK_ENCODER_PERIOD_MS     100U
#define LINK_ESP_ARM_SERVO_GPIO       7 /* reserved DS3128 arm signal, not initialized; GPIO4 is UART TX */
#define LINK_ESP_FOREARM_SERVO_GPIO   5 /* reserved MG995 forearm signal, not initialized */
#if LINK_ACTUATION_ENABLED != 0
#error Do not enable board-link this way: build the separate lower-motor target
#endif
#endif
