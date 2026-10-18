#include "servo_motion.h"
#include <string.h>
void servo_motion_reference(servo_motion_t *s, uint16_t pulse, uint32_t now)
{
    memset(s,0,sizeof(*s)); s->pulse=s->target=pulse;
    s->position_milli=(int32_t)pulse*1000; s->tick_ms=s->renewed_ms=now;
}
void servo_motion_freeze(servo_motion_t *s)
{
    s->target=s->pulse; s->position_milli=(int32_t)s->pulse*1000;
    s->velocity_milli=0; s->moving=0;
}
void servo_motion_target(servo_motion_t *s, uint16_t target, uint32_t received_ms)
{
    s->target=target; s->renewed_ms=received_ms;
    s->moving=(uint8_t)(s->position_milli!=(int32_t)target*1000 || s->velocity_milli!=0);
}
void servo_motion_tick(servo_motion_t *s, uint32_t now)
{
    uint32_t dt=now-s->tick_ms; int32_t error,desired,v,change,old;
    int64_t stopping;
    if(!s->moving) { s->tick_ms=now; return; }
    if(dt>SERVO_MOTION_MAX_GAP_MS || now-s->renewed_ms>=SERVO_MOTION_LEASE_MS) {
        servo_motion_freeze(s); s->tick_ms=now; return;
    }
    if(dt<10U) return;
    s->tick_ms=now; error=(int32_t)s->target*1000-s->position_milli;
    old=s->velocity_milli;
    stopping=(int64_t)old*old/(2L*SERVO_MOTION_ACCEL_US_S2*1000L);
    desired=error>0 ? (int32_t)SERVO_MOTION_SPEED_US_S*1000 : -(int32_t)SERVO_MOTION_SPEED_US_S*1000;
    if((error>=0 && old>0 && stopping>=error) ||
       (error<=0 && old<0 && stopping>=-(int64_t)error)) desired=0;
    change=(int32_t)(SERVO_MOTION_ACCEL_US_S2*dt);
    v=old; if(v<desired) { v+=change; if(v>desired) v=desired; }
    else if(v>desired) { v-=change; if(v<desired) v=desired; }
    s->position_milli+=(int32_t)((int64_t)(old+v)*dt/2000);
    s->velocity_milli=v;
    if((error>=0 && s->position_milli>=(int32_t)s->target*1000 && v>=0) ||
       (error<=0 && s->position_milli<=(int32_t)s->target*1000 && v<=0)) {
        s->position_milli=(int32_t)s->target*1000; s->velocity_milli=0; s->moving=0;
    }
    s->pulse=(uint16_t)((s->position_milli+500)/1000);
}
