#include "remote_bridge.h"
#include "manual_feedback.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static link_node_t client,stm;
static remote_bridge_t bridge;
static uint32_t now,seq;
static void transfer(link_node_t *a,link_node_t *b)
{
    uint8_t wire[LINK_WIRE_MAX]; size_t i,n=link_node_transmit(a,now,wire,sizeof(wire));
    for(i=0;i<n;i++) link_node_receive(b,wire[i],now);
}
static void cycle(unsigned count)
{
    const int16_t zero[4]={0,0,0,0};
    while(count--) {
        link_node_publish_speed(&stm,zero,zero,11,0,SPEED_FAULT_ENCODER,now);
        link_node_set_motor_state(&stm,1,!stm.requested_enable);
        remote_bridge_tick(&bridge,now); transfer(&client,&stm);
        if(stm.config_received) link_node_finish_config(&stm,0,0,SPEED_FAULT_ENCODER);
        transfer(&stm,&client); remote_bridge_tick(&bridge,now); now++;
    }
}
static void command(uint8_t op,int16_t throttle,int16_t steer,uint16_t limit)
{
    uint8_t p[20]={1,0,0,0}; p[1]=op;
    link_put_u32(p+4,bridge.control.session); link_put_u32(p+8,++seq);
    if(op==REMOTE_OPCODE_PWM) {
        link_put_u16(p+12,(uint16_t)throttle); link_put_u16(p+14,(uint16_t)steer);
        link_put_u16(p+16,limit); link_put_u16(p+18,200);
    }
    if(!remote_bridge_receive(&bridge,p,20,now,now)) {
        fprintf(stderr,"op=%u now=%lu session=%lu link=%u reason=%u mode=%u stop=%u flags=%u phase=%u\n",
            op,(unsigned long)now,(unsigned long)bridge.control.session,client.connected,
            bridge.control.reason,client.input_mode,client.pwm_stop_latched,client.remote_flags,bridge.phase);
        assert(0);
    }
}
int main(void)
{
    uint8_t status[20];
    link_node_init(&client,LINK_CLIENT,123,0); link_node_init(&stm,LINK_CONTROLLER,456,0);
    remote_bridge_init(&bridge,&client,789); cycle(130);
    assert(client.connected && !bridge.control.armed && !stm.requested_enable);
    assert(client.remote_speed_fault==SPEED_FAULT_ENCODER && !client.remote_speed_ready_mask);
    command(REMOTE_OPCODE_PWM,1000,0,1000); cycle(8);
    assert(stm.requested_enable && stm.requested_mode==LINK_PWM);
    assert(stm.requested_pwm[0]==1000 && stm.requested_pwm[1]==-1000);
    assert(stm.requested_pwm[2]==1000 && stm.requested_pwm[3]==1000);
    assert(!bridge.control.armed && bridge.control.active);
    cycle(70); remote_bridge_status(&bridge,0,status);
    assert(status[1]==0 && (status[2]&64) && !(status[2]&16) && !status[19]);
    command(REMOTE_OPCODE_PWM,0,-1000,600); cycle(5);
    assert(stm.requested_pwm[0]==600 && stm.requested_pwm[1]==600);
    cycle(230); assert(!stm.requested_enable && !bridge.control.active);
    cycle(350); command(REMOTE_OPCODE_PWM,-1000,0,900); cycle(5);
    assert(stm.requested_pwm[0]==-900 && stm.requested_pwm[1]==900);
    command(REMOTE_OPCODE_STOP,0,0,0); cycle(5); assert(!stm.requested_enable);
    assert(stm.quick_stop_received && stm.quick_stop_lease_ms<=40);
    command(REMOTE_OPCODE_PWM,1000,0,1000); cycle(5);
    assert(stm.requested_enable); /* No clear-fault or ARM between commands. */
    assert(!stm.quick_stop_received);
    {
        speed_control_t speeds; manual_feedback_t feedback; int32_t counts[4]={0,0,0,0};
        speed_control_init(&speeds,0,counts); manual_feedback_init(&feedback,0,counts);
        counts[0]=counts[1]=counts[2]=counts[3]=1;
        manual_feedback_sample(&feedback,10,counts,11,speeds.config);
        assert(feedback.valid_mask==11 && feedback.measured[0] && !feedback.measured[2]);
        counts[1]=100000; manual_feedback_sample(&feedback,20,counts,15,speeds.config);
        assert(feedback.valid_mask==13); /* Bad M2 doesn't erase the other three. */
        manual_feedback_sample(&feedback,30,counts,15,speeds.config);
        assert(feedback.valid_mask==15 && !feedback.measured[1]);
    }
    puts("C3 manual: full PWM without ARM/PID/calibration, bad encoder ignored, direction/mode/STOP/TTL correct, per-wheel telemetry recovers");
}
