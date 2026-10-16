#include "motor_control.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static const int16_t forward[4]={250,250,250,250};
static const int16_t reverse[4]={-250,-250,-250,-250};
static const int16_t zeros[4]={0,0,0,0};

static void expect_all(const motor_control_t *s, int16_t value)
{
    unsigned i;
    for(i=0;i<4;i++) assert(s->output_permille[i]==value);
    assert(s->active==(value!=0));
}
static void expect_stopped(const motor_control_t *s) { expect_all(s,0); }

static void test_lease_and_no_renewal(void)
{
    motor_control_t s;
    motor_control_init(&s,0);
    expect_stopped(&s); assert(!s.fault);
    assert(motor_control_update(&s,0,forward,1,0,200)); expect_stopped(&s);
    assert(motor_control_update(&s,50,forward,1,0,200)); expect_all(&s,50);
    assert(motor_control_update(&s,100,forward,1,0,200)); expect_all(&s,100);
    assert(motor_control_update(&s,150,forward,1,0,200)); expect_all(&s,150);
    assert(motor_control_update(&s,199,forward,1,0,200)); expect_all(&s,199);
    assert(!motor_control_update(&s,200,forward,1,0,200));
    expect_stopped(&s); assert(!s.fault);
    assert(!motor_control_update(&s,201,forward,1,0,200)); expect_stopped(&s);
    assert(motor_control_update(&s,202,forward,1,202,1)); expect_all(&s,1);
    assert(!motor_control_update(&s,203,forward,1,202,1)); expect_stopped(&s);
    puts("PASS original command lease, exact expiry, TTL=1, no cached renewal");
}

static void test_ramp_zero_and_per_wheel(void)
{
    motor_control_t s; unsigned t;
    const int16_t mixed[4]={250,-250,100,-40};
    const int16_t partial_stop[4]={0,-250,0,-40};
    motor_control_init(&s,0);
    for(t=50;t<=300;t+=50)
        assert(motor_control_update(&s,t,mixed,1,t,200));
    assert(s.output_permille[0]==250 && s.output_permille[1]==-250);
    assert(s.output_permille[2]==100 && s.output_permille[3]==-40 && s.active);
    assert(motor_control_update(&s,300,partial_stop,1,300,200));
    assert(s.output_permille[0]==0 && s.output_permille[1]==-250);
    assert(s.output_permille[2]==0 && s.output_permille[3]==-40 && s.active);
    assert(motor_control_update(&s,301,zeros,1,301,200)); expect_stopped(&s);
    assert(!s.fault);
    assert(motor_control_update(&s,302,mixed,1,302,200));
    assert(s.output_permille[0]==1 && s.output_permille[1]==-1);
    assert(s.output_permille[2]==1 && s.output_permille[3]==-1);
    puts("PASS independent bounded ramps, exact targets, per-wheel zero coast, zero active=0");
}

static void test_reversal_and_hold(void)
{
    motor_control_t s; unsigned t;
    motor_control_init(&s,0);
    assert(motor_control_update(&s,50,forward,1,50,200));
    assert(motor_control_update(&s,100,forward,1,100,200)); expect_all(&s,100);
    for(t=110;t<=200;t+=10) {
        assert(motor_control_update(&s,t,reverse,1,110,200));
        expect_all(&s,(int16_t)(200-t));
    }
    for(t=210;t<=250;t+=10) {
        assert(motor_control_update(&s,t,reverse,1,110,200)); expect_stopped(&s);
    }
    assert(motor_control_update(&s,251,reverse,1,110,200)); expect_all(&s,-1);
    assert(motor_control_update(&s,276,reverse,1,110,200)); expect_all(&s,-26);
    assert(motor_control_update(&s,300,reverse,1,110,200)); expect_all(&s,-50);
    /* A new opposite request starts by approaching zero, never swapping sign. */
    assert(motor_control_update(&s,310,forward,1,310,200)); expect_all(&s,-40);
    /* Returning to the existing direction while decelerating resumes its ramp. */
    assert(motor_control_update(&s,320,reverse,1,320,200)); expect_all(&s,-50);
    puts("PASS decelerate before reversing, full 50ms coast, no hold-time ramp credit");
}

