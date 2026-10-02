#include "calibration_control.h"
#include "calibration_store.h"
#include "motor.h"
#include "motor_tuning.h"
#include "delay.h"
#define KEY(button) ((uint16_t)(1U << (button)))
#define LED_INTERVAL_MS 150U

volatile uint8_t calibration_loaded;
volatile uint8_t calibration_dirty;
volatile uint8_t calibration_learning;
volatile uint8_t calibration_save_status;
static strafe_calibration_t saved;
static uint16_t previous_pressed;
static uint8_t idle_tracking, attempted_save;
static uint32_t idle_since, last_save_attempt;
static uint8_t feedback_edges, feedback_on;
static uint32_t feedback_time;
static uint32_t learn_since, learn_last_ms;
static float learn_fraction;
static int8_t learn_side_sign;
static uint8_t learn_steps;

static void learning_reset(void)
{
    calibration_learning = learn_steps = 0;
    learn_side_sign = 0; learn_fraction = 0.0f;
    learn_since = learn_last_ms = delay_millis();
}

void calibration_control_reset_feedback(void) { feedback_edges = feedback_on = 0; }
void calibration_control_reset_inputs(void)
{
    previous_pressed = 0; idle_tracking = 0; learning_reset();
}
void calibration_control_init(void)
{
    saved.front_percent = MOTOR_STRAFE_FRONT_PERCENT;
    saved.rear_percent = MOTOR_STRAFE_REAR_PERCENT;
    calibration_loaded = calibration_store_load(&saved);
    motor_set_strafe_percent(saved.front_percent, saved.rear_percent);
    calibration_dirty = calibration_save_status = attempted_save = 0;
    last_save_attempt = 0;
    calibration_control_reset_inputs();
    calibration_control_reset_feedback();
}
static uint8_t axes_centered(const ps2_data *data)
{
    return motor_joystick_is_centered(data->LJoy_UD) &&
           motor_joystick_is_centered(data->LJoy_LR) &&
           motor_joystick_is_centered(data->RJoy_LR);
}
static uint8_t adjust(uint8_t value, uint8_t down)
{
    if (down) return value >= CAL_GAIN_MIN + 5U ? value - 5U : CAL_GAIN_MIN;
    return value <= CAL_GAIN_MAX - 5U ? value + 5U : CAL_GAIN_MAX;
}
static void changed(uint8_t front, uint8_t rear)
{
    if (front == motor_get_strafe_front_percent() && rear == motor_get_strafe_rear_percent()) return;
    motor_set_strafe_percent(front, rear);
    calibration_dirty = !calibration_loaded || front != saved.front_percent || rear != saved.rear_percent;
    idle_tracking = 0;
}
static void learn_from_driver(const ps2_data *data, uint16_t pressed, uint32_t now)
{
    float forward, side, turn, side_abs, turn_abs;
    float raw_side = motor_joystick_axis(data->LJoy_LR);
    float raw_side_abs = raw_side < 0.0f ? -raw_side : raw_side;
    int8_t sign;
    uint32_t elapsed;
    uint8_t front, rear;
    motor_joystick_map(data->LJoy_UD, data->LJoy_LR, data->RJoy_LR,
                       &forward, &side, &turn);
    turn *= MOTOR_O_ROTATION_SIGN;
    side_abs = side < 0.0f ? -side : side;
    turn_abs = turn < 0.0f ? -turn : turn;
    sign = side > 0.0f ? 1 : -1;
    /* 只有SELECT单独按住、纯横移>=30%、小幅纠偏才推断“右杆是在纠偏”。
     * 人是反馈来源；不是由PWM推算车身已经走直，更不是轮速闭环。
     */
    if (pressed != KEY(PS2_BUTTON_SELECT) || !motor_joystick_is_centered(data->LJoy_UD) ||
        raw_side_abs < 0.30f || side_abs == 0.0f ||
        turn_abs > side_abs * 0.35f || !motor_output_above_percent(0))
    {
        /* 松开SELECT才恢复本次训练的步数预算。大幅转杆/停机只重计稳定时间。 */
        if (!(pressed & KEY(PS2_BUTTON_SELECT))) learn_steps = 0;
        learn_side_sign = 0; learn_fraction = 0.0f; calibration_learning = 0;
        learn_since = learn_last_ms = now;
        return;
    }
    if (sign != learn_side_sign)
    {
        learn_side_sign = sign; learn_since = learn_last_ms = now;
        learn_fraction = 0.0f;
    }
    elapsed = now - learn_last_ms; learn_last_ms = now;
    if ((uint32_t)(now - learn_since) < MOTOR_CAL_LEARN_SETTLE_MS ||
        learn_steps >= MOTOR_CAL_LEARN_SESSION_LIMIT) { calibration_learning = 0; return; }
    calibration_learning = 1;
    if (elapsed > MOTOR_UPDATE_MAX_MS) elapsed = MOTOR_UPDATE_MAX_MS;
    /* O形混合中，前轴横移项变化dG等价于增加约 side*dG/2 的旋转项。
     * 用操作者的小幅旋转纠偏逐步替代前/后轴的横移失衡，保持正常右杆控制。
     * 这是推导出的试验学习律，尚未在实车证明收敛；每秒及每轮训练均限幅。
     */
    learn_fraction += 2.0f * turn / side * MOTOR_CAL_LEARN_RATE * (float)elapsed / 1000.0f;
    front = motor_get_strafe_front_percent(); rear = motor_get_strafe_rear_percent();
    if (learn_fraction >= 1.0f)
    {
        if (front < CAL_GAIN_MAX) front++;
        else if (rear > CAL_GAIN_MIN) rear--;
        else { learn_fraction = 0.0f; return; }
        learn_fraction -= 1.0f; learn_steps++;
    }
    else if (learn_fraction <= -1.0f)
    {
        if (front > CAL_GAIN_MIN) front--;
        else if (rear < CAL_GAIN_MAX) rear++;
        else { learn_fraction = 0.0f; return; }
        learn_fraction += 1.0f; learn_steps++;
    }
    changed(front, rear);
}
static uint8_t save_now(uint32_t now)
{
    strafe_calibration_t data;
    uint8_t result;
    /* FPEC擦写会暂停Flash取指；先将8路CCR清零，不能带着旧PWM等待。 */
    motor_stop();
    data.front_percent = motor_get_strafe_front_percent();
    data.rear_percent = motor_get_strafe_rear_percent();
    result = calibration_store_save(&data);
    last_save_attempt = now; attempted_save = 1;
    if (result != CAL_STORE_ERROR)
    {
        saved = data; calibration_loaded = 1; calibration_dirty = 0;
        calibration_save_status = result;
        feedback_edges = 4U;
    }
    else { calibration_save_status = 3; feedback_edges = 10U; }
    feedback_on = 0;
    feedback_time = delay_millis() - LED_INTERVAL_MS;
    idle_tracking = 0;
    return 1;
}
uint8_t calibration_control_poll(const ps2_data *data)
{
    uint16_t pressed, edges;
    uint8_t front, rear, manual_save = 0;
    uint32_t now = delay_millis();
    /* 调用方只传模拟帧；额外拒绝其他模式，不能由坏帧触发写Flash。 */
    if (data == 0 || data->mode != PS2_MODE_ANALOG)
    {
        calibration_control_reset_inputs(); return 0;
    }
    pressed = (uint16_t)data->btn1 | ((uint16_t)data->btn2 << 8);
    edges = pressed & (uint16_t)~previous_pressed;
    previous_pressed = pressed;
    front = motor_get_strafe_front_percent(); rear = motor_get_strafe_rear_percent();
    if (pressed & KEY(PS2_BUTTON_SELECT))
    {
        if (edges & KEY(PS2_BUTTON_CROSS)) changed(100, 100);
        else
        {
            if ((pressed & (KEY(PS2_BUTTON_SQUARE) | KEY(PS2_BUTTON_TRIANGLE))) !=
                (KEY(PS2_BUTTON_SQUARE) | KEY(PS2_BUTTON_TRIANGLE)))
            {
                if (edges & KEY(PS2_BUTTON_SQUARE)) front = adjust(front, 1);
                if (edges & KEY(PS2_BUTTON_TRIANGLE)) front = adjust(front, 0);
            }
            if ((pressed & (KEY(PS2_BUTTON_DOWN) | KEY(PS2_BUTTON_UP))) !=
                (KEY(PS2_BUTTON_DOWN) | KEY(PS2_BUTTON_UP)))
            {
                if (edges & KEY(PS2_BUTTON_DOWN)) rear = adjust(rear, 1);
                if (edges & KEY(PS2_BUTTON_UP)) rear = adjust(rear, 0);
            }
            changed(front, rear);
        }
        if (edges & KEY(PS2_BUTTON_CIRCLE))
        {
            if (axes_centered(data)) { manual_save = 1; calibration_dirty = 1; }
            else calibration_save_status = 4; /* 忽略，松杆后需重新按 */
        }
    }
    learn_from_driver(data, pressed, now);
    if (pressed == 0 && axes_centered(data) && !motor_output_above_percent(0))
    {
        if (!idle_tracking) { idle_since = now; idle_tracking = 1; }
    }
    else idle_tracking = 0;
    if (calibration_dirty && (manual_save ||
        (idle_tracking && (uint32_t)(now - idle_since) >= MOTOR_CAL_SAVE_IDLE_MS)) &&
        (!attempted_save || (uint32_t)(now - last_save_attempt) >= MOTOR_CAL_SAVE_INTERVAL_MS))
        return save_now(now);
    return 0;
}
uint8_t calibration_control_led(uint8_t *on)
{
    uint32_t now;
    if (feedback_edges == 0 || on == 0) return 0;
    now = delay_millis();
    if ((uint32_t)(now - feedback_time) >= LED_INTERVAL_MS)
    {
        feedback_time = now; feedback_on = !feedback_on; feedback_edges--;
    }
    *on = feedback_on;
    return 1;
}
