#include "encoder_edge_filter.h"
#include "speed_control.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static void pulse(encoder_edge_filter_t *s, uint32_t t, uint8_t a, uint8_t b)
{
    encoder_edge_filter_irq(s,t,a,b);
    encoder_edge_filter_poll(s,t+20U,a,b);
}
static void clean_cycles(void)
{
    encoder_edge_filter_t s; uint32_t i,t=0;
    encoder_edge_filter_init(&s,0,20);
    for(i=0;i<13U*34U;i++) {
        pulse(&s,t+=100,1,0); pulse(&s,t+=100,0,1);
    }
    assert(s.count==884 && s.accepted==884); /* Preserve x2, not x4. */
    for(i=0;i<13U*34U;i++) {
        pulse(&s,t+=100,1,1); pulse(&s,t+=100,0,0);
    }
    assert(s.count==0 && s.accepted==1768);
    assert(!s.short_pulses && !s.duplicate_irqs);
    puts("PASS forward/reverse full wheel CPR and unchanged x2 direction");
}
static void duplicate_trace(void)
{
    encoder_edge_filter_t s; uint32_t i,old_count=0;
    speed_control_t old_speed,new_speed; int32_t old_counts[4]={0},new_counts[4]={0};
    encoder_edge_filter_init(&s,0,20);
    /* Reproduce the old ISR's defect: 96 deliveries sampling the same A/B
     * levels become 96 counts, despite observing only one A transition. This
     * is a synthetic race/noise trace, not a captured hardware waveform. */
    for(i=0;i<96;i++) {
        old_count++;
        encoder_edge_filter_irq(&s,100+i,1,0);
    }
    assert(s.count==0 && s.duplicate_irqs==95);
    encoder_edge_filter_poll(&s,215,1,1); /* B may advance before owner polls. */
    assert(s.count==1 && s.accepted==1);
    speed_control_init(&old_speed,0,old_counts);
    speed_control_init(&new_speed,0,new_counts);
    old_counts[3]=(int32_t)old_count; new_counts[3]=(int32_t)s.count;
    speed_control_update(&old_speed,10,old_counts,15,0,0,0,0);
    speed_control_update(&new_speed,10,new_counts,15,0,0,0,0);
    assert(old_speed.fault==SPEED_FAULT_ENCODER);
    assert(old_speed.fault_encoder_mask==8 && old_speed.fault_raw_delta[3]==96);
    assert(old_speed.fault_sample_dt==10 && old_speed.fault_sample_ms==10);
    assert(new_speed.fault==SPEED_FAULT_NONE);
    speed_control_stop(&old_speed,11);
    speed_control_update(&old_speed,20,old_counts,15,0,0,0,0);
    assert(old_speed.fault_encoder_mask==8 && old_speed.fault_raw_delta[3]==96);
    assert(speed_control_clear_fault(&old_speed));
    assert(!old_speed.fault_encoder_mask && !old_speed.fault_sample_dt);
    puts("PASS old duplicate-count defect reproduces fault=2; new decoder counts once, same speed threshold");
}
static void short_glitches_and_last_edge(void)
{
    encoder_edge_filter_t s; uint32_t i,t=0;
    encoder_edge_filter_init(&s,0,20);
    for(i=0;i<1000;i++) {
        encoder_edge_filter_irq(&s,t+=100,1,0);
        encoder_edge_filter_irq(&s,t+5,0,0);
        encoder_edge_filter_poll(&s,t+50,0,0);
    }
    assert(!s.count && !s.accepted && s.short_pulses==1000);
    encoder_edge_filter_irq(&s,t+100,1,0);
    encoder_edge_filter_poll(&s,t+119,1,0); assert(!s.count);
    encoder_edge_filter_poll(&s,t+120,1,1); assert(s.count==1);
    encoder_edge_filter_poll(&s,t+5000,1,1); assert(s.count==1);
    puts("PASS short pulses rejected; final edge committed once with original B direction");
}
static void between_polls_wrap_and_noise(void)
{
    encoder_edge_filter_t s; uint32_t t=UINT32_MAX-10U;
    encoder_edge_filter_init(&s,0,20);
    encoder_edge_filter_irq(&s,t,1,0);
    encoder_edge_filter_irq(&s,t+100,0,1); assert(s.count==1);
    encoder_edge_filter_irq(&s,t+200,1,0); assert(s.count==2);
    encoder_edge_filter_poll(&s,t+220,1,1); assert(s.count==3);
    s.count=UINT32_MAX;
    pulse(&s,t+300,0,1); assert(s.count==0);
    pulse(&s,t+400,1,1); assert(s.count==UINT32_MAX);
    /* An unobserved return edge cannot prove the pending pulse's duration. */
    encoder_edge_filter_init(&s,0,20);
    encoder_edge_filter_irq(&s,0,1,0);
    encoder_edge_filter_poll(&s,100,0,0);
    assert(!s.count && !s.pending && s.short_pulses==1);
    puts("PASS owner-loop latency, timestamp/count wrap, reversal and raced poll");
}
static void real_fast_motion_still_faults(void)
{
    encoder_edge_filter_t s; speed_control_t speed;
    int32_t counts[4]={0}; uint32_t i,t=0;
    encoder_edge_filter_init(&s,0,20); speed_control_init(&speed,0,counts);
    /* Real alternating A pulses lasting 100us exceed the existing velocity
     * envelope. They must not be suppressed just to avoid a fault. */
    for(i=0;i<48;i++) {
        pulse(&s,t+=100,1,0); pulse(&s,t+=100,0,1);
    }
    counts[3]=(int32_t)s.count;
    speed_control_update(&speed,10,counts,15,0,0,0,0);
    assert(s.count==96 && speed.fault==SPEED_FAULT_ENCODER);
    assert(speed.fault_encoder_mask==8 && speed.fault_raw_delta[3]==96);
    speed_control_init(&speed,0,counts);
    speed_control_update(&speed,1,counts,7,0,0,0,0);
    assert(speed.fault==SPEED_FAULT_ENCODER && speed.fault_encoder_mask==8);
    puts("PASS real excessive motion and invalid M4 feedback still latch encoder fault");
}
static void interrupt_storm_budget(void)
{
    encoder_irq_budget_t b; unsigned i; uint32_t start=UINT32_MAX-500U;
    encoder_irq_budget_init(&b,start,1000,32);
    for(i=0;i<32;i++) assert(encoder_irq_budget_allow(&b,start+i));
    assert(!encoder_irq_budget_allow(&b,start+32));
    assert(!encoder_irq_budget_allow(&b,start+5000)); /* No automatic resume. */
    encoder_irq_budget_init(&b,start,1000,32);
    for(i=0;i<1000;i++) assert(encoder_irq_budget_allow(&b,start+i*100));
    encoder_irq_budget_init(&b,0,1000,32);
    for(i=0;i<32;i++) assert(encoder_irq_budget_allow(&b,999));
    assert(encoder_irq_budget_allow(&b,1000));
    puts("PASS bounded ISR burst, latched failure, timestamp wrap and normal edge rate");
}
static void clock_domain_regression(void)
{
    encoder_irq_budget_t sleeping, continuous;
    encoder_edge_filter_t asleep_edge, awake_edge;
    unsigned i;
    /* Synthetic clock-gating model, NOT a captured board waveform: clean A
     * edges arrive 1ms apart, but only 10us of core execution separates them.
     * A CPU-time 1ms budget then falsely sees 33 edges in 320us. */
    encoder_irq_budget_init(&sleeping,0,72000,32);
    encoder_irq_budget_init(&continuous,0,72000,32);
    for(i=0;i<32;i++) {
        assert(encoder_irq_budget_allow(&sleeping,i*720));
        assert(encoder_irq_budget_allow(&continuous,i*72000));
    }
    assert(!encoder_irq_budget_allow(&sleeping,32*720));
    assert(encoder_irq_budget_allow(&continuous,32*72000));
    encoder_edge_filter_init(&asleep_edge,0,1440);
    encoder_edge_filter_init(&awake_edge,0,1440);
    encoder_edge_filter_irq(&asleep_edge,0,1,0);
    encoder_edge_filter_irq(&awake_edge,0,1,0);
    encoder_edge_filter_irq(&asleep_edge,720,0,1);
    encoder_edge_filter_irq(&awake_edge,72000,0,1);
    assert(asleep_edge.accepted==0 && asleep_edge.short_pulses==1);
    assert(awake_edge.accepted==1 && awake_edge.short_pulses==0);
    puts("PASS clock-gating model reproduces false storm/short pulse; continuous clock preserves thresholds");
}
int main(void)
{
    clean_cycles(); duplicate_trace(); short_glitches_and_last_edge();
    between_polls_wrap_and_noise(); real_fast_motion_still_faults(); interrupt_storm_budget();
    clock_domain_regression();
    return 0;
}
