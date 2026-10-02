#include "motor.h"
#include "pwm.h"
#include "delay.h"
#include "motor_tuning.h"
#include "calibration_store.h"
/* volatile 便于 ST-Link/Watch 查看；所有写入都经过 100% 上限函数。 */
static volatile uint8_t speed_percent = MOTOR_SPEED_INITIAL_PERCENT;
static float target_duty[4];
static float applied_duty[4];
static int8_t last_direction[4];
static uint32_t zero_since_ms[4];
static uint32_t last_update_ms;
static volatile uint8_t stop_phase;
static uint32_t stop_started_ms, brake_started_ms;
static float stop_duty[4];
static uint8_t startup_active;
static uint32_t startup_elapsed_ms, startup_duration_ms;
static motor_calibration_t wheel_calibration[4] = MOTOR_CALIBRATION_DEFAULTS;
static volatile uint8_t strafe_front_percent = MOTOR_STRAFE_FRONT_PERCENT;
static volatile uint8_t strafe_rear_percent = MOTOR_STRAFE_REAR_PERCENT;

static float absolute_value(float value)
{
    return value < 0.0f ? -value : value;
}

static int8_t direction_of(float duty)
{
    return duty > 0.0f ? 1 : (duty < 0.0f ? -1 : 0);
}

static void write_outputs(void)
{
    pwm_set1(applied_duty[0]);
    pwm_set2(applied_duty[1]);
    pwm_set3(applied_duty[2]);
    pwm_set4(applied_duty[3]);
}

/* TIM2/TIM3 的八个 PWM 输入由 pwm 模块统一初始化。 */
void motor_init(void)
{
    static const motor_calibration_t defaults[4] = MOTOR_CALIBRATION_DEFAULTS;
    uint8_t wheel;
    uint32_t now = delay_millis();
    pwm_init();
    stop_phase = MOTOR_STOP_IDLE;
    startup_active = 0; startup_elapsed_ms = startup_duration_ms = 0;
    for (wheel = 0; wheel < 4; wheel++)
    {
        target_duty[wheel] = applied_duty[wheel] = 0.0f;
        stop_duty[wheel] = 0.0f;
        last_direction[wheel] = 0;
        zero_since_ms[wheel] = now;
        wheel_calibration[wheel] = defaults[wheel];
    }
    last_update_ms = now;
    strafe_front_percent = MOTOR_STRAFE_FRONT_PERCENT;
    strafe_rear_percent = MOTOR_STRAFE_REAR_PERCENT;
    motor_set_speed_percent(MOTOR_SPEED_INITIAL_PERCENT);
}

uint8_t motor_set_strafe_percent(uint8_t front, uint8_t rear)
{
    if (front < CAL_GAIN_MIN || front > CAL_GAIN_MAX || rear < CAL_GAIN_MIN || rear > CAL_GAIN_MAX) return 0;
    strafe_front_percent = front; strafe_rear_percent = rear;
    return 1;
}
uint8_t motor_get_strafe_front_percent(void) { return strafe_front_percent; }
uint8_t motor_get_strafe_rear_percent(void) { return strafe_rear_percent; }

void motor_set_speed_percent(uint8_t percent)
{
    float largest = 0.0f;
    float largest_stop = 0.0f;
    float limit;
    uint8_t wheel;
    speed_percent = percent > MOTOR_SPEED_MAX_PERCENT ?
                    MOTOR_SPEED_MAX_PERCENT : percent;
    if (speed_percent == 0U)
    {
        motor_stop();
        return;
    }
    /* 降档上限立即生效，不能在慢减速期间继续超过用户选定档位。 */
    limit = (float)speed_percent / 100.0f;
    for (wheel = 0; wheel < 4; wheel++)
        if (absolute_value(applied_duty[wheel]) > largest)
            largest = absolute_value(applied_duty[wheel]);
    for (wheel = 0; wheel < 4; wheel++)
        if (absolute_value(stop_duty[wheel]) > largest_stop)
            largest_stop = absolute_value(stop_duty[wheel]);
    if (largest_stop > limit)
        for (wheel = 0; wheel < 4; wheel++) stop_duty[wheel] *= limit / largest_stop;
    if (largest > limit)
        for (wheel = 0; wheel < 4; wheel++) applied_duty[wheel] *= limit / largest;
    for (wheel = 0; wheel < 4; wheel++)
        if (absolute_value(target_duty[wheel]) > limit)
            target_duty[wheel] = direction_of(target_duty[wheel]) * limit;
    if (stop_phase != MOTOR_STOP_HOLD) write_outputs();
}

uint8_t motor_get_speed_percent(void)
{
    return speed_percent;
}

