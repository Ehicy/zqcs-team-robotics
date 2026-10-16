#include "board_link.h"
#include <string.h>

static void clear_config(link_node_t *n)
{
    n->config_pending=n->config_waiting=n->config_received=n->config_ack_pending=0;
    n->remote_config_valid=0;
}
static int config_valid(const uint8_t *p)
{
    return p && p[0]<4 && p[1]<8 && link_get_u16(p+2)>0 && link_get_u16(p+2)<=5000 &&
        link_get_u32(p+4)>0 && link_get_u32(p+4)<=1000000 &&
        link_get_u16(p+8)<=20000 && link_get_u16(p+10)<=20000 &&
        (link_get_u16(p+8) || link_get_u16(p+10)) &&
        link_get_u16(p+12)>0 && link_get_u16(p+12)<=300 &&
        link_get_u16(p+14)>0 && link_get_u16(p+14)<=5000 && link_get_u32(p+16)==0;
}
int link_node_request_config(link_node_t *n,const uint8_t p[20],uint8_t clear,uint32_t now)
{
    link_node_tick(n,now);
    if(n->role!=LINK_CLIENT || !n->connected || !n->token || n->pwm_stop_latched ||
       !(n->remote_flags&LINK_FLAG_SPEED_SUPPORTED) || n->config_pending || n->config_waiting ||
       n->input_enable || clear>1 || (!clear && !config_valid(p))) return 0;
    n->config_wheel=clear?255:p[0];
    if(!clear) memcpy(n->config_payload,p,20);
    n->config_pending=1; n->remote_config_valid=0; n->config_request_ms=now; return 1;
}
void link_node_finish_config(link_node_t *n,uint8_t result,uint8_t ready,uint8_t fault)
{
    if(n->role!=LINK_CONTROLLER || !n->config_received) return;
    n->config_received=0; n->config_ack_pending=1;
    n->config_result=result; n->config_ready_mask=ready; n->config_fault=fault;
}
void link_node_publish_speed(link_node_t *n,const int16_t measured[4],const int16_t targets[4],
                            uint8_t valid,uint8_t ready,uint8_t fault,uint32_t now)
{
    if(n->role!=LINK_CONTROLLER || !measured || !targets || valid>15 || ready>15) return;
    n->speed_supported=1; n->speed_valid_mask=valid; n->speed_ready_mask=ready;
    n->speed_fault=fault; n->speed_sample_ms=now;
    memcpy(n->speed_measured,measured,8); memcpy(n->speed_targets,targets,8);
}

