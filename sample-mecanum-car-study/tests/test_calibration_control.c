#include <assert.h>
#include <stdio.h>
#include "mock_hardware.h"
#include "mock_calibration_flash.h"
#include "calibration_store.h"
#define main firmware_main
#include "../hardware/Src/main.c"
#undef main
#define KEY(b) ((uint16_t)(1U << (b)))
#define SELECT KEY(PS2_BUTTON_SELECT)
static void stopped(void)
{
    unsigned i;
    for (i = 0; i < 8; i++) assert(mock_ccr[i] == 0);
}
static void frame(uint8_t forward, uint8_t side, uint8_t turn, uint16_t pressed, uint8_t mode)
{
    uint8_t data[9] = {0xFF,mode,0x5A,(uint8_t)~pressed,(uint8_t)~(pressed>>8),turn,128,side,forward};
    mock_frame(data,9); mock_millis += 10U; control_step(); mock_assert_poll_complete();
}
static void normal(uint8_t forward, uint8_t side, uint8_t turn, uint16_t pressed)
{ frame(forward,side,turn,pressed,0x73); }
static void boot(void)
{
    motor_init(); calibration_control_init(); control_ready = 0;
    buttons_reset(); speed_led_init(); ps2_init();
    mock_flash_write_check = stopped; /* 每次擦写都验证8路PWM先清零 */
    normal(128,128,128,0); stopped(); assert(control_ready);
}
static void reset(void) { mock_flash_init(); mock_millis = 0; boot(); }
static void nframes(unsigned count, uint8_t f, uint8_t s, uint8_t t, uint16_t p)
{ unsigned i; for (i=0;i<count;i++) normal(f,s,t,p); }
static void press(uint8_t button)
{
    normal(128,0,128,SELECT | KEY(button));
    normal(128,0,128,SELECT);
}
static void test_manual(void)
{
    unsigned i;
    reset(); assert(!calibration_loaded);
    normal(128,0,128,KEY(PS2_BUTTON_SQUARE)); assert(motor_get_strafe_front_percent()==100);
    normal(128,0,128,0);
    normal(128,0,128,SELECT | KEY(PS2_BUTTON_SQUARE));
    assert(motor_get_strafe_front_percent()==95);
    nframes(100,128,0,128,SELECT | KEY(PS2_BUTTON_SQUARE));
    assert(motor_get_strafe_front_percent()==95); /* 长按一次，不连续递减 */
    assert(mock_flash_begins==0);
    normal(128,0,128,SELECT); press(PS2_BUTTON_TRIANGLE);
    assert(motor_get_strafe_front_percent()==100);
    normal(128,0,128,SELECT | KEY(PS2_BUTTON_SQUARE) | KEY(PS2_BUTTON_TRIANGLE));
    assert(motor_get_strafe_front_percent()==100);
    normal(128,0,128,SELECT);
    for(i=0;i<20;i++) press(PS2_BUTTON_SQUARE);
    assert(motor_get_strafe_front_percent()==50);
    for(i=0;i<20;i++) press(PS2_BUTTON_TRIANGLE);
    assert(motor_get_strafe_front_percent()==100);
    press(PS2_BUTTON_DOWN); assert(motor_get_strafe_rear_percent()==90);
    press(PS2_BUTTON_UP); assert(motor_get_strafe_rear_percent()==95);
    press(PS2_BUTTON_CROSS); assert(motor_get_strafe_front_percent()==100);
    assert(motor_get_strafe_rear_percent()==100); /* 恢复真正的原始100/100基线 */
    normal(128,0,128,SELECT | KEY(PS2_BUTTON_CIRCLE));
    assert(calibration_save_status==4 && mock_flash_begins==0);
    normal(128,0,128,KEY(PS2_BUTTON_START)); stopped();
    assert(motor_get_strafe_front_percent()==100); /* START不擦掉校准 */
    puts("PASS: guarded manual trim, hold/conflict/bounds, shoulder-free control, START preserves calibration");
}
static void test_save(void)
{
    strafe_calibration_t data;
    unsigned programs;
    reset(); press(PS2_BUTTON_SQUARE); assert(calibration_dirty);
    nframes(199,128,128,128,0); assert(mock_flash_begins==0);
    nframes(4,128,128,128,0); assert(calibration_save_status==CAL_STORE_SAVED);
    assert(calibration_store_load(&data) && data.front_percent==95 && data.rear_percent==95);
    stopped(); assert(!calibration_dirty);
    programs=mock_flash_programs;
    normal(128,128,128,0); press(PS2_BUTTON_SQUARE);
    nframes(250,128,128,128,0); assert(mock_flash_programs==programs); /* 30s写入间隔 */
    mock_millis += 30000U; normal(128,128,128,0);
    assert(calibration_store_load(&data) && data.front_percent==90);
    boot(); assert(calibration_loaded && motor_get_strafe_front_percent()==90);
    frame(128,0,159,SELECT,0x41); stopped();
    nframes(300,128,0,159,SELECT); stopped(); /* 模式恢复但未回中，不能学习 */
    assert(motor_get_strafe_front_percent()==90);

    reset(); press(PS2_BUTTON_SQUARE); mock_flash_fail_after=0;
    nframes(205,128,128,128,0); stopped();
    assert(calibration_save_status==3 && calibration_dirty && mock_flash_begins==1);
    nframes(500,128,128,128,0); assert(mock_flash_begins==1);
    mock_flash_fail_after=-1; mock_millis+=30000U; normal(128,128,128,0);
    assert(!calibration_dirty && calibration_save_status==CAL_STORE_SAVED);

    reset(); normal(128,128,128,SELECT | KEY(PS2_BUTTON_CIRCLE)); stopped();
    assert(!control_ready && calibration_save_status==CAL_STORE_SAVED);
    assert(!(mock_gpio_c & GPIO_Pin_13)); /* 保存成功两闪，第一下亮 */
    nframes(15,128,128,128,0); assert(mock_gpio_c & GPIO_Pin_13);
    nframes(15,128,128,128,0); assert(!(mock_gpio_c & GPIO_Pin_13));
    nframes(15,128,128,128,0); assert(mock_gpio_c & GPIO_Pin_13);
    puts("PASS: stationary-only flash save, 2s idle/30s rate limit, boot restore, failure retry, rearm and LED acknowledgement");
}
static void test_driver_learning(void)
{
    reset();
    nframes(1000,128,0,159,0); /* 普通驾驶中的旋转绝不能当成纠偏学习 */
    assert(motor_get_strafe_front_percent()==100 && !calibration_dirty);
    nframes(80,128,0,159,SELECT);
    assert(motor_get_strafe_front_percent()==100); /* 等待稳定，且不足累计1个百分点 */
    nframes(300,128,0,159,SELECT);
    assert(motor_get_strafe_front_percent()<100 && motor_get_strafe_front_percent()>95);
    nframes(2000,128,0,159,SELECT);
    assert(motor_get_strafe_front_percent()==95); /* 单次训练最多5个百分点 */
    assert(mock_flash_begins==0); /* 学习中绝不写Flash */
    normal(128,0,128,0); nframes(2000,128,0,159,SELECT);
    assert(motor_get_strafe_front_percent()==90); /* 松SELECT后开启新一轮 */

    reset(); nframes(2000,128,255,97,SELECT);
    assert(motor_get_strafe_front_percent()==95); /* 右移反向纠偏，学习符号仍一致 */
    reset(); motor_set_strafe_percent(90,95); nframes(2000,128,0,97,SELECT);
    assert(motor_get_strafe_front_percent()==95); /* 相反纠偏方向应增加前轮项 */
    motor_set_strafe_percent(100,100);
    normal(128,0,128,0); nframes(2000,128,0,97,SELECT);
    assert(motor_get_strafe_front_percent()==100 && motor_get_strafe_rear_percent()==95);

    reset(); nframes(1000,0,0,159,SELECT); /* 斜移不学习 */
    nframes(1000,128,100,159,SELECT); /* 小于30%的横移不学习 */
    nframes(1000,128,0,220,SELECT); /* 大幅旋转不学习 */
    assert(motor_get_strafe_front_percent()==100 && !calibration_dirty);
    mock_millis=UINT32_MAX-500U; boot();
    nframes(2000,128,0,159,SELECT);
    assert(motor_get_strafe_front_percent()==95); /* 学习时间在毫秒回绕时有效 */
    puts("PASS: driver-assisted learning gates, settle/rate/session limits, bilateral sign, rear fallback, millisecond wrap");
}
static void test_upgrade(void)
{
    mock_flash_seed_legacy(); mock_millis=0; boot();
    assert(!calibration_loaded && !calibration_dirty);
    assert(motor_get_strafe_front_percent()==100 && motor_get_strafe_rear_percent()==95);
    nframes(300,128,128,128,0);
    assert(mock_flash_begins==0); /* 升级不会开机自动写默认值 */
    normal(128,128,128,SELECT | KEY(PS2_BUTTON_CIRCLE));
    assert(calibration_loaded && !calibration_dirty && mock_flash_erases==0);
    boot();
    assert(calibration_loaded && motor_get_strafe_front_percent()==100);
    assert(motor_get_strafe_rear_percent()==95);
    puts("PASS: firmware upgrade rejects v1, starts front100/rear95, saves v2 only on request, restores on boot");
}
int main(void)
{
    test_manual(); test_save(); test_driver_learning(); test_upgrade();
    return 0;
}
