#include "board_link.h"
#include "motor_command.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void feed(link_node_t *n,const uint8_t *bytes,size_t count,uint32_t now)
{ size_t i; for(i=0;i<count;i++) link_node_receive(n,bytes[i],now); }
static size_t transfer(link_node_t *a,link_node_t *b,uint32_t now)
{
    uint8_t bytes[LINK_WIRE_MAX]; size_t count=link_node_transmit(a,now,bytes,sizeof(bytes));
    feed(b,bytes,count,now); return count;
}
static void connect(link_node_t *c,link_node_t *s,uint32_t now)
{
    link_node_init(c,LINK_CLIENT,0x10203040UL,now);
    link_node_init(s,LINK_CONTROLLER,0x50607080UL,now);
    assert(transfer(c,s,now)); assert(s->connected && !s->command_valid);
    assert(transfer(s,c,now)); assert(c->connected && c->token==s->token);
}
static link_frame_t control(uint32_t token,uint32_t sequence,uint16_t ttl)
{
    link_frame_t f; memset(&f,0,sizeof(f)); f.type=LINK_CONTROL; f.length=12;
    f.session=token; f.sequence=sequence; link_put_u16(f.payload,ttl);
    return f;
}
static void send_frame(link_node_t *n,const link_frame_t *f,uint32_t now)
{
    uint8_t bytes[LINK_WIRE_MAX]; size_t count=link_encode(f,bytes,sizeof(bytes));
    assert(count); feed(n,bytes,count,now);
}
/* Independent COBS helper for corrupt-header tests; CRC remains valid so the
 * version/length/reserved checks, rather than only CRC, have to reject them. */
