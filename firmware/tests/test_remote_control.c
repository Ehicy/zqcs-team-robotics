#include "remote_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v)
{ unsigned i; for (i=0; i<4; ++i) p[i]=(uint8_t)(v>>(i*8)); }
static void packet(uint8_t *p, uint8_t op, uint32_t session, uint32_t sequence)
{
    memset(p, 0, 20); p[0]=1; p[1]=op;
    put32(p+4,session); put32(p+8,sequence);
}
static void drive(uint8_t *p, const remote_control_t *s, uint32_t sequence,
                  int throttle, int steer, unsigned limit, unsigned ttl)
{
    packet(p,REMOTE_OPCODE_DRIVE,s->session,sequence);
    put16(p+12,(uint16_t)throttle); put16(p+14,(uint16_t)steer);
    put16(p+16,(uint16_t)limit); put16(p+18,(uint16_t)ttl);
}
static void ready(remote_control_t *s)
{
    remote_control_init(s,0x12345678UL);
    assert(remote_control_tick(s,0,1,1));
    assert(s->link_up && s->drive_ready && !s->armed);
}
static void arm(remote_control_t *s, uint32_t sequence, uint32_t now)
{
    uint8_t p[20]; remote_action_t a;
    packet(p,REMOTE_OPCODE_ARM,s->session,sequence);
    assert(remote_control_receive(s,p,20,now,now,&a));
    assert(a.kind==REMOTE_ACTION_ARM && s->armed && !s->active);
}

static void test_arm_and_gate(void)
{
    remote_control_t s; remote_action_t a; uint8_t p[20]; uint32_t session;
    remote_control_init(&s,0);
    assert(s.session!=0);
    packet(p,REMOTE_OPCODE_ARM,s.session,1);
    assert(!remote_control_receive(&s,p,20,0,0,&a));
    assert(a.kind==REMOTE_ACTION_NONE && !s.armed);
    assert(remote_control_tick(&s,0,1,0));
    packet(p,REMOTE_OPCODE_ARM,s.session,1);
    assert(!remote_control_receive(&s,p,20,0,0,&a));
    assert(!s.armed && s.reason==REMOTE_REASON_NOT_READY);
    assert(!remote_control_tick(&s,0,1,1));
    drive(p,&s,1,0,0,100,100);
    assert(!remote_control_receive(&s,p,20,0,0,&a));
    session=s.session;
    packet(p,REMOTE_OPCODE_ARM,s.session,2); p[12]=1;
    assert(!remote_control_receive(&s,p,20,0,0,&a));
    assert(a.kind==REMOTE_ACTION_STOP && s.session!=session && !s.armed);
    arm(&s,1,0);
    session=s.session;
    drive(p,&s,2,100,0,100,100);
    assert(remote_control_receive(&s,p,20,0,0,&a));
    packet(p,REMOTE_OPCODE_ARM,s.session,3);
    assert(!remote_control_receive(&s,p,20,1,1,&a));
    assert(a.kind==REMOTE_ACTION_STOP && s.session!=session);
}

static void test_lease_and_delay(void)
{
    remote_control_t s; remote_action_t a; uint8_t p[20]; uint32_t session;
    ready(&s); arm(&s,1,100);
    drive(p,&s,2,1000,0,300,150);
    assert(remote_control_receive(&s,p,20,100,140,&a));
    assert(a.ttl_ms==110 && s.received_ms==100 && s.expires_ms==250);
    assert(a.targets[0]==300 && a.targets[1]==300 && a.targets[2]==300 && a.targets[3]==300);
    assert(s.active && s.armed);
    assert(!remote_control_tick(&s,249,1,1));
    session=s.session;
    assert(remote_control_tick(&s,250,1,1));
    assert(s.session!=session && !s.armed && !s.active && s.reason==REMOTE_REASON_EXPIRED);
    session=s.session;
    assert(!remote_control_tick(&s,1000,1,1) && s.session==session);
    arm(&s,1,1000);
    drive(p,&s,2,0,0,300,50);
    assert(!remote_control_receive(&s,p,20,1000,1050,&a));
    assert(a.kind==REMOTE_ACTION_STOP && s.reason==REMOTE_REASON_EXPIRED);
    arm(&s,1,1100);
    drive(p,&s,2,0,0,0,200);
    assert(remote_control_receive(&s,p,20,1100,1101,&a));
    assert(s.armed && s.active && s.left_mm_s==0 && s.right_mm_s==0);
    assert(a.kind==REMOTE_ACTION_DRIVE && a.ttl_ms==199);
}

