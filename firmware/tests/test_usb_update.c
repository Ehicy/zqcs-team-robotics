#include "update_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[65536], app[UPDATE_CAPACITY], header[UPDATE_PAGE];
static unsigned operations, fail_at, feeds;
static bool erase(uint32_t a) {
    assert(a>=UPDATE_BASE && a<UPDATE_END && a%UPDATE_PAGE==0);
    if(++operations==fail_at) return false;
    memset(flash+a-0x08000000,255,UPDATE_PAGE); return true;
}
static bool write(uint32_t a,uint32_t w) {
    assert(a>=UPDATE_BASE && a<=UPDATE_END-4 && a%4==0);
    if(++operations==fail_at) return false;
    for(unsigned i=0;i<4;i++) {
        unsigned at=a-0x08000000+i;
        assert((flash[at] & (uint8_t)(w>>(8*i)))==(uint8_t)(w>>(8*i)));
        flash[at]=(uint8_t)(w>>(8*i));
    }
    return true;
}
static uint32_t read(uint32_t a) {
    assert(a>=UPDATE_BASE && a<=UPDATE_END-4);
    uint32_t w=0; for(unsigned i=0;i<4;i++) w|=(uint32_t)flash[a-0x08000000+i]<<(8*i);
    return w;
}
static void feed(void) { feeds++; }
static const update_io_t io={erase,write,read,feed};
static void put(uint8_t *p,uint32_t w) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(w>>(8*i)); }
static void protected(void) {
    for(unsigned i=0;i<0x2000;i++) assert(flash[i]==0xCC);
    for(unsigned i=0xF800;i<65536;i++) assert(flash[i]==0xA5);
}
static void setup(unsigned length) {
    memset(flash,0xCC,sizeof flash); memset(flash+0xF800,0xA5,2048);
    for(unsigned i=0;i<length;i++) app[i]=(uint8_t)(i*13+7);
    put(app,0x20005000); put(app+4,UPDATE_BASE+0x101);
    memset(header,255,sizeof header); memcpy(header,"CRTCDFU1",8);
    put(header+8,1); put(header+12,UPDATE_BASE); put(header+16,length);
    put(header+20,update_crc(UINT32_MAX,app,length)^UINT32_MAX);
    put(header+24,update_crc(UINT32_MAX,header,24)^UINT32_MAX);
    operations=fail_at=feeds=0;
}
static bool send(update_t *s,unsigned len) {
    if(!update_accept(s,&io,0,header,UPDATE_PAGE)) return false;
    for(unsigned off=0,block=1;off<len;off+=UPDATE_PAGE,block++) {
        unsigned n=len-off<UPDATE_PAGE?len-off:UPDATE_PAGE;
        if(!update_accept(s,&io,(uint16_t)block,app+off,(uint16_t)n)) return false;
        assert(!update_vectors_valid(read(UPDATE_BASE),read(UPDATE_BASE+4),UPDATE_CAPACITY));
    }
    return update_finish(s,&io,s->next_block);
}
int main(void) {
    update_t s;
    assert((update_crc(UINT32_MAX,(const uint8_t *)"123456789",9)^UINT32_MAX)==0xCBF43926);
    for(unsigned len=260;len<=UPDATE_CAPACITY;len+=(len==260?1792:UPDATE_CAPACITY-2052)) {
        setup(len); update_init(&s); assert(send(&s,len));
        assert(s.complete && feeds && !memcmp(flash+0x2000,app,len)); protected();
        if(len==UPDATE_CAPACITY) break;
    }
    setup(2052); update_init(&s); assert(send(&s,2052)); unsigned total=operations;
    for(unsigned cut=1;cut<=total;cut++) {
        setup(2052); fail_at=cut; update_init(&s); assert(!send(&s,2052));
        assert(s.failed && !s.complete); protected();
        /* Erase failure can leave old vectors; mock old page was deliberately invalid. */
        assert(!update_vectors_valid(read(UPDATE_BASE),read(UPDATE_BASE+4),UPDATE_CAPACITY));
    }
    setup(2052); update_init(&s); assert(!update_accept(&s,&io,1,header,1024)); assert(!operations);
    setup(2052); update_init(&s); header[0]^=1;
    assert(!update_accept(&s,&io,0,header,1024) && !operations);
    setup(2052); update_init(&s); put(header+16,UPDATE_CAPACITY+4);
    put(header+24,update_crc(UINT32_MAX,header,24)^UINT32_MAX);
    assert(!update_accept(&s,&io,0,header,1024) && !operations);
    setup(2052); update_init(&s); assert(update_accept(&s,&io,0,header,1024));
    assert(!update_accept(&s,&io,1,app,1025)); protected();
    setup(2052); update_init(&s); assert(update_accept(&s,&io,0,header,1024));
    assert(!update_accept(&s,&io,1,app,512));
    setup(2052); update_init(&s); assert(update_accept(&s,&io,0,header,1024));
    assert(update_accept(&s,&io,1,app,1024)); assert(!update_finish(&s,&io,2));
    setup(2052); app[2000]^=1; update_init(&s); assert(!send(&s,2052)); protected();
    setup(2052); update_init(&s); assert(update_accept(&s,&io,0,header,1024));
    for(unsigned off=0,b=1;off<2052;off+=1024,b++)
        assert(update_accept(&s,&io,(uint16_t)b,app+off,(uint16_t)(2052-off<1024?2052-off:1024)));
    flash[0x2400]^=1; assert(!update_finish(&s,&io,4)); protected();
    uint32_t invalid_sp[]={0x20000000,0x20005008,0x20004FFF,0xFFFFFFFF};
    for(unsigned i=0;i<4;i++) assert(!update_vectors_valid(invalid_sp[i],UPDATE_BASE+0x101,1024));
    assert(!update_vectors_valid(0x20005000,UPDATE_BASE+0x100,1024));
    assert(!update_vectors_valid(0x20005000,UPDATE_END+1,1024));
    assert(!update_vectors_valid(0x20005000,0x08000001,1024));
    printf("PASS USB update: CRC, 260/2052/55296B, %u injected flash failures, vectors-last, bounds/order/length/corruption, protected regions\n",total);
    return 0;
}
