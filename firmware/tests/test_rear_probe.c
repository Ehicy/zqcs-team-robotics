#include "rear_probe.h"
#include "fallback.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    rear_probe_t s = {0};
    fb_motor_t m;
    const int16_t rear[4] = {0,0,REAR_PROBE_PWM,REAR_PROBE_PWM};
    uint32_t k = REAR_PROBE_KEY | 1;
    assert(rear_probe_poll(&s,0,1,0,0)==-1);
    fb_motor_init(&m,0);
    for (unsigned t=0;t<1000;t++) {
        int run=rear_probe_poll(&s,k,1,0,t);
        if(run) fb_motor_tick(&m,rear,1,100,t); else fb_motor_off(&m,t);
        assert(run==(t<500));
        assert(m.output[0]==0 && m.output[1]==0);
        assert(m.output[2]>=REAR_PROBE_PWM && m.output[2]<=0);
        assert(m.output[3]>=REAR_PROBE_PWM && m.output[3]<=0);
        if(t==499)assert(m.output[2]==REAR_PROBE_PWM && m.output[3]==REAR_PROBE_PWM);
        if(t>=500)assert(m.output[2]==0 && m.output[3]==0);
    }
    assert(rear_probe_poll(&s,k+1,1,0,1100)==0);
    assert(rear_probe_poll(&s,0,1,0,1101)==-1);
    assert(rear_probe_poll(&s,k,1,0,1102)==0); /* stale token rejected */
    assert(rear_probe_poll(&s,0,1,0,1103)==-1);
    assert(rear_probe_poll(&s,k+2,1,0,1104)==1);
    assert(rear_probe_poll(&s,k+3,1,0,1604)==0); /* no renewal */
    assert(rear_probe_poll(&s,0,1,0,1700)==-1);
    assert(rear_probe_poll(&s,k+4,1,0,1701)==1);
    assert(rear_probe_poll(&s,k+4,0,0,1702)==0);
    assert(rear_probe_poll(&s,k+4,1,0,1703)==0);
    assert(rear_probe_poll(&s,0,1,0,1800)==-1);
    assert(rear_probe_poll(&s,k+5,1,0,1801)==1);
    assert(rear_probe_poll(&s,k+5,1,1,1802)==0);
    assert(rear_probe_poll(&s,k+5,1,0,1803)==0);
    s=(rear_probe_t){0};
    assert(rear_probe_poll(&s,k,1,0,0xffffff00U)==1);
    assert(rear_probe_poll(&s,k,1,0,0x000000f3U)==1);
    assert(rear_probe_poll(&s,k,1,0,0x000000f4U)==0);
    puts("Rear probe: boot off, rear-only, reverse 100% output, 500ms cap, no replay/renewal, fault/STOP and wrap tests passed");
}
