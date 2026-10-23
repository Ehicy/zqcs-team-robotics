#include "device_settings.h"
#include "board_link.h"
#include <string.h>
static uint32_t crc32(const uint8_t *p,size_t size)
{
    uint32_t crc=0xffffffffUL; size_t i; unsigned b;
    for(i=0;i<size;i++) { crc^=p[i]; for(b=0;b<8;b++) crc=(crc>>1)^((crc&1U)?0xedb88320UL:0U); }
    return crc^0xffffffffUL;
}
int device_settings_valid(const device_settings_t *s)
{
    unsigned i;
    if(!s || !s->revision || s->kp>20000 || s->ki>20000 || s->kd>20000 || !(s->kp||s->ki)) return 0;
    for(i=0;i<4;i++) {
        const uint8_t *p=s->wheels[i];
        if(p[0]!=i || p[1]>7 || !link_get_u16(p+2) || link_get_u16(p+2)>5000 ||
           !link_get_u32(p+4) || link_get_u32(p+4)>1000000 || link_get_u16(p+8)!=s->kp ||
           link_get_u16(p+10)!=s->ki || !link_get_u16(p+12) || link_get_u16(p+12)>1000 ||
           !link_get_u16(p+14) || link_get_u16(p+14)>5000 || link_get_u32(p+16)) return 0;
    }
    for(i=0;i<3;i++) if(s->servo_flags[i]>3 || (i!=2 && (s->servo_flags[i]&2)) ||
        s->servo_min[i]<LINK_SERVO_MIN_US(i) || s->servo_max[i]>LINK_SERVO_MAX_US(i) ||
        s->servo_min[i]>=s->servo_max[i] || s->servo_reference[i]<s->servo_min[i] ||
        s->servo_reference[i]>s->servo_max[i]) return 0;
    return 1;
}
int device_settings_encode(const device_settings_t *s,uint8_t out[DEVICE_SETTINGS_BYTES])
{
    unsigned i;
    if(!out || !device_settings_valid(s)) return 0;
    memset(out,0,DEVICE_SETTINGS_BYTES); memcpy(out,"CRT5",4);
    link_put_u16(out+4,1); link_put_u16(out+6,s->revision);
    memcpy(out+8,s->wheels,80);
    link_put_u16(out+88,s->kp); link_put_u16(out+90,s->ki); link_put_u16(out+92,s->kd);
    for(i=0;i<3;i++) {
        link_put_u16(out+94+6*i,s->servo_min[i]); link_put_u16(out+96+6*i,s->servo_max[i]);
        link_put_u16(out+98+6*i,s->servo_reference[i]); out[112+i]=s->servo_flags[i];
    }
    link_put_u32(out+124,crc32(out,124)); return 1;
}
int device_settings_decode(device_settings_t *s,const uint8_t *bytes,size_t size)
{
    device_settings_t candidate; unsigned i;
    if(!s || !bytes || size!=DEVICE_SETTINGS_BYTES || memcmp(bytes,"CRT5",4) ||
       link_get_u16(bytes+4)!=1 || link_get_u32(bytes+124)!=crc32(bytes,124)) return 0;
    for(i=115;i<124;i++) if(bytes[i]) return 0;
    memset(&candidate,0,sizeof(candidate)); candidate.revision=link_get_u16(bytes+6);
    memcpy(candidate.wheels,bytes+8,80); candidate.kp=link_get_u16(bytes+88);
    candidate.ki=link_get_u16(bytes+90); candidate.kd=link_get_u16(bytes+92);
    for(i=0;i<3;i++) {
        candidate.servo_min[i]=link_get_u16(bytes+94+6*i); candidate.servo_max[i]=link_get_u16(bytes+96+6*i);
        candidate.servo_reference[i]=link_get_u16(bytes+98+6*i); candidate.servo_flags[i]=bytes[112+i];
    }
    if(!device_settings_valid(&candidate)) return 0;
    *s=candidate; return 1;
}
