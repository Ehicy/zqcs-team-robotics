#ifndef CRTC_MANUAL_FEEDBACK_H
#define CRTC_MANUAL_FEEDBACK_H
#include "speed_control.h"
#include <string.h>
/* Passive per-wheel telemetry. It never enables, inhibits, or modifies PWM. */
typedef struct {
    uint32_t previous[4], sample_ms;
    int16_t measured[4];
    uint8_t valid_mask, window_mask;
} manual_feedback_t;
static inline void manual_feedback_init(manual_feedback_t *s,uint32_t now,const int32_t counts[4])
{
    unsigned i; memset(s,0,sizeof(*s)); s->sample_ms=now; s->window_mask=15;
    for(i=0;i<4;i++) s->previous[i]=(uint32_t)counts[i];
}
static inline void manual_feedback_sample(manual_feedback_t *s,uint32_t now,
    const int32_t counts[4],uint8_t mask,const speed_config_t config[4])
{
    uint32_t dt=now-s->sample_ms; unsigned i;
    s->window_mask&=mask;
    if(dt<SPEED_SAMPLE_MS) return;
    s->valid_mask=0;
    for(i=0;i<4;i++) {
        const speed_config_t *c=&config[i];
        uint32_t raw=(uint32_t)counts[i]-s->previous[i];
        int64_t delta=raw<=INT32_MAX?(int64_t)raw:(int64_t)raw-4294967296LL;
        int64_t value=0;
        s->previous[i]=(uint32_t)counts[i]; s->measured[i]=0;
        if(!(s->window_mask&(1U<<i)) || dt>100U || !c->counts_per_rev) continue;
        value=delta*c->encoder_sign*(int64_t)c->diameter_tenth_mm*314159LL/
              ((int64_t)c->counts_per_rev*1000LL*dt);
        if(value<INT16_MIN || value>INT16_MAX || value>(int64_t)c->max_mm_s*4+200 ||
           value<-((int64_t)c->max_mm_s*4+200)) continue;
        s->measured[i]=(int16_t)value; s->valid_mask|=(uint8_t)(1U<<i);
    }
    s->sample_ms=now; s->window_mask=15;
}
#endif