uint8_t motor_output_above_percent(uint8_t percent)
{
    uint8_t wheel;
    for (wheel = 0; wheel < 4; wheel++)
    {
        /* 与PWM层的CCR量化一致，2160/3600=60%本身不触发。 */
        uint16_t compare = (uint16_t)(absolute_value(applied_duty[wheel]) * PWM_PERIOD_COUNTS);
        if ((uint32_t)compare * 100U > (uint32_t)percent * PWM_PERIOD_COUNTS) return 1;
    }
    return 0;
}

uint8_t motor_joystick_is_centered(uint8_t value)
{
    return value >= JOYSTICK_CENTER - JOYSTICK_DEADZONE &&
           value <= JOYSTICK_CENTER + JOYSTICK_DEADZONE;
}

/* 120～136回中；死区外从0连续增加至端点1，推杆幅度决定PWM。
 * 每轴中心128，两边端点距离不同，分别校正。
 */
float motor_joystick_axis(float value)
{
    float offset;
    if (!(value >= 0.0f && value <= 255.0f))
    {
        return 0.0f; /* 拒绝非法数值，包括 NaN。 */
    }
    offset = JOYSTICK_CENTER - value;
    /* 中心 128 两侧分别有 128/127 个计数，端点校正后对称。 */
    if (offset > JOYSTICK_DEADZONE)
        return (offset - JOYSTICK_DEADZONE) / (JOYSTICK_CENTER - JOYSTICK_DEADZONE);
    if (offset < -JOYSTICK_DEADZONE)
        return (offset + JOYSTICK_DEADZONE) / (255 - JOYSTICK_CENTER - JOYSTICK_DEADZONE);
    return 0.0f;
}

void motor_joystick_map(float raw_forward, float raw_sideways, float raw_turn,
                        float *forward, float *sideways, float *turn)
{
    float f = motor_joystick_axis(raw_forward);
    float s = motor_joystick_axis(raw_sideways);
    float t = motor_joystick_axis(raw_turn);
    float radius_squared = f * f + s * s;
    float left_expo = (float)MOTOR_TRANSLATION_EXPO_PERCENT / 100.0f;
    float turn_expo = (float)MOTOR_TURN_EXPO_PERCENT / 100.0f;
    float left_gain;
    if (forward == 0 || sideways == 0 || turn == 0) return;
    /* 左杆统一缩放，避免分别立方扭曲斜向；超出单位圆保持原混合限幅。
     * r<=1时幅度为(1-k)*r+k*r^3，无log/pow/sqrt计算或额外时间滤波。
     */
    if (radius_squared > 1.0f) radius_squared = 1.0f;
    left_gain = 1.0f - left_expo + left_expo * radius_squared;
    *forward = f * left_gain;
    *sideways = s * left_gain;
    *turn = t * (1.0f - turn_expo + turn_expo * t * t);
}

void motor_stop(void)
{
    uint8_t wheel;
    uint32_t now = delay_millis();
    stop_phase = MOTOR_STOP_IDLE;
    startup_active = 0; startup_elapsed_ms = 0;
    for (wheel = 0; wheel < 4; wheel++)
    {
        if (applied_duty[wheel] != 0.0f) zero_since_ms[wheel] = now;
        target_duty[wheel] = applied_duty[wheel] = 0.0f;
    }
    last_update_ms = now;
    /* DRV8833 双输入均为 0：滑行，不是主动制动。 */
    pwm_stop_all();
}

uint8_t motor_get_stop_phase(void) { return stop_phase; }

