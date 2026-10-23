#include "remote_bridge.h"
#include <string.h>

static void stop_uart(remote_bridge_t *b,uint32_t now)
{
#ifdef CRTC_C3_DASHBOARD
    const int16_t zero[4]={0,0,0,0};
    link_node_t *n=b->link;
    if(!link_node_submit_pwm(n,n->have_input_sequence?n->input_sequence+1U:1U,
                            zero,0,200,now)) link_node_cancel_input(n);
#else
    link_node_request_stop(b->link);
#endif
    b->link->last_tx_ms=now-LINK_COMMAND_PERIOD_MS;
}

static void freeze_servos(remote_bridge_t *b)
{
    unsigned i;
    for(i=0;i<LINK_SERVO_COUNT;i++) servo_motion_freeze(&b->servos[i]);
}
static int servo_moving(const remote_bridge_t *b)
{
    unsigned i; for(i=0;i<LINK_SERVO_COUNT;i++) if(b->servos[i].moving) return 1;
    return 0;
}
static int servo_standstill(const remote_bridge_t *b,uint32_t now)
{
    const link_node_t *n=b->link; unsigned i;
#ifdef CRTC_C3_DASHBOARD
    (void)now; (void)i;
    return n->connected && !n->input_enable && !b->control.active && !b->control.armed;
#else
    if(!n->connected || n->remote_speed_fault || n->remote_speed_valid_mask!=15 ||
       now-n->remote_speed_rx_ms>=200U || n->input_enable || b->control.armed || b->control.active ||
       !(n->remote_flags&LINK_FLAG_OUTPUT_INHIBITED)) return 0;
    for(i=0;i<4;i++) if(n->remote_speed_measured[i] || n->remote_speed_targets[i]) return 0;
    return 1;
#endif
}
static void tick_servos(remote_bridge_t *b,uint32_t now)
{
    unsigned i;
    if(!servo_standstill(b,now)) { freeze_servos(b); return; }
    for(i=0;i<LINK_SERVO_COUNT;i++) {
        servo_motion_t candidate=b->servos[i];
        if(b->servo_error_mask & (1U<<i)) { servo_motion_freeze(&b->servos[i]); continue; }
        servo_motion_tick(&candidate,now);
#ifdef CRTC_C3_DASHBOARD
        if(candidate.pulse && (candidate.pulse<b->settings.servo_min[i] || candidate.pulse>b->settings.servo_max[i])) {
            candidate=b->servos[i]; servo_motion_freeze(&candidate); /* Never jump to a newly changed limit. */
        }
#endif
        if(candidate.pulse<LINK_SERVO_MIN_US(i) && candidate.pulse) {
            candidate.pulse=LINK_SERVO_MIN_US(i); servo_motion_freeze(&candidate);
        }
        if(candidate.pulse>LINK_SERVO_MAX_US(i)) {
            candidate.pulse=LINK_SERVO_MAX_US(i); servo_motion_freeze(&candidate);
        }
        if(candidate.pulse!=b->servo_us[i]) {
            if(!b->servo_write || !b->servo_write((uint8_t)i,candidate.pulse)) {
                b->servo_error_mask|=(uint8_t)(1U<<i); servo_motion_freeze(&b->servos[i]); continue;
            }
            b->servo_us[i]=candidate.pulse;
        }
        b->servos[i]=candidate;
    }
}