static void test_replay_stop_and_foreign(void)
{
    remote_control_t s; remote_action_t a; uint8_t p[20], old[20]; uint32_t session;
    ready(&s); arm(&s,1,0);
    drive(p,&s,2,500,-500,300,200);
    assert(remote_control_receive(&s,p,20,0,0,&a));
    assert(s.left_mm_s==0 && s.right_mm_s==300);
    /* Left arc: physical right wheels M1/M3 move, left M2/M4 stay still. */
    assert(a.targets[0]==300 && a.targets[2]==300);
    assert(a.targets[1]==0 && a.targets[3]==0);
    assert(!remote_control_receive(&s,p,20,150,150,&a));
    assert(a.kind==REMOTE_ACTION_NONE && s.received_ms==0 && s.expires_ms==200);
    memcpy(old,p,20); put32(p+4,s.session+1); p[0]=0; p[2]=9;
    assert(!remote_control_receive(&s,p,20,150,150,&a));
    assert(s.armed && s.active && a.kind==REMOTE_ACTION_NONE);
    packet(p,REMOTE_OPCODE_LED,s.session,3); p[12]=1;
    assert(remote_control_receive(&s,p,20,160,160,&a));
    assert(a.kind==REMOTE_ACTION_LED && a.led_on && s.expires_ms==200);
    session=s.session;
    packet(p,REMOTE_OPCODE_STOP,s.session,0); /* Duplicate/old sequence still stops. */
    assert(remote_control_receive(&s,p,20,0,180,&a));
    assert(a.kind==REMOTE_ACTION_STOP && s.session!=session && !s.armed);
    assert(!remote_control_receive(&s,old,20,181,181,&a));
    assert(a.kind==REMOTE_ACTION_NONE && !s.active);
}

static void test_link_and_readiness(void)
{
    remote_control_t s; uint32_t session;
    ready(&s); session=s.session;
    assert(!remote_control_tick(&s,1,1,0));
    assert(s.session==session && !s.drive_ready); /* Idle config ACK remains current. */
    assert(!remote_control_tick(&s,2,1,1));
    arm(&s,1,2);
    assert(remote_control_tick(&s,3,1,0));
    assert(s.session!=session && !s.armed && s.reason==REMOTE_REASON_NOT_READY);
    session=s.session;
    assert(!remote_control_tick(&s,4,1,0) && s.session==session);
    assert(remote_control_tick(&s,5,0,0));
    session=s.session;
    assert(!remote_control_tick(&s,6,0,0) && s.session==session);
    assert(remote_control_tick(&s,7,1,1));
    assert(s.session!=session && !s.armed && !s.active);
    session=s.session;
    remote_control_reset(&s,REMOTE_REASON_QUEUE);
    assert(s.session!=session && !s.armed && !s.active && s.reason==REMOTE_REASON_QUEUE);
}

static void test_all_configuration(void)
{
    remote_control_t s; remote_action_t a; uint8_t p[20];
    ready(&s);
    assert(!remote_control_tick(&s,0,1,0)); /* Can configure before calibration is ready. */
    packet(p,REMOTE_OPCODE_MECHANICAL,s.session,1);
    p[12]=3; p[13]=7; put16(p+14,650); put32(p+16,1560);
    assert(remote_control_receive(&s,p,20,0,0,&a));
    assert(a.kind==REMOTE_ACTION_MECHANICAL && a.wheel==3 && a.flags==7);
    assert(a.diameter_tenth_mm==650 && a.counts_per_rev==1560 && a.source_sequence==1);
    packet(p,REMOTE_OPCODE_TUNING,s.session,2);
    p[12]=1; put16(p+14,1000); put16(p+16,250); put16(p+18,300);
    assert(remote_control_receive(&s,p,20,0,0,&a));
    assert(a.kind==REMOTE_ACTION_TUNING && a.wheel==1 && a.kp_milli==1000);
    assert(a.ki_milli==250 && a.max_mm_s==300 && a.source_sequence==2);
    packet(p,REMOTE_OPCODE_CLEAR_FAULT,s.session,3);
    assert(remote_control_receive(&s,p,20,0,0,&a) && a.kind==REMOTE_ACTION_CLEAR_FAULT);
    packet(p,REMOTE_OPCODE_LED,s.session,4);
    assert(remote_control_receive(&s,p,20,0,0,&a) && a.kind==REMOTE_ACTION_LED && !a.led_on);
    assert(!remote_control_tick(&s,1,1,1));
    arm(&s,5,1);
    packet(p,REMOTE_OPCODE_CLEAR_FAULT,s.session,6);
    assert(!remote_control_receive(&s,p,20,1,1,&a));
    assert(a.kind==REMOTE_ACTION_STOP && !s.armed && s.reason==REMOTE_REASON_LOCKED);
}

