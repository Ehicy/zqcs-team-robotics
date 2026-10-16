#include "remote_control.h"
#include <string.h>

static uint16_t get_u16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t get_i16(const uint8_t *p)
{
    uint16_t raw=get_u16(p);
    return raw<32768U ? (int32_t)raw : (int32_t)raw-65536L;
}
static int sequence_newer(uint32_t current, uint32_t previous)
{
    uint32_t distance=current-previous;
    return distance!=0 && distance<0x80000000UL;
}
static int32_t magnitude(int32_t value) { return value<0 ? -value : value; }
static void clear_input(remote_control_t *s)
{
    s->active=0; s->left_mm_s=0; s->right_mm_s=0;
    s->received_ms=0; s->expires_ms=0; s->ttl_ms=0;
}
void remote_control_init(remote_control_t *s, uint32_t session)
{
    memset(s,0,sizeof(*s)); s->session=session ? session : 1;
    s->reason=REMOTE_REASON_LOCKED;
}
void remote_control_reset(remote_control_t *s, uint8_t reason)
{
    s->session+=0x9e3779b9UL;
    if(!s->session) s->session=1;
    s->armed=0; s->have_sequence=0; s->last_sequence=0;
    s->reason=reason; clear_input(s);
}
int remote_control_tick(remote_control_t *s, uint32_t now, uint8_t link_up)
{
    uint8_t was_up=s->link_up;
    s->link_up=link_up ? 1 : 0;
    if(!s->link_up && (was_up || s->armed || s->active)) {
        remote_control_reset(s,REMOTE_REASON_LINK); return 1;
    }
    if(s->active && (uint32_t)(now-s->received_ms)>=s->ttl_ms) {
        remote_control_reset(s,REMOTE_REASON_EXPIRED); return 1;
    }
    return 0;
}
static int reject(remote_control_t *s)
{ s->rejected++; return 0; }
static int stop_reject(remote_control_t *s, remote_action_t *a, uint8_t reason)
{
    remote_control_reset(s,reason); a->kind=REMOTE_ACTION_STOP;
    return reject(s);
}
int remote_control_receive(remote_control_t *s, const uint8_t *p, size_t length,
                           uint32_t received_ms, uint32_t now, remote_action_t *a)
{
    uint8_t opcode; uint32_t sequence, age; uint16_t limit, ttl;
    int32_t throttle, steer, left, right, scale; unsigned i;
    memset(a,0,sizeof(*a));
    if(remote_control_tick(s,now,s->link_up)) a->kind=REMOTE_ACTION_STOP;
    /* A truncated or foreign-session packet cannot rotate the live generation. */
    if(!p || length<8 || get_u32(p+4)!=s->session) return reject(s);
    if(length!=REMOTE_CONTROL_PACKET_SIZE || p[0]!=REMOTE_CONTROL_VERSION ||
       p[2]!=0 || p[3]!=0) return stop_reject(s,a,REMOTE_REASON_INVALID);
    opcode=p[1]; sequence=get_u32(p+8);
    throttle=get_i16(p+12); steer=get_i16(p+14);
    limit=get_u16(p+16); ttl=get_u16(p+18);
    switch(opcode) {
    case REMOTE_OPCODE_ARM:
    case REMOTE_OPCODE_STOP:
        for(i=12;i<REMOTE_CONTROL_PACKET_SIZE;i++)
            if(p[i]) return stop_reject(s,a,REMOTE_REASON_INVALID);
        break;
    case REMOTE_OPCODE_LED:
        if(throttle<0 || throttle>1 || steer || limit || ttl)
            return stop_reject(s,a,REMOTE_REASON_INVALID);
        break;
    case REMOTE_OPCODE_DRIVE:
        if(throttle<-1000 || throttle>1000 || steer<-1000 || steer>1000 ||
           limit>REMOTE_CONTROL_TARGET_LIMIT || ttl<REMOTE_CONTROL_MIN_TTL_MS ||
           ttl>REMOTE_CONTROL_MAX_TTL_MS)
            return stop_reject(s,a,REMOTE_REASON_INVALID);
        break;
    default:
        return stop_reject(s,a,REMOTE_REASON_INVALID);
    }
    /* Replayed events are harmless rejections, including old queued duplicates.
     * STOP remains a fail-safe even if its sequence was already consumed. */
    if(opcode!=REMOTE_OPCODE_STOP && s->have_sequence &&
       !sequence_newer(sequence,s->last_sequence)) return reject(s);
    age=now-received_ms;
    if(age>=REMOTE_CONTROL_MAX_AGE_MS ||
       (opcode==REMOTE_OPCODE_DRIVE && age>=ttl))
        return stop_reject(s,a,REMOTE_REASON_EXPIRED);
    if(opcode==REMOTE_OPCODE_STOP) {
        remote_control_reset(s,REMOTE_REASON_STOP); a->kind=REMOTE_ACTION_STOP;
        return 1;
    }
    if((opcode==REMOTE_OPCODE_ARM || opcode==REMOTE_OPCODE_DRIVE) && !s->link_up)
        return reject(s);
    if(opcode==REMOTE_OPCODE_DRIVE && !s->armed) return reject(s);
    s->have_sequence=1; s->last_sequence=sequence;
    if(opcode==REMOTE_OPCODE_ARM) {
        clear_input(s); s->armed=1; s->reason=REMOTE_REASON_READY;
        a->kind=REMOTE_ACTION_STOP; return 1;
    }
    if(opcode==REMOTE_OPCODE_LED) {
        a->kind=REMOTE_ACTION_LED; a->led_on=(uint8_t)throttle; return 1;
    }
    left=throttle+steer; right=throttle-steer; scale=1000;
    if(magnitude(left)>scale) scale=magnitude(left);
    if(magnitude(right)>scale) scale=magnitude(right);
    s->left_mm_s=(int16_t)(left*(int32_t)limit/scale);
    s->right_mm_s=(int16_t)(right*(int32_t)limit/scale);
    s->active=1; s->received_ms=received_ms; s->ttl_ms=ttl;
    s->expires_ms=received_ms+(uint32_t)ttl; s->reason=REMOTE_REASON_READY;
    a->kind=REMOTE_ACTION_DRIVE; a->ttl_ms=(uint16_t)(ttl-age);
    a->targets[0]=a->targets[2]=s->left_mm_s;
    a->targets[1]=a->targets[3]=s->right_mm_s;
    return 1;
}
