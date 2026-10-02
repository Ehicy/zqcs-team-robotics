#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "motor.h"

static void close_to(float actual, float expected)
{ assert(fabsf(actual - expected) < 0.00001f); }
static void test_anchor_points(void)
{
    const float raw[] = {128,90,60,30,0}; /* 死区后幅度0、1/4、1/2、3/4、1 */
    const float left[] = {0,0.19140625f,0.40625f,0.66796875f,1};
    const float yaw[] = {0,0.1328125f,0.3125f,0.5859375f,1};
    unsigned i;
    float f,s,t;
    for (i=0; i<5; i++)
    {
        motor_joystick_map(raw[i],128,raw[i],&f,&s,&t);
        close_to(f,left[i]); close_to(s,0); close_to(t,yaw[i]);
        motor_joystick_map(128,raw[i],raw[i],&f,&s,&t);
        close_to(f,0); close_to(s,left[i]); close_to(t,yaw[i]);
    }
    motor_joystick_map(195.5f,128,195.5f,&f,&s,&t);
    close_to(f,-0.40625f); close_to(t,-0.3125f); /* 归一化负半幅 */
    motor_joystick_map(255,255,255,&f,&s,&t);
    close_to(f,-1); close_to(s,-1); close_to(t,-1);
    motor_joystick_map(60,60,128,&f,&s,&t);
    close_to(f,0.4375f); close_to(s,0.4375f); /* 两轴共用增益 */
    motor_joystick_map(30,90,128,&f,&s,&t);
    close_to(f,0.6796875f); close_to(s,0.2265625f); /* 保留3:1方向 */
    motor_joystick_map(0,60,128,&f,&s,&t);
    close_to(f,1); close_to(s,0.5f); /* 单位圆外仍由麦轮混合统一限幅 */
    puts("PASS: joystick cubic-blend anchors, negative symmetry, full throw, joint left-stick direction");
}
static void test_properties(void)
{
    unsigned i,j;
    float f,s,t,previous=1;
    for(i=0; i<256; i++)
    {
        float linear=motor_joystick_axis((float)i);
        motor_joystick_map((float)i,128,(float)i,&f,&s,&t);
        assert(isfinite(f) && isfinite(s) && isfinite(t));
        assert(f<=previous); previous=f;
        assert(fabsf(f)<=fabsf(linear)+0.000001f && fabsf(t)<=fabsf(linear)+0.000001f);
        assert((f==0)==motor_joystick_is_centered((uint8_t)i));
        assert((t==0)==motor_joystick_is_centered((uint8_t)i));
        assert(f*linear>=0 && t*linear>=0 && s==0);
    }
    for(i=0; i<256; i++) for(j=0; j<256; j++)
    {
        float before_f=motor_joystick_axis((float)i);
        float before_s=motor_joystick_axis((float)j);
        float first_f,first_s,first_t;
        motor_joystick_map((float)i,(float)j,128,&first_f,&first_s,&first_t);
        motor_joystick_map((float)i,(float)j,0,&f,&s,&t);
        close_to(f,first_f); close_to(s,first_s); /* 转杆不影响左杆曲线 */
        assert(fabsf(f)<=1 && fabsf(s)<=1 && t==1);
        assert(fabsf(f*before_s-s*before_f)<0.000001f);
    }
    motor_joystick_map(NAN,INFINITY,-INFINITY,&f,&s,&t);
    close_to(f,0); close_to(s,0); close_to(t,0);
    puts("PASS: all 256 axis values monotonic/deadzone/bounded, 65536 direction/independence checks, nonfinite rejection");
}
int main(void)
{ test_anchor_points(); test_properties(); return 0; }
