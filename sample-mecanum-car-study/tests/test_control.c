#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mock_hardware.h"

/* 将真正的主程序编入测试；不运行无限循环，逐次调用同一 control_step。 */
#define main firmware_main
#include "../hardware/Src/main.c"
#undef main

static void assert_stopped(void)
{
    unsigned i;
    for (i = 0; i < 8; i++) assert(mock_ccr[i] == 0);

}
static void run_frame(uint8_t forward, uint8_t sideways, uint8_t turn,
                      uint16_t pressed, uint8_t mode, uint8_t marker)
{
    uint8_t frame[9] = {0xFF, mode, marker, (uint8_t)~pressed,
                       (uint8_t)~(pressed >> 8), turn, 128, sideways, forward};
    mock_frame(frame, 9);
    mock_millis += 12U; /* 含手柄通信的实际轮询周期，非固定10ms假设 */
    control_step();
    mock_assert_poll_complete();
}
static void normal(uint8_t forward, uint8_t sideways, uint8_t turn, uint16_t pressed)
{ run_frame(forward, sideways, turn, pressed, 0x73, 0x5A); }

static void test_pwm(void)
{
    unsigned ch, i, other;
    void (*setters[4])(float) = {pwm_set1,pwm_set2,pwm_set3,pwm_set4};
    const float input[] = {0.0f, 0.00001f, 0.5f, 1.0f, 1.4f,
                           -0.5f, -1.0f, -1.4f, NAN, INFINITY, -INFINITY, -0.0f};
    const int expected[] = {0, 0, 1800, 3600, 3600, -1800, -3600, -3600,
                            0, 3600, -3600, 0};
    for (ch = 0; ch < 4; ch++)
    {
        for (i = 0; i < sizeof input / sizeof input[0]; i++)
        {
            setters[ch](input[i]);
            assert((int)mock_ccr[ch*2] - mock_ccr[ch*2+1] == expected[i]);
            for (other = 0; other < 8; other++)
                if (other / 2 != ch) assert(mock_ccr[other] == 0);
        }
        setters[ch](1.0f); setters[ch](-1.0f); setters[ch](1.0f);
        motor_stop(); assert_stopped();
    }
    puts("PASS: four DRV8833 pairs, signed PWM, saturation, NaN/Inf, reversal write order");
}

static void assert_wheels(int a, int b, int c, int d)
{
    const int expected[4] = {a,b,c,d};
    unsigned i;
    /* 保留原方向/档位/混合的稳态验收；瞬态斜坡/换向在独立测试覆盖。 */
    for (i = 0; i < 150; i++)
    {
        mock_millis += 10U;
        motor_update();
    }
    for (i = 0; i < 4; i++)
    {
        if (abs((int)mock_ccr[2*i] - mock_ccr[2*i+1] - expected[i]) > 1)
            fprintf(stderr, "wheel %u: actual=%d expected=%d speed=%u\n", i+1,
                    (int)mock_ccr[2*i] - mock_ccr[2*i+1], expected[i],
                    motor_get_speed_percent());
        assert(abs((int)mock_ccr[2*i] - mock_ccr[2*i+1] - expected[i]) <= 1);
    }
}

