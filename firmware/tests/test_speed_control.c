#include "speed_control.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int32_t no_counts[4]={0,0,0,0};
static const int16_t zero_target[4]={0,0,0,0};
static const int16_t forward[4]={100,100,100,100};
static const int16_t reverse_target[4]={-100,-100,-100,-100};
static int absolute(int value) { return value<0 ? -value : value; }
static int32_t signed_count(uint32_t raw)
{ return raw<=INT32_MAX ? (int32_t)raw : -(int32_t)(UINT32_MAX-raw)-1; }
static void stopped(const speed_control_t *s)
{
    unsigned i; assert(!s->active);
    for(i=0;i<4;i++) assert(!s->pwm_permille[i] && !s->ramped_mm_s[i] && !s->integral_micro[i]);
}
static void confirm(speed_control_t *s)
{
    unsigned i;
    assert(speed_control_configure_pid(s,1000,2000,0));
    for(i=0;i<4;i++) {
        speed_config_t c=s->config[i]; c.confirmed=1;
        assert(speed_control_configure(s,i,&c));
    }
    assert(s->ready_mask==15);
}
static void ready(speed_control_t *s, uint32_t now)
{ speed_control_init(s,now,no_counts); confirm(s); }

static void test_config_and_confirmation(void)
{
    speed_control_t s; speed_config_t c; int32_t counts[4]={8,8,8,4};
    speed_control_init(&s,0,no_counts); stopped(&s);
    assert(s.ready_mask==0 && s.config[0].counts_per_rev==1768 && s.config[3].counts_per_rev==884);
    assert(s.config[0].diameter_tenth_mm==650 && s.config[0].kp_milli==1000);
    assert(s.config[0].ki_milli==2000 && s.config[0].max_mm_s==300 && s.config[0].accel_mm_s2==500);
    assert(!speed_control_update(&s,0,no_counts,15,forward,1,0,200));
    assert(s.fault==SPEED_FAULT_UNCALIBRATED); stopped(&s);
    confirm(&s); assert(speed_control_clear_fault(&s));
    c=s.config[0]; c.counts_per_rev=0; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.counts_per_rev=1000001; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.diameter_tenth_mm=5001; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.kp_milli=20001; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.ki_milli=20001; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.kp_milli=c.ki_milli=0; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.motor_sign=0; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.encoder_sign=2; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.confirmed=2; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.max_mm_s=1001; assert(!speed_control_configure(&s,0,&c));
    c=s.config[0]; c.accel_mm_s2=0; assert(!speed_control_configure(&s,0,&c));
    assert(!speed_control_configure(&s,4,&s.config[0]));
    assert(!speed_control_configure(&s,0,0));
    assert(speed_control_update(&s,10,no_counts,15,forward,1,10,200)); assert(s.active);
    assert(!speed_control_configure(&s,0,&s.config[0])); assert(!speed_control_clear_fault(&s));
    speed_control_stop(&s,11); stopped(&s);
    assert(!speed_control_update(&s,20,counts,15,zero_target,0,0,0));
    assert(s.measured_mm_s[0]>5);
    assert(!speed_control_configure(&s,0,&s.config[0]));
    assert(!speed_control_update(&s,30,counts,15,zero_target,0,0,0));
    c=s.config[0]; c.confirmed=0;
    assert(speed_control_configure(&s,0,&c)); assert(s.ready_mask==14);
    assert(!speed_control_update(&s,40,counts,15,forward,1,40,200));
    assert(s.fault==SPEED_FAULT_UNCALIBRATED); stopped(&s);
    puts("PASS provisional defaults, all-wheel confirmation, bounded integer config and physical-stop gate");
}

static void test_measurement_signs_and_continuity(void)
{
    speed_control_t s; speed_config_t c; unsigned i;
    int32_t counts[4]={8,8,8,4};
    ready(&s,0);
    assert(!speed_control_update(&s,10,counts,15,0,0,0,0));
    for(i=0;i<4;i++) assert(s.measured_mm_s[i]==92);
    speed_control_stop(&s,11);
    assert(s.last_sample_ms==10 && s.last_counts[0]==8);
    for(i=0;i<4;i++) counts[i]*=2;
    assert(!speed_control_update(&s,20,counts,15,0,0,0,0));
    for(i=0;i<4;i++) assert(s.measured_mm_s[i]==92);
    ready(&s,0); c=s.config[0]; c.motor_sign=-1; c.encoder_sign=-1;
    assert(speed_control_configure(&s,0,&c));
    assert(speed_control_update(&s,10,no_counts,15,forward,1,10,200));
    assert(s.pwm_permille[0]<0 && s.pwm_permille[1]>0);
    counts[0]=-8; counts[1]=counts[2]=8; counts[3]=4;
    assert(speed_control_update(&s,20,counts,15,forward,1,20,200));
    for(i=0;i<4;i++) assert(s.measured_mm_s[i]==92);
    puts("PASS x4/x2 CPR conversion, motor/encoder signs and continuous measurement while stopped");
}

