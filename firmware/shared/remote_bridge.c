#include "remote_bridge.h"
#include <string.h>

static uint16_t speed_limit(const remote_bridge_t *b)
{
    unsigned i; uint16_t limit=300;
    for(i=0;i<4;i++) { uint16_t wheel=link_get_u16(b->configs[i]+12); if(wheel<limit) limit=wheel; }
    return limit;
}

static void acknowledge(remote_bridge_t *b,uint8_t result)
{
    memset(b->ack,0,20); b->ack[0]=1; b->ack[1]=3;
    b->ack[2]=b->config_wheel; b->ack[3]=result;
    link_put_u32(b->ack+4,b->config_session);
    link_put_u32(b->ack+8,b->config_source_sequence); b->ack_pending=1;
}
void remote_bridge_init(remote_bridge_t *b,link_node_t *link,uint32_t session)
{
    unsigned i; memset(b,0,sizeof(*b)); b->link=link;
    remote_control_init(&b->control,session); b->uart_token=link->token;
    for(i=0;i<4;i++) {
        uint8_t *p=b->configs[i]; p[0]=(uint8_t)i;
        link_put_u16(p+2,650); link_put_u32(p+4,i==3?884:1768);
        link_put_u16(p+8,1000); link_put_u16(p+10,2000);
        link_put_u16(p+12,300); link_put_u16(p+14,500);
    }
}
void remote_bridge_stop(remote_bridge_t *b,uint8_t reason,uint32_t now)
{
    remote_control_reset(&b->control,reason);
    link_node_request_stop(b->link); b->link->last_tx_ms=now-LINK_COMMAND_PERIOD_MS;
    if(b->config_pending) { acknowledge(b,3); b->config_pending=0; }
}
void remote_bridge_tick(remote_bridge_t *b,uint32_t now)
{
    link_node_t *n=b->link; uint8_t had_motion,ready;
    link_node_tick(n,now);
    if(b->uart_token!=n->token) {
        unsigned i;
        b->uart_token=n->token; remote_control_reset(&b->control,REMOTE_REASON_LINK);
        link_node_cancel_input(n);
        /* A token change can be an STM reboot: a later gain-only edit must
         * never restore an old physical confirmation from the ESP cache. */
        for(i=0;i<4;i++) b->configs[i][1]&=(uint8_t)~4U;
        if(b->config_pending) { acknowledge(b,3); b->config_pending=0; }
    }
    ready=(uint8_t)(n->connected && !n->pwm_stop_latched &&
        (n->remote_flags&LINK_FLAG_SPEED_SUPPORTED) && n->remote_speed_valid_mask==15 &&
        n->remote_speed_ready_mask==15 && n->remote_speed_fault==0 &&
        !(n->input_enable && n->input_mode==LINK_PWM));
    had_motion=b->control.armed || b->control.active;
    if(remote_control_tick(&b->control,now,n->connected,ready) && had_motion) {
        link_node_request_stop(n); n->last_tx_ms=now-LINK_COMMAND_PERIOD_MS;
    }
    if(b->config_pending) {
        if(n->remote_config_valid) {
            acknowledge(b,n->config_result);
            if(!n->config_result && b->config_wheel<4)
                memcpy(b->configs[b->config_wheel],b->pending_config,20);
            b->config_pending=0; n->remote_config_valid=0;
        } else if((uint32_t)(now-b->config_ms)>=600U || (!n->config_pending && !n->config_waiting)) {
            acknowledge(b,3); b->config_pending=0;
        }
    }
}
int remote_bridge_receive(remote_bridge_t *b,const uint8_t *p,size_t size,uint32_t arrival,uint32_t now)
{
    remote_action_t a; link_node_t *n=b->link; int accepted;
    remote_bridge_tick(b,now);
    if(p && size==20 && p[1]==REMOTE_OPCODE_ARM && (b->config_pending || b->ack_pending)) return 0;
    accepted=remote_control_receive(&b->control,p,size,arrival,now,&a);
    if(a.kind==REMOTE_ACTION_STOP) {
        link_node_request_stop(n); n->last_tx_ms=now-LINK_COMMAND_PERIOD_MS;
        if(b->config_pending) { acknowledge(b,3); b->config_pending=0; }
    } else if(a.kind==REMOTE_ACTION_DRIVE) {
        unsigned i; int32_t peak=0; uint16_t limit=speed_limit(b);
        for(i=0;i<4;i++) { int32_t value=a.targets[i]; if(value<0) value=-value; if(value>peak) peak=value; }
        if(peak>limit) for(i=0;i<4;i++) a.targets[i]=(int16_t)((int32_t)a.targets[i]*limit/peak);
        b->control.left_mm_s=a.targets[1]; b->control.right_mm_s=a.targets[0];
        if(!link_node_submit_input(n,n->have_input_sequence?n->input_sequence+1U:1U,
                                  a.targets,1,a.ttl_ms,now)) {
            remote_bridge_stop(b,REMOTE_REASON_LINK,now); return 0;
        }
        n->last_tx_ms=now-LINK_COMMAND_PERIOD_MS;
    } else if(a.kind==REMOTE_ACTION_ARM) {
        /* ARM must never emit STOP: STOP rotates UART token and revokes ARM. */
        link_node_cancel_input(n);
    } else if(a.kind==REMOTE_ACTION_LED) {
        (void)link_node_request_led(n,a.led_on,now);
    } else if(a.kind>=REMOTE_ACTION_MECHANICAL && a.kind<=REMOTE_ACTION_CLEAR_FAULT) {
        if(b->config_pending || b->ack_pending) {
            remote_bridge_stop(b,REMOTE_REASON_QUEUE,now); return 0;
        }
        b->config_wheel=a.kind==REMOTE_ACTION_CLEAR_FAULT?255:a.wheel;
        b->config_session=b->control.session; b->config_source_sequence=a.source_sequence;
        b->config_ms=now;
        if(b->config_wheel<4) {
            memcpy(b->pending_config,b->configs[a.wheel],20);
            if(a.kind==REMOTE_ACTION_MECHANICAL) {
                b->pending_config[1]=a.flags; link_put_u16(b->pending_config+2,a.diameter_tenth_mm);
                link_put_u32(b->pending_config+4,a.counts_per_rev);
            } else {
                link_put_u16(b->pending_config+8,a.kp_milli); link_put_u16(b->pending_config+10,a.ki_milli);
                link_put_u16(b->pending_config+12,a.max_mm_s);
            }
        }
        if(link_node_request_config(n,b->pending_config,b->config_wheel==255,now)) b->config_pending=1;
        else acknowledge(b,1);
    }
    return accepted;
}
void remote_bridge_status(remote_bridge_t *b,uint8_t kind,uint8_t p[20])
{
    link_node_t *n=b->link; unsigned i;
    if(b->ack_pending) { memcpy(p,b->ack,20); b->ack_pending=0; return; }
    memset(p,0,20); p[0]=1; p[1]=kind;
    if(kind==1) {
        p[2]=n->remote_encoder_valid_mask;
        for(i=0;i<4;i++) link_put_u32(p+4+4*i,(uint32_t)n->remote_encoder_counts[i]);
    } else if(kind==2) {
        p[2]=n->remote_speed_valid_mask; p[3]=n->remote_speed_fault;
        for(i=0;i<4;i++) { link_put_u16(p+4+2*i,(uint16_t)n->remote_speed_measured[i]);
            link_put_u16(p+12+2*i,(uint16_t)n->remote_speed_targets[i]); }
    } else {
        uint8_t active=b->control.active && (b->control.left_mm_s || b->control.right_mm_s) &&
            n->connected && (n->remote_flags&LINK_FLAG_COMMAND_VALID) &&
            !(n->remote_flags&LINK_FLAG_OUTPUT_INHIBITED);
        p[1]=0; p[2]=(n->connected?1:0)|(b->control.armed?2:0)|(active?4:0)|
            (b->control.drive_ready?8:0)|(n->remote_speed_fault?16:0);
        p[3]=b->control.reason; link_put_u32(p+4,b->control.session);
        link_put_u32(p+8,b->control.have_sequence?b->control.last_sequence:0);
        if(n->remote_speed_valid_mask==15) {
            link_put_u16(p+12,(uint16_t)(((int32_t)n->remote_speed_measured[1]+n->remote_speed_measured[3])/2));
            link_put_u16(p+14,(uint16_t)(((int32_t)n->remote_speed_measured[0]+n->remote_speed_measured[2])/2));
        }
        link_put_u16(p+16,speed_limit(b)); p[18]=n->remote_speed_ready_mask; p[19]=n->remote_speed_fault;
    }
}