static void test_motor(void)
{
    unsigned i, j, k, axis;
    const uint8_t values[] = {0,1,64,119,120,127,128,129,136,137,192,254,255};
    /* 保留已核算的85/100混合回归；新版默认值在test_motor_tuning覆盖。 */
    assert(motor_set_strafe_percent(85,100));
    assert(motor_get_speed_percent() == 100U);
    motor(128,128,128); assert_stopped();
    motor_set_speed_percent(10);
    motor(0,128,128); assert_wheels(-360,-360,360,360);
    motor_set_speed_percent(0); assert_stopped();
    motor(0,0,0); assert_stopped();
    motor_set_speed_percent(255); /* API 自身也封顶，不能绕过100%。 */
    assert(motor_get_speed_percent() == 100U);
    motor(0,128,128); assert_wheels(-3600,-3600,3600,3600);
    motor(128,128,0); assert_wheels(-3600,3600,3600,-3600);
    motor(0,0,0); assert_wheels(-1263,-1263,3600,-1073);
    motor_set_speed_percent(50);
    /* 按实测电气极性与轮位检查：1左后、2右后、3左前、4右前。
     * O 形左移：左前/右后前进，右前/左后后退。
     */
    motor(0,128,128); assert_wheels(-1800,-1800,1800,1800);
    motor(255,128,128); assert_wheels(1800,1800,-1800,-1800);
    motor(128,0,128); assert_wheels(1800,-1800,1530,-1530);
    motor(128,255,128); assert_wheels(-1800,1800,-1530,1530);
    motor(128,128,0); assert_wheels(-1800,1800,1800,-1800);
    motor(128,128,255); assert_wheels(1800,-1800,-1800,1800);
    /* 横移项前85/后100的试验补偿也改变斜移；原前轮零项变为非零。 */
    motor(0,0,128); assert_wheels(0,-1800,1665,135);
    motor(0,255,128); assert_wheels(-1800,0,135,1665);
    motor(255,0,128); assert_wheels(1800,0,-135,-1665);
    motor(255,255,128); assert_wheels(0,1800,-1665,-135);
    /* 混合机械输出1,1,2.85,-0.85，统一限幅后乘50%并补后轮极性。 */
    motor(0,0,0); assert_wheels(-631,-631,1800,-536);
    /* 死区外连续调速，左杆与右杆都保留幅度。 */
    motor(119,128,128); assert_wheels(-11,-11,11,11);
    motor(137,128,128); assert_wheels(11,11,-11,-11);
    motor(128,119,128); assert_wheels(11,-11,9,-9);
    motor(128,128,119); assert_wheels(-7,7,7,-7);
    motor(0,64,128); assert_wheels(-655,-1800,1714,740);
    motor(64,96,128); assert_wheels(-390,-977,933,434);

    for (i = 0; i < 256; i++)
    {
        assert(motor_joystick_is_centered((uint8_t)i) == (i >= 120 && i <= 136));
        motor((float)i, 128, 128);
        if (i >= 120 && i <= 136) assert_stopped();
    }
    motor(119,128,128); assert_wheels(-11,-11,11,11);
    motor(137,128,128); assert_wheels(11,11,-11,-11);
    motor(NAN,128,128); assert_stopped();
    motor_set_speed_percent(100);
    for (i = 0; i < sizeof values; i++)
        for (j = 0; j < sizeof values; j++)
            for (k = 0; k < sizeof values; k++)
            {
                motor(values[i], values[j], values[k]);
                mock_millis += 12U;
                motor_update();
                for (axis = 0; axis < 8; axis++) assert(mock_ccr[axis] <= 3600U);
            }
    motor_stop();
    puts("PASS: O wheels with front85/rear100 strafe trial, unchanged pure forward/yaw, continuous amplitude, 2197 mixed limits");
}

static void test_ps2(void)
{
    ps2_data data;
    unsigned i;
    uint8_t frame[9] = {0xFF,0x73,0x5A,0xFE,0x7F,3,250,77,199};
    for (i = 0; i < 256; i++)
    {
        frame[5] = (uint8_t)i;
        mock_frame(frame, 9);
        assert(ps2_read(&data) == 1);
        mock_assert_poll_complete();
        assert(data.btn1 == 1 && data.btn2 == 128);
        assert(data.RJoy_LR == i && data.RJoy_UD == 250);
        assert(data.LJoy_LR == 77 && data.LJoy_UD == 199);
    }
    frame[2] = 0;
    mock_frame(frame,9);
    assert(ps2_read(&data) == 0);
    assert(data.mode == 0 && data.btn1 == 0 && data.btn2 == 0);
    assert(data.RJoy_LR == 128 && data.RJoy_UD == 128 &&
           data.LJoy_LR == 128 && data.LJoy_UD == 128);
    memset(frame,0xFF,9); mock_frame(frame,9); assert(ps2_read(&data) == 0);
    memset(frame,0,9); mock_frame(frame,9); assert(ps2_read(&data) == 0);
    assert(ps2_read(NULL) == 0);
    puts("PASS: LSB-first wire simulation, 256 byte values, frame layout, invalid clearing");
}