static void test_stop_cannot_bypass_reverse_hold(void)
{
    motor_control_t s;
    motor_control_init(&s,0);
    assert(motor_control_update(&s,50,forward,1,50,200)); expect_all(&s,50);
    assert(!motor_control_update(&s,51,NULL,0,0,0)); expect_stopped(&s);
    assert(!motor_control_update(&s,70,NULL,0,0,0)); expect_stopped(&s);
    assert(motor_control_update(&s,90,reverse,1,90,200)); expect_stopped(&s);
    assert(motor_control_update(&s,101,reverse,1,90,200)); expect_stopped(&s);
    assert(motor_control_update(&s,102,reverse,1,90,200)); expect_all(&s,-1);
    /* Explicit zero commands also retain the actual last direction. */
    assert(motor_control_update(&s,103,zeros,1,103,200)); expect_stopped(&s);
    assert(motor_control_update(&s,150,forward,1,150,200)); expect_stopped(&s);
    assert(motor_control_update(&s,154,forward,1,150,200)); expect_all(&s,1);
    puts("PASS disable and zero-stop preserve reversal dead time, repeated stop does not extend it");
}

static void test_invalid_commands_stop_atomically(void)
{
    motor_control_t s; int16_t bad[4]; unsigned i;
    const int16_t invalid[]={251,-251,INT16_MIN,INT16_MAX};
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        motor_control_init(&s,0);
        assert(motor_control_update(&s,50,forward,1,50,200));
        memcpy(bad,forward,sizeof(bad)); bad[3]=invalid[i];
        assert(!motor_control_update(&s,51,bad,1,51,200));
        expect_stopped(&s); assert(!s.fault);
    }
    motor_control_init(&s,0);
    assert(motor_control_update(&s,50,reverse,1,50,200));
    assert(!motor_control_update(&s,51,forward,1,51,0)); expect_stopped(&s);
    assert(!motor_control_update(&s,52,forward,1,52,201)); expect_stopped(&s);
    assert(!motor_control_update(&s,53,forward,2,53,200)); expect_stopped(&s);
    assert(!motor_control_update(&s,54,NULL,1,54,200)); expect_stopped(&s);
    assert(!motor_control_update(&s,55,forward,1,56,200)); expect_stopped(&s);
    assert(!s.fault);
    puts("PASS out-of-range rejection, INT16 extremes, bad enable/TTL/null/future command, atomic stop");
}

static void test_scheduler_fault_latch(void)
{
    motor_control_t s;
    motor_control_init(&s,0);
    assert(motor_control_update(&s,50,forward,1,50,200)); expect_all(&s,50);
    assert(!s.fault);
    assert(!motor_control_update(&s,101,forward,1,101,200));
    expect_stopped(&s); assert(s.fault);
    assert(!motor_control_update(&s,102,forward,1,102,200));
    expect_stopped(&s); assert(s.fault);
    /* Neither valid zero nor invalid commands acknowledge a watchdog fault. */
    assert(!motor_control_update(&s,103,zeros,1,103,200)); assert(s.fault);
    assert(!motor_control_update(&s,104,forward,2,104,200)); assert(s.fault);
    assert(!motor_control_update(&s,105,NULL,0,0,0)); assert(!s.fault);
    assert(motor_control_update(&s,106,forward,1,106,200)); expect_all(&s,1);
    assert(!motor_control_update(&s,1000,NULL,0,0,0));
    expect_stopped(&s); assert(!s.fault);
    assert(motor_control_update(&s,1001,forward,1,1001,200)); expect_all(&s,1);
    puts("PASS 50/51ms update boundary, latched stall fault, explicit disable acknowledgement");
}