static void enter_short_brake(uint32_t now)
{
    uint8_t wheel;
    for (wheel=0; wheel<4; wheel++)
    {
        applied_duty[wheel] = 0.0f;
        zero_since_ms[wheel] = now;
    }
    brake_started_ms = now; stop_phase = MOTOR_STOP_HOLD;
    pwm_brake_all();
}
void motor_quick_stop(void)
{
    uint8_t wheel;
    float largest=0.0f;
    uint32_t now=delay_millis();
    /* 松杆/START长按均只触发一次；停止请求撤销期间排队的新目标。 */
    for (wheel=0; wheel<4; wheel++) target_duty[wheel]=0.0f;
    if (stop_phase != MOTOR_STOP_IDLE) return;
    if (!motor_output_above_percent(0)) { motor_stop(); return; }
    for (wheel=0; wheel<4; wheel++)
    {
        float value=absolute_value(applied_duty[wheel]);
        if (value>largest) largest=value;
        stop_duty[wheel]=-applied_duty[wheel]*(float)MOTOR_STOP_REVERSE_PERCENT/100.0f;
        applied_duty[wheel]=0.0f; zero_since_ms[wheel]=now;
    }
    startup_active=0; startup_elapsed_ms=0; last_update_ms=now;
    stop_started_ms=now;
    pwm_stop_all();
    if (MOTOR_STOP_REVERSE_PERCENT != 0 && largest*100.0f >= MOTOR_STOP_REVERSE_MIN_PERCENT)
        stop_phase=MOTOR_STOP_COAST;
    else enter_short_brake(now); /* 低PWM或静止不施加反转脉冲 */
}
static uint8_t update_stopping(uint32_t now)
{
    uint8_t wheel;
    uint32_t elapsed=now-stop_started_ms;
    if (stop_phase==MOTOR_STOP_IDLE) return 0;
    if (stop_phase==MOTOR_STOP_COAST && elapsed>=MOTOR_STOP_COAST_MS)
    {
        /* 主循环晚到时跳过已过期的反向窗口，不能补发旧反转脉冲。 */
        if (elapsed>=MOTOR_STOP_COAST_MS+MOTOR_STOP_REVERSE_MS) enter_short_brake(now);
        else
        {
            for (wheel=0; wheel<4; wheel++) applied_duty[wheel]=stop_duty[wheel];
            stop_phase=MOTOR_STOP_REVERSE; write_outputs();
        }
    }
    if (stop_phase==MOTOR_STOP_REVERSE && elapsed>=MOTOR_STOP_COAST_MS+MOTOR_STOP_REVERSE_MS)
        enter_short_brake(now);
    if (stop_phase==MOTOR_STOP_HOLD && (uint32_t)(now-brake_started_ms)>=MOTOR_STOP_BRAKE_MS)
    {
        stop_phase=MOTOR_STOP_IDLE;
        pwm_stop_all(); /* 只释放制动；下轮才允许曲线起步 */
    }
    return 1;
}

uint8_t motor_set_calibration(uint8_t wheel, const motor_calibration_t *calibration)
{
    if (wheel >= 4U || calibration == 0 ||
        calibration->forward_gain_percent > 100U ||
        calibration->reverse_gain_percent > 100U ||
        calibration->forward_min_percent > 100U ||
        calibration->reverse_min_percent > 100U) return 0;
    wheel_calibration[wheel] = *calibration;
    return 1;
}

void motor_update(void)
{
    uint32_t now = delay_millis();
    uint32_t elapsed = now - last_update_ms;
    uint8_t wheel;
    uint8_t reversing = 0;
    uint8_t curving = 0;
    float factor = 1.0f;
    float largest_applied=0.0f, largest_target=0.0f, curve_gain=1.0f;
    float desired[4];
    last_update_ms = now;
    if (update_stopping(now)) return;
    if (elapsed > MOTOR_UPDATE_MAX_MS) elapsed = MOTOR_UPDATE_MAX_MS;

    for (wheel = 0; wheel < 4; wheel++)
    {
        int8_t direction = direction_of(target_duty[wheel]);
        if (direction != 0 && last_direction[wheel] != 0 &&
            direction != last_direction[wheel] &&
            (applied_duty[wheel] != 0.0f ||
             (uint32_t)(now - zero_since_ms[wheel]) < MOTOR_REVERSAL_COAST_MS))
            reversing = 1;
        if (absolute_value(applied_duty[wheel])>largest_applied)
            largest_applied=absolute_value(applied_duty[wheel]);
        if (absolute_value(target_duty[wheel])>largest_target)
            largest_target=absolute_value(target_duty[wheel]);
    }
    if (reversing) startup_active=0;
    else if (!startup_active && largest_applied==0.0f && largest_target>0.0f)
    {
        startup_active=1; startup_elapsed_ms=0;
        startup_duration_ms=(uint32_t)(largest_target*MOTOR_STARTUP_CURVE_MS+0.5f);
        if (startup_duration_ms<MOTOR_STARTUP_MIN_MS) startup_duration_ms=MOTOR_STARTUP_MIN_MS;
    }
    if (startup_active)
    {
        float x;
        curving=1;
        startup_elapsed_ms+=elapsed;
        if (startup_elapsed_ms>=startup_duration_ms)
        { startup_elapsed_ms=startup_duration_ms; startup_active=0; }
        x=(float)startup_elapsed_ms/(float)startup_duration_ms;
        /* 五次smoothstep：两端一阶、二阶导数为0；四轮共用幅度包络。
         * 目标变化不重启计时；另用公共斜率限幅约束包络内的目标跳变。
         */
        curve_gain=x*x*x*(10.0f+x*(-15.0f+6.0f*x));
        if (curve_gain>1.0f) curve_gain=1.0f;
    }
    /* 任一轮需要换向时，四轮统一降到零，避免不同轮换向时车身突扭。
     * 无转速/电流传感器，零输出等待时间可调，不能证明转子已经停下。
     */
    for (wheel = 0; wheel < 4; wheel++)
    {
        float difference;
        float rate;
        float allowed;
        desired[wheel] = reversing ? 0.0f : target_duty[wheel]*curve_gain;
        difference = absolute_value(desired[wheel] - applied_duty[wheel]);
        rate = absolute_value(desired[wheel]) > absolute_value(applied_duty[wheel]) ?
               (curving ? MOTOR_STARTUP_MAX_RATE : MOTOR_ACCEL_PERCENT_PER_SECOND) :
               MOTOR_DECEL_PERCENT_PER_SECOND;
        allowed = rate * (float)elapsed / 100000.0f;
        if (difference > allowed && difference > 0.0f && allowed / difference < factor)
            factor = allowed / difference;
    }
    /* 同一个插值比例用于四轮：缓起步保持目标比例，停车保持旧比例。 */
    for (wheel = 0; wheel < 4; wheel++)
    {
        float previous = applied_duty[wheel];
        if (factor >= 1.0f) applied_duty[wheel] = desired[wheel];
        else applied_duty[wheel] += (desired[wheel] - applied_duty[wheel]) * factor;
        if (applied_duty[wheel] == 0.0f && previous != 0.0f) zero_since_ms[wheel] = now;
        if (applied_duty[wheel] != 0.0f) last_direction[wheel] = direction_of(applied_duty[wheel]);
    }
    write_outputs();
}