static void test_lease_and_sample_schedule(void)
{
    speed_control_t s; unsigned t;
    ready(&s,0);
    assert(speed_control_update(&s,0,no_counts,15,forward,1,0,200)); stopped(&s);
    assert(speed_control_update(&s,9,no_counts,15,forward,1,0,200)); stopped(&s);
    assert(speed_control_update(&s,10,no_counts,15,forward,1,0,200));
    assert(s.ramped_mm_s[0]==5 && s.pwm_permille[0]==5);
    for(t=20;t<=190;t+=10) assert(speed_control_update(&s,t,no_counts,15,forward,1,0,200));
    assert(speed_control_update(&s,199,no_counts,15,forward,1,0,200));
    assert(!speed_control_update(&s,200,no_counts,15,forward,1,0,200)); stopped(&s);
    assert(!s.fault);
    assert(!speed_control_update(&s,201,no_counts,15,forward,1,0,200)); stopped(&s);
    assert(!speed_control_update(&s,202,no_counts,15,forward,1,202,201)); stopped(&s);
    assert(!speed_control_update(&s,203,no_counts,15,forward,1,204,200)); stopped(&s);
    assert(speed_control_update(&s,210,no_counts,15,forward,1,210,200)); assert(s.active);
    assert(speed_control_update(&s,211,no_counts,15,zero_target,1,211,200)); stopped(&s);
    assert(!s.fault);
    ready(&s,0);
    assert(speed_control_update(&s,50,no_counts,15,forward,1,50,200));
    assert(s.ramped_mm_s[0]==25); /* actual dt=50, not an assumed 10ms */
    puts("PASS 10ms sampling with actual dt, immediate zero coast and original lease never renewed");
}

static void test_faults(void)
{
    speed_control_t s; unsigned t,i; int32_t counts[4]={0,0,0,0};
    const int16_t crawl[4]={1,1,1,1};
    ready(&s,0);
    assert(speed_control_update(&s,10,counts,15,forward,1,10,200));
    assert(!speed_control_update(&s,11,counts,7,forward,1,11,200));
    assert(s.fault==SPEED_FAULT_ENCODER); stopped(&s);
    assert(!speed_control_clear_fault(&s));
    assert(!speed_control_update(&s,21,counts,15,forward,1,21,200));
    assert(speed_control_clear_fault(&s));
    counts[0]=100000;
    assert(!speed_control_update(&s,31,counts,15,forward,1,31,200));
    assert(s.fault==SPEED_FAULT_ENCODER); stopped(&s);
    ready(&s,0);
    assert(!speed_control_update(&s,51,no_counts,15,forward,1,51,200));
    assert(s.fault==SPEED_FAULT_TIMING); stopped(&s);
    speed_control_stop(&s,52); assert(s.fault==SPEED_FAULT_TIMING);
    assert(!speed_control_update(&s,61,no_counts,15,zero_target,0,61,200));
    assert(speed_control_clear_fault(&s));
    assert(!speed_control_update(&s,60,no_counts,15,forward,1,60,200));
    assert(s.fault==SPEED_FAULT_TIMING); stopped(&s);
    ready(&s,0);
    for(t=10;t<=700 && !s.fault;t+=10)
        (void)speed_control_update(&s,t,no_counts,15,forward,1,t,200);
    assert(s.fault==SPEED_FAULT_STALL && t<=700); stopped(&s);
    assert(!speed_control_update(&s,t,no_counts,15,forward,1,t,200)); stopped(&s);
    assert(speed_control_clear_fault(&s));
    ready(&s,0);
    for(t=10;t<=550 && !s.fault;t+=10)
        (void)speed_control_update(&s,t,no_counts,15,crawl,1,t,200);
    assert(s.fault==SPEED_FAULT_STALL); stopped(&s);
    ready(&s,0); memset(counts,0,sizeof(counts));
    for(t=10;t<=300 && !s.fault;t+=10) {
        for(i=0;i<4;i++) counts[i]-=(i==3)?2:4;
        (void)speed_control_update(&s,t,counts,15,forward,1,t,200);
    }
    assert(s.fault==SPEED_FAULT_DIRECTION && t<=300); stopped(&s);
    puts("PASS invalid mask/jump, >50ms timing, backwards time, 500ms no-progress and opposite-feedback latch");
}

