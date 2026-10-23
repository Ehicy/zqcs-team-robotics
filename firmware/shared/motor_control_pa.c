#include "motor_control.h"
#ifdef CRTC_PA_CHASSIS
#include <string.h>

static int direction(int value) { return (value>0)-(value<0); }
static int magnitude(int value) { return value<0?-value:value; }
static int braking(uint8_t phase) { return phase==MOTOR_PA_COAST || phase==MOTOR_PA_BRAKE; }
static void idle_wheel(motor_control_t *s,unsigned i,uint32_t now)
{
    if(s->output_permille[i]) s->zero_since_ms[i]=now;
    s->output_permille[i]=s->origin[i]=s->goal[i]=0;
    s->phase[i]=MOTOR_PA_IDLE;
}
void motor_control_init(motor_control_t *s,uint32_t now)
{
    unsigned i; memset(s,0,sizeof(*s)); s->last_update_ms=now;
    for(i=0;i<4;i++) s->zero_since_ms[i]=now;
}
void motor_control_abort(motor_control_t *s,uint32_t now)
{
    unsigned i;
    for(i=0;i<4;i++) idle_wheel(s,i,now);
    s->active=s->fault=s->braking_mask=s->short_brake_mask=s->operator_braking=0;
    s->last_update_ms=now;
}
void motor_control_set_feedback(motor_control_t *s,const int16_t velocity[4],uint8_t mask)
{
    s->feedback_mask=velocity?(uint8_t)(mask&15U):0;
    if(velocity) memcpy(s->feedback_physical_mm_s,velocity,sizeof(s->feedback_physical_mm_s));
}
static void begin_brake(motor_control_t *s,unsigned i,int16_t actual,uint32_t now,uint16_t lease)
{
    if(braking(s->phase[i])) return; /* Repeated STOP cannot renew the window. */
    if(!actual || lease<=MOTOR_BRAKE_COAST_MS) { idle_wheel(s,i,now); return; }
    s->brake_direction[i]=(int8_t)direction(actual);
    s->brake_pwm[i]=(int16_t)(magnitude(actual)<MOTOR_BRAKE_MAX_PERMILLE?magnitude(actual):MOTOR_BRAKE_MAX_PERMILLE);
    s->brake_start_ms[i]=now; s->brake_lease_ms[i]=lease;
    s->phase[i]=MOTOR_PA_COAST; s->output_permille[i]=0;
    s->zero_since_ms[i]=now;
}
void motor_control_quick_stop(motor_control_t *s,uint32_t now,const int16_t actual[4],uint16_t remaining)
{
    unsigned i;
    if(!actual || !remaining || remaining>MOTOR_BRAKE_COAST_MS+MOTOR_BRAKE_WINDOW_MS) {
        motor_control_abort(s,now); return;
    }
    for(i=0;i<4;i++) begin_brake(s,i,actual[i],now,remaining);
    s->operator_braking=1;
}
static void update_brake(motor_control_t *s,unsigned i,uint32_t now)
{
    uint32_t age=now-s->brake_start_ms[i];
    int velocity=s->feedback_physical_mm_s[i];
    s->output_permille[i]=0;
    if(age>=s->brake_lease_ms[i]) { idle_wheel(s,i,now); return; }
    if(s->feedback_mask&(1U<<i)) {
        if(magnitude(velocity)>5 && direction(velocity)!=s->brake_direction[i]) {
            idle_wheel(s,i,now); return; /* Feedback crossed zero: never push it further. */
        }
    }
    s->braking_mask|=(uint8_t)(1U<<i);
    if(age<MOTOR_BRAKE_COAST_MS) return;
    s->phase[i]=MOTOR_PA_BRAKE;
    if(!(s->feedback_mask&(1U<<i)) || magnitude(velocity)<=5) {
        s->short_brake_mask|=(uint8_t)(1U<<i);
    } else {
        s->output_permille[i]=(int16_t)(-s->brake_direction[i]*s->brake_pwm[i]);
    }
}
static int16_t approach(int16_t value,int16_t target,int step)
{
    int delta=(int)target-value;
    return (int16_t)(value+(delta>step?step:delta<-step?-step:delta));
}
static void drive_wheel(motor_control_t *s,unsigned i,int16_t target,uint32_t now,uint32_t dt)
{
    int current=s->output_permille[i];
    if(braking(s->phase[i])) { update_brake(s,i,now); return; }
    if(current && (!target || direction(current)!=direction(target))) {
        begin_brake(s,i,(int16_t)current,now,MOTOR_BRAKE_COAST_MS+MOTOR_BRAKE_WINDOW_MS);
        update_brake(s,i,now); return;
    }
    if(!target) { idle_wheel(s,i,now); return; }
    if(s->phase[i]==MOTOR_PA_IDLE) {
        /* Retain a brief off interval even across a forced coast/session change. */
        if(s->last_direction[i] && s->last_direction[i]!=direction(target) &&
           (uint32_t)(now-s->zero_since_ms[i])<MOTOR_BRAKE_COAST_MS) return;
        s->ramp_start_ms[i]=now; s->origin[i]=(int16_t)current; s->phase[i]=MOTOR_PA_RAMP;
    }
    s->goal[i]=target;
    if(s->phase[i]==MOTOR_PA_RAMP) {
        uint32_t age=now-s->ramp_start_ms[i],x,w;
        int16_t candidate;
        if(age>=MOTOR_START_CURVE_MS) { age=MOTOR_START_CURVE_MS; s->phase[i]=MOTOR_PA_RUN; }
        x=age*1000U/MOTOR_START_CURVE_MS;
        w=(uint32_t)((int64_t)x*x*x*(10000000LL-15000LL*x+6LL*x*x)/1000000000000LL);
        candidate=(int16_t)(s->origin[i]+(int32_t)(target-s->origin[i])*(int32_t)w/1000);
        s->output_permille[i]=approach((int16_t)current,candidate,(int)(dt*20U));
    } else s->output_permille[i]=approach((int16_t)current,target,(int)(dt*5U));
    if(s->output_permille[i]) s->last_direction[i]=(int8_t)direction(s->output_permille[i]);
}
int motor_control_update(motor_control_t *s,uint32_t now,const int16_t target[4],
                         uint8_t enabled,uint32_t command_ms,uint16_t ttl)
{
    unsigned i; uint32_t dt=now-s->last_update_ms;
    int current=enabled==1 && target && ttl && ttl<=MOTOR_COMMAND_MAX_TTL_MS &&
        (uint32_t)(now-command_ms)<ttl;
    s->last_update_ms=now; s->fault=0;
    if(enabled>1) { motor_control_abort(s,now); return 0; }
    if(enabled && !current) { motor_control_abort(s,now); return 0; }
    if(current) for(i=0;i<4;i++) if(target[i]>MOTOR_MAX_PERMILLE || target[i]<-MOTOR_MAX_PERMILLE) {
        motor_control_abort(s,now); return 0;
    }
    if(!current && !s->operator_braking) { motor_control_abort(s,now); return 0; }
    if(dt>MOTOR_MAX_UPDATE_GAP_MS) dt=MOTOR_MAX_UPDATE_GAP_MS;
    s->active=s->braking_mask=s->short_brake_mask=0;
    for(i=0;i<4;i++) {
        if(current) drive_wheel(s,i,target[i],now,dt);
        else if(braking(s->phase[i])) update_brake(s,i,now);
        else idle_wheel(s,i,now);
        if(s->output_permille[i] || (s->short_brake_mask&(1U<<i))) s->active=1;
    }
    if(!s->braking_mask) s->operator_braking=0;
    return current?1:0;
}
#endif
