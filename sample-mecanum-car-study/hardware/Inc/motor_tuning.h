#ifndef MOTOR_TUNING_H
#define MOTOR_TUNING_H

/* 开环调试起点，不是本批 TT 的实测最优参数。
 * 公共方案依据：https://www.pololu.com/docs/0J77/5.2
 * 起转/单轮补偿参考：https://github.com/ArminJo/PWMMotorControl
 * 本工程用 C 实现其公开调参方法，没有引入 Arduino 运行库。
 */
#define MOTOR_ACCEL_PERCENT_PER_SECOND 500U /* 0 -> 100% 约 0.2 秒，减轻黏糊 */
#define MOTOR_DECEL_PERCENT_PER_SECOND 1000U
#define MOTOR_REVERSAL_COAST_MS         40U /* 零输出等待，不是主动刹车 */
#define MOTOR_UPDATE_MAX_MS             20U /* 循环卡顿后也不突跳到目标 */

/* 2026-10-02 横移带弧线的试验补偿：推测前轮横向作用偏强，尚未实测标定。
 * 仅缩放麦轮混合中的横移分量，纯前后和纯右杆旋转保持原值。
 * 无有效Flash记录时默认前轮85%、后轮100%；已保存值优先。
 * SELECT+叉恢复前后100/100，停车回中后保存，可撤销横移补偿。
 * 斜移/平移加旋转也含横移分量，因此同样受补偿影响，须实车重测。
 */
#define MOTOR_STRAFE_FRONT_PERCENT      85U
#define MOTOR_STRAFE_REAR_PERCENT       100U
#define MOTOR_CAL_SAVE_IDLE_MS         2000U
#define MOTOR_CAL_SAVE_INTERVAL_MS     30000U
#define MOTOR_CAL_LEARN_SETTLE_MS      600U
#define MOTOR_CAL_LEARN_SESSION_LIMIT  10U /* 每次按住SELECT最多改变10个百分点 */
#define MOTOR_CAL_LEARN_RATE           5.0f /* 2*纠偏/横移量 × 5个百分点/秒 */

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
#if MOTOR_STRAFE_FRONT_PERCENT < 50 || MOTOR_STRAFE_FRONT_PERCENT > 100 || \
    MOTOR_STRAFE_REAR_PERCENT < 50 || MOTOR_STRAFE_REAR_PERCENT > 100
#error Strafe gains must be between 50 and 100 percent
#endif
#endif