void motor(float joystick_forward, float joystick_sideways, float joystick_turn)
{
    float forward, sideways, turn;
    float speed = (float)motor_get_speed_percent() / 100.0f;
    float front_sideways, rear_sideways;
    /* 2026-09-30 实测：正电气输出使两只后轮后退、两只前轮前进。
     * 顺序：1左后(×)、2右后(○)、3左前(□)、4右前(△)。
     */
    static const float polarity[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    float output[4];
    float largest = 1.0f;
    float magnitude;
    uint8_t wheel;

    if (!(joystick_forward>=0.0f && joystick_forward<=255.0f &&
          joystick_sideways>=0.0f && joystick_sideways<=255.0f &&
          joystick_turn>=0.0f && joystick_turn<=255.0f))
    { motor_stop(); return; }
    motor_joystick_map(joystick_forward, joystick_sideways, joystick_turn,
                       &forward, &sideways, &turn);
    front_sideways = sideways * (float)strafe_front_percent / 100.0f;
    rear_sideways = sideways * (float)strafe_rear_percent / 100.0f;
    /* 左杆方向与幅度控制平移，右杆左右与幅度控制旋转；超限统一缩小。 */
    turn *= MOTOR_O_ROTATION_SIGN;

    /* 俯视 O 形滚子，F=前、L=左；先算“让车前进”为正的轮输出。
     * O 形的旋转杠杆为半轴距减半轮距，不能套 X 形的加和。
     * 默认前后轴距 > 左右轮距，turn 是旋转轮输出的方向分量。
     * 这是开环 PWM 比例，不是编码器测得的轮速，没有 PID。
     */
    /* 试验补偿只作用于横移项；不压低前轮的前进项或旋转项。
     * 100/95不是测得的轮速比，待用户比较横移圆弧是否减小。
     */
    output[0] = forward - rear_sideways + turn; /* 左后 */
    output[1] = forward + rear_sideways - turn; /* 右后 */
    output[2] = forward + front_sideways + turn; /* 左前 */
    output[3] = forward - front_sideways - turn; /* 右前 */

    /* 超过 1 时四轮一起缩小，保留四轮之间的比例。 */
    for (wheel = 0; wheel < 4; wheel++)
    {
        magnitude = absolute_value(output[wheel]);
        if (magnitude > largest)
        {
            largest = magnitude;
        }
    }
    for (wheel = 0; wheel < 4; wheel++)
    {
        /* 先整体限幅，再乘统一速度；任意混合均不超过选定百分比。 */
        float mechanical = output[wheel] / largest * speed;
        const motor_calibration_t *calibration = &wheel_calibration[wheel];
        float gain = mechanical > 0.0f ? calibration->forward_gain_percent :
                                         calibration->reverse_gain_percent;
        float minimum = (float)(mechanical > 0.0f ? calibration->forward_min_percent :
                                                      calibration->reverse_min_percent) / 100.0f;
        float value = absolute_value(mechanical) * gain / 100.0f;
        /* 零轮必须仍为零；最低 PWM 只补非零目标，且服从整体档位上限。 */
        if (value > 0.0f && value < minimum) value = minimum;
        if (value > speed) value = speed;
        target_duty[wheel] = direction_of(mechanical) * value * polarity[wheel];
    }

    /* 有效松杆请求有限制动；速度0/非法输入走motor_stop硬撤输出。 */
    if (target_duty[0] == 0.0f && target_duty[1] == 0.0f &&
        target_duty[2] == 0.0f && target_duty[3] == 0.0f) motor_quick_stop();
}
