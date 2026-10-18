#include "speed_control.h"
#include "remote_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void pid_core(void)
{
    speed_control_t a,b; speed_config_t c; unsigned i;
    int32_t counts[4]={0,0,0,0}; const int16_t target[4]={300,300,300,300};
    speed_control_init(&a,0,counts);
    for(i=0;i<4;i++) { c=a.config[i]; c.confirmed=1; c.accel_mm_s2=5000; assert(speed_control_configure(&a,i,&c)); }
    assert(!a.pid_initialized && !speed_control_update(&a,0,counts,15,target,1,0,200));
    assert(speed_control_clear_fault(&a));
    assert(speed_control_configure_pid(&a,1000,0,50)); b=a;
    assert(speed_control_configure_pid(&b,1000,0,0));
    assert(speed_control_update(&a,10,counts,15,target,1,10,200));
    assert(speed_control_update(&b,10,counts,15,target,1,10,200));
    for(i=0;i<4;i++) assert(a.pwm_permille[i]==b.pwm_permille[i]); /* no setpoint kick */
    counts[0]=1;
    assert(speed_control_update(&a,20,counts,15,target,1,20,200));
    assert(speed_control_update(&b,20,counts,15,target,1,20,200));
    assert(a.derivative_milli[0]<0 && a.derivative_milli[0]>-1200000);
    assert(a.pwm_permille[0]<b.pwm_permille[0]);
    for(i=1;i<4;i++) assert(a.derivative_milli[i]==0 && a.pwm_permille[i]==b.pwm_permille[i]);
    assert(!speed_control_configure_pid(&a,2000,1000,100)); /* moving: atomic reject */
    for(i=0;i<4;i++) assert(a.config[i].kp_milli==1000 && a.config[i].kd_milli==50);
    speed_control_stop(&a,21);
    for(i=0;i<4;i++) assert(!a.derivative_valid[i] && !a.derivative_milli[i] && !a.integral_micro[i]);
    (void)speed_control_update(&a,30,counts,15,0,0,0,0);
    assert(!speed_control_configure_pid(&a,1000,2000,20001));
    assert(speed_control_configure_pid(&a,1200,900,20));
    for(i=0;i<4;i++) assert(a.config[i].kp_milli==1200 && a.config[i].ki_milli==900 && a.config[i].kd_milli==20);
    puts("PASS PID: explicit initialization, four independent D histories, measured derivative/filter, no target kick, stopped-only atomic gains");
}
static link_node_t esp,stm;
static remote_bridge_t bridge;
static speed_control_t speed;
static uint32_t now,sequence;
static uint8_t reject_pid;
static int32_t zero[4];
static void transfer(link_node_t *a,link_node_t *b)
{
    uint8_t packet[LINK_WIRE_MAX]; size_t i,n=link_node_transmit(a,now,packet,sizeof(packet));
    for(i=0;i<n;i++) link_node_receive(b,packet[i],now);
}
static void cycle(unsigned count)
{
    unsigned j;
    for(j=0;j<count;j++,now++) {
        (void)speed_control_update(&speed,now,zero,15,0,0,0,0);
        link_node_publish_speed(&stm,speed.measured_mm_s,speed.ramped_mm_s,15,
                               speed.pid_initialized?speed.ready_mask:0,speed.fault,now);
        link_node_set_motor_state(&stm,1,1);
        remote_bridge_tick(&bridge,now); transfer(&esp,&stm);
        if(stm.config_received) {
            uint8_t result=0;
            if(stm.config_wheel==253) {
                if(reject_pid) result=2;
                else result=speed_control_configure_pid(&speed,link_get_u16(stm.config_payload),
                    link_get_u16(stm.config_payload+2),link_get_u16(stm.config_payload+4))?0:2;
            }
            link_node_finish_config(&stm,result,speed.pid_initialized?speed.ready_mask:0,speed.fault);
        }
        transfer(&stm,&esp); remote_bridge_tick(&bridge,now);
    }
}
static void setup(void)
{
    now=sequence=0; reject_pid=0; memset(zero,0,sizeof(zero));
    link_node_init(&esp,LINK_CLIENT,123,0); link_node_init(&stm,LINK_CONTROLLER,456,0);
    speed_control_init(&speed,0,zero); remote_bridge_init(&bridge,&esp,789);
}
static void command(uint8_t p[20],uint8_t opcode)
{
    memset(p,0,20); p[0]=1;p[1]=opcode;
    link_put_u32(p+4,bridge.control.session);link_put_u32(p+8,++sequence);
}
static void bridge_pid(void)
{
    uint8_t p[20],ack[20]; unsigned i;
    setup(); bridge.kp_milli=1234; bridge.ki_milli=567; bridge.kd_milli=23;
    cycle(180); assert(speed.pid_initialized && bridge.pid_initialized);
    assert(bridge.phase==CHASSIS_WAIT_CALIBRATION && !bridge.control.armed && !bridge.ack_pending);
    for(i=0;i<4;i++) assert(speed.config[i].kp_milli==1234 && speed.config[i].kd_milli==23);
    command(p,REMOTE_OPCODE_ARM); assert(!remote_bridge_receive(&bridge,p,20,now,now));
    command(p,REMOTE_OPCODE_PID); link_put_u16(p+14,900);link_put_u16(p+16,800);link_put_u16(p+18,10);
    assert(remote_bridge_receive(&bridge,p,20,now,now));
    assert(speed.config[0].kp_milli==1234); /* asynchronous, before UART ACK */
    cycle(10);remote_bridge_status(&bridge,0,ack);assert(ack[1]==3 && ack[2]==253 && !ack[3]);
    for(i=0;i<4;i++) assert(speed.config[i].kp_milli==900 && speed.config[i].ki_milli==800 && speed.config[i].kd_milli==10);
    assert(!bridge.control.armed && !bridge.control.drive_ready);
    setup();reject_pid=1;cycle(180);assert(!bridge.pid_initialized && bridge.pid_init_failed);
    command(p,REMOTE_OPCODE_ARM);assert(!remote_bridge_receive(&bridge,p,20,now,now));
    cycle(180);assert(!bridge.pid_initialized && !bridge.config_pending);
    reject_pid=0;command(p,REMOTE_OPCODE_PID);link_put_u16(p+14,1000);link_put_u16(p+16,2000);
    assert(remote_bridge_receive(&bridge,p,20,now,now));cycle(10);assert(bridge.pid_initialized);
    puts("PASS real UART PID init/ACK, ESP unified source, no auto-arm/calibration, backend rejection locks and explicit retry recovers");
}
static void states(void)
{
    assert(chassis_state_update(0,0,0,0,0,0)==CHASSIS_WAIT_LINK);
    assert(chassis_state_update(1,0,0,0,0,0)==CHASSIS_WAIT_PID);
    assert(chassis_state_update(1,1,0,0,0,0)==CHASSIS_WAIT_CALIBRATION);
    assert(chassis_state_update(1,1,1,0,0,0)==CHASSIS_LOCKED);
    assert(chassis_state_update(1,1,1,0,1,0)==CHASSIS_ARMED);
    assert(chassis_state_update(1,1,1,0,1,1)==CHASSIS_RUNNING);
    assert(chassis_state_update(1,1,1,1,1,1)==CHASSIS_FAULT);
    assert(chassis_state_update(1,1,1,0,0,1)==CHASSIS_LOCKED);
    puts("PASS explicit chassis state transitions and priority gates");
}
int main(void) { pid_core();bridge_pid();states();return 0; }
