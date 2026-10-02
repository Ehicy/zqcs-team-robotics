#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mock_hardware.h"

#define main motor_buttons_firmware_main
#include "../examples/motor-buttons/main.c"
#undef main

static void poll(uint16_t pressed, uint8_t mode, uint8_t marker)
{
    uint8_t frame[9] = {0xFF, mode, marker, (uint8_t)~pressed,
                       (uint8_t)~(pressed >> 8), 128, 128, 128, 128};
    if (mode == 0x41) /* 数字模式后面的字节没有摇杆含义 */
    {
        frame[5] = 0xFF; frame[6] = 0; frame[7] = 0xFF; frame[8] = 0;
    }
    mock_frame(frame, 9);
    motor_test_step();
    mock_assert_poll_complete();
}

static void normal(uint16_t pressed) { poll(pressed, 0x73, 0x5A); }

static void assert_outputs(unsigned mask)
{
    unsigned i;
    assert(motor_buttons_debug.motor_mask == mask);
    for (i = 0; i < 4; i++)
    {
        assert(mock_ccr[i * 2] == ((mask & (1U << i)) ? 3600U : 0U));
        assert(mock_ccr[i * 2 + 1] == 0);
    }
}

int main(void)
{
    unsigned i;
    uint16_t all = 0;
    const uint8_t buttons[4] = {PS2_BUTTON_CROSS, PS2_BUTTON_CIRCLE,
                               PS2_BUTTON_SQUARE, PS2_BUTTON_TRIANGLE};
    const uint16_t start = 1U << PS2_BUTTON_START;
    pwm_init();
    ps2_init();
    motor_test_stop();
    normal(1U << PS2_BUTTON_CROSS); assert_outputs(0); /* 上电按住不启动 */
    normal(0); assert(motor_buttons_debug.armed == 1); assert_outputs(0);
    for (i = 0; i < 4; i++)
    {
        uint16_t key = (uint16_t)(1U << buttons[i]);
        normal(key); assert_outputs(1U << i); /* 仅对应电机满占空比 */
        normal(key); normal(key); assert_outputs(1U << i);
        assert(motor_buttons_debug.toggles[i] == 1);
        assert(motor_buttons_debug.last_motor == i + 1U);
        normal(0); assert_outputs(1U << i); /* 松开保持运行 */
        normal(key); assert_outputs(0); /* 再按停止 */
        assert(motor_buttons_debug.toggles[i] == 2);
        normal(0);
        all = (uint16_t)(all | key);
    }
    normal(all); assert_outputs(15);
    normal(0);
    normal(1U << PS2_BUTTON_CIRCLE); assert_outputs(13); /* 另外三只保持 */
    normal(0);
    normal(start | all); assert_outputs(0); /* START 优先 */
    normal(all); assert_outputs(0); /* 全停后先松开才重新使能 */
    normal(0); normal(all); assert_outputs(15);
    poll(0, 0xFF, 0xFF); assert_outputs(0);
    assert(motor_buttons_debug.armed == 0);
    normal(all); assert_outputs(0); /* 断线重连按住不自动恢复 */
    normal(0); normal(all); assert_outputs(15);
    poll(0, 0x73, 0); assert_outputs(0);
    normal(0); normal(all); assert_outputs(15);
    normal(start); assert_outputs(0);
    poll(0, 0x41, 0x5A); assert_outputs(0);
    assert(controller.RJoy_LR == 128 && controller.RJoy_UD == 128 &&
           controller.LJoy_LR == 128 && controller.LJoy_UD == 128);
    for (i = 0; i < 4; i++)
    {
        uint16_t key = (uint16_t)(1U << buttons[i]);
        poll(key, 0x41, 0x5A); assert_outputs(1U << i);
        poll(key, 0x41, 0x5A); assert_outputs(1U << i);
        poll(0, 0x41, 0x5A); assert_outputs(1U << i);
        poll(key, 0x41, 0x5A); assert_outputs(0);
        poll(0, 0x41, 0x5A);
    }
    poll(all, 0x41, 0x5A); assert_outputs(15);
    poll(0, 0x79, 0x5A); assert_outputs(0);
    puts("PASS: four independent keys at 100%, hold, independent stop, START priority, digital/analog modes, invalid frame and rearm");
    return 0;
}
