#ifndef CRTC_ENCODER_ORIGIN_H
#define CRTC_ENCODER_ORIGIN_H
#include <stdint.h>
#include <limits.h>
typedef struct { uint32_t raw[4]; } encoder_origin_t;
static inline void encoder_origin_set(encoder_origin_t *origin,const int32_t raw[4])
{ unsigned i; for(i=0;i<4;i++) origin->raw[i]=(uint32_t)raw[i]; }
static inline void encoder_origin_apply(const encoder_origin_t *origin,int32_t values[4])
{
    unsigned i; for(i=0;i<4;i++) {
        uint32_t value=(uint32_t)values[i]-origin->raw[i];
        values[i]=value<=INT32_MAX?(int32_t)value:-(int32_t)(UINT32_MAX-value)-1;
    }
}
#endif