static void test_wrap_and_fractional_ramp(void)
{
    speed_control_t s; speed_config_t c; uint32_t start=0xfffffff0UL; unsigned i,t;
    int32_t counts[4]={-4,-4,-4,-2};
    speed_control_init(&s,start,counts); confirm(&s);
    for(i=0;i<4;i++) counts[i]=signed_count((uint32_t)counts[i]+((i==3)?5U:10U));
    assert(!speed_control_update(&s,start+10U,counts,15,0,0,0,0));
    for(i=0;i<4;i++) assert(s.measured_mm_s[i]==115);
    for(i=0;i<4;i++) counts[i]=signed_count((uint32_t)counts[i]-((i==3)?5U:10U));
    assert(!speed_control_update(&s,start+20U,counts,15,0,0,0,0));
    for(i=0;i<4;i++) assert(s.measured_mm_s[i]==-115);
    assert(!s.fault);
    ready(&s,start);
    for(t=10;t<200;t+=10) assert(speed_control_update(&s,start+t,no_counts,15,forward,1,start,200));
    assert(!speed_control_update(&s,start+200U,no_counts,15,forward,1,start,200)); stopped(&s);
    ready(&s,start);
    assert(speed_control_update(&s,start+10U,no_counts,15,forward,1,start+10U,200));
    speed_control_stop(&s,start+11U);
    assert(speed_control_update(&s,start+40U,no_counts,15,reverse_target,1,start+40U,200)); stopped(&s);
    assert(speed_control_update(&s,start+61U,no_counts,15,reverse_target,1,start+61U,200)); stopped(&s);
    assert(speed_control_update(&s,start+71U,no_counts,15,reverse_target,1,start+71U,200));
    assert(s.pwm_permille[0]<0 && !s.fault);
    ready(&s,0);
    for(i=0;i<4;i++) { c=s.config[i]; c.accel_mm_s2=1; assert(speed_control_configure(&s,i,&c)); }
    for(t=10;t<=1000;t+=10) assert(speed_control_update(&s,t,no_counts,15,forward,1,t,200));
    assert(s.ramp_milli[0]==1000 && s.ramped_mm_s[0]==1);
    assert(!s.fault);
    puts("PASS millis/count32 wrap, negative deltas and fractional acceleration without floating point");
}

/* The plant exists ONLY in this host test: first-order motor response with
 * load/deadband and quantized encoders. It is not simulated firmware feedback. */
typedef struct {
    speed_control_t controller;
    double velocity[4], total_counts[4], gain, load;
    int8_t motor_sign[4], encoder_sign[4];
    int32_t counts[4]; uint32_t now;
} plant_t;
static void plant_init(plant_t *p)
{
    unsigned i;
    memset(p,0,sizeof(*p)); p->gain=1.2; p->load=20;
    speed_control_init(&p->controller,0,p->counts);
    assert(speed_control_configure_pid(&p->controller,1000,2000,0));
    for(i=0;i<4;i++) {
        speed_config_t c=p->controller.config[i];
        p->motor_sign[i]=(i&1)?-1:1; p->encoder_sign[i]=(i==1 || i==2)?-1:1;
        c.motor_sign=p->motor_sign[i]; c.encoder_sign=p->encoder_sign[i]; c.confirmed=1;
        assert(speed_control_configure(&p->controller,i,&c));
    }
}
static void plant_step(plant_t *p, const int16_t target[4])
{
    unsigned i; p->now+=10;
    for(i=0;i<4;i++) {
        double drive=p->controller.pwm_permille[i]*p->motor_sign[i], demanded=0;
        double cpr=(i==3)?884.0:1768.0;
        if(drive>p->load) demanded=(drive-p->load)*p->gain;
        if(drive<-p->load) demanded=(drive+p->load)*p->gain;
        p->velocity[i]+=(demanded-p->velocity[i])*(0.01/0.12);
        p->total_counts[i]+=p->velocity[i]*0.01*cpr/(65.0*3.14159)*p->encoder_sign[i];
        p->counts[i]=signed_count((uint32_t)(int64_t)llround(p->total_counts[i]));
    }
    assert(speed_control_update(&p->controller,p->now,p->counts,15,target,1,p->now,200));
    assert(!p->controller.fault);
    for(i=0;i<4;i++) assert(absolute(p->controller.pwm_permille[i])<=SPEED_PWM_LIMIT);
}
static void plant_run(plant_t *p, const int16_t target[4], unsigned steps)
{ unsigned i; for(i=0;i<steps;i++) plant_step(p,target); }
static void near_velocity(const plant_t *p, int target, double tolerance)
{ unsigned i; for(i=0;i<4;i++) assert(fabs(p->velocity[i]-target)<=tolerance); }

