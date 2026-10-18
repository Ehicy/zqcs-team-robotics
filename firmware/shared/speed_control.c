#include "speed_control.h"
#include <limits.h>
#include <string.h>

#define PID_SCALE 1000000LL

static int sign32(int32_t value) { return value>0 ? 1 : (value<0 ? -1 : 0); }
static int32_t abs32(int32_t value) { return value<0 ? -value : value; }
static int config_valid(const speed_config_t *c)
{
    return c && c->counts_per_rev && c->counts_per_rev<=1000000UL &&
        c->diameter_tenth_mm && c->diameter_tenth_mm<=5000U &&
        c->kp_milli<=20000U && c->ki_milli<=20000U && c->kd_milli<=20000U && (c->kp_milli || c->ki_milli) &&
        c->max_mm_s && c->max_mm_s<=1000U && c->accel_mm_s2 && c->accel_mm_s2<=5000U &&
        (c->motor_sign==1 || c->motor_sign==-1) &&
        (c->encoder_sign==1 || c->encoder_sign==-1) && c->confirmed<=1;
}
static void coast_wheel(speed_control_t *s, unsigned i, uint32_t now)
{
    if(s->ramp_milli[i] || s->pwm_permille[i]) {
        s->last_direction[i]=(int8_t)(s->ramp_milli[i] ? sign32(s->ramp_milli[i]) :
            sign32((int32_t)s->pwm_permille[i]*s->config[i].motor_sign));
        s->zero_since_ms[i]=now;
    }
    s->ramp_milli[i]=0; s->ramped_mm_s[i]=0; s->pwm_permille[i]=0;
    s->integral_micro[i]=0; s->tracking_stall[i]=0; s->tracking_direction[i]=0;
    s->derivative_milli[i]=0; s->derivative_valid[i]=0;
}
void speed_control_stop(speed_control_t *s, uint32_t now)
{
    unsigned i;
    for(i=0;i<SPEED_WHEELS;i++) coast_wheel(s,i,now);
    s->active=0;
}
static void latch_fault(speed_control_t *s, uint8_t fault, uint32_t now)
{
    if(s->fault==SPEED_FAULT_NONE) s->fault=fault;
    speed_control_stop(s,now);
}
static int physically_stopped(const speed_control_t *s)
{
    unsigned i;
    if(s->active || s->feedback_valid_mask!=15) return 0;
    for(i=0;i<SPEED_WHEELS;i++)
        if(s->pwm_permille[i] || s->ramp_milli[i] ||
           abs32(s->measured_mm_s[i])>SPEED_STOPPED_MM_S) return 0;
    return 1;
}
void speed_control_init(speed_control_t *s, uint32_t now, const int32_t counts[4])
{
    unsigned i;
    memset(s,0,sizeof(*s)); s->last_update_ms=s->last_sample_ms=now;
    s->feedback_valid_mask=counts ? 15 : 0;
    for(i=0;i<SPEED_WHEELS;i++) {
        speed_config_t *c=&s->config[i];
        c->counts_per_rev=(i==3)?884U:1768U;
        c->diameter_tenth_mm=650; c->kp_milli=1000; c->ki_milli=2000;
        c->max_mm_s=300; c->accel_mm_s2=500;
        c->motor_sign=c->encoder_sign=1;
        s->last_counts[i]=counts ? (uint32_t)counts[i] : 0;
        s->zero_since_ms[i]=now;
    }
    if(!counts) s->fault=SPEED_FAULT_ENCODER;
}
int speed_control_configure(speed_control_t *s, unsigned wheel, const speed_config_t *c)
{
    if(wheel>=SPEED_WHEELS || !config_valid(c) || !physically_stopped(s)) return 0;
    if(s->pid_initialized && (c->kp_milli!=s->config[wheel].kp_milli ||
       c->ki_milli!=s->config[wheel].ki_milli || c->kd_milli!=s->config[wheel].kd_milli)) return 0;
    s->config[wheel]=*c;
    s->integral_micro[wheel]=0; s->last_direction[wheel]=0;
    s->derivative_milli[wheel]=0; s->derivative_valid[wheel]=0;
    s->tracking_stall[wheel]=0; s->tracking_direction[wheel]=0;
    if(c->confirmed) s->ready_mask|=(uint8_t)(1U<<wheel);
    else s->ready_mask&=(uint8_t)~(1U<<wheel);
    return 1;
}
int speed_control_configure_pid(speed_control_t *s, uint16_t kp, uint16_t ki, uint16_t kd)
{
    unsigned i;
    if(kp>20000U || ki>20000U || kd>20000U || !(kp || ki) || !physically_stopped(s)) return 0;
    for(i=0;i<SPEED_WHEELS;i++) {
        s->config[i].kp_milli=kp; s->config[i].ki_milli=ki; s->config[i].kd_milli=kd;
        s->integral_micro[i]=0; s->derivative_milli[i]=0; s->derivative_valid[i]=0;
    }
    s->pid_initialized=1;
    return 1;
}
int speed_control_clear_fault(speed_control_t *s)
{
    if(!physically_stopped(s)) return 0;
    s->fault=SPEED_FAULT_NONE;
    s->fault_encoder_mask=0; s->fault_sample_ms=s->fault_sample_dt=0;
    memset(s->fault_raw_delta,0,sizeof(s->fault_raw_delta));
    return 1;
}

