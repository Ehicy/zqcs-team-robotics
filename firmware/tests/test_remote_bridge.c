#include "remote_bridge.h"
#include "actuation_guard.h"
#include "speed_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static link_node_t client,stm;
static remote_bridge_t bridge;
static uint32_t now,seq;
static uint8_t ready=15,fault;
static void transfer(link_node_t *a,link_node_t *b)
{
    uint8_t p[LINK_WIRE_MAX]; size_t i,len=link_node_transmit(a,now,p,sizeof(p));
    for(i=0;i<len;i++) link_node_receive(b,p[i],now);
}
static void cycle(unsigned count)
{
    unsigned i; const int16_t measured[4]={20,-20,30,-30},targets[4]={100,-100,100,-100};
    for(i=0;i<count;i++,now++) {
        link_node_publish_speed(&stm,measured,targets,15,ready,fault,now);
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
    now=seq=0; ready=15; fault=0;
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
static unsigned servo_command(uint8_t channel,uint16_t pulse)
{
    uint8_t p[20],ack[20]; uint32_t session=bridge.control.session;
    packet(p,REMOTE_OPCODE_SERVO); p[12]=channel; link_put_u16(p+14,pulse);
    assert(remote_bridge_receive(&bridge,p,20,now,now));
    remote_bridge_status(&bridge,4,ack);
    assert(ack[1]==3 && ack[2]==254 && link_get_u32(ack+4)==session && link_get_u32(ack+8)==seq);
    return ack[3];
}
static void test_servos(void)
{
    uint8_t p[20],status[20]; unsigned before;
    initialize(); servo_writes=0; servo_hardware_ok=1;
    bridge.servo_write=fake_servo_write; servo_standstill();
    assert(servo_command(0,1600)==2 && !servo_writes); /* no initial jump */
    assert(servo_command(0,1500)==0 && bridge.servo_us[0]==1500);
    assert(servo_command(0,1510)==2); /* 100ms firmware rate gate */
    cycle(100); servo_standstill();
    assert(servo_command(0,1525)==0);
    assert(servo_command(1,1500)==0); /* independent channels */
    remote_bridge_status(&bridge,4,status);
    assert(status[1]==4 && status[2]==3 && status[3]==7);
    assert(link_get_u16(status+8)==1525 && link_get_u16(status+10)==1500);
    cycle(100); servo_standstill();
    assert(servo_command(0,1600)==2); /* maximum step */
    client.remote_speed_measured[3]=1;
    assert(servo_command(0,1550)==1); servo_standstill();
    client.remote_speed_targets[2]=1;
    assert(servo_command(0,1550)==1); servo_standstill();
    client.remote_speed_valid_mask=7;
    assert(servo_command(0,1550)==1); servo_standstill();
    client.remote_speed_rx_ms=now-200;
    assert(servo_command(0,1550)==1); servo_standstill();
    before=servo_writes;
    packet(p,REMOTE_OPCODE_SERVO); p[12]=0; link_put_u16(p+14,1550);
    assert(remote_bridge_receive(&bridge,p,20,now,now));
    remote_bridge_status(&bridge,4,status);
    assert(!remote_bridge_receive(&bridge,p,20,now,now) && servo_writes==before+1); /* replay */
    remote_bridge_stop(&bridge,REMOTE_REASON_STOP,now);
    assert(bridge.servo_us[0]==1550 && bridge.servo_us[1]==1500); /* no release/drop */
    assert(!remote_bridge_receive(&bridge,p,20,now,now)); /* stale session */
    cycle(350); servo_standstill();
    before=servo_writes;
    packet(p,REMOTE_OPCODE_SERVO); link_put_u16(p+14,1575);
    assert(!remote_bridge_receive(&bridge,p,20,now-200,now) && before==servo_writes);
    cycle(350); servo_standstill();
    packet(p,REMOTE_OPCODE_SERVO); link_put_u16(p+14,2000);
    assert(!remote_bridge_receive(&bridge,p,20,now,now) && before==servo_writes);
    cycle(350); servo_standstill();
    packet(p,REMOTE_OPCODE_ARM); assert(remote_bridge_receive(&bridge,p,20,now,now));
    packet(p,REMOTE_OPCODE_SERVO); link_put_u16(p+14,1575);
    assert(!remote_bridge_receive(&bridge,p,20,now,now) && before==servo_writes && !bridge.control.armed);
    cycle(350); servo_standstill();
    servo_hardware_ok=0;
    assert(servo_command(0,1575)==4 && bridge.servo_us[0]==1550);
    servo_hardware_ok=1;
    assert(servo_command(0,0)==0 && bridge.servo_us[0]==0);
    remote_bridge_stop(&bridge,REMOTE_REASON_LINK,now);
    cycle(350); servo_standstill();
    assert(bridge.servo_us[0]==0 && bridge.servo_us[1]==1500);
    assert(servo_command(0,1525)==2); /* released channel must be explicitly enabled */
    cycle(350); servo_standstill();
    assert(servo_command(2,1500)==0);
    for(unsigned pulse=1525;pulse<=2000;pulse+=25) { cycle(100); servo_standstill(); assert(servo_command(2,(uint16_t)pulse)==0); }
    assert(bridge.servo_us[2]==2000 && bridge.servo_us[1]==1500 && bridge.servo_us[0]==0);
    remote_bridge_status(&bridge,5,status);
    assert(status[1]==5 && status[2]==6 && status[3]==15 && link_get_u16(status+12)==2000);
    assert(link_get_u16(status+14)==1000 && link_get_u16(status+16)==2000 && link_get_u16(status+18)==25);
    cycle(100); servo_standstill(); assert(servo_command(2,0)==0);
    puts("PASS servo ACK, three independent channels, door1000-2000, replay/session/age, startup/step/rate, standstill, STOP hold, release and HAL error");
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
    test_watchdog_fault_diagnostics(); test_servos(); return 0;
}