static size_t raw_wire(uint8_t *body,size_t n,uint8_t *wire)
{
    size_t i,pos=2,at=1; uint8_t code=1;
    link_put_u16(body+n,link_crc16(body,n)); n+=2; wire[0]=0;
    for(i=0;i<n;i++) {
        if(body[i]) { wire[pos++]=body[i]; code++; }
        else { wire[at]=code; at=pos++; code=1; }
    }
    wire[at]=code; wire[pos++]=0; return pos;
}
static void test_codec(void)
{
    link_frame_t f={0}, decoded; link_parser_t p;
    uint8_t wire[LINK_WIRE_MAX], raw[LINK_BODY_MAX]; size_t n,i; unsigned hits;
    assert(link_crc16((const uint8_t *)"123456789",9)==0x29b1U);
    assert(link_sequence_newer(0,0xffffffffUL));
    assert(!link_sequence_newer(1,1)); assert(!link_sequence_newer(0,1));
    assert(!link_sequence_newer(0x80000000UL,0));
    f.type=LINK_STATUS; f.length=LINK_MAX_PAYLOAD;
    f.sequence=0x0000ff00; f.session=0x12345678;
    for(i=0;i<f.length;i++) f.payload[i]=(uint8_t)((i&1)?0:255);
    n=link_encode(&f,wire,sizeof(wire)); assert(n<=sizeof(wire));
    assert(!link_encode(&f,wire,n-1));
    link_parser_init(&p); hits=0;
    for(i=0;i<n;i++) hits+=(unsigned)link_parser_feed(&p,wire[i],(uint32_t)i,&decoded);
    assert(hits==1 && decoded.type==f.type && decoded.sequence==f.sequence &&
           decoded.session==f.session && !memcmp(decoded.payload,f.payload,f.length));
    /* Two coalesced frames; delimiter and arbitrary chunks do not matter. */
    for(i=0;i<n;i++) hits+=(unsigned)link_parser_feed(&p,wire[i],40,&decoded);
    assert(hits==2);
    wire[n-2]^=0x80;
    for(i=0;i<n;i++) assert(!link_parser_feed(&p,wire[i],41,&decoded));
    assert(p.errors);
    memset(raw,0,sizeof(raw)); raw[0]=LINK_VERSION+1; raw[1]=LINK_HELLO;
    n=raw_wire(raw,LINK_HEADER_SIZE,wire);
    for(i=0;i<n;i++) assert(!link_parser_feed(&p,wire[i],42,&decoded));
    raw[0]=LINK_VERSION; raw[2]=1; n=raw_wire(raw,LINK_HEADER_SIZE,wire);
    for(i=0;i<n;i++) assert(!link_parser_feed(&p,wire[i],43,&decoded));
    raw[2]=0; raw[3]=1; n=raw_wire(raw,LINK_HEADER_SIZE,wire);
    for(i=0;i<n;i++) assert(!link_parser_feed(&p,wire[i],44,&decoded));
    /* Oversized noise is bounded, followed by an intact frame. */
    for(i=0;i<500;i++) assert(!link_parser_feed(&p,1,45,&decoded));
    n=link_encode(&f,wire,sizeof(wire)); hits=0;
    for(i=0;i<n;i++) hits+=(unsigned)link_parser_feed(&p,wire[i],46,&decoded);
    assert(hits==1);
    puts("PASS codec: CRC golden vector, COBS, bounds, version/length/reserved, noise, coalescing");
}
static void test_frames_and_order(void)
{
    link_node_t c,s; link_frame_t f; uint8_t bytes[LINK_WIRE_MAX]; size_t n;
    uint32_t deadline,token;
    connect(&c,&s,0); f=control(s.token,1,150); n=link_encode(&f,bytes,sizeof(bytes));
    feed(&s,bytes,n/2,10); assert(!s.command_valid);
    feed(&s,bytes+n/2,n-n/2,20); assert(s.command_valid && s.command_ms==20);
    deadline=s.command_ms; send_frame(&s,&f,30); assert(s.command_ms==deadline);
    f.sequence=0; send_frame(&s,&f,40); assert(s.command_ms==deadline);
    f.sequence=2; f.payload[2]=2; send_frame(&s,&f,50); assert(s.command_ms==deadline);
    f.payload[2]=0; link_put_u16(f.payload,201); send_frame(&s,&f,60);
    assert(s.command_ms==deadline);
    link_put_u16(f.payload,150); f.type=99; send_frame(&s,&f,70);
    assert(s.command_ms==deadline);
    f.type=LINK_CONTROL; f.length=13; send_frame(&s,&f,80); assert(s.command_ms==deadline);
    f.length=12; f.session^=1; send_frame(&s,&f,90); assert(s.command_ms==deadline);
    f.session=s.token; f.payload[2]=1; link_put_u16(f.payload+4,1001);
    send_frame(&s,&f,100); assert(s.command_ms==deadline);
    link_put_u16(f.payload+4,0); f.payload[2]=0;
    bytes[n-2]^=1; feed(&s,bytes,n,110); assert(s.command_ms==deadline);
    token=s.token; f.sequence=3;
    /* A unique old-token command arriving exactly at expiry cannot revive it. */
    send_frame(&s,&f,170);
    assert(!s.command_valid && s.token!=token && s.reason==LINK_CONTROL_EXPIRED);
    assert(s.accepted_controls==1 && s.rejected_frames>=9);
    puts("PASS receiver: duplicate/out-of-order/invalid/old-token frames never refresh control deadline");
}
static void test_truncation_and_reconnect(void)
{
    link_node_t c,s; link_frame_t f; uint8_t bytes[LINK_WIRE_MAX]; size_t n;
    connect(&c,&s,0); f=control(s.token,1,100); n=link_encode(&f,bytes,sizeof(bytes));
    feed(&s,bytes,n/2,10); feed(&s,bytes+n/2,n-n/2,60);
    assert(!s.command_valid && s.parser.errors);
    send_frame(&s,&f,61); assert(s.command_valid);
    f.sequence=2; n=link_encode(&f,bytes,sizeof(bytes));
    feed(&s,bytes,n/2,70); send_frame(&s,&f,71); assert(s.accepted_controls==2);
    link_node_tick(&s,171); assert(!s.command_valid);
    c.tx_sequence=3; /* Two controls were injected directly above, using seq 1/2. */
    assert(transfer(&s,&c,171)); assert(!c.connected && !c.input_valid);
    assert(transfer(&c,&s,172)); assert(transfer(&s,&c,172)); assert(c.connected);
    assert(transfer(&c,&s,222)); assert(s.command_valid && !s.requested_enable);
    link_node_tick(&c,522); assert(!c.connected && !c.input_valid);
    link_node_tick(&s,522); assert(!s.connected && !s.command_valid);
    assert(transfer(&c,&s,523)); assert(transfer(&s,&c,523)); assert(c.connected);
    link_node_transport_error(&s,524); assert(!s.connected && !s.command_valid);
    assert(transfer(&s,&c,624)); assert(!c.connected);
    assert(transfer(&c,&s,625)); assert(transfer(&s,&c,625)); assert(c.connected);
    /* Client reboot => different boot id and new controller token. */
    link_node_init(&c,LINK_CLIENT,0x11223344,626);
    assert(transfer(&c,&s,626)); assert(transfer(&s,&c,626)); assert(c.token==s.token);
    puts("PASS truncation, timeout boundary, reconnect, transport fault, client restart");
}
static void test_input_lease_and_status(void)
{
    link_node_t c,s; int16_t targets[4]={100,-100,100,-100};
    link_frame_t f; uint8_t wire[LINK_WIRE_MAX]; size_t count,i; link_parser_t parser;
    connect(&c,&s,0);
    assert(!link_node_submit_input(&c,1,targets,1,201,10));
    assert(link_node_submit_input(&c,1,targets,1,150,10));
    assert(transfer(&c,&s,50)); assert(s.requested_enable && s.requested_mm_s[1]==-100);
    assert(s.command_ms+s.command_ttl_ms==160);
    assert(!link_node_submit_input(&c,1,targets,1,150,70)); /* replayed input event */
    assert(!link_node_submit_input(&c,0,targets,1,150,70));
    assert(transfer(&s,&c,100));
    assert(c.remote_flags&LINK_FLAG_OUTPUT_INHIBITED);
    assert(c.remote_flags&LINK_FLAG_COMMAND_VALID);
    assert(c.remote_last_control_sequence==s.last_control_sequence);
    assert(transfer(&c,&s,100)); assert(s.command_ms+s.command_ttl_ms==160);
    /* Re-sending a valid input does not reset its ORIGINAL lease. */
    assert(transfer(&c,&s,150)); assert(s.command_ms+s.command_ttl_ms==160);
    count=link_node_transmit(&c,160,wire,sizeof(wire)); assert(!count && !c.input_valid);
    count=link_node_transmit(&c,200,wire,sizeof(wire)); assert(count);
    link_parser_init(&parser); memset(&f,0,sizeof(f));
    for(i=0;i<count;i++) link_parser_feed(&parser,wire[i],200,&f);
    assert(f.type==LINK_CONTROL && !f.payload[2]);
    for(i=4;i<12;i++) assert(!f.payload[i]);
    feed(&s,wire,count,200); assert(!s.command_valid); /* epoch already expired */
    assert(transfer(&s,&c,200)); assert(!c.input_valid && !c.connected);
    puts("PASS input API: replay rejection, remaining TTL, no cached-input extension, disabled heartbeat/status");
}
static void test_wraparound_and_reboot(void)
{
    link_node_t c,s; link_frame_t f; uint32_t start=0xffffffc0UL,token;
    connect(&c,&s,start); f=control(s.token,0xffffffffUL,100);
    /* HELLO used seq 0: set the legitimate previous sequence near wrap. */
    s.peer_sequence=0xfffffffeUL; send_frame(&s,&f,start+10);
    assert(s.command_valid); f.sequence=0; send_frame(&s,&f,start+20);
    assert(s.accepted_controls==2);
    token=s.token; link_node_tick(&s,start+120); assert(!s.command_valid && s.token!=token);
    send_frame(&s,&f,start+121); assert(!s.command_valid);
    connect(&c,&s,0); transfer(&c,&s,50); transfer(&s,&c,100);
    link_node_init(&s,LINK_CONTROLLER,0x87654321,101);
    /* Controller restart may reset outgoing seq: client must time out first. */
    transfer(&s,&c,101); link_node_tick(&c,400); assert(!c.connected);
    assert(transfer(&c,&s,401)); assert(transfer(&s,&c,401)); assert(c.connected);
    assert(transfer(&c,&s,451)); assert(s.command_valid && !s.requested_enable);
    puts("PASS sequence/time wraparound and controller reboot recovery");
}
static void test_fuzz(void)
{
    link_parser_t p; link_frame_t f; uint32_t rng=42; unsigned i;
    link_parser_init(&p);
    for(i=0;i<100000;i++) {
        rng=rng*1664525UL+1013904223UL;
        (void)link_parser_feed(&p,(uint8_t)(rng>>24),i/100,&f);
    }
    assert(p.used<=sizeof(p.bytes));
    puts("PASS 100000 deterministic noise bytes (memory/UB sanitizer compatible)");
}
static void test_encoder_feedback(void)
{
    link_node_t c,s; int32_t counts[4]={123,-456,2147483647L,(-2147483647L-1L)};
    link_frame_t stale;
    connect(&c,&s,0); transfer(&c,&s,50); transfer(&s,&c,100);
    assert(transfer(&s,&c,101));
    assert(!c.remote_encoder_valid_mask); /* zero placeholders are explicitly unknown */
    assert(!link_node_publish_encoders(&s,counts,16,110));
    assert(!link_node_publish_encoders(&c,counts,15,110));
    assert(link_node_publish_encoders(&s,counts,15,110));
    transfer(&c,&s,150); transfer(&c,&s,200); transfer(&s,&c,200);
    assert(transfer(&s,&c,201)); assert(c.remote_encoder_valid_mask==15);
    assert(!memcmp(c.remote_encoder_counts,counts,sizeof(counts)));
    assert(c.remote_encoder_rx_ms==110); /* transmitted age preserved */
    memset(&stale,0,sizeof(stale)); stale.type=LINK_ENCODERS; stale.length=20;
    stale.session=s.token; stale.sequence=s.tx_sequence; stale.payload[0]=15;
    link_put_u16(stale.payload+2,200); send_frame(&c,&stale,202);
    assert(c.remote_encoder_rx_ms==110); /* stale sample cannot be refreshed */
    link_node_tick(&c,310); assert(!c.remote_encoder_valid_mask);
    link_node_tick(&s,310); assert(!s.encoder_valid_mask);
    puts("PASS encoder feedback interface: explicit validity, signed counts, sample age and expiry");
}
static link_frame_t transmit_frame(link_node_t *n,uint32_t now)
{
    uint8_t wire[LINK_WIRE_MAX]; link_parser_t parser; link_frame_t f={0};
    size_t count=link_node_transmit(n,now,wire,sizeof(wire)),i; unsigned hits=0;
    assert(count); link_parser_init(&parser);
    for(i=0;i<count;i++) hits+=(unsigned)link_parser_feed(&parser,wire[i],now,&f);
    assert(hits==1); return f;
}
static link_frame_t led_frame(uint8_t type,uint32_t token,uint32_t sequence,
                              uint32_t request,uint8_t on)
{
    link_frame_t f={0}; f.type=type; f.session=token; f.sequence=sequence;
    if(type==LINK_LED_SET) { f.length=1; f.payload[0]=on; }
    else { f.length=5; link_put_u32(f.payload,request); f.payload[4]=on; }
    return f;
}
static void test_led_roundtrip_and_scheduling(void)
{
    link_node_t c,s,unconnected; link_frame_t f; uint8_t short_wire[1]; uint32_t seq;
    link_node_init(&unconnected,LINK_CLIENT,1,0);
    assert(!link_node_request_led(&unconnected,1,0));
    connect(&c,&s,0);
    assert(!link_node_request_led(&s,1,1)); assert(!link_node_request_led(&c,2,1));
    assert(link_node_request_led(&c,1,10)); assert(c.led_pending);
    assert(!link_node_request_led(&c,0,10)); seq=c.tx_sequence;
    assert(!link_node_transmit(&c,10,short_wire,sizeof(short_wire)));
    assert(c.led_pending && c.tx_sequence==seq && !c.led_waiting_ack);
    f=transmit_frame(&c,10); assert(f.type==LINK_LED_SET && f.length==1);
    assert(c.led_waiting_ack && !c.led_pending && c.led_request_ms==10);
    assert(!link_node_request_led(&c,0,11)); send_frame(&s,&f,11);
    assert(s.led_on && s.led_ack_pending && !s.command_valid && !s.requested_enable);
    assert(!s.accepted_controls && !s.command_ms);
    assert(!link_node_transmit(&s,11,short_wire,sizeof(short_wire)));
    assert(s.led_ack_pending); f=transmit_frame(&s,11);
    assert(f.type==LINK_LED_ACK && f.length==5 && link_get_u32(f.payload)==seq);
    send_frame(&c,&f,11);
    assert(c.remote_led_valid && c.remote_led_on && c.remote_led_sequence==seq);
    assert(!c.led_waiting_ack && c.last_tx_ms==0 && s.last_tx_ms==0);
    assert(link_node_request_led(&c,0,49)); assert(!c.remote_led_valid);
    assert(transfer(&c,&s,49)); assert(!s.led_on); assert(transfer(&s,&c,49));
    assert(c.remote_led_valid && !c.remote_led_on);
    f=transmit_frame(&c,50); assert(f.type==LINK_CONTROL && !f.payload[2]);
    send_frame(&s,&f,50); assert(s.command_valid && !s.requested_enable);
    assert(link_node_request_led(&c,1,100));
    f=transmit_frame(&c,100); assert(f.type==LINK_CONTROL && c.led_pending);
    send_frame(&s,&f,100); f=transmit_frame(&c,100); assert(f.type==LINK_LED_SET);
    send_frame(&s,&f,100); f=transmit_frame(&s,100); assert(f.type==LINK_STATUS);
    send_frame(&c,&f,100); f=transmit_frame(&s,100); assert(f.type==LINK_LED_ACK);
    send_frame(&c,&f,100); assert(c.remote_led_valid && c.remote_led_on);
    assert(c.last_tx_ms==100 && s.last_tx_ms==100);
    puts("PASS LED on/off ACK, one outstanding request, small-buffer retry, CONTROL/STATUS scheduling priority");
}
static void test_led_rejection(void)
{
    link_node_t c,s; link_frame_t request,ack,bad; uint32_t rejected,peer;
    uint8_t wire[LINK_WIRE_MAX]; size_t count;
    connect(&c,&s,0); assert(link_node_request_led(&c,1,10));
    request=transmit_frame(&c,10); peer=s.peer_sequence; rejected=s.rejected_frames;
    bad=request; bad.payload[0]=2; send_frame(&s,&bad,11);
    bad=request; bad.length=2; send_frame(&s,&bad,12);
    bad=request; bad.session^=1; send_frame(&s,&bad,13);
    bad=request; bad.sequence=peer; send_frame(&s,&bad,14);
    assert(s.rejected_frames==rejected+4 && s.peer_sequence==peer && !s.led_on);
    count=link_encode(&request,wire,sizeof(wire)); wire[count-2]^=0x80;
    feed(&s,wire,count,15); assert(s.parser.errors && !s.led_on);
    send_frame(&s,&request,16); assert(s.led_on);
    send_frame(&s,&request,17); assert(s.last_rx_ms==16); /* duplicate, no liveness */
    ack=transmit_frame(&s,17); rejected=c.rejected_frames; peer=c.peer_sequence;
    bad=ack; bad.payload[4]=0; send_frame(&c,&bad,18);
    bad=ack; link_put_u32(bad.payload,request.sequence+1); send_frame(&c,&bad,19);
    bad=ack; bad.session^=1; send_frame(&c,&bad,20);
    bad=ack; bad.length=4; send_frame(&c,&bad,21);
    bad=ack; bad.sequence=peer; send_frame(&c,&bad,22);
    assert(c.rejected_frames==rejected+5 && c.peer_sequence==peer);
    assert(!c.remote_led_valid && c.led_waiting_ack);
    send_frame(&c,&ack,23); assert(c.remote_led_valid && c.remote_led_on);
    send_frame(&c,&ack,24); assert(c.last_rx_ms==23 && c.rejected_frames==rejected+6);
    assert(link_node_request_led(&c,0,25)); request=transmit_frame(&c,25);
    bad=ack; bad.sequence+=100; send_frame(&c,&bad,26);
    assert(c.led_waiting_ack && !c.remote_led_valid && c.peer_sequence==ack.sequence);
    send_frame(&s,&request,27); ack=transmit_frame(&s,27); send_frame(&c,&ack,28);
    assert(c.remote_led_valid && !c.remote_led_on);
    /* LED and CONTROL share one receive sequence, likewise ACK and STATUS. */
    bad=control(s.token,s.peer_sequence+2,200); send_frame(&s,&bad,30);
    request=led_frame(LINK_LED_SET,s.token,bad.sequence-1,0,1);
    send_frame(&s,&request,31); assert(!s.led_on && s.last_rx_ms==30);
    assert(link_node_request_led(&c,1,32)); request=transmit_frame(&c,32);
    bad.type=LINK_STATUS; bad.length=16; bad.session=c.token;
    bad.sequence=c.peer_sequence+2; memset(bad.payload,0,sizeof(bad.payload));
    send_frame(&c,&bad,33);
    ack=led_frame(LINK_LED_ACK,c.token,bad.sequence-1,request.sequence,1);
    send_frame(&c,&ack,34); assert(!c.remote_led_valid && c.led_waiting_ack);
    puts("PASS LED malformed/CRC/duplicate/old-token/wrong-request/wrong-state rejection and global sequence ordering");
}
static void keep_client_alive(link_node_t *c,uint32_t now)
{
    link_frame_t status={0}; status.type=LINK_STATUS; status.length=16;
    status.session=c->token; status.sequence=c->peer_sequence+1;
    send_frame(c,&status,now);
}
static void test_led_expiry_and_reset(void)
{
    link_node_t c,s; link_frame_t request,ack,f; uint32_t i,token,start=0xffffff00UL;
    /* Keep STATUS alive independently so the 500 ms LED deadline is exercised. */
    connect(&c,&s,start); assert(link_node_request_led(&c,1,start+10));
    for(i=100;i<=500;i+=100) keep_client_alive(&c,start+i);
    link_node_tick(&c,start+509); assert(c.led_pending);
    link_node_tick(&c,start+510); assert(!c.led_pending && !c.remote_led_valid);
    connect(&c,&s,0); assert(link_node_request_led(&c,1,10)); request=transmit_frame(&c,10);
    for(i=100;i<=500;i+=100) keep_client_alive(&c,i);
    link_node_tick(&c,509); assert(c.led_waiting_ack);
    link_node_tick(&c,510); assert(!c.led_waiting_ack && !c.remote_led_valid);
    ack=led_frame(LINK_LED_ACK,c.token,c.peer_sequence+1,request.sequence,1);
    send_frame(&c,&ack,511); assert(!c.remote_led_valid);
    /* LED_SET does not extend a 200 ms CONTROL lease or enable outputs. */
    connect(&c,&s,0); assert(transfer(&c,&s,50)); token=s.token;
    assert(link_node_request_led(&c,1,149)); f=transmit_frame(&c,149);
    assert(f.type==LINK_CONTROL); /* heartbeat takes priority; deliberately drop it */
    request=transmit_frame(&c,149); send_frame(&s,&request,149);
    assert(s.led_on && s.command_ms==50 && s.command_ttl_ms==200 && !s.requested_enable);
    link_node_tick(&s,250); assert(s.token!=token && !s.led_on && !s.led_ack_pending);
    assert(!s.command_valid && s.reason==LINK_CONTROL_EXPIRED);
    request.sequence++; send_frame(&s,&request,251); assert(!s.led_on);
    /* Peer loss extinguishes the controller and invalidates client readback. */
    connect(&c,&s,0); assert(link_node_request_led(&c,1,10));
    assert(transfer(&c,&s,10)); assert(transfer(&s,&c,10));
    assert(s.led_on && c.remote_led_valid);
    link_node_tick(&s,310); link_node_tick(&c,310);
    assert(!s.connected && !s.led_on && !s.led_ack_pending);
    assert(!c.connected && !c.remote_led_valid && !c.led_pending && !c.led_waiting_ack);
    /* Both transport faults and token replacement clear in-flight state. */
    connect(&c,&s,0); assert(link_node_request_led(&c,1,10)); assert(transfer(&c,&s,10));
    link_node_transport_error(&s,11); link_node_transport_error(&c,11);
    assert(!s.led_on && !s.led_ack_pending && !c.led_waiting_ack && !c.remote_led_valid);
    connect(&c,&s,0); assert(link_node_request_led(&c,1,10)); assert(transfer(&c,&s,10));
    f.type=LINK_HELLO_ACK; f.length=4; f.session=c.boot_id; f.sequence=c.peer_sequence+1;
    link_put_u32(f.payload,c.token+1); send_frame(&c,&f,11);
    assert(!c.led_waiting_ack && !c.remote_led_valid);
    f.type=LINK_HELLO; f.length=0; f.session=s.peer_id+1; f.sequence=0;
    send_frame(&s,&f,12); assert(!s.led_on && !s.led_ack_pending);
    puts("PASS LED 500 ms pending/ACK expiry with clock wrap, no CONTROL lease extension, loss/fault/token cleanup");
}
static void reserve(uint8_t used[2][16],unsigned port,unsigned pin)
{ assert(port<2 && pin<16 && !used[port][pin]); used[port][pin]=1; }
static void test_pwm_capability_and_lease(void)
{
    link_node_t c,s; link_frame_t f; uint32_t token,accepted;
    int16_t pwm[4]={250,-250,0,120};
    connect(&c,&s,0);
    assert(!link_node_submit_pwm(&c,1,pwm,1,200,1)); /* old firmware */
    f=control(s.token,1,200); f.type=LINK_PWM; f.payload[2]=1;
    link_put_u16(f.payload+4,100); send_frame(&s,&f,2);
    assert(!s.command_valid); /* no backend, even if client bypasses capability */
    link_node_set_motor_state(&s,1,1);
    assert(transfer(&s,&c,100)); assert(c.remote_flags&LINK_FLAG_PWM_SUPPORTED);
    assert(link_node_submit_pwm(&c,1,pwm,1,200,101));
    assert(transfer(&c,&s,101));
    assert(s.command_valid && s.requested_mode==LINK_PWM && s.requested_enable);
    assert(!memcmp(s.requested_pwm,pwm,sizeof(pwm)) && s.requested_mm_s[0]==0);
    assert(s.command_ms+s.command_ttl_ms==301);
    assert(transfer(&c,&s,151)); assert(s.command_ms+s.command_ttl_ms==301);
    accepted=s.accepted_controls;
    f=control(s.token,s.peer_sequence+1U,200); f.type=LINK_PWM; f.payload[2]=1;
    link_put_u16(f.payload+4,251); send_frame(&s,&f,160);
    assert(s.accepted_controls==accepted && s.command_ms+s.command_ttl_ms==301);
    link_put_u16(f.payload+4,(uint16_t)-251); send_frame(&s,&f,161);
    assert(s.accepted_controls==accepted);
    assert(!link_node_submit_pwm(&c,1,pwm,1,200,162)); /* no input replay */
    pwm[0]=251; assert(!link_node_submit_pwm(&c,2,pwm,1,200,163)); pwm[0]=250;
    link_node_set_motor_state(&s,1,0); assert(transfer(&s,&c,200));
    assert(!(c.remote_flags&LINK_FLAG_OUTPUT_INHIBITED));
    assert(transfer(&c,&s,201)); assert(s.command_ms+s.command_ttl_ms==301);
    token=s.token; link_node_tick(&s,301);
    assert(!s.command_valid && !s.requested_enable && s.requested_mode==0 && s.token!=token);
    assert(s.requested_pwm[0]==0);
    assert(transfer(&s,&c,301)); assert(!c.connected && !c.input_valid && c.remote_flags==0);
    assert(transfer(&c,&s,302)); assert(transfer(&s,&c,302));
    assert(!link_node_submit_pwm(&c,2,pwm,1,200,303)); /* new capability awaited */
    assert(transfer(&c,&s,352)); assert(!s.requested_enable && s.requested_mode==LINK_CONTROL);
    assert(transfer(&s,&c,402));
    assert(link_node_submit_pwm(&c,2,pwm,1,200,403)); assert(transfer(&c,&s,403));
    assert(s.requested_mode==LINK_PWM);
    /* Switching out of a driving PWM session revokes it, never reinterprets units. */
    token=s.token;
    assert(link_node_submit_input(&c,3,pwm,1,200,404)); assert(transfer(&c,&s,453));
    assert(s.token!=token && !s.command_valid && !s.requested_enable && s.requested_pwm[0]==0);
    puts("PASS PWM capability gate, 25% bounds, unchanged input expiry, mode isolation and no reconnect replay");
}
static void test_pwm_stop_and_same_boot_reconnect(void)
{
    link_node_t c,s; link_frame_t pwm,stop,hello; uint32_t token,accepted;
    int16_t values[4]={100,0,0,0};
    connect(&c,&s,0); link_node_set_motor_state(&s,1,1);
    assert(transfer(&s,&c,100)); token=s.token;
    /* Even STOP before the first PWM revokes that token and blocks the client. */
    link_node_request_stop(&c);
    assert(!link_node_submit_pwm(&c,1,values,1,200,101));
    stop=transmit_frame(&c,101);
    assert(stop.type==LINK_PWM && stop.payload[2]==0 && !c.stop_pending);
    send_frame(&s,&stop,101); assert(s.token!=token && !s.command_valid);
    pwm=control(token,stop.sequence+1,200); pwm.type=LINK_PWM; pwm.payload[2]=1;
    link_put_u16(pwm.payload+4,100); accepted=s.accepted_controls;
    send_frame(&s,&pwm,102); assert(s.accepted_controls==accepted && !s.requested_enable);
    assert(!link_node_submit_pwm(&c,2,values,1,200,103)); /* latch persists after send */
    assert(transfer(&s,&c,200)); assert(!c.connected);
    assert(transfer(&c,&s,201)); assert(transfer(&s,&c,201));
    assert(!c.pwm_stop_latched && !c.input_valid);
    assert(transfer(&s,&c,301));
    assert(link_node_submit_pwm(&c,2,values,1,200,302)); assert(transfer(&c,&s,302));
    assert(s.command_valid && s.requested_enable); token=s.token;
    /* ESP RX fault: same boot_id, new HELLO, controller never timed out. */
    link_node_transport_error(&c,303); c.last_tx_ms=203;
    hello=transmit_frame(&c,303); assert(hello.type==LINK_HELLO && hello.session==s.peer_id);
    send_frame(&s,&hello,303); assert(s.token!=token && !s.command_valid && !s.requested_enable);
    token=s.token; send_frame(&s,&hello,304); assert(s.token==token); /* replay cannot reset again */
    assert(transfer(&s,&c,304)); assert(c.connected && !c.input_valid);
    /* XOR on EVERY HELLO would have a fixed point at this boot/token value. */
    s.token=s.peer_id=0x9e3779b9UL;
    hello.session=s.peer_id; hello.sequence=s.peer_sequence+1U;
    send_frame(&s,&hello,305); assert(s.token!=0x9e3779b9UL);
    /* A dropped explicit stop is retried, never replaced by a lease-renewing
     * disabled CONTROL heartbeat that could leave the client stop-latched. */
    connect(&c,&s,0); link_node_set_motor_state(&s,1,1);
    link_node_request_stop(&c); /* before first capability STATUS */
    stop=transmit_frame(&c,1); assert(stop.type==LINK_PWM && !stop.payload[2]);
    stop=transmit_frame(&c,51); assert(stop.type==LINK_PWM && !stop.payload[2]);
    send_frame(&s,&stop,51); assert(!s.command_valid && s.token!=c.token);
    puts("PASS explicit STOP barrier before/after drive, old queued PWM rejection, same-boot fast reconnect reset");
}
static void test_pwm_console_parser(void)
{
    int16_t p[4]={7,7,7,7}; uint32_t token=99;
    const char *invalid[]={"PWM", "PWM 12345678 1 2 3", "PWM 12345678 1 2 3 4 5",
        "PWM 12345678 251 0 0 0", "PWM 12345678 -251 0 0 0",
        "PWM 12345678 999999999999999999999 0 0 0", "PWM 12345678 --1 0 0 0",
        "PWM 12345678 1x 0 0 0", "PWM 12345678 1.0 0 0 0", "PWM 12345678 0 0 0 0junk",
        "PWM 12345678 0 0 0 -", "PWM 00000000 1 2 3 4", "PWM abcd 1 2 3 4",
        "PWM 123456789 1 2 3 4", "PWM z2345678 1 2 3 4", "PWM 1 2 3 4"};
    unsigned i;
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        assert(!motor_command_parse(invalid[i],&token,p)); assert(p[0]==7 && p[3]==7 && token==99);
    }
    assert(!motor_command_parse(0,&token,p));
    assert(!motor_command_parse("PWM 12345678 0 0 0 0",&token,0));
    assert(!motor_command_parse("PWM 12345678 0 0 0 0",0,p));
    assert(motor_command_parse("PWM abCDef12 250 -250  0 -1  ",&token,p));
    assert(token==0xabcdef12UL);
    assert(p[0]==250 && p[1]==-250 && p[2]==0 && p[3]==-1);
    puts("PASS PWM console strict parsing, range/overflow rejection and no partial command");
}
static void test_resource_allocation(void)
{
    uint8_t used[2][16]={{0}}, ports[4]=LINK_STM_ENCODER_PORTS;
    uint8_t a[4]=LINK_STM_ENCODER_A_PINS,b[4]=LINK_STM_ENCODER_B_PINS;
    uint8_t timers[4]=LINK_STM_ENCODER_TIMERS,pwm[4]=LINK_STM_MOTOR_PWM_PINS;
    uint8_t d1[4]=LINK_STM_MOTOR_IN1_PINS,d2[4]=LINK_STM_MOTOR_IN2_PINS;
    unsigned i,j;
    reserve(used,0,LINK_STM_TX_PIN); reserve(used,0,LINK_STM_RX_PIN);
    reserve(used,0,13); reserve(used,0,14); /* SWD */
    reserve(used,1,LINK_STM_DRIVER_INHIBIT_PIN);
    for(i=0;i<4;i++) {
        reserve(used,ports[i],a[i]); reserve(used,ports[i],b[i]);
        reserve(used,1,pwm[i]); reserve(used,1,d1[i]); reserve(used,1,d2[i]);
        assert(timers[i]!=LINK_STM_MOTOR_PWM_TIMER);
        for(j=0;j<i;j++) if(timers[i]) assert(timers[i]!=timers[j]);
    }
    assert(LINK_ESP_TX_GPIO==4 && LINK_ESP_RX_GPIO==6);
    assert(LINK_ESP_TX_GPIO!=LINK_ESP_ARM_SERVO_GPIO && LINK_ESP_RX_GPIO!=LINK_ESP_ARM_SERVO_GPIO);
    assert(LINK_ESP_TX_GPIO!=LINK_ESP_FOREARM_SERVO_GPIO && LINK_ESP_RX_GPIO!=LINK_ESP_FOREARM_SERVO_GPIO);
    puts("PASS resource map: UART, eight encoder inputs, four PWM, directions, STBY and SWD have no overlap");
}
static void test_speed_config_protocol(void)
{
    link_node_t c,s; uint8_t p[20]={0},wire[LINK_WIRE_MAX];
    const int16_t measured[4]={10,-20,30,-40},targets[4]={100,-100,200,-200};
    link_frame_t f; uint32_t rejected,stamp;
    connect(&c,&s,0); link_node_set_motor_state(&s,1,1);
    link_node_publish_speed(&s,measured,targets,15,7,3,100);
    assert(transfer(&s,&c,100)); assert(c.remote_flags&LINK_FLAG_SPEED_SUPPORTED);
    assert(transfer(&s,&c,101)); assert(transfer(&s,&c,102));
    assert(c.remote_speed_valid_mask==15 && c.remote_speed_ready_mask==7 && c.remote_speed_fault==3);
    assert(c.remote_speed_measured[3]==-40 && c.remote_speed_targets[2]==200);
    stamp=c.remote_speed_rx_ms;
    memset(&f,0,sizeof(f)); f.type=LINK_SPEED; f.length=20; f.session=c.token; f.sequence=s.tx_sequence;
    f.payload[0]=15; f.payload[2]=15; f.payload[3]=20;
    rejected=c.rejected_frames; send_frame(&c,&f,103); assert(c.rejected_frames==rejected+1 && c.remote_speed_rx_ms==stamp);
    p[0]=3; p[1]=4; link_put_u16(p+2,650); link_put_u32(p+4,884);
    link_put_u16(p+8,1000); link_put_u16(p+10,2000); link_put_u16(p+12,300); link_put_u16(p+14,500);
    p[19]=1; assert(!link_node_request_config(&c,p,0,103)); p[19]=0;
    assert(link_node_request_config(&c,p,0,103)); assert(!link_node_request_config(&c,p,0,103));
    assert(transfer(&c,&s,103)); /* mandatory heartbeat still has priority */
    assert(!link_node_transmit(&c,104,wire,1) && c.config_pending);
    assert(transfer(&c,&s,104)); assert(s.config_received && s.config_wheel==3 && c.config_waiting);
    assert(!memcmp(s.config_payload,p,20));
    memset(&f,0,sizeof(f)); f.type=LINK_CONFIG_ACK; f.length=8; f.session=c.token; f.sequence=s.tx_sequence;
    link_put_u32(f.payload,c.config_sequence+1); f.payload[4]=3; f.payload[6]=15;
    send_frame(&c,&f,105); assert(c.config_waiting && !c.remote_config_valid);
    link_node_finish_config(&s,2,7,3); assert(transfer(&s,&c,105));
    assert(c.remote_config_valid && c.config_result==2 && !c.config_waiting);
    assert(link_node_request_config(&c,0,1,106)); assert(transfer(&c,&s,106));
    assert(s.config_received && s.config_wheel==255);
    link_node_transport_error(&c,107); assert(!c.config_waiting && !c.remote_config_valid && !c.remote_speed_valid_mask);
    link_node_tick(&s,300); assert(s.speed_valid_mask==0);
    puts("PASS speed capability/telemetry freshness, bounded config, ACK correlation/rejection, clear and reset");
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    test_codec(); test_frames_and_order(); test_truncation_and_reconnect();
    test_input_lease_and_status(); test_wraparound_and_reboot(); test_encoder_feedback();
    test_led_roundtrip_and_scheduling(); test_led_rejection(); test_led_expiry_and_reset();
    test_resource_allocation(); test_pwm_capability_and_lease(); test_pwm_stop_and_same_boot_reconnect();
    test_pwm_console_parser(); test_speed_config_protocol(); test_fuzz();
    puts("PASS all board-link host tests; no hardware or motor calls"); return 0;
}