static void test_plant_convergence_and_load(void)
{
    plant_t p; double before; plant_init(&p);
    plant_run(&p,forward,800); near_velocity(&p,100,8);
    before=p.velocity[0]; p.load=60;
    plant_run(&p,forward,15); assert(p.velocity[0]<before-10);
    plant_run(&p,forward,800); near_velocity(&p,100,8);
    printf("PASS four-wheel quantized plant convergence/load rejection: %.1f %.1f %.1f %.1f mm/s\n",
           p.velocity[0],p.velocity[1],p.velocity[2],p.velocity[3]);
}
static void test_plant_saturation_recovery(void)
{
    plant_t p; unsigned i; int64_t saturated_integral[4];
    const int16_t fast[4]={300,300,300,300};
    const int16_t slow[4]={80,80,80,80};
    plant_init(&p); p.gain=0.2; p.load=0;
    plant_run(&p,fast,800); /* 100% ceiling needs longer to reach saturation than the old 50% ceiling. */
    for(i=0;i<4;i++) saturated_integral[i]=p.controller.integral_micro[i];
    plant_run(&p,fast,600);
    for(i=0;i<4;i++) {
        assert(absolute(p.controller.pwm_permille[i])>=SPEED_PWM_LIMIT-10);
        assert(p.controller.integral_micro[i]==saturated_integral[i]);
        assert(llabs(p.controller.integral_micro[i])<(int64_t)SPEED_PWM_LIMIT*1000000LL);
    }
    p.gain=1.2;
    plant_run(&p,slow,600); near_velocity(&p,80,8);
    puts("PASS unreachable target saturation does not wind up; release/load change converges again");
}
static void test_plant_reversal_and_stop_hold(void)
{
    plant_t p; unsigned step; uint32_t zero_at=0; int saw_reverse=0;
    plant_init(&p); plant_run(&p,forward,600); near_velocity(&p,100,8);
    for(step=0;step<800;step++) {
        int before=p.controller.ramped_mm_s[0];
        plant_step(&p,reverse_target);
        if(before>0 && !p.controller.ramped_mm_s[0] && !zero_at) zero_at=p.now;
        if(p.controller.pwm_permille[0]<0) {
            assert(zero_at && (uint32_t)(p.now-zero_at)>=SPEED_REVERSE_HOLD_MS);
            saw_reverse=1;
        }
    }
    assert(saw_reverse); near_velocity(&p,-100,8);
    speed_control_stop(&p.controller,p.now); stopped(&p.controller);
    zero_at=p.now;
    for(step=0;step<5;step++) {
        plant_step(&p,forward); assert(p.controller.pwm_permille[0]==0);
    }
    assert(p.now-zero_at==50);
    plant_run(&p,forward,800); near_velocity(&p,100,8);
    puts("PASS signed four-wheel plant reversal, 50ms coast, stop cannot bypass reverse hold");
}
int main(void)
{
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    setvbuf(stdout,NULL,_IONBF,0);
    test_config_and_confirmation(); test_measurement_signs_and_continuity();
    test_lease_and_sample_schedule(); test_faults(); test_wrap_and_fractional_ramp();
    test_plant_convergence_and_load(); test_plant_saturation_recovery();
    test_plant_reversal_and_stop_hold();
    puts("All speed-control tests passed. Plant simulation is not physical calibration.");
    return 0;
}