/* Rebase the local delta accumulator after a missing sample or timing fault.
 * This never changes the hardware's continuous encoder count. Require a new
 * valid speed sample before configuration/fault acknowledgement can proceed. */
static void rebase(speed_control_t *s, uint32_t now, const int32_t counts[4])
{
    unsigned i;
    if(counts) for(i=0;i<SPEED_WHEELS;i++) s->last_counts[i]=(uint32_t)counts[i];
    s->last_sample_ms=now; s->feedback_valid_mask=0;
}
static int sample_feedback(speed_control_t *s, uint32_t now, const int32_t counts[4],
                           uint32_t dt, int32_t deltas[4])
{
    unsigned i; uint8_t invalid=0; int32_t raw_deltas[4];
    for(i=0;i<SPEED_WHEELS;i++) {
        const speed_config_t *c=&s->config[i];
        uint32_t raw=(uint32_t)counts[i]-s->last_counts[i];
        int64_t delta=raw<=INT32_MAX ? (int64_t)raw : (int64_t)raw-4294967296LL;
        int64_t numerator, denominator, value;
        raw_deltas[i]=(int32_t)delta;
        s->last_counts[i]=(uint32_t)counts[i];
        delta*=c->encoder_sign;
        /* pi ~= 314159/100000; diameter unit is 0.1mm; dt is milliseconds.
         * Validated config bounds keep the worst INT32 delta within int64. */
        numerator=delta*(int64_t)c->diameter_tenth_mm*314159LL;
        denominator=(int64_t)c->counts_per_rev*1000LL*dt;
        value=numerator>=0 ? (numerator+denominator/2)/denominator :
                             -((-numerator+denominator/2)/denominator);
        /* Generous provisional physical plausibility envelope. It catches a
         * counter discontinuity before that value reaches the PID integrator. */
        if(value>(int64_t)c->max_mm_s*4+200 || value<-((int64_t)c->max_mm_s*4+200)) {
            invalid|=(uint8_t)(1U<<i); s->measured_mm_s[i]=0; deltas[i]=0;
        } else {
            s->measured_mm_s[i]=(int16_t)value;
            /* A valid velocity bounds delta well below INT32_MAX. */
            deltas[i]=(int32_t)delta;
        }
    }
    s->last_sample_ms=now; s->feedback_valid_mask=invalid ? 0 : 15;
    if(invalid) {
        if(!s->fault) {
            s->fault_encoder_mask=invalid; s->fault_sample_ms=now; s->fault_sample_dt=dt;
            memcpy(s->fault_raw_delta,raw_deltas,sizeof(raw_deltas));
        }
        latch_fault(s,SPEED_FAULT_ENCODER,now); return 0;
    }
    return 1;
}
static int32_t approach(int32_t value, int32_t target, int32_t step)
{
    if(target-value>step) return value+step;
    if(value-target>step) return value-step;
    return target;
}
static void update_ramp(speed_control_t *s, unsigned i, int16_t target,
                        uint32_t now, uint32_t dt)
{
    int32_t current=s->ramp_milli[i], wanted=(int32_t)target*1000;
    int desired=sign32(wanted);
    if(!desired) { coast_wheel(s,i,now); return; }
    if(current && sign32(current)!=desired) {
        s->ramp_milli[i]=approach(current,0,(int32_t)s->config[i].accel_mm_s2*(int32_t)dt);
        if(!s->ramp_milli[i]) {
            coast_wheel(s,i,now);
            s->last_direction[i]=(int8_t)sign32(current); s->zero_since_ms[i]=now;
        }
    } else {
        if(!current && s->last_direction[i] && s->last_direction[i]!=desired) {
            uint32_t age=now-s->zero_since_ms[i];
            if(age<=SPEED_REVERSE_HOLD_MS) return;
            if(dt>age-SPEED_REVERSE_HOLD_MS) dt=age-SPEED_REVERSE_HOLD_MS;
        }
        s->ramp_milli[i]=approach(current,wanted,(int32_t)s->config[i].accel_mm_s2*(int32_t)dt);
        if(s->ramp_milli[i]) s->last_direction[i]=(int8_t)sign32(s->ramp_milli[i]);
    }
    s->ramped_mm_s[i]=(int16_t)(s->ramp_milli[i]/1000);
}
static void update_pid(speed_control_t *s, unsigned i, uint32_t dt)
{
    int desired=sign32(s->ramp_milli[i]);
    int32_t error=(int32_t)s->ramped_mm_s[i]-s->measured_mm_s[i];
    int64_t low=desired>0 ? 0 : -SPEED_PWM_LIMIT*PID_SCALE;
    int64_t high=desired>0 ? SPEED_PWM_LIMIT*PID_SCALE : 0;
    int64_t p=(int64_t)s->config[i].kp_milli*error*1000LL;
    int64_t increment=(int64_t)s->config[i].ki_milli*error*dt;
    int64_t candidate=s->integral_micro[i]+increment, total, d=0;
    if(!desired) { s->pwm_permille[i]=0; s->integral_micro[i]=0; return; }
    /* D acts on measured speed, not the setpoint: no derivative kick on a
     * target change. Use actual sample dt and per-wheel filtered histories.
     * Bounds: raw derivative <= 840,000,000 milli-mm/s^2; int64 products
     * remain below 2^63 even at the maximum accepted gains/configuration. */
    if(s->derivative_valid[i]) {
        int64_t raw=-((int64_t)s->measured_mm_s[i]-s->derivative_measurement[i])*1000000LL/dt;
        s->derivative_milli[i]+=(int32_t)((raw-s->derivative_milli[i])*dt/(SPEED_D_FILTER_MS+dt));
        d=(int64_t)s->config[i].kd_milli*s->derivative_milli[i];
    }
    s->derivative_measurement[i]=s->measured_mm_s[i]; s->derivative_valid[i]=1;
    if(candidate>SPEED_PWM_LIMIT*PID_SCALE) candidate=SPEED_PWM_LIMIT*PID_SCALE;
    if(candidate<-SPEED_PWM_LIMIT*PID_SCALE) candidate=-SPEED_PWM_LIMIT*PID_SCALE;
    total=p+d+candidate;
    /* Conditional integration: only integrate within the actuator interval or
     * while unwinding saturation. The interval forbids reverse braking before
     * the ramp has crossed zero and completed its required coast interval. */
    if((total>=low && total<=high) || (total>high && increment<0) ||
       (total<low && increment>0)) s->integral_micro[i]=candidate;
    total=p+d+s->integral_micro[i];
    if(total>high) total=high;
    if(total<low) total=low;
    s->pwm_permille[i]=(int16_t)((total/PID_SCALE)*s->config[i].motor_sign);
}
static int diagnose_wheel(speed_control_t *s, unsigned i, int32_t delta, uint32_t now)
{
    int pwm=abs32(s->pwm_permille[i]);
    /* Even a low-speed request must not integrate forever against a missing
     * encoder or a motor that never starts. Every nonzero drive gets a lease
     * on real count progress; there is no low-target exemption. */
    if(!pwm || !s->ramp_milli[i]) {
        s->tracking_stall[i]=s->tracking_direction[i]=0; return 1;
    }
    if(!s->tracking_stall[i] || delta!=0) {
        s->tracking_stall[i]=1; s->progress_ms[i]=now;
    } else if((uint32_t)(now-s->progress_ms[i])>=SPEED_STALL_MS) {
        latch_fault(s,SPEED_FAULT_STALL,now); return 0;
    }
    if(delta!=0 && sign32(delta)!=sign32(s->ramp_milli[i]) &&
       abs32(s->measured_mm_s[i])>=10) {
        if(!s->tracking_direction[i]) {
            s->tracking_direction[i]=1; s->direction_ms[i]=now;
        } else if((uint32_t)(now-s->direction_ms[i])>=SPEED_DIRECTION_MS) {
            latch_fault(s,SPEED_FAULT_DIRECTION,now); return 0;
        }
    } else s->tracking_direction[i]=0;
    return 1;
}
int speed_control_update(speed_control_t *s, uint32_t now, const int32_t counts[4],
                         uint8_t valid_mask, const int16_t targets[4], uint8_t enabled,
                         uint32_t command_ms, uint16_t ttl_ms)
{
    uint32_t elapsed=now-s->last_update_ms, dt=now-s->last_sample_ms;
    int32_t deltas[4]={0,0,0,0}; unsigned i; int sampled=0;
    s->last_update_ms=now;
    if(elapsed>SPEED_MAX_UPDATE_GAP_MS || dt>SPEED_MAX_UPDATE_GAP_MS) {
        rebase(s,now,counts); latch_fault(s,SPEED_FAULT_TIMING,now); return 0;
    }
    if(!counts || valid_mask!=15) {
        if(!s->fault) {
            s->fault_encoder_mask=counts ? (uint8_t)(15U&~valid_mask) : 15U;
            s->fault_sample_ms=now; s->fault_sample_dt=dt;
            memset(s->fault_raw_delta,0,sizeof(s->fault_raw_delta));
        }
        rebase(s,now,counts); latch_fault(s,SPEED_FAULT_ENCODER,now); return 0;
    }
    for(i=0;i<SPEED_WHEELS;i++) if(!config_valid(&s->config[i])) {
        latch_fault(s,SPEED_FAULT_UNCALIBRATED,now); return 0;
    }
    if(dt>=SPEED_SAMPLE_MS) {
        sampled=1;
        if(!sample_feedback(s,now,counts,dt,deltas)) return 0;
    }
    if(s->fault) { speed_control_stop(s,now); return 0; }
    if(enabled!=1 || !targets || !ttl_ms || ttl_ms>SPEED_MAX_TTL_MS ||
       (uint32_t)(now-command_ms)>=ttl_ms) {
        speed_control_stop(s,now); return 0;
    }
    if(s->ready_mask!=15 || !s->pid_initialized) { latch_fault(s,SPEED_FAULT_UNCALIBRATED,now); return 0; }
    for(i=0;i<SPEED_WHEELS;i++) if(targets[i]>s->config[i].max_mm_s ||
            targets[i]<-(int32_t)s->config[i].max_mm_s) {
        speed_control_stop(s,now); return 0;
    }
    s->active=0;
    for(i=0;i<SPEED_WHEELS;i++) {
        if(!targets[i]) coast_wheel(s,i,now);
        else if(sampled) {
            update_ramp(s,i,targets[i],now,dt);
            update_pid(s,i,dt);
            if(!diagnose_wheel(s,i,deltas[i],now)) return 0;
        }
        if(s->pwm_permille[i]) s->active=1;
    }
    return 1;
}