static void test_control(void)
{
    uint8_t button;
    const uint8_t modes[] = {0,0x41,0x79,0xFF};
    unsigned i;
    motor_init(); control_ready = 0;
    normal(0,128,128,0); assert_stopped(); /* 推杆上电不能启动 */
    normal(128,128,128,0); assert_stopped();
    normal(0,128,128,0); assert_wheels(-3600,-3600,3600,3600);
    assert(control_mode == PS2_MODE_ANALOG);
    for (i = 0; i < sizeof modes; i++)
    {
        run_frame(0,128,128,0,modes[i],0x5A); assert_stopped();
        normal(0,128,128,0); assert_stopped();
        normal(128,128,128,1); assert_stopped();
        normal(128,128,128,0); assert_stopped();
        normal(0,128,128,0); assert_wheels(-3600,-3600,3600,3600);
    }
    run_frame(0,128,128,0,0x73,0); assert_stopped();
    normal(128,128,128,0); assert_stopped();
    normal(0,128,128,0); assert_wheels(-3600,-3600,3600,3600);

    normal(0,0,0,(uint16_t)(1U << PS2_BUTTON_START)); assert_stopped();
    assert(control_ready == 0 && last_released_button == 255);
    assert(mock_gpio_c & GPIO_Pin_13);
    for (i = 0; i < 20; i++) normal(0,128,128,0);
    assert_stopped(); /* 斜坡不能恢复被START清掉的目标 */
    normal(128,128,128,(uint16_t)(1U << PS2_BUTTON_START)); assert_stopped();
    normal(128,128,128,0); assert_stopped(); assert(control_ready == 1);
    normal(128,0,128,0); assert_wheels(3420,-3420,3600,-3600);
    normal(128,128,0,0); assert_wheels(-3600,3600,3600,-3600);
    normal(128,128,128,0); assert_stopped();

    for (button = 0; button < PS2_BUTTON_COUNT; button++)
    {
        if (button == PS2_BUTTON_START) continue;
        last_released_button = 255;
        normal(128,128,128,(uint16_t)(1U << button));
        normal(128,128,128,(uint16_t)(1U << button));
        assert(last_released_button == 255);
        normal(128,128,128,0); assert(last_released_button == button);
        last_released_button = 255;
        normal(128,128,128,0); assert(last_released_button == 255);
    }
    normal(0,128,128,(uint16_t)(1U << PS2_BUTTON_CROSS));
    last_released_button = 255;
    run_frame(0,128,128,0,0xFF,0xFF);
    assert_stopped(); assert(last_released_button == 255);
    normal(128,128,128,0); normal(128,128,128,0);
    assert(last_released_button == 255);
    puts("PASS: zero-output startup, analog-only, START reset/rearm, immediate fault stop, no phantom release");
}