static uint16_t speed_limit(const remote_bridge_t *b)
{
    unsigned i; uint16_t limit=REMOTE_CONTROL_TARGET_LIMIT;
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
void remote_bridge_get_settings(const remote_bridge_t *b,device_settings_t *out)
{
    *out=b->settings; memcpy(out->wheels,b->configs,80);
    out->kp=b->kp_milli; out->ki=b->ki_milli; out->kd=b->kd_milli;
}
int remote_bridge_load_settings(remote_bridge_t *b,const device_settings_t *s)
{
    if(!device_settings_valid(s)) return 0;
    b->settings=*s; memcpy(b->configs,s->wheels,80);
    b->kp_milli=s->kp; b->ki_milli=s->ki; b->kd_milli=s->kd;
    b->sync_step=0; b->synced_revision=0; b->pid_initialized=0; return 1;
}
#ifdef CRTC_C3_DASHBOARD
static void settings_ack(remote_bridge_t *b,uint32_t sequence,uint8_t wheel,uint8_t result)
{
    memset(b->ack,0,20); b->ack[0]=1; b->ack[1]=3; b->ack[2]=wheel; b->ack[3]=result;
    link_put_u32(b->ack+4,b->control.session); link_put_u32(b->ack+8,sequence); b->ack_pending=1;
}
static uint8_t commit_settings(remote_bridge_t *b,device_settings_t *candidate)
{
    device_settings_t current; uint8_t before[DEVICE_SETTINGS_BYTES],after[DEVICE_SETTINGS_BYTES];
    unsigned i;
    remote_bridge_get_settings(b,&current); candidate->revision=current.revision;
    for(i=0;i<4;i++) { link_put_u16(candidate->wheels[i]+8,candidate->kp); link_put_u16(candidate->wheels[i]+10,candidate->ki); }
    if(!device_settings_encode(candidate,after)) return 2;
    if(device_settings_encode(&current,before) && !memcmp(before,after,sizeof(before))) return 0;
    if(++candidate->revision==0) candidate->revision=1;
    if(b->save_settings && !b->save_settings(candidate)) return 7; /* No RAM/STM change on Flash failure. */
    if(!remote_bridge_load_settings(b,candidate)) return 2;
    if(b->save_settings) b->settings_loaded=1;
    return 0;
}
static int receive_settings(remote_bridge_t *b,const remote_action_t *a)
{
    device_settings_t candidate; uint8_t wheel=a->wheel,result=0;
    if(a->kind!=REMOTE_ACTION_MECHANICAL && a->kind!=REMOTE_ACTION_TUNING &&
       a->kind!=REMOTE_ACTION_PID && a->kind!=REMOTE_ACTION_SERVO_SETTINGS) return 0;
    remote_bridge_get_settings(b,&candidate);
    if(a->kind==REMOTE_ACTION_PID) {
        wheel=253; candidate.kp=a->kp_milli; candidate.ki=a->ki_milli; candidate.kd=a->kd_milli;
    } else if(a->kind==REMOTE_ACTION_MECHANICAL) {
        uint8_t *p=candidate.wheels[wheel]; p[1]=a->flags;
        link_put_u16(p+2,a->diameter_tenth_mm); link_put_u32(p+4,a->counts_per_rev);
    } else if(a->kind==REMOTE_ACTION_TUNING) {
        if(a->kp_milli!=candidate.kp || a->ki_milli!=candidate.ki) result=2;
        link_put_u16(candidate.wheels[wheel]+12,a->max_mm_s);
    } else {
        unsigned ch=a->wheel; wheel=254;
        candidate.servo_min[ch]=a->diameter_tenth_mm; candidate.servo_max[ch]=a->max_mm_s;
        candidate.servo_reference[ch]=a->servo_reference_us; candidate.servo_flags[ch]=a->flags;
        if(b->servo_us[ch] && (b->servo_us[ch]<candidate.servo_min[ch] || b->servo_us[ch]>candidate.servo_max[ch])) result=2;
        if(b->servos[ch].target && (b->servos[ch].target<candidate.servo_min[ch] || b->servos[ch].target>candidate.servo_max[ch])) result=2;
    }
    if(!result) result=commit_settings(b,&candidate);
    settings_ack(b,a->source_sequence,wheel,result); return 1;
}
static void synchronize_settings(remote_bridge_t *b,uint32_t now)
{
    link_node_t *n=b->link;
    if(b->config_pending && b->config_initializing) {
        if(n->remote_config_valid) {
            int same=b->syncing_revision==b->settings.revision;
            if(!n->config_result && same) {
                if(b->sync_step==0) b->pid_initialized=1;
                if(++b->sync_step>=5) b->synced_revision=b->settings.revision;
            } else { b->sync_step=0; b->pid_init_failed=1; }
            n->remote_config_valid=0; b->config_pending=b->config_initializing=0;
        } else if((uint32_t)(now-b->config_ms)>=600U || (!n->config_pending && !n->config_waiting)) {
            b->config_pending=b->config_initializing=0; b->sync_step=0; b->pid_init_failed=1;
        }
    }
    if(b->synced_revision==b->settings.revision || b->config_pending || b->ack_pending ||
       !n->connected || !(n->remote_flags&LINK_FLAG_SPEED_SUPPORTED) || b->control.active || b->control.armed ||
       (b->pid_init_failed && (uint32_t)(now-b->config_ms)<500U)) return;
    b->syncing_revision=b->settings.revision;
    if(b->sync_step==0) {
        if(!link_node_request_pid(n,b->kp_milli,b->ki_milli,b->kd_milli,now)) return;
        b->config_wheel=253;
    } else {
        unsigned wheel=b->sync_step-1U;
        if(wheel>=4 || !link_node_request_config(n,b->configs[wheel],0,now)) return;
        b->config_wheel=(uint8_t)wheel;
    }
    b->config_pending=b->config_initializing=1; b->config_ms=now; b->pid_init_failed=0;
}
#endif
void remote_bridge_init(remote_bridge_t *b,link_node_t *link,uint32_t session)
{
    unsigned i; memset(b,0,sizeof(*b)); b->link=link;
    remote_control_init(&b->control,session); b->uart_token=link->token;
#ifdef CRTC_C3_DASHBOARD
    b->manual_pwm=1;
#endif
    b->kp_milli=LINK_PID_KP_MILLI; b->ki_milli=LINK_PID_KI_MILLI;
    b->kd_milli=LINK_PID_KD_MILLI; b->phase=CHASSIS_WAIT_LINK;
    for(i=0;i<4;i++) {
        uint8_t *p=b->configs[i]; p[0]=(uint8_t)i;
        link_put_u16(p+2,650);
#ifdef CRTC_C3_DASHBOARD
        { const int8_t signs[4]=LINK_DEFAULT_MOTOR_SIGNS; p[1]=(uint8_t)((signs[i]<0?1U:0U)|((i&1U)?0U:2U)); }
        link_put_u32(p+4,i>=2?884:1768);
#else
        link_put_u32(p+4,i==3?884:1768);
#endif
        link_put_u16(p+8,b->kp_milli); link_put_u16(p+10,b->ki_milli);
        link_put_u16(p+12,300); link_put_u16(p+14,500);
    }
    b->settings.revision=1;
    for(i=0;i<3;i++) {
        b->settings.servo_min[i]=LINK_SERVO_MIN_US(i); b->settings.servo_max[i]=LINK_SERVO_MAX_US(i);
        b->settings.servo_reference[i]=1500;
    }
    remote_bridge_get_settings(b,&b->settings);
}
void remote_bridge_stop(remote_bridge_t *b,uint8_t reason,uint32_t now)
{
    freeze_servos(b);
    remote_control_reset(&b->control,reason);
    stop_uart(b,now);
    if(b->config_pending) { if(!b->config_initializing) acknowledge(b,3); b->config_pending=0; b->config_initializing=0; }
}
#ifndef CRTC_C3_DASHBOARD
static void finish_pid(remote_bridge_t *b)
{
    unsigned i;
    b->kp_milli=link_get_u16(b->pending_config);
    b->ki_milli=link_get_u16(b->pending_config+2);
    b->kd_milli=link_get_u16(b->pending_config+4);
    b->pid_initialized=1; b->pid_init_failed=0;
    for(i=0;i<4;i++) {
        link_put_u16(b->configs[i]+8,b->kp_milli);
        link_put_u16(b->configs[i]+10,b->ki_milli);
    }
}
#endif
void remote_bridge_tick(remote_bridge_t *b,uint32_t now)
{
    link_node_t *n=b->link; uint8_t had_motion,ready;
    link_node_tick(n,now);
    if(b->uart_token!=n->token) {
        unsigned i;
        freeze_servos(b);
        b->uart_token=n->token; remote_control_reset(&b->control,REMOTE_REASON_LINK);
        link_node_cancel_input(n);
#ifndef CRTC_C3_DASHBOARD
        for(i=0;i<4;i++) b->configs[i][1]&=(uint8_t)~4U;
#else
        (void)i; b->synced_revision=0; b->sync_step=0;
#endif
        b->pid_initialized=b->pid_init_failed=0;
        if(b->config_pending) {
            if(!b->config_initializing) acknowledge(b,3);
            b->config_pending=b->config_initializing=0;
        }
    }
#ifdef CRTC_C3_DASHBOARD
    synchronize_settings(b,now);
    if(b->config_pending && !b->config_initializing) {
        if(n->remote_config_valid) {
            if(b->config_wheel==252 && !n->config_result) {
                memset(n->remote_encoder_counts,0,sizeof(n->remote_encoder_counts));
                memset(n->remote_speed_measured,0,sizeof(n->remote_speed_measured));
                memset(n->remote_speed_targets,0,sizeof(n->remote_speed_targets));
                n->remote_encoder_rx_ms=n->remote_speed_rx_ms=now;
            }
            acknowledge(b,n->config_result); n->remote_config_valid=0; b->config_pending=0;
        } else if((uint32_t)(now-b->config_ms)>=600U) { acknowledge(b,3); b->config_pending=0; }
    }
#else
    if(b->config_pending) {
        if(n->remote_config_valid) {
            if(!n->config_result) {
                if(b->config_wheel==253) finish_pid(b);
                else if(b->config_wheel<4) memcpy(b->configs[b->config_wheel],b->pending_config,20);
            } else if(b->config_initializing) b->pid_init_failed=1;
            if(!b->config_initializing) acknowledge(b,n->config_result);
            b->config_pending=b->config_initializing=0; n->remote_config_valid=0;
        } else if((uint32_t)(now-b->config_ms)>=600U || (!n->config_pending && !n->config_waiting)) {
            if(b->config_initializing) b->pid_init_failed=1;
            else acknowledge(b,3);
            b->config_pending=b->config_initializing=0;
        }
    }
#endif
    /* Asynchronous coefficient synchronization, never automatic ARM or physical
     * confirmation. Legacy targets try once; C3 retries while idle at >=500ms.
     * The PA STM32 already has usable presets during a failed handshake. */
#ifndef CRTC_C3_DASHBOARD
    if(!b->pid_initialized &&
#ifdef CRTC_C3_DASHBOARD
       (!b->pid_init_failed || (uint32_t)(now-b->config_ms)>=500U) &&
#else
       !b->pid_init_failed &&
#endif
       !b->config_pending && !b->ack_pending &&
       n->connected && (n->remote_flags&LINK_FLAG_SPEED_SUPPORTED) && !b->control.armed && !b->control.active) {
        if(link_node_request_pid(n,b->kp_milli,b->ki_milli,b->kd_milli,now)) {
            link_put_u16(b->pending_config,b->kp_milli);
            link_put_u16(b->pending_config+2,b->ki_milli);
            link_put_u16(b->pending_config+4,b->kd_milli);
            b->config_wheel=253; b->config_pending=b->config_initializing=1; b->config_ms=now;
        }
    }
#endif
    ready=(uint8_t)(b->pid_initialized && n->connected && !n->pwm_stop_latched &&
        (n->remote_flags&LINK_FLAG_SPEED_SUPPORTED) && n->remote_speed_valid_mask==15 &&
        n->remote_speed_ready_mask==15 && n->remote_speed_fault==0 &&
        !(n->input_enable && n->input_mode==LINK_PWM));
#ifdef CRTC_C3_DASHBOARD
    /* Adaptive PA firmware uses feedforward when an encoder is absent.
     * A sensor-valid mask is telemetry, not a control-readiness latch. */
    ready=(uint8_t)(b->pid_initialized && n->connected && !n->pwm_stop_latched &&
        (n->remote_flags&LINK_FLAG_SPEED_SUPPORTED) && n->remote_speed_ready_mask==15 &&
        n->remote_speed_fault==0 && !(n->input_enable && n->input_mode==LINK_PWM));
    if(b->manual_pwm) ready=(uint8_t)(n->connected && !n->pwm_stop_latched &&
        (n->remote_flags&LINK_FLAG_PWM_SUPPORTED));
#endif
    had_motion=b->control.armed || b->control.active;
    if(remote_control_tick(&b->control,now,n->connected,ready) && had_motion) {
        stop_uart(b,now);
    }
    tick_servos(b,now);
    b->phase=chassis_state_update(n->connected,b->pid_initialized,
        ready,!b->manual_pwm && n->remote_speed_fault!=0,b->control.armed,
        b->control.active && (b->control.left_mm_s || b->control.right_mm_s));
}
int remote_bridge_receive(remote_bridge_t *b,const uint8_t *p,size_t size,uint32_t arrival,uint32_t now)
{
    remote_action_t a; link_node_t *n=b->link; int accepted;
    uint8_t fresh_stop=(uint8_t)(p && size==20 && p[1]==REMOTE_OPCODE_STOP &&
        (uint32_t)(now-arrival)<REMOTE_CONTROL_MAX_AGE_MS &&
        (!b->control.have_sequence || link_sequence_newer(link_get_u32(p+8),b->control.last_sequence)));
    remote_bridge_tick(b,now);
    if(p && size==20 && p[1]==REMOTE_OPCODE_ARM &&
       (b->config_pending || b->ack_pending || servo_moving(b) || b->phase!=CHASSIS_LOCKED)) return 0;
    if(p && size==20 && p[1]==REMOTE_OPCODE_DRIVE &&
       b->phase!=CHASSIS_ARMED && b->phase!=CHASSIS_RUNNING) return 0;
    accepted=remote_control_receive(&b->control,p,size,arrival,now,&a);
#ifdef CRTC_C3_DASHBOARD
    if(accepted && receive_settings(b,&a)) { remote_bridge_tick(b,now); return 1; }
#endif
    if(a.kind==REMOTE_ACTION_STOP) {
        freeze_servos(b);
        if(!accepted || !fresh_stop || b->control.reason!=REMOTE_REASON_STOP ||
           !link_node_request_quick_stop(n,now)) stop_uart(b,now);
        if(b->config_pending) { if(!b->config_initializing) acknowledge(b,3); b->config_pending=0; b->config_initializing=0; }
    } else if(a.kind==REMOTE_ACTION_PWM) {
        unsigned i;
        b->manual_pwm=1;
        /* The protocol keeps physical PWM distinct from mm/s. Encoder/PID
         * readiness never gates this explicit manual command. */
        for(i=0;i<4;i++) if(b->configs[i][1]&1U) a.targets[i]=(int16_t)-a.targets[i];
        if(!link_node_submit_pwm(n,n->have_input_sequence?n->input_sequence+1U:1U,
                                a.targets,1,a.ttl_ms,now)) {
            remote_bridge_stop(b,REMOTE_REASON_LINK,now); return 0;
        }
        n->last_tx_ms=now-LINK_COMMAND_PERIOD_MS;
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
        b->manual_pwm=0;
        /* ARM must never emit STOP: STOP rotates UART token and revokes ARM. */
        link_node_cancel_input(n);
    } else if(a.kind==REMOTE_ACTION_LED) {
        (void)link_node_request_led(n,a.led_on,now);
    } else if(a.kind==REMOTE_ACTION_SERVO) {
        uint8_t result=0, ch=a.wheel;
        if(b->config_pending || b->ack_pending) {
            remote_bridge_stop(b,REMOTE_REASON_QUEUE,now); return 0;
        }
        b->config_wheel=254; b->config_session=b->control.session;
        b->config_source_sequence=a.source_sequence;
        if(!b->servo_write) result=4;
        if(a.servo_us) {
            if(!(LINK_SERVO_POSITION_MASK & (1U<<ch))) result=5;
            if(!servo_standstill(b,now)) result=1;
            /* Old clients cannot initiate an abrupt 1500us takeover. */
            if(!a.flags) result=6;
            if(b->servo_error_mask & (1U<<ch)) result=4;
            if(a.flags && !b->servo_us[ch] && !a.servo_reference_us) result=2;
            /* A reference is allowed only while PWM is released. */
            if(a.flags && b->servo_us[ch] && a.servo_reference_us) result=2;
            if(a.servo_us<b->settings.servo_min[ch] || a.servo_us>b->settings.servo_max[ch] ||
               (a.servo_reference_us && (a.servo_reference_us<b->settings.servo_min[ch] ||
                                        a.servo_reference_us>b->settings.servo_max[ch]))) result=2;
        }
        if(!result) {
            if(!a.servo_us || !b->servo_us[ch]) {
                uint16_t first=a.servo_us?a.servo_reference_us:0;
                if(!b->servo_write(ch,first)) {
                    result=4; b->servo_error_mask|=(uint8_t)(1U<<ch);
                    servo_motion_freeze(&b->servos[ch]);
                }
                else {
                    b->servo_us[ch]=first; servo_motion_reference(&b->servos[ch],first,now);
                    b->servo_error_mask&=(uint8_t)~(1U<<ch);
                }
            }
            if(!result && a.servo_us) servo_motion_target(&b->servos[ch],a.servo_us,arrival);
        }
        acknowledge(b,result);
    } else if(a.kind==REMOTE_ACTION_ENCODER_RESET) {
        if(b->config_pending || b->ack_pending) return 0;
        b->config_wheel=252; b->config_session=b->control.session;
        b->config_source_sequence=a.source_sequence; b->config_ms=now;
        if(link_node_request_encoder_reset(n,now)) b->config_pending=1;
        else acknowledge(b,1);
    } else if(a.kind==REMOTE_ACTION_PID) {
        if(b->config_pending || b->ack_pending) {
            remote_bridge_stop(b,REMOTE_REASON_QUEUE,now); return 0;
        }
        b->config_wheel=253; b->config_session=b->control.session;
        b->config_source_sequence=a.source_sequence; b->config_ms=now;
        link_put_u16(b->pending_config,a.kp_milli);
        link_put_u16(b->pending_config+2,a.ki_milli);
        link_put_u16(b->pending_config+4,a.kd_milli);
        b->config_initializing=0;
        if(link_node_request_pid(n,a.kp_milli,a.ki_milli,a.kd_milli,now)) b->config_pending=1;
        else acknowledge(b,1);
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
                /* Legacy per-wheel tuning may set the speed cap, but cannot
                 * create different PID coefficients for individual wheels. */
                if(a.kp_milli!=b->kp_milli || a.ki_milli!=b->ki_milli) { acknowledge(b,2); return 1; }
                link_put_u16(b->pending_config+8,b->kp_milli); link_put_u16(b->pending_config+10,b->ki_milli);
                link_put_u16(b->pending_config+12,a.max_mm_s);
            }
        }
        if(link_node_request_config(n,b->pending_config,b->config_wheel==255,now)) b->config_pending=1;
        else acknowledge(b,1);
    }
    remote_bridge_tick(b,now);
    return accepted;
}
void remote_bridge_status(remote_bridge_t *b,uint8_t kind,uint8_t p[20])
{
    link_node_t *n=b->link; unsigned i;
    if(b->ack_pending) { memcpy(p,b->ack,20); b->ack_pending=0; return; }
    memset(p,0,20); p[0]=1; p[1]=kind;
    if(kind==7) {
        unsigned wheel=b->settings_wheel; const uint8_t *cfg=b->configs[wheel];
        p[2]=(uint8_t)wheel; p[3]=cfg[1]; link_put_u32(p+4,b->control.session);
        link_put_u32(p+8,link_get_u32(cfg+4)); link_put_u16(p+12,link_get_u16(cfg+2));
        link_put_u16(p+14,link_get_u16(cfg+12)); link_put_u16(p+16,link_get_u16(cfg+14));
        link_put_u16(p+18,b->settings.revision); b->settings_wheel=(uint8_t)((wheel+1U)%4U);
    } else if(kind==8) {
        p[2]=(uint8_t)((b->settings_loaded?1U:0U)|(b->synced_revision==b->settings.revision?2U:0U));
        link_put_u32(p+4,b->control.session); link_put_u16(p+8,b->kp_milli);
        link_put_u16(p+10,b->ki_milli); link_put_u16(p+12,b->kd_milli);
        link_put_u16(p+14,200); link_put_u16(p+16,50); link_put_u16(p+18,b->settings.revision);
    } else if(kind==9) {
        unsigned ch=b->settings_servo; p[2]=(uint8_t)ch; p[3]=b->settings.servo_flags[ch];
        link_put_u32(p+4,b->control.session); link_put_u16(p+8,b->settings.servo_min[ch]);
        link_put_u16(p+10,b->settings.servo_max[ch]); link_put_u16(p+12,b->settings.servo_reference[ch]);
        link_put_u16(p+14,(b->settings.servo_flags[2]&2U)?b->settings.servo_max[2]:b->settings.servo_min[2]);
        link_put_u16(p+16,(b->settings.servo_flags[2]&2U)?b->settings.servo_min[2]:b->settings.servo_max[2]);
        link_put_u16(p+18,b->settings.revision); b->settings_servo=(uint8_t)((ch+1U)%3U);
    } else if(kind==5) {
        for(i=0;i<LINK_SERVO_COUNT;i++) {
            if(b->servo_us[i]) p[2]|=(uint8_t)(1U<<i);
            link_put_u16(p+8+2*i,b->servo_us[i]);
        }
        p[3]=b->servo_write?(0x80U|1U|(LINK_SERVO_POSITION_MASK<<1)):0;
#ifdef CRTC_C3_DASHBOARD
        if(b->servo_write) p[3]|=0x10U;
        if(n->remote_flags&LINK_FLAG_QUICK_STOP_SUPPORTED) p[3]|=0x20U;
        if(n->remote_flags&LINK_FLAG_ENCODER_RESET_SUPPORTED) p[3]|=0x40U;
#endif
        link_put_u32(p+4,b->control.session);
        link_put_u16(p+14,1000); link_put_u16(p+16,2000); link_put_u16(p+18,25);
    } else if(kind==6) {
        for(i=0;i<LINK_SERVO_COUNT;i++) {
            if(b->servos[i].moving) p[2]|=(uint8_t)(1U<<i);
            link_put_u16(p+8+2*i,b->servos[i].target);
        }
        p[3]=b->servo_error_mask; link_put_u32(p+4,b->control.session);
        link_put_u16(p+14,SERVO_MOTION_SPEED_US_S);
        link_put_u16(p+16,SERVO_MOTION_ACCEL_US_S2);
        link_put_u16(p+18,SERVO_MOTION_LEASE_MS);
    } else if(kind==4) { /* Legacy two-servo status retained for old host tools. */
        p[2]=(b->servo_us[0]?1:0)|(b->servo_us[1]?2:0);
        p[3]=b->servo_write?(1U|((LINK_SERVO_POSITION_MASK&3U)<<1)):0;
        link_put_u32(p+4,b->control.session);
        link_put_u16(p+8,b->servo_us[0]); link_put_u16(p+10,b->servo_us[1]);
        link_put_u16(p+12,1300); link_put_u16(p+14,1700);
        link_put_u16(p+16,25); link_put_u16(p+18,100);
    } else if(kind==1) {
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
            (b->control.drive_ready?8:0)|(!b->manual_pwm && n->remote_speed_fault?16:0)|(b->pid_initialized?32:0);
#ifdef CRTC_C3_DASHBOARD
        p[2]|=64U; /* Explicit direct-PWM opcode supported. */
#endif
        p[3]=(uint8_t)(((unsigned)b->phase<<4)|(b->control.reason&15U)); link_put_u32(p+4,b->control.session);
        link_put_u32(p+8,b->control.have_sequence?b->control.last_sequence:0);
        if(n->remote_speed_valid_mask==15) {
            link_put_u16(p+12,(uint16_t)(((int32_t)n->remote_speed_measured[1]+n->remote_speed_measured[3])/2));
            link_put_u16(p+14,(uint16_t)(((int32_t)n->remote_speed_measured[0]+n->remote_speed_measured[2])/2));
        }
        link_put_u16(p+16,speed_limit(b)); p[18]=n->remote_speed_ready_mask; p[19]=b->manual_pwm?0:n->remote_speed_fault;
    }
}
