#ifndef PWM_H
#define PWM_H
/* 默认 TIM2/TIM3 时钟 72 MHz：72 MHz / 1 / 3600 = 20 kHz。
 * 这是电机调试起点，需实测温升与低速表现；TIM4 留给舵机。
 * ARR = 周期计数数 - 1；PSC = 分频倍数 - 1。
 */
#define PWM_PERIOD_COUNTS  3600U
#define PWM_PRESCALER_DIV  1U
#if PWM_PERIOD_COUNTS < 1 || PWM_PERIOD_COUNTS > 65535
#error PWM_PERIOD_COUNTS must fit a 16-bit compare register
#endif
#if PWM_PRESCALER_DIV < 1 || PWM_PRESCALER_DIV > 65536
#error PWM_PRESCALER_DIV is outside the timer range
#endif

/* 独占 TIM2/TIM3，默认引脚映射。初始化后八个输入均为低。
 * 1: PA0/PA1; 2: PA2/PA3; 3: PA6/PA7; 4: PB0/PB1。
 * nSLEEP 由驱动板硬件保持高；本模块不控制 PA8。
 */
void pwm_init(void);
void pwm_stop_all(void);
/* DRV8833 xIN1=xIN2=1：两电机输出接低端的短路制动，非反向驱动。 */
void pwm_brake_all(void);
/* duty 改为带符号的 -1～1：越界限幅，NaN 归零。
 * 正向 PWM/0，反向 0/PWM；0/0 为滑行。仅在主循环调用。
 */
void pwm_set1(float duty);
void pwm_set2(float duty);
void pwm_set3(float duty);
void pwm_set4(float duty);
#endif
