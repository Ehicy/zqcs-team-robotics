#include "remote_bridge.h"
#include "actuation_guard.h"
#include "speed_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static link_node_t client,stm;
static remote_bridge_t bridge;
static uint32_t now,seq;
static uint8_t ready=15,fault,servo_fixture;
static void transfer(link_node_t *a,link_node_t *b)
{
    uint8_t p[LINK_WIRE_MAX]; size_t i,len=link_node_transmit(a,now,p,sizeof(p));
    for(i=0;i<len;i++) link_node_receive(b,p[i],now);
}
static void cycle(unsigned count)
{
    unsigned i; const int16_t measured[4]={20,-20,30,-30},targets[4]={100,-100,100,-100};
    for(i=0;i<count;i++,now++) {
        const int16_t zero[4]={0,0,0,0};
        link_node_publish_speed(&stm,servo_fixture?zero:measured,servo_fixture?zero:targets,15,ready,fault,now);
        link_node_set_motor_state(&stm,1,!stm.requested_enable);
        remote_bridge_tick(&bridge,now);
        transfer(&client,&stm);
        /* The real STM owner adds standstill/config validation. This fixture
         * supplies explicit ACK results so transport behavior can be checked. */
        if(stm.config_received) link_node_finish_config(&stm,0,ready,fault);
        transfer(&stm,&client);
        remote_bridge_tick(&bridge,now);
    }
}
static void packet(uint8_t p[20],uint8_t op)
{
    memset(p,0,20); p[0]=1; p[1]=op;
    link_put_u32(p+4,bridge.control.session); link_put_u32(p+8,++seq);
}
static void initialize(void)
{
    now=seq=0; ready=15; fault=0; servo_fixture=0;
    link_node_init(&client,LINK_CLIENT,123,0); link_node_init(&stm,LINK_CONTROLLER,456,0);
    remote_bridge_init(&bridge,&client,789); cycle(120);
    assert(client.connected && bridge.control.drive_ready && !bridge.control.armed);
}
static void test_config_ack(void)
{
    uint8_t p[20],status[20]; uint32_t session,source;
    initialize(); packet(p,REMOTE_OPCODE_MECHANICAL); p[12]=3; p[13]=7;
    link_put_u16(p+14,650); link_put_u32(p+16,884);
    session=bridge.control.session; source=seq;
    assert(remote_bridge_receive(&bridge,p,20,now,now)); cycle(5);
    assert(!bridge.config_pending && bridge.ack_pending);
    remote_bridge_status(&bridge,0,status);
    assert(status[1]==3 && status[2]==3 && status[3]==0);
    assert(link_get_u32(status+4)==session && link_get_u32(status+8)==source);
    assert(bridge.configs[3][1]==7 && link_get_u32(bridge.configs[3]+4)==884);
    /* A config/LED frame cannot renew an existing motor command lease. */
    packet(p,REMOTE_OPCODE_TUNING); p[12]=3;
    link_put_u16(p+14,1000); link_put_u16(p+16,2000); link_put_u16(p+18,200);
    assert(remote_bridge_receive(&bridge,p,20,now,now)); cycle(5);
    remote_bridge_status(&bridge,0,status); assert(status[3]==0);
    assert(link_get_u16(bridge.configs[3]+8)==1000);
    assert(link_get_u32(bridge.configs[3]+4)==884 && bridge.configs[3][1]==7);
    packet(p,REMOTE_OPCODE_CLEAR_FAULT); assert(remote_bridge_receive(&bridge,p,20,now,now));
    cycle(5); remote_bridge_status(&bridge,0,status); assert(status[2]==255 && !status[3]);
    puts("PASS BLE-to-UART mechanics/gains merge, matching ACK and clear-fault roundtrip");
}
static void test_drive_stop_lease(void)
{
    uint8_t p[20],old[20]; uint32_t session,token;
    initialize(); packet(p,REMOTE_OPCODE_ARM); session=bridge.control.session; token=client.token;
    assert(remote_bridge_receive(&bridge,p,20,now,now)); cycle(10);
    assert(bridge.control.armed && client.token==token && bridge.control.session==session);
    packet(p,REMOTE_OPCODE_DRIVE); link_put_u16(p+12,1000); link_put_u16(p+16,100); link_put_u16(p+18,200);
    memcpy(old,p,20); assert(remote_bridge_receive(&bridge,p,20,now-30,now)); cycle(2);
    assert(stm.requested_mode==LINK_CONTROL && stm.requested_mm_s[0]==100);
    assert(stm.command_ttl_ms<=170 && stm.command_ttl_ms>0);
    cycle(20); assert(!remote_bridge_receive(&bridge,p,20,now,now));
    assert(bridge.control.received_ms+bridge.control.ttl_ms<now+200);
    packet(p,REMOTE_OPCODE_DRIVE); link_put_u16(p+16,100); link_put_u16(p+18,200);
    assert(remote_bridge_receive(&bridge,p,20,now,now)); cycle(2);
    assert(stm.requested_mm_s[0]==0 && bridge.control.armed && client.token==token);
    packet(p,REMOTE_OPCODE_STOP); assert(remote_bridge_receive(&bridge,p,20,now,now)); cycle(350);
    assert(!bridge.control.armed && !stm.requested_enable && client.token!=token);
    assert(!remote_bridge_receive(&bridge,old,20,now,now)); assert(!bridge.control.armed);
    packet(p,REMOTE_OPCODE_ARM); assert(remote_bridge_receive(&bridge,p,20,now,now));
    packet(p,REMOTE_OPCODE_DRIVE); link_put_u16(p+12,1000); link_put_u16(p+16,100); link_put_u16(p+18,50);
    assert(remote_bridge_receive(&bridge,p,20,now-20,now)); cycle(35);
    assert(!bridge.control.armed && !stm.requested_enable);
    cycle(350); link_put_u16(bridge.configs[2]+12,80);
    packet(p,REMOTE_OPCODE_ARM); assert(remote_bridge_receive(&bridge,p,20,now,now));
    packet(p,REMOTE_OPCODE_DRIVE); link_put_u16(p+12,1000); link_put_u16(p+14,500);
    link_put_u16(p+16,300); link_put_u16(p+18,200);
    assert(remote_bridge_receive(&bridge,p,20,now,now)); cycle(2);
    assert(stm.requested_mm_s[0]==26 && stm.requested_mm_s[2]==26);
    assert(stm.requested_mm_s[1]==80 && stm.requested_mm_s[3]==80);
    assert(bridge.control.left_mm_s==80 && bridge.control.right_mm_s==26);
    puts("PASS ARM without STOP loop, velocity units, original TTL, zero release, STOP and old-session rejection");
}
static void test_fault_and_telemetry(void)
{
    uint8_t p[20],status[20]; uint32_t session;
    initialize(); remote_bridge_status(&bridge,2,status);
    assert(status[1]==2 && status[2]==15 && link_get_u16(status+4)==20);
    assert(link_get_u16(status+6)==(uint16_t)-20);
    remote_bridge_status(&bridge,0,status);
    assert(link_get_u16(status+12)==(uint16_t)-25); /* Left: M2/M4. */
    assert(link_get_u16(status+14)==25); /* Right: M1/M3. */
    packet(p,REMOTE_OPCODE_ARM); assert(remote_bridge_receive(&bridge,p,20,now,now));
    fault=3; cycle(110); assert(!bridge.control.armed && !bridge.control.drive_ready);
    packet(p,REMOTE_OPCODE_ARM); assert(!remote_bridge_receive(&bridge,p,20,now,now));
    cycle(120); fault=0; cycle(110);
    /* Idle readiness changes must not invalidate a configuration transaction. */
    session=bridge.control.session; ready=7; cycle(110);
    assert(bridge.control.session==session && !bridge.control.drive_ready);
    remote_bridge_stop(&bridge,REMOTE_REASON_LINK,now); cycle(110);
    assert(!bridge.control.armed && !stm.requested_enable);
    puts("PASS wheel telemetry, fault/readiness interlock and disconnect lockout");
}
static void test_watchdog_fault_diagnostics(void)
{
    uint8_t p[20],status[20]; uint32_t token;
    initialize();
    fault=SPEED_FAULT_WATCHDOG; cycle(120); token=client.token;
    assert(client.connected && stm.connected && !bridge.control.drive_ready);
    assert(client.remote_speed_fault==SPEED_FAULT_WATCHDOG);
    remote_bridge_status(&bridge,0,status);
    assert((status[2]&1) && (status[2]&16) && status[19]==SPEED_FAULT_WATCHDOG);
    packet(p,REMOTE_OPCODE_ARM);
    assert(!remote_bridge_receive(&bridge,p,20,now,now));
    assert(!bridge.control.armed && !stm.requested_enable);
    /* A persistent motor fault must allow actual framed LED transactions,
     * telemetry and multiple heartbeats without changing the UART session. */
    assert(link_node_request_led(&client,1,now)); cycle(110);
    assert(stm.led_on && !client.led_waiting_ack);
    assert(link_node_request_led(&client,0,now)); cycle(110);
    assert(!stm.led_on && !client.led_waiting_ack);
    cycle(500);
    assert(client.connected && client.token==token);
    assert(client.remote_speed_fault==SPEED_FAULT_WATCHDOG);
    assert(!stm.requested_enable && !bridge.control.armed);
    fault=0; cycle(110);
    assert(bridge.control.drive_ready && !bridge.control.armed);
    puts("PASS watchdog fault telemetry keeps UART/LED alive, rejects ARM and never auto-resumes");
}
static unsigned servo_writes;
static int servo_hardware_ok=1;
static int fake_servo_write(uint8_t channel,uint16_t pulse)
{ assert(channel<LINK_SERVO_COUNT); assert(!pulse || (pulse>=LINK_SERVO_MIN_US(channel) && pulse<=LINK_SERVO_MAX_US(channel))); servo_writes++; return servo_hardware_ok; }
static void servo_standstill(void)
{
    unsigned i; client.remote_flags|=LINK_FLAG_OUTPUT_INHIBITED;
    client.remote_speed_valid_mask=15; client.remote_speed_rx_ms=now;
    client.input_enable=0;
    for(i=0;i<4;i++) client.remote_speed_measured[i]=client.remote_speed_targets[i]=0;
}
static unsigned servo_target(uint8_t channel,uint16_t pulse,uint16_t reference)
{
    uint8_t p[20],ack[20]; uint32_t session=bridge.control.session;
    packet(p,REMOTE_OPCODE_SERVO_TARGET); p[12]=channel; link_put_u16(p+14,pulse); link_put_u16(p+16,reference);
    assert(remote_bridge_receive(&bridge,p,20,now,now));
    remote_bridge_status(&bridge,5,ack);
    assert(ack[1]==3 && ack[2]==254 && link_get_u32(ack+4)==session && link_get_u32(ack+8)==seq);
    return ack[3];
}
static void test_motion_math(void)
{
    servo_motion_t s; unsigned t; uint16_t previous; int32_t v;
    servo_motion_reference(&s,1300,0); servo_motion_target(&s,1700,0);
    for(t=10;t<=5000;t+=10) {
        previous=s.pulse; v=s.velocity_milli;
        if(t%150==0) servo_motion_target(&s,1700,t);
        servo_motion_tick(&s,t);
        assert(s.pulse>=previous && s.pulse-previous<=2);
        assert(s.velocity_milli>=0 && s.velocity_milli<=100000);
        if(s.moving) assert(s.velocity_milli-v<=2000 && v-s.velocity_milli<=2000);
        if(t==100) assert(s.pulse==1301); /* 0.5*a*t^2, not a full-speed jump */
    }
    assert(s.pulse==1700 && !s.moving && !s.velocity_milli);
    servo_motion_reference(&s,1400,0xfffffff0U);
    servo_motion_target(&s,1700,0xfffffff0U);
    servo_motion_tick(&s,4); assert(s.moving); /* wrap-safe dt20 */
    servo_motion_tick(&s,55); assert(!s.moving && s.target==s.pulse); /* gap51 */
    servo_motion_reference(&s,1400,0); servo_motion_target(&s,1700,0);
    for(t=10;t<=500;t+=10) servo_motion_tick(&s,t);
    assert(!s.moving && s.pulse<1450); /* lease expires without a giant catch-up */
    previous=s.pulse; servo_motion_tick(&s,510); assert(s.pulse==previous);
    servo_motion_reference(&s,1400,0); servo_motion_target(&s,1700,0);
    for(t=10;t<=500;t+=10) {servo_motion_target(&s,1700,t);servo_motion_tick(&s,t);}
    servo_motion_target(&s,1300,500); v=s.velocity_milli;
    servo_motion_tick(&s,510); assert(s.velocity_milli==v-2000); /* gentle reversal */
    puts("PASS servo fixed-point speed/acceleration, approach, reversal, lease, clock wrap and scheduler gap");
}
static void test_servos(void)
{
    uint8_t p[20],status[20]; unsigned before; uint16_t held;
    initialize(); servo_fixture=1; servo_writes=0; servo_hardware_ok=1;
    bridge.servo_write=fake_servo_write; servo_standstill();
    assert(!servo_writes && !bridge.servo_us[0]); /* no boot motion */
    assert(servo_target(0,1700,0)==2 && !servo_writes);
    assert(servo_target(0,1700,1300)==0 && bridge.servo_us[0]==1300 && servo_writes==1);
    assert(servo_target(0,1600,1500)==2); /* cannot re-reference live PWM */
    cycle(100); assert(bridge.servo_us[0]>1300 && bridge.servo_us[0]<=1302);
    before=servo_writes;
    packet(p,REMOTE_OPCODE_ARM); assert(!remote_bridge_receive(&bridge,p,20,now,now)); /* moving interlock */
    packet(p,REMOTE_OPCODE_SERVO_TARGET); link_put_u16(p+14,1600);
    assert(remote_bridge_receive(&bridge,p,20,now,now)); remote_bridge_status(&bridge,5,status);
    assert(!remote_bridge_receive(&bridge,p,20,now,now) && servo_writes==before); /* no replay renewal */
    assert(bridge.servos[0].target==1600 && bridge.servo_us[0]!=1600); /* ACK != arrival */
    cycle(510); held=bridge.servo_us[0]; assert(!bridge.servos[0].moving);
    cycle(100); assert(bridge.servo_us[0]==held); /* no auto-resume */
    assert(servo_target(0,1700,0)==0); cycle(100);
    remote_bridge_stop(&bridge,REMOTE_REASON_STOP,now); held=bridge.servo_us[0];
    assert(!bridge.servos[0].moving && held!=1700); cycle(350);
    assert(bridge.servo_us[0]==held); servo_standstill();
    assert(servo_target(0,1700,0)==0); cycle(100);
    client.remote_speed_measured[3]=1; remote_bridge_tick(&bridge,now);
    held=bridge.servo_us[0]; assert(!bridge.servos[0].moving);
    assert(servo_target(0,1700,0)==1); servo_standstill();
    client.remote_speed_rx_ms=now-200; assert(servo_target(0,1700,0)==1); servo_standstill();
    client.remote_speed_fault=1; assert(servo_target(0,1700,0)==1); client.remote_speed_fault=0;
    assert(servo_target(0,1700,0)==0); cycle(20); servo_hardware_ok=0; cycle(100);
    assert(bridge.servo_error_mask&1); assert(!bridge.servos[0].moving);
    held=bridge.servo_us[0]; assert(servo_target(0,1700,0)==4 && bridge.servo_us[0]==held);
    servo_hardware_ok=1; assert(servo_target(0,0,0)==0 && !bridge.servo_us[0]);
    assert(!(bridge.servo_error_mask&1)); assert(servo_target(0,1700,0)==2);
    assert(servo_target(1,1550,1550)==0); assert(servo_target(2,1000,1000)==0);
    remote_bridge_status(&bridge,5,status);
    assert(status[1]==5 && status[2]==6 && status[3]==143 && link_get_u16(status+10)==1550 && link_get_u16(status+12)==1000);
    remote_bridge_status(&bridge,6,status);
    assert(status[1]==6 && !status[2] && !status[3] && link_get_u16(status+14)==100 && link_get_u16(status+16)==200 && link_get_u16(status+18)==500);
    before=servo_writes;
    packet(p,REMOTE_OPCODE_SERVO); link_put_u16(p+14,1500);
    assert(remote_bridge_receive(&bridge,p,20,now,now)); remote_bridge_status(&bridge,5,status);
    assert(status[3]!=0 && servo_writes==before); /* legacy client cannot cause first center jump */
    packet(p,REMOTE_OPCODE_SERVO_TARGET); link_put_u16(p+14,1700); link_put_u16(p+16,1300);
    assert(!remote_bridge_receive(&bridge,p,20,now-200,now)); assert(servo_writes==before);
    cycle(350); servo_standstill();
    packet(p,REMOTE_OPCODE_SERVO_TARGET); p[12]=2; link_put_u16(p+14,2001);
    assert(!remote_bridge_receive(&bridge,p,20,now,now)); assert(servo_writes==before);
    cycle(350); servo_standstill();
    assert(servo_target(2,2000,0)==0); cycle(100); remote_bridge_stop(&bridge,REMOTE_REASON_LINK,now);
    held=bridge.servo_us[2]; cycle(350); assert(bridge.servo_us[2]==held);
    puts("PASS explicit cold reference without centering, target ACK, no replay, lease/STOP/fault hold, release, HAL error, three channels and old-client rejection");
}

int main(void)
{
    actuation_guard_t g={0};
    assert(actuation_guard_allows(&g,LINK_CONTROL,0));
    actuation_guard_record(&g,LINK_CONTROL,10);
    assert(!actuation_guard_allows(&g,LINK_PWM,12)); /* disabled CONTROL cannot erase history */
    actuation_guard_stop(&g,59); /* 49ms loop gap: output was active until t59 */
    assert(!actuation_guard_allows(&g,LINK_PWM,60));
    actuation_guard_stop(&g,80); /* repeated stop cannot restart the hold */
    assert(!actuation_guard_allows(&g,LINK_PWM,108));
    assert(actuation_guard_allows(&g,LINK_PWM,109));
    actuation_guard_record(&g,LINK_PWM,0xfffffff0U);
    actuation_guard_stop(&g,0xfffffff0U);
    assert(!actuation_guard_allows(&g,LINK_CONTROL,20));
    assert(actuation_guard_allows(&g,LINK_CONTROL,34));
    puts("PASS physical output mode guard across STOP, new session and clock wrap");
    test_config_ack(); test_drive_stop_lease(); test_fault_and_telemetry();
    test_watchdog_fault_diagnostics(); test_motion_math(); test_servos(); return 0;
}