static void test_bad_configuration_and_payloads(void)
{
    unsigned test;
    for (test=0; test<19; ++test) {
        remote_control_t s; remote_action_t a; uint8_t p[20]; uint32_t session;
        ready(&s); session=s.session;
        packet(p,REMOTE_OPCODE_TUNING,s.session,1);
        put16(p+14,1); put16(p+18,1);
        switch(test) {
        case 0: p[12]=4; break;
        case 1: p[13]=1; break;
        case 2: put16(p+14,20001); break;
        case 3: put16(p+16,20001); break;
        case 4: put16(p+14,0); break;
        case 5: put16(p+18,0); break;
        case 6: put16(p+18,301); break;
        case 7: p[1]=REMOTE_OPCODE_MECHANICAL; p[13]=8; break;
        case 8: p[1]=REMOTE_OPCODE_MECHANICAL; put16(p+14,0); break;
        case 9: p[1]=REMOTE_OPCODE_MECHANICAL; put16(p+14,5001); break;
        case 10: p[1]=REMOTE_OPCODE_MECHANICAL; put32(p+16,0); break;
        case 11: p[1]=REMOTE_OPCODE_MECHANICAL; put32(p+16,1000001); break;
        case 12: p[1]=99; break;
        case 13: p[0]=2; break;
        case 14: p[2]=1; break;
        case 15: p[3]=1; break;
        case 16: p[1]=REMOTE_OPCODE_LED; p[12]=2; break;
        case 17: packet(p,REMOTE_OPCODE_LED,s.session,1); p[19]=1; break;
        case 18: packet(p,REMOTE_OPCODE_CLEAR_FAULT,s.session,1); p[19]=1; break;
        }
        assert(!remote_control_receive(&s,p,20,0,0,&a));
        assert(a.kind==REMOTE_ACTION_STOP && s.session!=session && s.reason==REMOTE_REASON_INVALID);
    }
}

static void test_lengths_and_ranges(void)
{
    unsigned length, test;
    for (length=0; length<260; ++length) {
        remote_control_t s; remote_action_t a; uint8_t p[20]; uint32_t session;
        ready(&s); session=s.session;
        packet(p,REMOTE_OPCODE_ARM,s.session,1);
        if (length==20) continue;
        /* Deliberately only 20 bytes allocated: reject length before reading more. */
        assert(!remote_control_receive(&s,p,length,0,0,&a));
        assert(a.kind==(length>=8 ? REMOTE_ACTION_STOP : REMOTE_ACTION_NONE));
        assert((s.session!=session)==(length>=8));
    }
    for (test=0; test<7; ++test) {
        remote_control_t s; remote_action_t a; uint8_t p[20];
        ready(&s); arm(&s,1,0); drive(p,&s,2,0,0,300,100);
        switch(test) {
        case 0: put16(p+12,1001); break;
        case 1: put16(p+12,(uint16_t)-1001); break;
        case 2: put16(p+14,1001); break;
        case 3: put16(p+14,(uint16_t)-1001); break;
        case 4: put16(p+16,301); break;
        case 5: put16(p+18,49); break;
        case 6: put16(p+18,201); break;
        }
        assert(!remote_control_receive(&s,p,20,0,0,&a));
        assert(a.kind==REMOTE_ACTION_STOP && !s.armed);
    }
}

static void test_wrap_and_expiry_before_receive(void)
{
    remote_control_t s; remote_action_t a; uint8_t p[20]; uint32_t session;
    ready(&s); arm(&s,UINT32_MAX-1,UINT32_MAX-100);
    drive(p,&s,UINT32_MAX,100,100,300,150);
    assert(remote_control_receive(&s,p,20,UINT32_MAX-100,UINT32_MAX-90,&a));
    drive(p,&s,0,-1000,1000,300,100);
    assert(remote_control_receive(&s,p,20,UINT32_MAX-20,0,&a));
    assert(a.ttl_ms==79 && a.targets[0]==-300 && a.targets[1]==0);
    assert(!remote_control_tick(&s,78,1,1));
    session=s.session;
    packet(p,REMOTE_OPCODE_ARM,s.session,1);
    assert(!remote_control_receive(&s,p,20,79,79,&a));
    assert(a.kind==REMOTE_ACTION_STOP && s.session!=session && !s.armed);
    arm(&s,1,1000);
    packet(p,REMOTE_OPCODE_LED,s.session,2);
    assert(!remote_control_receive(&s,p,20,1000,1200,&a));
    assert(a.kind==REMOTE_ACTION_STOP && !s.armed);
}

static void test_mix_envelope(void)
{
    int throttle, steer;
    remote_control_t s; remote_action_t a; uint8_t p[20]; uint32_t seq=2;
    ready(&s); arm(&s,1,0);
    for (throttle=-1000; throttle<=1000; throttle+=25)
        for (steer=-1000; steer<=1000; steer+=25) {
            drive(p,&s,seq++,throttle,steer,300,100);
            assert(remote_control_receive(&s,p,20,0,0,&a));
            assert(a.targets[0]>=-300 && a.targets[0]<=300);
            assert(a.targets[1]>=-300 && a.targets[1]<=300);
            assert(a.targets[0]==a.targets[2] && a.targets[1]==a.targets[3]);
            if (steer==0) assert(a.targets[0]==a.targets[1]);
            if (throttle==0) assert(a.targets[0]==-a.targets[1]);
        }
}

int main(void)
{
    test_arm_and_gate();
    test_lease_and_delay();
    test_replay_stop_and_foreign();
    test_link_and_readiness();
    test_all_configuration();
    test_bad_configuration_and_payloads();
    test_lengths_and_ranges();
    test_wrap_and_expiry_before_receive();
    test_mix_envelope();
    puts("remote control: 9 groups passed (6,561 normalized mixes; all opcodes, lease/fault gates)");
    return 0;
}
