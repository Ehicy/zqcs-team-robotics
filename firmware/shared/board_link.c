#include "board_link.h"
#include <string.h>

void link_put_u16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
void link_put_u32(uint8_t *p, uint32_t v)
{ unsigned i; for(i=0;i<4;i++) p[i]=(uint8_t)(v>>(8U*i)); }
uint16_t link_get_u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
uint32_t link_get_u32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
int link_sequence_newer(uint32_t a, uint32_t b)
{ uint32_t delta=a-b; return delta!=0U && delta<0x80000000UL; }
uint16_t link_crc16(const uint8_t *data, size_t n)
{
    uint16_t crc=0xffffU; unsigned bit;
    while(n--) {
        crc^=(uint16_t)((uint16_t)*data++<<8);
        for(bit=0;bit<8;bit++) crc=(uint16_t)((crc&0x8000U)?
            ((uint32_t)crc<<1)^0x1021U:((uint32_t)crc<<1));
    }
    return crc;
}

/* COBS and leading/trailing zero delimiters: an incomplete previous frame is
 * discarded at the next delimiter. No heap, packed structs or native endian. */
size_t link_encode(const link_frame_t *f, uint8_t *out, size_t cap)
{
    uint8_t body[LINK_BODY_MAX], code=1; size_t n,i,pos=2,code_pos=1;
    uint16_t crc;
    if(!f || !out || f->length>LINK_MAX_PAYLOAD) return 0;
    n=LINK_HEADER_SIZE+f->length;
    if(cap<n+5U) return 0;
    body[0]=LINK_VERSION; body[1]=f->type; body[2]=f->length; body[3]=0;
    link_put_u32(body+4,f->sequence); link_put_u32(body+8,f->session);
    memcpy(body+12,f->payload,f->length);
    crc=link_crc16(body,n); link_put_u16(body+n,crc); n+=2;
    out[0]=0;
    for(i=0;i<n;i++) {
        if(body[i]==0) { out[code_pos]=code; code_pos=pos++; code=1; }
        else { out[pos++]=body[i]; code++; }
    }
    out[code_pos]=code; out[pos++]=0; return pos;
}
void link_parser_init(link_parser_t *p) { memset(p,0,sizeof(*p)); }
int link_parser_feed(link_parser_t *p, uint8_t b, uint32_t now, link_frame_t *f)
{
    uint8_t body[LINK_BODY_MAX]; size_t read=0,write=0,count,i;
    if(p->used && (uint32_t)(now-p->last_byte_ms)>=LINK_FRAME_TIMEOUT_MS) {
        p->used=0; p->discard=1; p->errors++;
    }
    p->last_byte_ms=now;
    if(b) {
        if(!p->discard) {
            if(p->used<sizeof(p->bytes)) p->bytes[p->used++]=b;
            else { p->discard=1; p->used=0; p->errors++; }
        }
        return 0;
    }
    if(p->discard) { p->discard=0; p->used=0; return 0; }
    if(!p->used) return 0;
    count=p->used; p->used=0;
    while(read<count) {
        uint8_t code=p->bytes[read++];
        if(!code || read+code-1U>count) goto bad;
        for(i=1;i<code;i++) {
            if(write>=sizeof(body)) goto bad;
            body[write++]=p->bytes[read++];
        }
        if(code!=255U && read<count) {
            if(write>=sizeof(body)) goto bad;
            body[write++]=0;
        }
    }
    if(write<LINK_HEADER_SIZE+2U || body[0]!=LINK_VERSION || body[3]!=0 ||
       body[2]>LINK_MAX_PAYLOAD || write!=LINK_HEADER_SIZE+body[2]+2U ||
       link_crc16(body,write-2U)!=link_get_u16(body+write-2U)) goto bad;
    f->type=body[1]; f->length=body[2]; f->sequence=link_get_u32(body+4);
    f->session=link_get_u32(body+8); memcpy(f->payload,body+12,f->length);
    return 1;
bad:
    p->errors++; return 0;
}