static void clear_led(link_node_t *n)
{
    n->led_on=0; n->led_pending=0; n->led_waiting_ack=0;
    n->led_ack_pending=0; n->remote_led_valid=0;
}
static void rotate_token(link_node_t *n)
{
    n->token+=0x9e3779b9UL;
    if(!n->token) n->token=1;
    n->command_valid=0; n->requested_enable=0;
    clear_led(n);
    clear_config(n);
    memset(n->requested_mm_s,0,sizeof(n->requested_mm_s));
    memset(n->requested_pwm,0,sizeof(n->requested_pwm)); n->requested_mode=0;
}
void link_node_cancel_input(link_node_t *n)
{
    n->input_valid=0; n->input_enable=0; n->input_mode=0;
    memset(n->input_mm_s,0,sizeof(n->input_mm_s));
    memset(n->input_pwm,0,sizeof(n->input_pwm));
}
void link_node_request_stop(link_node_t *n)
{
    link_node_cancel_input(n);
    if(n->role!=LINK_CLIENT) return;
    n->pwm_stop_latched=1;
    clear_config(n);
    n->stop_pending=(uint8_t)(n->connected && n->token);
}
void link_node_set_motor_state(link_node_t *n,uint8_t supported,uint8_t inhibited)
{
    if(n->role!=LINK_CONTROLLER) return;
    n->pwm_supported=supported?1:0; n->output_inhibited=inhibited?1:0;
}
int link_node_publish_encoders(link_node_t *n,const int32_t counts[4],uint8_t mask,uint32_t now)
{
    unsigned i;
    if(n->role!=LINK_CONTROLLER || mask>15 || (mask && !counts)) return 0;
    n->encoder_valid_mask=mask; n->encoder_sample_ms=now;
    for(i=0;i<4;i++) n->encoder_counts[i]=(mask&(1U<<i))?counts[i]:0;
    return 1;
}
void link_node_init(link_node_t *n,uint8_t role,uint32_t boot,uint32_t now)
{
    memset(n,0,sizeof(*n)); n->role=role; n->boot_id=boot?boot:1;
    n->token=role==LINK_CONTROLLER?n->boot_id:0; n->last_rx_ms=now;
    n->last_tx_ms=now-LINK_HELLO_PERIOD_MS; n->reason=LINK_WAITING;
    n->encoder_tx_ms=now;
    n->output_inhibited=1;
}
void link_node_tick(link_node_t *n,uint32_t now)
{
    if((n->config_pending || n->config_waiting || n->config_received || n->config_ack_pending) &&
       (uint32_t)(now-n->config_request_ms)>=500U) clear_config(n);
    if((uint32_t)(now-n->speed_sample_ms)>=LINK_ENCODER_SAMPLE_MAX_AGE_MS) n->speed_valid_mask=0;
    if((uint32_t)(now-n->remote_speed_rx_ms)>=LINK_ENCODER_SAMPLE_MAX_AGE_MS) n->remote_speed_valid_mask=0;
    if((n->led_pending || n->led_waiting_ack || n->led_ack_pending) &&
       (uint32_t)(now-n->led_request_ms)>=LINK_LED_TIMEOUT_MS) {
        n->led_pending=0; n->led_waiting_ack=0; n->led_ack_pending=0;
        n->remote_led_valid=0;
    }
    if((uint32_t)(now-n->encoder_sample_ms)>=LINK_ENCODER_SAMPLE_MAX_AGE_MS)
        n->encoder_valid_mask=0;
    if((uint32_t)(now-n->remote_encoder_rx_ms)>=LINK_ENCODER_SAMPLE_MAX_AGE_MS)
        n->remote_encoder_valid_mask=0;
    if(n->input_valid && (uint32_t)(now-n->input_ms)>=n->input_ttl_ms)
        link_node_cancel_input(n);
    if(n->role==LINK_CONTROLLER && n->command_valid &&
       (uint32_t)(now-n->command_ms)>=n->command_ttl_ms) {
        rotate_token(n); n->reason=LINK_CONTROL_EXPIRED;
    }
    if(n->connected && (uint32_t)(now-n->last_rx_ms)>=LINK_PEER_TIMEOUT_MS) {
        n->connected=0; n->have_peer_sequence=0; n->reason=LINK_PEER_EXPIRED;
        n->remote_encoder_valid_mask=0;
        n->remote_flags=0;
        n->remote_speed_valid_mask=0; clear_config(n);
        n->stop_pending=0;
        clear_led(n);
        link_node_cancel_input(n);
        if(n->role==LINK_CONTROLLER) rotate_token(n);
        else n->token=0;
    }
}
void link_node_transport_error(link_node_t *n,uint32_t now)
{
    link_parser_init(&n->parser); n->connected=0; n->have_peer_sequence=0;
    n->reason=LINK_TRANSPORT_ERROR; n->last_rx_ms=now; n->ack_pending=0;
    n->remote_encoder_valid_mask=0;
    n->remote_flags=0;
    n->remote_speed_valid_mask=0; clear_config(n);
    n->stop_pending=0;
    clear_led(n);
    link_node_cancel_input(n);
    if(n->role==LINK_CONTROLLER) rotate_token(n); else n->token=0;
}
int link_node_request_led(link_node_t *n,uint8_t on,uint32_t now)
{
    link_node_tick(n,now);
    if(n->role!=LINK_CLIENT || !n->connected || !n->token || on>1 ||
       n->led_pending || n->led_waiting_ack) return 0;
    n->led_expected_on=on; n->led_request_ms=now;
    n->led_pending=1; n->remote_led_valid=0; return 1;
}
static int submit_input(link_node_t *n,uint32_t seq,const int16_t v[4],
                        uint8_t enable,uint16_t ttl,uint32_t now,uint8_t mode)
{
    unsigned i; int limit=mode==LINK_PWM?LINK_PWM_LIMIT_PERMILLE:LINK_TARGET_LIMIT_MM_S;
    link_node_tick(n,now);
    if(n->role!=LINK_CLIENT || !n->connected || !n->token || !v || enable>1 ||
       !ttl || ttl>LINK_COMMAND_MAX_TTL_MS ||
       (n->have_input_sequence && !link_sequence_newer(seq,n->input_sequence))) return 0;
    if(n->pwm_stop_latched || (mode==LINK_PWM && !(n->remote_flags&LINK_FLAG_PWM_SUPPORTED))) return 0;
    for(i=0;i<4;i++) if(v[i]>limit || v[i]<-limit) return 0;
    if(!enable) for(i=0;i<4;i++) if(v[i]!=0) return 0;
    n->input_sequence=seq; n->have_input_sequence=1; n->input_ms=now;
    n->input_ttl_ms=ttl; n->input_enable=enable; n->input_valid=1;
    n->input_mode=mode;
    memset(n->input_mm_s,0,sizeof(n->input_mm_s));
    memset(n->input_pwm,0,sizeof(n->input_pwm));
    memcpy(mode==LINK_PWM?n->input_pwm:n->input_mm_s,v,sizeof(n->input_mm_s)); return 1;
}
int link_node_submit_input(link_node_t *n,uint32_t seq,const int16_t v[4],
                          uint8_t enable,uint16_t ttl,uint32_t now)
{ return submit_input(n,seq,v,enable,ttl,now,LINK_CONTROL); }
int link_node_submit_pwm(link_node_t *n,uint32_t seq,const int16_t v[4],
                        uint8_t enable,uint16_t ttl,uint32_t now)
{ return submit_input(n,seq,v,enable,ttl,now,LINK_PWM); }
static int accept_sequence(link_node_t *n,const link_frame_t *f)
{
    if(n->have_peer_sequence && !link_sequence_newer(f->sequence,n->peer_sequence)) return 0;
    n->peer_sequence=f->sequence; n->have_peer_sequence=1; return 1;
}
static int receive_controller(link_node_t *n,const link_frame_t *f,uint32_t now)
{
    unsigned i; int16_t targets[4]; uint16_t ttl; uint8_t any=0;
    uint8_t previous_mode=(n->command_valid && n->requested_enable)?n->requested_mode:0;
    int limit=f->type==LINK_PWM?LINK_PWM_LIMIT_PERMILLE:LINK_TARGET_LIMIT_MM_S;
    if(f->type==LINK_HELLO && f->length==0 && f->session) {
        uint32_t old_token=n->token;
        uint8_t new_peer=f->session!=n->peer_id;
        if(f->session!=n->peer_id) {
            n->peer_id=f->session; n->have_peer_sequence=0;
        }
        if(!accept_sequence(n,f)) return 0;
        /* Every accepted handshake revokes the old motion session, including
         * a fast client-only transport recovery with the SAME boot id. */
        if(new_peer) n->token^=f->session;
        rotate_token(n);
        if(n->token==old_token) rotate_token(n);
        n->reason=LINK_WAITING;
        n->connected=1; n->last_rx_ms=now; n->ack_pending=1; return 1;
    }
    if(f->type==LINK_LED_SET && f->length==1 && n->connected &&
       f->session==n->token && f->payload[0]<=1 && !n->led_ack_pending) {
        if(!accept_sequence(n,f)) return 0;
        n->led_on=f->payload[0]; n->led_request_sequence=f->sequence;
        n->led_request_ms=now; n->led_ack_pending=1; n->last_rx_ms=now;
        return 1;
    }
    if((f->type==LINK_SPEED_CONFIG || f->type==LINK_CLEAR_FAULT) && n->speed_supported &&
       n->connected && f->session==n->token && !n->config_received && !n->config_ack_pending) {
        if((f->type==LINK_SPEED_CONFIG && (f->length!=20 || !config_valid(f->payload))) ||
           (f->type==LINK_CLEAR_FAULT && f->length!=0) || !accept_sequence(n,f)) return 0;
        n->config_wheel=f->type==LINK_CLEAR_FAULT?255:f->payload[0];
        memcpy(n->config_payload,f->payload,20); n->config_sequence=f->sequence;
        n->config_received=1; n->config_request_ms=now; n->last_rx_ms=now; return 1;
    }
    if((f->type!=LINK_CONTROL && f->type!=LINK_PWM) ||
       (f->type==LINK_PWM && !n->pwm_supported) || f->length!=12 || !n->connected ||
       f->session!=n->token || f->payload[2]>1 || f->payload[3]!=0) return 0;
    ttl=link_get_u16(f->payload);
    if(!ttl || ttl>LINK_COMMAND_MAX_TTL_MS) return 0;
    for(i=0;i<4;i++) {
        uint16_t raw=link_get_u16(f->payload+4+2*i);
        int32_t signed_value=raw<32768U?(int32_t)raw:(int32_t)raw-65536L;
        if(signed_value>limit || signed_value<-limit ||
           (!f->payload[2] && signed_value!=0)) return 0;
        targets[i]=(int16_t)signed_value;
        if(signed_value) any=1;
    }
    if(!accept_sequence(n,f)) return 0;
    n->last_rx_ms=n->command_ms=now; n->command_ttl_ms=ttl; n->command_valid=1;
    n->requested_enable=f->payload[2]; n->last_control_sequence=f->sequence;
    n->requested_mode=f->type;
    memset(n->requested_mm_s,0,sizeof(n->requested_mm_s));
    memset(n->requested_pwm,0,sizeof(n->requested_pwm));
    memcpy(f->type==LINK_PWM?n->requested_pwm:n->requested_mm_s,targets,sizeof(targets));
    n->accepted_controls++;
    if((f->type==LINK_PWM && (!f->payload[2] || !any)) ||
       (previous_mode && previous_mode!=f->type)) rotate_token(n);
    n->reason=LINK_WAITING; return 1;
}
static int receive_client(link_node_t *n,const link_frame_t *f,uint32_t now)
{
    if(f->type==LINK_HELLO_ACK && f->length==4 && f->session==n->boot_id &&
       link_get_u32(f->payload)) {
        if(!accept_sequence(n,f)) return 0;
        if(!n->connected || n->token!=link_get_u32(f->payload)) {
            link_node_cancel_input(n); n->remote_encoder_valid_mask=0;
            n->remote_flags=0;
            n->remote_speed_valid_mask=0; clear_config(n);
            n->stop_pending=0; n->pwm_stop_latched=0;
            clear_led(n);
        }
        n->token=link_get_u32(f->payload); n->connected=1; n->last_rx_ms=now; return 1;
    }
    if(f->type==LINK_LED_ACK && f->length==5 && n->connected &&
       f->session==n->token && n->led_waiting_ack &&
       link_get_u32(f->payload)==n->led_request_sequence &&
       f->payload[4]==n->led_expected_on) {
        if(!accept_sequence(n,f)) return 0;
        n->remote_led_on=f->payload[4];
        n->remote_led_sequence=link_get_u32(f->payload);
        n->remote_led_valid=1; n->led_waiting_ack=0; n->last_rx_ms=now;
        return 1;
    }
    if(f->type==LINK_ENCODERS && f->length==20 && n->connected && f->session==n->token) {
        unsigned i; uint16_t age=link_get_u16(f->payload+2);
        if(f->payload[0]>15 || f->payload[1]!=0 || age>=LINK_ENCODER_SAMPLE_MAX_AGE_MS ||
           !accept_sequence(n,f)) return 0;
        n->remote_encoder_valid_mask=f->payload[0]; n->remote_encoder_rx_ms=now-age;
        for(i=0;i<4;i++) {
            uint32_t raw=link_get_u32(f->payload+4+4*i);
            int64_t value=raw<0x80000000UL?(int64_t)raw:(int64_t)raw-4294967296LL;
            n->remote_encoder_counts[i]=(int32_t)value;
        }
        n->last_rx_ms=now; return 1;
    }
    if(f->type==LINK_SPEED && f->length==20 && n->connected && f->session==n->token) {
        unsigned i; uint32_t age=(uint32_t)f->payload[3]*10U;
        if(f->payload[0]>15 || f->payload[2]>15 || age>=LINK_ENCODER_SAMPLE_MAX_AGE_MS ||
           !accept_sequence(n,f)) return 0;
        n->remote_speed_ready_mask=f->payload[0]; n->remote_speed_fault=f->payload[1];
        n->remote_speed_valid_mask=f->payload[2]; n->remote_speed_rx_ms=now-age;
        for(i=0;i<4;i++) {
            uint16_t m=link_get_u16(f->payload+4+2*i),t=link_get_u16(f->payload+12+2*i);
            n->remote_speed_measured[i]=(int16_t)(m<32768U?(int32_t)m:(int32_t)m-65536L);
            n->remote_speed_targets[i]=(int16_t)(t<32768U?(int32_t)t:(int32_t)t-65536L);
        }
        n->last_rx_ms=now; return 1;
    }
    if(f->type==LINK_CONFIG_ACK && f->length==8 && n->connected && f->session==n->token &&
       n->config_waiting && link_get_u32(f->payload)==n->config_sequence &&
       f->payload[4]==n->config_wheel && f->payload[6]<=15) {
        if(!accept_sequence(n,f)) return 0;
        n->config_result=f->payload[5]; n->config_ready_mask=f->payload[6]; n->config_fault=f->payload[7];
        n->config_waiting=0; n->remote_config_valid=1; n->last_rx_ms=now; return 1;
    }
    if(f->type!=LINK_STATUS || f->length!=16 || !n->connected ||
       f->payload[2]!=0 || f->payload[3]!=0 || f->payload[0]>31 ||
       f->payload[1]>LINK_TRANSPORT_ERROR) return 0;
    if(!accept_sequence(n,f)) return 0;
    if(f->session!=n->token) {
        n->connected=0; n->token=0; n->remote_encoder_valid_mask=0;
        n->remote_flags=0;
        n->remote_speed_valid_mask=0; clear_config(n);
        n->stop_pending=0;
        clear_led(n);
        link_node_cancel_input(n); return 1;
    }
    n->remote_flags=f->payload[0]; n->remote_reason=f->payload[1];
    n->remote_last_control_sequence=link_get_u32(f->payload+4);
    n->last_rx_ms=now; return 1;
}
void link_node_receive(link_node_t *n,uint8_t byte,uint32_t now)
{
    link_frame_t f; int accepted;
    /* Check deadlines BEFORE parsing a newly arrived/buffered frame. */
    link_node_tick(n,now);
    if(!link_parser_feed(&n->parser,byte,now,&f)) return;
    accepted=n->role==LINK_CONTROLLER?receive_controller(n,&f,now):receive_client(n,&f,now);
    if(!accepted) n->rejected_frames++;
}
size_t link_node_transmit(link_node_t *n,uint32_t now,uint8_t *out,size_t cap)
{
    link_frame_t f; size_t length; unsigned i;
    link_node_tick(n,now); memset(&f,0,sizeof(f));
    if(n->role==LINK_CONTROLLER) {
        if(n->ack_pending) {
            f.type=LINK_HELLO_ACK; f.session=n->peer_id; f.length=4;
            link_put_u32(f.payload,n->token);
        } else if((uint32_t)(now-n->last_tx_ms)>=LINK_STATUS_PERIOD_MS) {
            f.type=LINK_STATUS; f.session=n->token; f.length=16;
            f.payload[0]=(uint8_t)((n->output_inhibited?LINK_FLAG_OUTPUT_INHIBITED:0) |
                (n->pwm_supported?LINK_FLAG_PWM_SUPPORTED:0) |
                (n->speed_supported?LINK_FLAG_SPEED_SUPPORTED:0) |
                (n->connected?LINK_FLAG_CONNECTED:0) |
                (n->command_valid?LINK_FLAG_COMMAND_VALID:0));
            f.payload[1]=n->reason;
            link_put_u32(f.payload+4,n->last_control_sequence);
            link_put_u32(f.payload+8,n->accepted_controls);
            link_put_u32(f.payload+12,n->rejected_frames+n->parser.errors);
        } else if(n->connected && n->config_ack_pending) {
            f.type=LINK_CONFIG_ACK; f.session=n->token; f.length=8;
            link_put_u32(f.payload,n->config_sequence); f.payload[4]=n->config_wheel;
            f.payload[5]=n->config_result; f.payload[6]=n->config_ready_mask; f.payload[7]=n->config_fault;
        } else if(n->connected && n->led_ack_pending) {
            f.type=LINK_LED_ACK; f.session=n->token; f.length=5;
            link_put_u32(f.payload,n->led_request_sequence);
            f.payload[4]=n->led_on;
        } else if(n->connected && (uint32_t)(now-n->encoder_tx_ms)>=LINK_ENCODER_PERIOD_MS) {
            f.type=LINK_ENCODERS; f.session=n->token; f.length=20;
            f.payload[0]=n->encoder_valid_mask;
            if(n->encoder_valid_mask) {
                link_put_u16(f.payload+2,(uint16_t)(now-n->encoder_sample_ms));
                for(i=0;i<4;i++) link_put_u32(f.payload+4+4*i,(uint32_t)n->encoder_counts[i]);
            }
        } else if(n->connected && n->speed_supported && (uint32_t)(now-n->speed_tx_ms)>=100U) {
            f.type=LINK_SPEED; f.session=n->token; f.length=20;
            f.payload[0]=n->speed_ready_mask; f.payload[1]=n->speed_fault; f.payload[2]=n->speed_valid_mask;
            if(n->speed_valid_mask) {
                f.payload[3]=(uint8_t)((now-n->speed_sample_ms)/10U);
                for(i=0;i<4;i++) { link_put_u16(f.payload+4+2*i,(uint16_t)n->speed_measured[i]);
                    link_put_u16(f.payload+12+2*i,(uint16_t)n->speed_targets[i]); }
            }
        } else {
            return 0;
        }
    } else if(!n->connected || !n->token) {
        if((uint32_t)(now-n->last_tx_ms)<LINK_HELLO_PERIOD_MS) return 0;
        f.type=LINK_HELLO; f.session=n->boot_id;
    } else {
        if(n->stop_pending || (n->pwm_stop_latched &&
           (uint32_t)(now-n->last_tx_ms)>=LINK_COMMAND_PERIOD_MS)) {
            f.type=LINK_PWM; f.session=n->token; f.length=12;
            link_put_u16(f.payload,LINK_COMMAND_MAX_TTL_MS); /* enable=0, targets=0 */
        } else if((uint32_t)(now-n->last_tx_ms)>=LINK_COMMAND_PERIOD_MS) {
            f.type=LINK_CONTROL; f.session=n->token; f.length=12;
            link_put_u16(f.payload,LINK_COMMAND_MAX_TTL_MS);
            if(n->input_valid) {
                uint32_t remaining=n->input_ttl_ms-(uint32_t)(now-n->input_ms);
                f.type=n->input_mode;
                link_put_u16(f.payload,(uint16_t)remaining); f.payload[2]=n->input_enable;
                for(i=0;i<4;i++) link_put_u16(f.payload+4+2*i,
                    (uint16_t)(n->input_mode==LINK_PWM?n->input_pwm[i]:n->input_mm_s[i]));
            }
            /* No input or expired input => disabled, zero command heartbeat. */
        } else if(n->config_pending) {
            f.type=n->config_wheel==255?LINK_CLEAR_FAULT:LINK_SPEED_CONFIG; f.session=n->token;
            if(n->config_wheel!=255) { f.length=20; memcpy(f.payload,n->config_payload,20); }
        } else if(n->led_pending) {
            f.type=LINK_LED_SET; f.session=n->token; f.length=1;
            f.payload[0]=n->led_expected_on;
        } else return 0;
    }
    f.sequence=n->tx_sequence;
    length=link_encode(&f,out,cap);
    if(length) {
        n->tx_sequence++;
        if(f.type==LINK_PWM && !f.payload[2]) n->stop_pending=0;
        if(f.type==LINK_ENCODERS) n->encoder_tx_ms=now;
        else if(f.type==LINK_SPEED) n->speed_tx_ms=now;
        else if(f.type==LINK_CONFIG_ACK) n->config_ack_pending=0;
        else if(f.type==LINK_SPEED_CONFIG || f.type==LINK_CLEAR_FAULT) {
            n->config_pending=0; n->config_waiting=1; n->config_sequence=f.sequence;
        }
        else if(f.type==LINK_LED_SET) {
            n->led_pending=0; n->led_waiting_ack=1;
            n->led_request_sequence=f.sequence;
        } else if(f.type==LINK_LED_ACK) n->led_ack_pending=0;
        else n->last_tx_ms=now;
        if(f.type==LINK_HELLO_ACK) n->ack_pending=0;
    }
    return length;
}