static void test_clock_wrap(void)
{
    motor_control_t s; uint32_t start=0xfffffff0UL;
    motor_control_init(&s,start);
    assert(motor_control_update(&s,start+10U,forward,1,start,200)); expect_all(&s,10);
    assert(motor_control_update(&s,start+30U,forward,1,start,200)); expect_all(&s,30);
    assert(!motor_control_update(&s,start+31U,NULL,0,0,0)); expect_stopped(&s);
    assert(motor_control_update(&s,start+70U,reverse,1,start+31U,200)); expect_stopped(&s);
    assert(motor_control_update(&s,start+81U,reverse,1,start+31U,200)); expect_stopped(&s);
    assert(motor_control_update(&s,start+82U,reverse,1,start+31U,200)); expect_all(&s,-1);
    assert(motor_control_update(&s,start+120U,reverse,1,start+31U,200));
    assert(motor_control_update(&s,start+170U,reverse,1,start+31U,200));
    assert(motor_control_update(&s,start+220U,reverse,1,start+31U,200));
    assert(motor_control_update(&s,start+230U,reverse,1,start+31U,200));
    assert(!motor_control_update(&s,start+231U,reverse,1,start+31U,200));
    expect_stopped(&s); assert(!s.fault);
    /* The reverse hold itself must also straddle wrap. */
    motor_control_init(&s,start-40U);
    assert(motor_control_update(&s,start,forward,1,start,200));
    assert(!motor_control_update(&s,start+1U,NULL,0,0,0));
    assert(motor_control_update(&s,start+50U,reverse,1,start+1U,200)); expect_stopped(&s);
    assert(motor_control_update(&s,start+52U,reverse,1,start+1U,200)); expect_all(&s,-1);
    puts("PASS millis wrap across ramps, original command expiry and reversal hold");
}

static uint32_t random_u32(uint32_t *seed)
{ *seed=*seed*1664525UL+1013904223UL; return *seed; }
static int sign(int value) { return value>0 ? 1 : (value<0 ? -1 : 0); }
static void test_deterministic_invariants(void)
{
    motor_control_t s; uint32_t seed=42, now=0xfffff000UL; unsigned iteration,i;
    motor_control_init(&s,now);
    for(iteration=0;iteration<50000;iteration++) {
        int16_t target[4], before[4]; uint32_t dt=random_u32(&seed)%55U;
        uint32_t age=random_u32(&seed)%220U; uint16_t ttl=(uint16_t)(random_u32(&seed)%220U);
        uint8_t enabled=(iteration%13U==0) ? 0 : 1; int accepted,any=0;
        memcpy(before,s.output_permille,sizeof(before)); now+=dt;
        for(i=0;i<4;i++) target[i]=(int16_t)((int32_t)(random_u32(&seed)%521U)-260);
        accepted=motor_control_update(&s,now,target,enabled,now-age,ttl);
        for(i=0;i<4;i++) {
            int value=s.output_permille[i];
            assert(value>=-MOTOR_MAX_PERMILLE && value<=MOTOR_MAX_PERMILLE);
            if(value) any=1;
            if(before[i] && value) assert(sign(before[i])==sign(value));
            if(accepted && target[i] && value) {
                int delta=value-before[i];
                if(delta<0) delta=-delta;
                assert((uint32_t)delta<=dt);
            }
        }
        assert(s.active==any);
        if(!accepted || s.fault) expect_stopped(&s);
        if(s.fault) assert(!accepted);
    }
    puts("PASS 50000 deterministic transitions: bounded outputs, no direct reversal, correct active/fault");
}

int main(void)
{
    test_lease_and_no_renewal();
    test_ramp_zero_and_per_wheel();
    test_reversal_and_hold();
    test_stop_cannot_bypass_reverse_hold();
    test_invalid_commands_stop_atomically();
    test_scheduler_fault_latch();
    test_clock_wrap();
    test_deterministic_invariants();
    puts("All open-loop motor-control tests passed (no hardware accessed).");
    return 0;
}
