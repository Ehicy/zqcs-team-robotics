#include "motor_control.h"
#include "speed_control.h"
#include "remote_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static motor_control_t motor;
static const int16_t forward[4]={1000,-1000,-1000,-1000};
static const int16_t backward[4]={-1000,1000,1000,1000};
static const int16_t velocity[4]={100,-100,-100,-100};
static const int32_t counts[4]={0,0,0,0};
static void step(uint32_t now,const int16_t target[4],uint8_t enabled)
{ (void)motor_control_update(&motor,now,target,enabled,now,200); }
static void running(uint32_t base)
{
    unsigned i; motor_control_init(&motor,base);
    motor_control_set_feedback(&motor,velocity,15);
    step(base,forward,1);
    for(i=1;i<=200;i++) step(base+i,forward,1);
    assert(!memcmp(motor.output_permille,forward,sizeof(forward)));
}
static void presets(void)
{
    speed_control_t speed; int32_t sample[4]={0,0,0,0}; unsigned n,i;
    const int16_t half[4]={500,-500,500,500};
    speed_control_init(&speed,0,counts);
    assert(speed.pid_initialized && !speed.ready_mask);
    for(i=0;i<4;i++) {
        assert(speed.config[i].kp_milli==1000 && speed.config[i].ki_milli==2000 && !speed.config[i].kd_milli);
        assert(!speed.config[i].confirmed);
    }
    for(n=1;n<20;n++) {
        for(i=0;i<4;i++) sample[i]+=speed.config[i].encoder_sign;
        assert(speed_control_assist_pwm(&speed,n*10,sample,15,half,1,n*10,200));
    }
    assert(speed.pid_initialized && !speed.fault);
    for(i=0;i<4;i++) assert(speed.pwm_permille[i]*speed.config[i].motor_sign>500);
    speed_control_stop(&speed,200);
    assert(speed.pid_initialized); /* STOP is not loss of gain initialization. */
}
static void curve_and_reversal(void)
{
    unsigned t; int previous=0;
    motor_control_init(&motor,0);
    for(t=0;t<=200;t++) {
        step(t,forward,1);
        assert(motor.output_permille[0]>=previous && motor.output_permille[0]-previous<=20);
        if(t==50) assert(motor.output_permille[0]>=100 && motor.output_permille[0]<=105);
        if(t==100) assert(motor.output_permille[0]==500);
        previous=motor.output_permille[0];
    }
    assert(motor.output_permille[0]==1000);
    motor_control_set_feedback(&motor,velocity,15);
    step(201,backward,1); assert(!motor.output_permille[0] && motor.braking_mask==15);
    step(210,backward,1); assert(!motor.output_permille[0]);
    step(211,backward,1); assert(motor.output_permille[0]==-50 && !motor.short_brake_mask);
    step(240,backward,1); assert(motor.output_permille[0]==-50);
    step(241,backward,1); assert(!motor.output_permille[0] && !motor.braking_mask);
    step(242,backward,1); assert(!motor.output_permille[0]);
    for(t=243;t<=442;t++) step(t,backward,1);
    assert(motor.output_permille[0]==-1000);
}
static void explicit_stop_and_no_feedback(void)
{
    unsigned t; int16_t zero[4]={0,0,0,0};
    running(0); motor_control_quick_stop(&motor,201,forward,40);
    step(201,zero,0); assert(!motor.output_permille[0] && motor.braking_mask==15);
    for(t=202;t<241;t++) {
        motor_control_quick_stop(&motor,t,forward,40);
        step(t,zero,0);
        assert(motor.brake_start_ms[0]==201);
        if(t>=211) assert(motor.output_permille[0]==-50);
    }
    step(241,zero,0); assert(!motor.active && !motor.braking_mask);
    motor_control_quick_stop(&motor,242,zero,40); step(242,zero,0); assert(!motor.active);
    running(0); motor_control_set_feedback(&motor,0,0);
    motor_control_quick_stop(&motor,201,forward,40);
    step(211,zero,0); assert(motor.short_brake_mask==15 && !motor.output_permille[0] && motor.active);
    step(241,zero,0); assert(!motor.short_brake_mask && !motor.active);
    running(0); motor_control_set_feedback(&motor,zero,15);
    motor_control_quick_stop(&motor,201,forward,40); step(211,zero,0);
    assert(motor.short_brake_mask==15 && !motor.output_permille[0]);
}
static void crossing_abort_expiry_and_wrap(void)
{
    int16_t crossed[4]={-10,10,10,10},zero[4]={0,0,0,0};
    running(0); motor_control_quick_stop(&motor,201,forward,40); step(211,zero,0);
    motor_control_set_feedback(&motor,crossed,15); step(212,zero,0);
    assert(!motor.braking_mask && !motor.active);
    running(0); motor_control_quick_stop(&motor,201,forward,40); step(211,zero,0);
    motor_control_abort(&motor,212); step(213,zero,0); assert(!motor.active && !motor.braking_mask);
    running(0); motor_control_quick_stop(&motor,201,forward,40);
    step(1000,zero,0); assert(!motor.active); /* Missed window is skipped, never replayed. */
    running(0); step(201,backward,1);
    assert(!motor_control_update(&motor,211,backward,1,11,200));
    assert(!motor.active && !motor.braking_mask); /* Expiry never grants braking torque. */
    running(UINT32_MAX-300U);
    motor_control_quick_stop(&motor,UINT32_MAX-5U,forward,40);
    step(5,zero,0); assert(motor.output_permille[0]==-50);
    step(34,zero,0); assert(!motor.active);
    running(0); motor_control_quick_stop(&motor,201,forward,15);
    step(211,zero,0); assert(motor.output_permille[0]==-50);
    step(216,zero,0); assert(!motor.active); /* Source residence consumed the 40ms lease. */
}
static void wire_stop(void)
{
    link_node_t client,stm; uint8_t wire[LINK_WIRE_MAX]; size_t n,i;
    link_node_init(&client,LINK_CLIENT,1,0); link_node_init(&stm,LINK_CONTROLLER,2,0);
    client.connected=stm.connected=1; client.token=stm.token=42;
    client.remote_flags=LINK_FLAG_QUICK_STOP_SUPPORTED; stm.pwm_supported=1;
    assert(link_node_request_quick_stop(&client,10));
    assert(link_node_request_quick_stop(&client,20) && client.quick_stop_ms==10);
    n=link_node_transmit(&client,25,wire,sizeof(wire)); assert(n);
    for(i=0;i<n;i++) link_node_receive(&stm,wire[i],25);
    assert(stm.quick_stop_received && stm.quick_stop_lease_ms==25);
    assert(stm.accepted_controls==1 && !stm.requested_enable && !client.quick_stop_pending);
    assert(link_node_request_quick_stop(&client,30));
    n=link_node_transmit(&client,80,wire,sizeof(wire));
    for(i=0;i<n;i++) link_node_receive(&stm,wire[i],80);
    assert(!stm.quick_stop_received); /* Expired pending STOP falls back to disabled heartbeat. */
    client.remote_flags=0; assert(!link_node_request_quick_stop(&client,81));
    client.remote_flags=LINK_FLAG_QUICK_STOP_SUPPORTED;
    assert(link_node_request_quick_stop(&client,82));
    link_node_transport_error(&client,83); assert(!client.quick_stop_pending);
}
int main(void)
{
    presets(); curve_and_reversal(); explicit_stop_and_no_feedback();
    crossing_abort_expiry_and_wrap(); wire_stop();
    puts("PA quick profile: boot PID, 200ms curve, 10+30ms <=5% brake, short-brake fallback, no renewal, expiry/coast, source lease, wrap passed");
}
