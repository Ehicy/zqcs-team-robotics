#ifndef CRTC_FALLBACK_PINS_H
#define CRTC_FALLBACK_PINS_H
/* MOTOR BOARD ONLY: all wheel signals on GPIOA. USB PA11/12 and SWD
 * PA13/14 stay reserved. Shared STBY is the one GPIOB control wire.
 * Wheel order: RF, LF, RR, LR. Do not use the old lower-motor pin map. */
#define FB_PWM_PINS {0, 1, 2, 3} /* TIM2 CH1..4, GPIOA */
#define FB_IN1_PINS {4, 6, 8, 10}
#define FB_IN2_PINS {5, 7, 9, 15}
#define FB_STBY_PIN 15U     /* GPIOB */
#define FB_MOTOR_TX_PIN 10U /* GPIOB USART3 */
#define FB_MOTOR_RX_PIN 11U
#define FB_REMOTE_TX_PIN 2U /* GPIOA USART2 */
#define FB_REMOTE_RX_PIN 3U
#endif