static void test_analog_controls(void)
{
    unsigned i;
    motor_init(); control_ready = 0;
    normal(128,128,128,0x0800); assert_stopped();
    normal(128,128,128,0); assert_stopped();
    normal(119,128,128,0); assert_wheels(-22,-22,22,22);
    normal(64,128,128,0); assert_wheels(-1351,-1351,1351,1351);
    normal(0,128,128,0); assert_wheels(-3600,-3600,3600,3600);
    /* L1/R1、L2/R2无调速绑定，按住和同按都不改变摇杆目标。 */
    for (i = 0; i < 20; i++) normal(0,128,128,0x0800);
    assert_wheels(-3600,-3600,3600,3600);
    normal(0,128,128,0x0400); assert_wheels(-3600,-3600,3600,3600);
    normal(0,128,128,0x0C00); assert_wheels(-3600,-3600,3600,3600);
    normal(0,128,128,0x0300); assert_wheels(-3600,-3600,3600,3600);
    assert(motor_get_speed_percent() == 100);
    normal(128,128,128,0); assert_stopped();
    normal(128,128,64,0); assert_wheels(-1022,1022,1022,-1022);
    normal(128,128,128,0); assert_stopped();
    run_frame(0,128,128,0x0800,0x41,0x5A); assert_stopped();
    normal(128,128,128,0x0800); assert_stopped(); assert(!control_ready);
    normal(128,128,128,0); assert_stopped(); assert(control_ready);
    normal(0,128,128,0); assert_wheels(-3600,-3600,3600,3600);
    motor_init(); assert_stopped();
    puts("PASS: continuous left/right stick control, no shoulder speed binding, neutral rearm");
}

static void test_speed_led(void)
{
    unsigned i;
    motor_init(); speed_led_init(); control_ready = 0;
    normal(128,128,128,0);
    assert(mock_gpio_c & GPIO_Pin_13); /* 上限100%但实际零输出，灯灭 */
    motor_set_speed_percent(60);
    for (i = 0; i < 40; i++)
    {
        normal(0,128,128,0);
        assert(mock_gpio_c & GPIO_Pin_13);
    }
    assert(!motor_output_above_percent(60));
    motor_set_speed_percent(70);
    for (i = 0; i < 10; i++) normal(0,128,128,0);
    assert(motor_output_above_percent(60));
    speed_led_init(); normal(0,128,128,0);
    assert(!(mock_gpio_c & GPIO_Pin_13));
    for (i = 0; i < SPEED_LED_BLINK_STEPS; i++) normal(0,128,128,0);
    assert(mock_gpio_c & GPIO_Pin_13);
    for (i = 0; i < SPEED_LED_BLINK_STEPS; i++) normal(0,128,128,0);
    assert(!(mock_gpio_c & GPIO_Pin_13));
    normal(128,128,128,0); assert_stopped(); assert(mock_gpio_c & GPIO_Pin_13);
    for (i = 0; i < 35; i++) normal(0,128,128,0);
    assert(!(mock_gpio_c & GPIO_Pin_13));
    run_frame(0,0,0,0x0808,0x41,0x5A);
    assert_stopped(); assert(!control_ready && last_released_button == 255);
    assert(mock_gpio_c & GPIO_Pin_13);
    assert(speed_led_active == 0 && speed_led_steps == 0 && speed_led_on == 0);
    for (i = 0; i < PS2_BUTTON_COUNT; i++) assert(button_was_down[i] == 0);
    normal(128,128,128,0x0800); assert_stopped(); assert(!control_ready);
    normal(128,128,128,0); assert_stopped(); assert(control_ready);
    run_frame(0,128,128,0,0xFF,0xFF);
    assert_stopped(); assert(mock_gpio_c & GPIO_Pin_13);
    puts("PASS: PC13 actual PWM threshold, blink phase, immediate neutral/START/fault off");
}

int main(void)
{
    motor_init(); /* 同时初始化两个定时器和八路输出 */
    assert(mock_af_a == (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
                         GPIO_Pin_6 | GPIO_Pin_7));
    assert(mock_af_b == (GPIO_Pin_0 | GPIO_Pin_1));
    ps2_init();
    speed_led_init();
    assert(mock_period[0] == 3599 && mock_prescaler[0] == 0);
    assert(mock_period[1] == 3599 && mock_prescaler[1] == 0);
    assert_stopped();
    test_pwm();
    test_motor();
    test_ps2();
    test_control();
    test_analog_controls();
    test_speed_led();
    puts("All host tests passed (not a hardware or Keil build test).");
    return 0;
}
