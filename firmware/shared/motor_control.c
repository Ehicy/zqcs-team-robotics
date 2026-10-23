#include "motor_control.h"
#include <string.h>

static int8_t direction(int16_t value)
{ return value>0 ? 1 : (value<0 ? -1 : 0); }

static void coast_wheel(motor_control_t *s, unsigned wheel, uint32_t now)
{
    if(s->output_permille[wheel]!=0) {
        s->last_direction[wheel]=direction(s->output_permille[wheel]);
        s->zero_since_ms[wheel]=now;
        s->output_permille[wheel]=0;
    }
}

static void stop_all(motor_control_t *s, uint32_t now)
{
    unsigned wheel;
    for(wheel=0;wheel<MOTOR_WHEEL_COUNT;wheel++) coast_wheel(s,wheel,now);
    s->active=0;
}

void motor_control_init(motor_control_t *s, uint32_t now)
{
    unsigned wheel;
    memset(s,0,sizeof(*s)); s->last_update_ms=now;
    for(wheel=0;wheel<MOTOR_WHEEL_COUNT;wheel++) s->zero_since_ms[wheel]=now;
}

static int16_t approach(int16_t current, int16_t target, uint32_t elapsed_ms)
{
    int32_t step=(int32_t)(elapsed_ms*MOTOR_SLEW_PER_SECOND/1000U);
    int32_t distance=(int32_t)target-(int32_t)current;
    if(distance>step) return (int16_t)((int32_t)current+step);
    if(distance<-step) return (int16_t)((int32_t)current-step);
    return target;
}

int motor_control_update(motor_control_t *s, uint32_t now, const int16_t target[4],
                         uint8_t enabled, uint32_t command_ms, uint16_t ttl_ms)
{
    uint32_t elapsed=now-s->last_update_ms;
    unsigned wheel;
    s->last_update_ms=now;
    /* Explicit stop also acknowledges a watchdog fault, even after a long gap.
     * A subsequent enabled update must carry a fresh valid command lease. */
    if(enabled==0) { stop_all(s,now); s->fault=0; return 0; }
#ifdef CRTC_PA_CHASSIS
    /* A delayed owner tick continues from current PWM; do not accumulate a
     * huge ramp jump or require a manual fault-clear cycle. */
    s->fault=0;
    if(elapsed>MOTOR_MAX_UPDATE_GAP_MS) elapsed=MOTOR_MAX_UPDATE_GAP_MS;
#else
    if(elapsed>MOTOR_MAX_UPDATE_GAP_MS) s->fault=1;
#endif
    if(s->fault || enabled!=1 || !target || !ttl_ms ||
       ttl_ms>MOTOR_COMMAND_MAX_TTL_MS || (uint32_t)(now-command_ms)>=ttl_ms) {
        stop_all(s,now); return 0;
    }
    for(wheel=0;wheel<MOTOR_WHEEL_COUNT;wheel++) {
        if(target[wheel]>MOTOR_MAX_PERMILLE || target[wheel]<-MOTOR_MAX_PERMILLE) {
            stop_all(s,now); return 0;
        }
    }
    s->active=0;
    for(wheel=0;wheel<MOTOR_WHEEL_COUNT;wheel++) {
        int16_t current=s->output_permille[wheel], next;
        int8_t desired_direction=direction(target[wheel]);
        uint32_t ramp_ms=elapsed;
        if(!desired_direction) {
            coast_wheel(s,wheel,now); continue;
        }
        if(current!=0 && direction(current)!=desired_direction) {
            next=approach(current,0,ramp_ms);
            if(next==0) coast_wheel(s,wheel,now);
            else s->output_permille[wheel]=next;
        } else {
            if(current==0 && s->last_direction[wheel]!=0 &&
               s->last_direction[wheel]!=desired_direction) {
                uint32_t zero_age=now-s->zero_since_ms[wheel];
                if(zero_age<=MOTOR_REVERSE_HOLD_MS) continue;
                /* Use only time AFTER the hold, never time spent waiting. */
                if(ramp_ms>zero_age-MOTOR_REVERSE_HOLD_MS)
                    ramp_ms=zero_age-MOTOR_REVERSE_HOLD_MS;
            }
            next=approach(current,target[wheel],ramp_ms);
            s->output_permille[wheel]=next;
            if(next!=0) s->last_direction[wheel]=direction(next);
        }
        if(s->output_permille[wheel]!=0) s->active=1;
    }
    return 1;
}
