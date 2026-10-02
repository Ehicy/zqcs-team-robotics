#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "motor.h"
#include "motor_tuning.h"
#include "mock_hardware.h"

static int output(unsigned wheel)
{ return (int)mock_ccr[wheel * 2] - mock_ccr[wheel * 2 + 1]; }
static void advance(uint32_t ms)
{ mock_millis += ms; motor_update(); }
static void stopped(void)
{
    unsigned wheel;
    for (wheel = 0; wheel < 4; wheel++) assert(output(wheel) == 0);
}
static void forward(int compare)
{
    assert(abs(output(0) + compare) <= 1 && abs(output(1) + compare) <= 1);
    assert(abs(output(2) - compare) <= 1 && abs(output(3) - compare) <= 1);
}
static void settle(void)
{
    unsigned i;
    for (i = 0; i < 120; i++) advance(10);
}

static void test_ramp(void)
{
    unsigned i;
    motor_init(); stopped();
    motor(0,128,128); stopped();
    advance(0); stopped();
    advance(10); forward(180); /* 500%/s，10ms增加5% */
    advance(10); forward(360);
    for (i = 0; i < 48; i++) advance(10);
    forward(3600);
    motor(64,128,128); advance(10); forward(3240); /* 减少10% */
    settle(); forward(1680);
    motor(128,128,128); stopped(); /* 松杆立即撤输出，不等减速斜坡 */
    advance(500); stopped();

    motor_init(); motor(0,0,0); advance(10);
    assert(abs(output(0) + 63) <= 1 && abs(output(1) + 63) <= 1);
    assert(output(2) == 180 && abs(output(3) + 53) <= 1);

    motor_init(); motor(0,128,128); advance(1000);
    forward(360); /* 大间隔只按20ms推进，不突然全速 */
    motor_set_speed_percent(0); stopped(); advance(50); stopped();
    puts("PASS: elapsed-time ramp, common four-wheel ratio, immediate stop, delayed-loop cap");
}

static void test_reverse(void)
{
    unsigned i;
    motor_init(); motor(0,128,128); settle(); forward(3600);
    motor(255,128,128); advance(10); forward(3240);
    for (i = 0; i < 35 && output(0) != 0; i++)
    {
        advance(10);
        assert(output(0) <= 0 && output(1) <= 0);
        assert(output(2) >= 0 && output(3) >= 0);
    }
    stopped();
    advance(MOTOR_REVERSAL_COAST_MS - 1); stopped();
    advance(1);
    assert(output(0) > 0 && output(1) > 0 && output(2) < 0 && output(3) < 0);
    settle(); forward(-3600);

    /* 快速“松杆再反推”也不能绕过零输出等待；重复stop不延长等待。 */
    motor_stop(); advance(10); motor(0,128,128); advance(0); stopped();
    motor_stop(); motor(0,128,128);
    advance(MOTOR_REVERSAL_COAST_MS - 11); stopped();
    advance(1); assert(output(0) < 0);
    motor_stop(); advance(1000); stopped();

    /* 毫秒计数回绕不破坏换向等待。 */
    mock_millis = UINT32_MAX - 30U;
    motor_init(); motor(0,128,128); advance(20);
    motor_stop(); motor(255,128,128);
    advance(MOTOR_REVERSAL_COAST_MS - 1); stopped();
    advance(1); assert(output(0) > 0);
    puts("PASS: reversal deceleration/coast, neutral bypass prevention, timer wrap");
}

static void test_calibration(void)
{
    motor_calibration_t calibration = {80, 60, 20, 30};
    motor_init();
    assert(motor_set_calibration(0, &calibration));
    motor(64,128,128); settle();
    assert(abs(output(0) + 1344) <= 1 && abs(output(2) - 1680) <= 1);
    motor(119,128,128); settle(); assert(abs(output(0) + 720) <= 1);
    motor(137,128,128); settle(); assert(abs(output(0) - 1080) <= 1);
    motor(255,128,128); settle(); assert(abs(output(0) - 2160) <= 1);
    motor_set_speed_percent(10); motor(119,128,128); settle();
    assert(abs(output(0)) <= 360); /* 20%最低PWM也不能越过10%上限 */
    motor(0,0,128); settle(); assert(output(0) == 0); /* 数学零轮不能起转 */
    motor(128,128,128); stopped();
    assert(!motor_set_calibration(4, &calibration));
    assert(!motor_set_calibration(0, NULL));
    calibration.forward_min_percent = 101;
    assert(!motor_set_calibration(0, &calibration));

    motor_init(); motor(0,128,128); settle(); forward(3600);
    motor_set_speed_percent(60); forward(2160);
    assert(!motor_output_above_percent(60));
    motor_set_speed_percent(100); motor(0,128,128); advance(10);
    assert(motor_output_above_percent(60));
    motor_stop(); assert(!motor_output_above_percent(0));
    puts("PASS: mechanical forward/reverse trim, PWM floor/cap, zero wheel, actual-output LED threshold");
}

static void test_strafe_trial(void)
{
    unsigned wheel;
    int left[4];
    /* 从静止起步时保持前85/后100的比例；40ms达后轮20%、前轮17%。 */
    motor_init(); motor(128,0,128);
    advance(20); advance(20);
    assert(output(0) == 720 && output(1) == -720);
    assert(abs(output(2) - 612) <= 1 && abs(output(3) + 612) <= 1);
    settle();
    assert(output(0) == 3600 && output(1) == -3600);
    assert(abs(output(2) - 3060) <= 1 && abs(output(3) + 3060) <= 1);
    for (wheel = 0; wheel < 4; wheel++) left[wheel] = output(wheel);
    motor(128,255,128); settle();
    for (wheel = 0; wheel < 4; wheel++) assert(abs(output(wheel) + left[wheel]) <= 1);
    motor_set_speed_percent(50); motor(128,0,128); settle();
    assert(output(0) == 1800 && output(1) == -1800);
    assert(abs(output(2) - 1530) <= 1 && abs(output(3) + 1530) <= 1);
    motor(128,128,128); stopped();
    /* 该补偿不降低纯前进/后退或纯旋转的前轮分量。 */
    motor_set_speed_percent(100); motor(0,128,128); settle(); forward(3600);
    motor(255,128,128); settle(); forward(-3600);
    motor(128,128,0); settle();
    assert(output(0) == -3600 && output(1) == 3600);
    assert(output(2) == 3600 && output(3) == -3600);
    motor_stop(); stopped(); advance(200); stopped();
    puts("PASS: strafe trial ramp ratio, left/right symmetry, PWM cap, unchanged forward/back/yaw, stop");
}

int main(void)
{
    test_ramp(); test_reverse(); test_calibration(); test_strafe_trial();
    return 0;
}
