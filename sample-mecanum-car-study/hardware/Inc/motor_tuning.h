#ifndef MOTOR_TUNING_H
#define MOTOR_TUNING_H

/* 开环调试起点，不是本批 TT 的实测最优参数。
 * 公共方案依据：https://www.pololu.com/docs/0J77/5.2
 * 起转/单轮补偿参考：https://github.com/ArminJo/PWMMotorControl
 * 本工程用 C 实现其公开调参方法，没有引入 Arduino 运行库。
 */
#define MOTOR_ACCEL_PERCENT_PER_SECOND 200U /* 行驶中普通增幅；零起步使用下方S曲线 */
#define MOTOR_DECEL_PERCENT_PER_SECOND 1000U
#define MOTOR_REVERSAL_COAST_MS         40U /* 零输出等待，不是主动刹车 */
#define MOTOR_UPDATE_MAX_MS             20U /* 循环卡顿后也不突跳到目标 */
#define MOTOR_STARTUP_CURVE_MS          500U /* 满幅五次S曲线起步时间 */
#define MOTOR_STARTUP_MIN_MS            80U  /* 小幅目标按比例缩短，但不小于80ms */
#define MOTOR_STARTUP_MAX_RATE          375U /* 五次曲线满幅峰值斜率1.875/0.5秒 */
/* 开环试验：按先前PWM比例反向10%，满幅时最多10%反向占空比。
 * 无测速/电流反馈，不是PID，也不能证明脉冲结束时转子已停。
 */
#define MOTOR_STOP_REVERSE_PERCENT      10U
#define MOTOR_STOP_REVERSE_MIN_PERCENT  30U /* 低于此前30%输出只用短路制动 */
#define MOTOR_STOP_COAST_MS             20U
#define MOTOR_STOP_REVERSE_MS           30U
#define MOTOR_STOP_BRAKE_MS             100U

/* 曲线混合：0=线性，100=纯三次；保留中心斜率，避免纯三次过黏。
 * 左杆两轴用共同增益保持死区处理后的方向；右杆旋转单独映射。
 * https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput#dead-zone
 */
#define MOTOR_TRANSLATION_EXPO_PERCENT  25U
#define MOTOR_TURN_EXPO_PERCENT         50U

/* 2026-10-02 用户反馈85/100横移更差：反向缩小为前100/后95，接近原始值。
 * 仅缩放麦轮混合中的横移分量，纯前后和纯右杆旋转保持原值。
 * 无本版有效Flash记录时默认前轮100%、后轮95%；本版保存值优先。
 * SELECT+叉恢复前后100/100，停车回中后保存，可撤销横移补偿。
 * 斜移/平移加旋转也含横移分量，因此同样受补偿影响，须实车重测。
 */
#define MOTOR_STRAFE_FRONT_PERCENT      100U
#define MOTOR_STRAFE_REAR_PERCENT       95U
#define MOTOR_CAL_SAVE_IDLE_MS         2000U
#define MOTOR_CAL_SAVE_INTERVAL_MS     30000U
#define MOTOR_CAL_LEARN_SETTLE_MS      600U
#define MOTOR_CAL_LEARN_SESSION_LIMIT  5U /* 每次按住SELECT最多改变5个百分点 */
#define MOTOR_CAL_LEARN_RATE           2.0f /* 2*纠偏/横移量 × 2个百分点/秒 */

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
#if MOTOR_STARTUP_CURVE_MS < 1 || MOTOR_STARTUP_MIN_MS < 1 || \
    MOTOR_STARTUP_MIN_MS > MOTOR_STARTUP_CURVE_MS || MOTOR_STARTUP_MAX_RATE < 1
#error Invalid startup curve timing or slope
#endif
#if MOTOR_STOP_REVERSE_PERCENT > 20 || MOTOR_STOP_REVERSE_MIN_PERCENT > 100 || \
    MOTOR_STOP_REVERSE_MS < 1 || MOTOR_STOP_REVERSE_MS > 100 || \
    MOTOR_STOP_COAST_MS < 1 || MOTOR_STOP_BRAKE_MS < 1 || MOTOR_STOP_BRAKE_MS > 500
#error Invalid open-loop stopping pulse limits
#endif
#if MOTOR_TRANSLATION_EXPO_PERCENT > 100 || MOTOR_TURN_EXPO_PERCENT > 100
#error Joystick expo percentages must be between 0 and 100
#endif
#if MOTOR_STRAFE_FRONT_PERCENT < 50 || MOTOR_STRAFE_FRONT_PERCENT > 100 || \
    MOTOR_STRAFE_REAR_PERCENT < 50 || MOTOR_STRAFE_REAR_PERCENT > 100
#error Strafe gains must be between 50 and 100 percent
#endif
#endif
