#ifndef MOTOR_TUNING_H
#define MOTOR_TUNING_H

/* 开环调试起点，不是本批 TT 的实测最优参数。
 * 公共方案依据：https://www.pololu.com/docs/0J77/5.2
 * 起转/单轮补偿参考：https://github.com/ArminJo/PWMMotorControl
 * 本工程用 C 实现其公开调参方法，没有引入 Arduino 运行库。
 */
#define MOTOR_ACCEL_PERCENT_PER_SECOND 200U /* 0 -> 100% 约 0.5 秒 */
#define MOTOR_DECEL_PERCENT_PER_SECOND 400U
#define MOTOR_REVERSAL_COAST_MS         80U /* 零输出等待，不是主动刹车 */
#define MOTOR_UPDATE_MAX_MS             20U /* 循环卡顿后也不突跳到目标 */

/* 顺序：左后、右后、左前、右前；方向指“让车前进/后退”的机械方向。
 * 每行：前进增益%、后退增益%、前进最低PWM%、后退最低PWM%。
 * 增益 0..100：只压低偏快的轮子。最低 PWM 未测，默认关闭(0)。
 * 最低 PWM 补偿也不超过当前速度档；非零补偿会改变混合轮速比例。
 */
#define MOTOR_CALIBRATION_DEFAULTS { \
    {100U, 100U, 0U, 0U}, \
    {100U, 100U, 0U, 0U}, \
    {100U, 100U, 0U, 0U}, \
    {100U, 100U, 0U, 0U}  \
}

#if MOTOR_ACCEL_PERCENT_PER_SECOND < 1 || MOTOR_DECEL_PERCENT_PER_SECOND < 1
#error Motor ramp rates must be positive
#endif
#if MOTOR_UPDATE_MAX_MS < 1 || MOTOR_UPDATE_MAX_MS > 1000
#error Motor update interval must be between 1 and 1000 ms
#endif
#endif
