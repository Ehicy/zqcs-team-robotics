#include "speed_control.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifndef CRTC_PA_CHASSIS
#error Build this test with CRTC_PA_CHASSIS=1
#endif
static speed_control_t state;
static int32_t counts[4];
static uint32_t now;
static const int16_t forward[4]={500,-500,500,500};
static const int16_t curve[4]={500,-250,500,250};
static const int16_t spin[4]={500,500,500,-500};
static int absolute(int x) { return x<0?-x:x; }
static int32_t as_signed(uint32_t x)
{ return x<=INT32_MAX?(int32_t)x:-(int32_t)(UINT32_MAX-x)-1; }
static void setup(int initialized)
{
    unsigned i; now=0; memset(counts,0,sizeof(counts));
    speed_control_init(&state,now,counts);
    assert(state.pid_initialized); /* PA presets work before any C3 transaction. */
    assert(state.config[0].motor_sign==1 && state.config[1].motor_sign==-1);
    assert(state.config[0].encoder_sign==-1 && state.config[1].encoder_sign==1);
    assert(state.config[2].motor_sign==1 && state.config[2].encoder_sign==-1);
    if(initialized) assert(speed_control_configure_pid(&state,1000,2000,1000));
    else state.pid_initialized=0; /* Explicitly simulate a missing/disabled controller. */
    for(i=0;i<4;i++) {
        speed_config_t c=state.config[i];
        /* ~1000mm circumference, 1000CPR => ~1mm per tick. */
        c.counts_per_rev=1000; c.diameter_tenth_mm=3183; c.accel_mm_s2=5000;
        c.confirmed=0; assert(speed_control_configure(&state,i,&c));
    }
    now=10;
    assert(!speed_control_assist_pwm(&state,now,counts,15,forward,0,now,200));
    assert(!state.active && !state.ready_mask);
}
static void advance(const int16_t ff[4],uint8_t mask,const int steps[4])
{
    unsigned i;
    for(i=0;i<4;i++) counts[i]=as_signed((uint32_t)counts[i]+(uint32_t)(steps[i]*state.config[i].encoder_sign));
    now+=10;
    assert(speed_control_assist_pwm(&state,now,counts,mask,ff,1,now,200));
    for(i=0;i<4;i++) {
        assert(absolute(state.pwm_permille[i])<=1000);
        assert(!ff[i] || state.pwm_permille[i]==0 || (state.pwm_permille[i]>0)==(ff[i]>0));
        assert(state.assist_quality[i]<=1000);
    }
}
static void warm(const int16_t ff[4],uint8_t mask,const int steps[4])
{ unsigned n; for(n=0;n<20;n++) advance(ff,mask,steps); }
static void normal_and_unconfirmed(void)
{
    const int moving[4]={1,1,1,1}; unsigned i;
    setup(1); warm(forward,15,moving);
    assert(state.feedback_valid_mask==15 && !state.ready_mask && state.pid_initialized);
    for(i=0;i<4;i++) {
        assert(state.assist_source[i]==SPEED_ASSIST_OWN && state.assist_quality[i]==1000);
        assert(state.measured_mm_s[i]==100 && state.assist_control_mm_s[i]==100);
        assert(absolute(state.pwm_permille[i])>500); assert(!state.config[i].confirmed);
    }
    assert(!state.fault);
    setup(0); warm(forward,15,moving);
    assert(!memcmp(state.pwm_permille,forward,sizeof(forward)));
}
static void one_bad_and_substitution(void)
{
    const int moving[4]={1,1,0,1}; int64_t prior; unsigned n;
    setup(1); warm(forward,11,moving);
    assert(state.feedback_valid_mask==11);
    assert(state.assist_source[2]==SPEED_ASSIST_SAME_SIDE);
    assert(state.assist_control_mm_s[2]==100 && !state.measured_mm_s[2]);
    assert(state.assist_quality[2]==600 && !state.assist_integral_micro[2]);
    assert(state.pwm_permille[2]>500 && state.active && !state.fault);
    /* A replacement may leak an old integral, but may never increase it. */
    state.assist_integral_micro[2]=100000000LL; prior=state.assist_integral_micro[2];
    for(n=0;n<3;n++) { advance(forward,11,moving); assert(state.assist_integral_micro[2]<=prior); prior=state.assist_integral_micro[2]; }
}
static void turn_never_uses_cross_side(void)
{
    const int moving_curve[4]={1,2,0,2},moving_spin[4]={1,-1,0,-1};
    setup(1); warm(curve,11,moving_curve);
    assert(state.assist_source[2]==SPEED_ASSIST_SAME_SIDE);
    assert(state.assist_control_mm_s[2]==100); /* Not opposite-side 200mm/s. */
    setup(1); warm(spin,11,moving_spin);
    assert(state.assist_source[2]==SPEED_ASSIST_SAME_SIDE && state.assist_control_mm_s[2]==100);
    setup(1); warm(spin,10,moving_spin);
    assert(state.assist_source[0]==SPEED_ASSIST_FEEDFORWARD && state.assist_source[2]==SPEED_ASSIST_FEEDFORWARD);
    assert(state.pwm_permille[0]==500 && state.pwm_permille[2]==500 && state.active);
    assert(state.feedback_valid_mask==10 && !state.fault);
    {
        const int16_t gentle[4]={500,-490,500,490};
        setup(1); warm(gentle,10,moving_curve);
        assert(state.assist_source[0]==SPEED_ASSIST_FEEDFORWARD && state.assist_source[2]==SPEED_ASSIST_FEEDFORWARD);
        assert(state.pwm_permille[0]==500 && state.pwm_permille[2]==500);
    }
}
static void straight_cross_and_all_bad(void)
{
    const int moving[4]={0,1,0,1}; unsigned i;
    setup(1); warm(forward,10,moving);
    assert(state.assist_source[0]==SPEED_ASSIST_STRAIGHT_CROSS && state.assist_source[2]==SPEED_ASSIST_STRAIGHT_CROSS);
    assert(state.assist_quality[0]==300 && state.assist_control_mm_s[0]==100);
    assert(!state.measured_mm_s[0] && state.feedback_valid_mask==10);
    advance(forward,0,moving);
    for(i=0;i<4;i++) { assert(state.assist_source[i]==SPEED_ASSIST_FEEDFORWARD); assert(!state.assist_quality[i]); assert(state.pwm_permille[i]==forward[i]); }
    /* A latched legacy diagnostic cannot inhibit the independent PWM path. */
    state.fault=SPEED_FAULT_ENCODER; advance(forward,0,moving);
    assert(state.active && !memcmp(state.pwm_permille,forward,sizeof(forward)));
}
static void restoration_and_no_reverse(void)
{
    const int dead[4]={1,1,0,1},alive[4]={1,1,1,1}; int16_t prior; unsigned n;
    setup(1); warm(forward,11,dead);
    counts[2]=1000000; prior=state.pwm_permille[2]; advance(forward,15,alive);
    assert(!(state.feedback_valid_mask&4)); assert(absolute(state.pwm_permille[2]-prior)<=10);
    prior=state.pwm_permille[2]; advance(forward,15,alive);
    assert(state.feedback_valid_mask==15 && state.assist_own_quality[2]==100);
    assert(absolute(state.pwm_permille[2]-prior)<=10); assert(!state.assist_derivative_milli[2]);
    for(n=0;n<30;n++) advance(forward,15,alive);
    {
        const int16_t small[4]={20,-20,20,20},reverse[4]={-20,20,-20,-20},zero[4]={0,0,0,0};
        for(n=0;n<10;n++) advance(small,15,alive);
        advance(zero,15,alive); assert(!state.active);
        advance(reverse,15,alive);
        assert(state.pwm_permille[0]<=0 && state.pwm_permille[1]>=0 && state.pwm_permille[2]<=0);
    }
}
static void no_pulses_direction_and_outliers(void)
{
    const int stationary[4]={0,0,0,0},wrong[4]={-1,1,1,1},jump[4]={100000,1,1,1}; unsigned i;
    setup(1); warm(forward,15,stationary); warm(forward,15,stationary);
    assert(!state.feedback_valid_mask && state.active && !state.fault);
    for(i=0;i<4;i++) assert(state.pwm_permille[i]==forward[i]);
    setup(1); warm(curve,15,wrong);
    assert(!(state.feedback_valid_mask&1) && state.assist_source[0]==SPEED_ASSIST_SAME_SIDE);
    setup(1); advance(forward,15,jump);
    assert(!(state.feedback_valid_mask&1) && state.active && !state.fault);
}
static void stop_expiry_disabled_sampling(void)
{
    const int moving[4]={1,1,1,1}; unsigned i;
    setup(1); warm(forward,15,moving);
    now+=10;
    assert(!speed_control_assist_pwm(&state,now,counts,15,forward,1,now-200,200));
    assert(!state.active);
    for(i=0;i<4;i++) assert(!state.pwm_permille[i] && !state.assist_integral_micro[i] && !state.assist_derivative_valid[i]);
    for(i=0;i<4;i++) counts[i]+=state.config[i].encoder_sign;
    now+=10;
    assert(!speed_control_assist_pwm(&state,now,counts,15,forward,0,now,200));
    assert(!state.active && state.feedback_valid_mask==15);
    for(i=0;i<4;i++) assert(state.measured_mm_s[i]==100 && !state.pwm_permille[i]);
    assert(!speed_control_assist_pwm(&state,now,counts,15,0,1,now,200));
}
static void ten_ms_window_and_wrap(void)
{
    unsigned n,i; const int one[4]={1,1,1,1};
    setup(1);
    for(n=1;n<10;n++) {
        assert(speed_control_assist_pwm(&state,10+n,counts,n==3?14:15,forward,1,10+n,200));
        assert(state.assist_last_sample_ms==10);
    }
    advance(forward,15,one); assert(state.feedback_valid_mask==14);
    advance(forward,15,one); assert(state.feedback_valid_mask==14);
    advance(forward,15,one); assert(state.feedback_valid_mask==15);
    setup(1);
    for(i=0;i<4;i++) {
        counts[i]=(i&1U)?INT32_MAX:INT32_MIN;
        state.assist_last_counts[i]=(uint32_t)counts[i];
    }
    advance(forward,15,one);
    for(i=0;i<4;i++) assert(state.measured_mm_s[i]==100);
}
static void malformed_sensor_and_timing_never_disable_ff(void)
{
    const int one[4]={1,1,1,1}; unsigned i;
    setup(1); state.config[0].counts_per_rev=0; state.config[1].encoder_sign=0;
    advance(forward,15,one);
    assert(state.pwm_permille[0]==forward[0] && state.pwm_permille[1]==forward[1] && state.active);
    now+=1000;
    assert(speed_control_assist_pwm(&state,now,counts,15,forward,1,now,200));
    for(i=0;i<4;i++) assert(state.pwm_permille[i]==forward[i]);
    assert(!state.feedback_valid_mask && !state.fault);
}
static void long_idle_does_not_count_as_failed_start(void)
{
    const int stationary[4]={0,0,0,0}; unsigned n;
    setup(1);
    for(n=0;n<100;n++) {
        now+=10; assert(!speed_control_assist_pwm(&state,now,counts,15,forward,0,now,200));
    }
    advance(forward,15,stationary);
    assert(state.feedback_valid_mask==15); /* New request has its own progress grace. */
    for(n=0;n<30;n++) advance(forward,15,stationary);
    assert(!state.feedback_valid_mask && state.active);
}
static void four_wheel_loaded_plant(void)
{
    double velocity[4]={0,0,0,0},travel[4]={0,0,0,0};
    const double load[4]={0,20,40,60}; unsigned i,n;
    setup(1);
    assert(speed_control_configure_pid(&state,1000,2000,0));
    for(i=0;i<4;i++) {
        speed_config_t c=state.config[i];
        c.diameter_tenth_mm=650; c.counts_per_rev=i<2?1768:884;
        assert(speed_control_configure(&state,i,&c));
    }
    for(n=0;n<1800;n++) {
        for(i=0;i<4;i++) {
            double drive=(double)state.pwm_permille[i]*state.config[i].motor_sign;
            double demand=drive*0.3-load[i];
            if(demand<0) demand=0;
            velocity[i]+=(demand-velocity[i])*0.1;
            travel[i]+=velocity[i]*0.01;
            counts[i]=(int32_t)(travel[i]*state.config[i].counts_per_rev/(65.0*3.14159))*state.config[i].encoder_sign;
        }
        now+=10;
        assert(speed_control_assist_pwm(&state,now,counts,15,forward,1,now,200));
    }
    for(i=0;i<4;i++) {
        assert(velocity[i]>140 && velocity[i]<160);
        assert(state.assist_source[i]==SPEED_ASSIST_OWN);
    }
    assert(state.assist_integral_micro[3]>state.assist_integral_micro[1]);
    assert(!state.ready_mask && !state.fault);
    printf("Adaptive four-wheel quantized plant at model target150: %.1f %.1f %.1f %.1f mm/s (simulation only)\n",
        velocity[0],velocity[1],velocity[2],velocity[3]);
}
int main(void)
{
    normal_and_unconfirmed(); one_bad_and_substitution(); turn_never_uses_cross_side();
    straight_cross_and_all_bad(); restoration_and_no_reverse(); no_pulses_direction_and_outliers();
    stop_expiry_disabled_sampling(); ten_ms_window_and_wrap(); malformed_sensor_and_timing_never_disable_ff();
    long_idle_does_not_count_as_failed_start();
    four_wheel_loaded_plant();
    puts("Adaptive PWM: own/same-side/straight-cross feedback, independent histories, unconfirmed models, bad sensors, smooth recovery, direction/STOP/TTL/10ms/wrap passed");
    return 0;
}
