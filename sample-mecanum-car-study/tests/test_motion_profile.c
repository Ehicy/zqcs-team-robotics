#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "mock_hardware.h"
#include "mock_calibration_flash.h"
#include "motor_tuning.h"
#define main firmware_main
#include "../hardware/Src/main.c"
#undef main
#define START ((uint16_t)(1U<<PS2_BUTTON_START))
static int output(unsigned wheel)
{ return (int)mock_ccr[2*wheel]-(int)mock_ccr[2*wheel+1]; }
static void coast(void)
{ unsigned i; for(i=0;i<8;i++) assert(mock_ccr[i]==0); }
static void short_brake(void)
{ unsigned i; for(i=0;i<8;i++) assert(mock_ccr[i]==PWM_PERIOD_COUNTS); }
static void advance(uint32_t ms)
{ mock_millis+=ms; motor_update(); }
static void reset(void)
{ mock_millis=0; motor_init(); }
static void full_forward(void)
{ unsigned i; motor(0,128,128); for(i=0;i<60;i++) advance(10); assert(output(2)==3600); }
static void test_s_curve(void)
{
    unsigned i; int previous=0;
    reset(); motor(0,128,128);
    for(i=1;i<=50;i++)
    {
        advance(10);
        assert(output(2)>=previous && output(2)-previous<=136);
        previous=output(2);
        assert(abs(output(0)+output(2))<=1 && abs(output(1)+output(2))<=1);
        if(i==10) assert(abs(output(2)-208)<=1); /* S(0.2)=0.05792 */
        if(i==20) assert(abs(output(2)-1142)<=1); /* S(0.4)=0.31744 */
        if(i==25) assert(output(2)==1800);
        if(i==40) assert(abs(output(2)-3391)<=1);
        if(i==49) assert(output(2)<3600);
    }
    assert(output(2)==3600);
    reset(); motor_set_speed_percent(10); motor(0,128,128);
    for(i=0;i<7;i++) advance(10);
    assert(output(2)<360); advance(10); assert(output(2)==360); /* 最短80ms */
    reset(); motor(0,128,128); advance(1000); assert(output(2)<=3);
    /* 包络内改变目标：不重启计时，每轮跳变受公共斜率限制。 */
    for(i=0;i<15;i++) advance(10);
    previous=output(2); motor(0,0,128); advance(10);
    assert(abs(output(2)-previous)<=136);
    motor_stop(); coast(); advance(1000); coast();
    puts("PASS: quintic startup timing/anchors/monotonicity, four-wheel ratio, low-target duration, stall cap, retarget slope, abort");
}
static void test_reverse_stop(void)
{
    unsigned i; int previous[4];
    reset(); full_forward();
    for(i=0;i<4;i++) previous[i]=output(i);
    motor(128,128,128); coast(); assert(motor_get_stop_phase()==MOTOR_STOP_COAST);
    advance(19); coast(); motor_quick_stop(); /* 重复松杆/START不能重计时 */
    advance(1); assert(motor_get_stop_phase()==MOTOR_STOP_REVERSE);
    for(i=0;i<4;i++) assert(abs(output(i)+previous[i]/10)<=1);
    advance(20); motor_quick_stop();
    advance(9); assert(motor_get_stop_phase()==MOTOR_STOP_REVERSE);
    advance(1); short_brake(); assert(!motor_output_above_percent(0));
    motor_set_speed_percent(50); short_brake(); /* 改档不得提前释放短路制动 */
    advance(99); short_brake(); advance(1); coast();
    assert(motor_get_stop_phase()==MOTOR_STOP_IDLE);
    advance(1000); coast(); /* 单次停止，不可转成持续反向行驶 */

    reset(); motor_set_strafe_percent(100,100); motor(0,0,128);
    for(i=0;i<60;i++) advance(10);
    assert(output(0)==0 && output(3)==0);
    motor_quick_stop(); advance(20);
    assert(output(0)==0 && output(3)==0 && output(1)==360 && output(2)==-360);
    motor_stop(); coast(); assert(motor_get_stop_phase()==MOTOR_STOP_IDLE);

}
static void test_bounds_and_deadlines(void)
{
    unsigned i;
    reset(); motor_set_speed_percent(20); motor(0,128,128);
    for(i=0;i<30;i++) advance(10);
    assert(output(2)==720);
    motor_quick_stop(); short_brake(); assert(motor_get_stop_phase()==MOTOR_STOP_HOLD);
    advance(100); coast();

    reset(); full_forward(); motor_quick_stop(); motor_set_speed_percent(1);
    advance(20); for(i=0;i<4;i++) assert(abs(output(i))<=36);
    motor_set_speed_percent(0); coast(); advance(1000); coast();
    reset(); full_forward(); motor_quick_stop(); advance(1000);
    short_brake(); assert(motor_get_stop_phase()==MOTOR_STOP_HOLD); /* 过期窗口跳过反转 */
    advance(100); coast();
    reset(); full_forward(); mock_millis=UINT32_MAX-10U; motor_quick_stop();
    advance(20); assert(output(2)==-360); advance(30); short_brake(); advance(100); coast();
    reset(); full_forward(); motor_quick_stop(); advance(20);
    motor(NAN,128,128); coast(); assert(motor_get_stop_phase()==MOTOR_STOP_IDLE);
    puts("PASS: single proportional reverse window, zero wheels, short-brake truth table, low-speed bypass, caps, late-window skip, wrap, abort");
}
static void frame(uint8_t forward,uint16_t pressed,uint8_t mode,uint8_t marker)
{
    uint8_t b[9]={0xff,mode,marker,(uint8_t)~pressed,(uint8_t)~(pressed>>8),128,128,128,forward};
    mock_frame(b,9); mock_millis+=10; control_step(); mock_assert_poll_complete();
}
static void control_boot(void)
{
    reset(); mock_flash_init(); calibration_control_init(); control_ready=0;
    buttons_reset(); speed_led_init(); ps2_init(); frame(128,0,0x73,0x5a);
    assert(control_ready); coast();
}
static void test_protocol_stop(void)
{
    unsigned i;
    control_boot(); for(i=0;i<60;i++) frame(0,0,0x73,0x5a);
    frame(0,START,0x73,0x5a); coast(); assert(!control_ready);
    frame(128,START,0x73,0x5a); frame(128,START,0x73,0x5a);
    assert(motor_get_stop_phase()==MOTOR_STOP_REVERSE && output(2)==-360);
    frame(128,0,0x73,0x5a); assert(!control_ready); /* 脉冲中回中不能恢复 */
    for(i=0;i<20;i++) frame(0,START,0x73,0x5a);
    assert(motor_get_stop_phase()==MOTOR_STOP_IDLE && !control_ready); coast();
    frame(0,0,0x73,0x5a); coast(); assert(!control_ready);
    frame(128,0,0x73,0x5a); coast(); assert(control_ready);
    for(i=0;i<60;i++) frame(0,0,0x73,0x5a);
    frame(128,0,0x73,0x5a); assert(motor_get_stop_phase()==MOTOR_STOP_COAST);
    frame(128,0,0x73,0x5a); frame(128,0,0x73,0x5a);
    assert(motor_get_stop_phase()==MOTOR_STOP_REVERSE);
    frame(128,START,0x73,0); coast(); assert(motor_get_stop_phase()==MOTOR_STOP_IDLE);
    frame(128,0,0x73,0x5a); assert(control_ready);
    for(i=0;i<60;i++) frame(0,0,0x73,0x5a);
    frame(0,START,0x41,0x5a); coast(); assert(!control_ready);
    assert(motor_get_stop_phase()==MOTOR_STOP_IDLE); /* 数字START也不允许反转 */
    puts("PASS: analog START and neutral braking, held START single-shot, rearm gate, invalid-frame cancellation, digital hard stop");
}
int main(void)
{
    setbuf(stdout,NULL);
    test_s_curve(); test_reverse_stop(); test_bounds_and_deadlines(); test_protocol_stop();
    return 0;
}
