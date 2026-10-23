#include "remote_bridge.h"
#include "speed_control.h"
#include "encoder_origin.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static link_node_t client,stm;
static remote_bridge_t bridge;
static speed_control_t speed;
static uint8_t flash[DEVICE_SETTINGS_BYTES];
static unsigned saves,writes,fail;
static uint32_t now,sequence;
static int store(const device_settings_t *s){if(fail)return 0;saves++;return device_settings_encode(s,flash);}
static int servo(uint8_t ch,uint16_t pulse){(void)ch;(void)pulse;writes++;return 1;}
static void transfer(link_node_t *a,link_node_t *b){uint8_t wire[LINK_WIRE_MAX];size_t i,n=link_node_transmit(a,now,wire,sizeof(wire));for(i=0;i<n;i++)link_node_receive(b,wire[i],now);}
static void cycle(unsigned count){
    const int16_t zero[4]={0,0,0,0};
    while(count--){
        link_node_publish_speed(&stm,zero,zero,15,0,0,now);link_node_set_motor_state(&stm,1,1);
        remote_bridge_tick(&bridge,now);transfer(&client,&stm);
        if(stm.config_received){
            if(stm.config_wheel==253)assert(speed_control_configure_pid(&speed,link_get_u16(stm.config_payload),link_get_u16(stm.config_payload+2),link_get_u16(stm.config_payload+4)));
            else if(stm.config_wheel<4){
                unsigned w=stm.config_wheel;const uint8_t *p=stm.config_payload;speed_config_t c=speed.config[w];
                c.motor_sign=(p[1]&1)?-1:1;c.encoder_sign=(p[1]&2)?-1:1;c.confirmed=!!(p[1]&4);
                c.diameter_tenth_mm=link_get_u16(p+2);c.counts_per_rev=link_get_u32(p+4);c.max_mm_s=link_get_u16(p+12);
                assert(speed_control_configure(&speed,w,&c));
            }else assert(stm.config_wheel==252);
            link_node_finish_config(&stm,0,0,0);
        }
        transfer(&stm,&client);remote_bridge_tick(&bridge,now);now++;
    }
}
static uint8_t send(uint8_t op,uint8_t channel,uint16_t x,uint16_t y,uint16_t z){
    uint8_t p[20]={1,0},ack[20];p[1]=op;link_put_u32(p+4,bridge.control.session);link_put_u32(p+8,++sequence);
    if(op==REMOTE_OPCODE_PID){link_put_u16(p+14,x);link_put_u16(p+16,y);link_put_u16(p+18,z);}
    else if(op==REMOTE_OPCODE_SERVO_SETTINGS){p[12]=channel;p[13]=1;link_put_u16(p+14,x);link_put_u16(p+16,y);link_put_u16(p+18,z);}
    assert(remote_bridge_receive(&bridge,p,sizeof(p),now,now));
    if(op==REMOTE_OPCODE_ENCODER_RESET)cycle(50);
    remote_bridge_status(&bridge,0,ack);assert(ack[1]==3);return ack[3];
}
int main(void){
    device_settings_t s,decoded,before;uint8_t bad[DEVICE_SETTINGS_BYTES],status[20];unsigned i;
    int32_t counts[4]={0,0,0,0};
    link_node_init(&client,LINK_CLIENT,1,0);link_node_init(&stm,LINK_CONTROLLER,2,0);
    speed_control_init(&speed,0,counts);remote_bridge_init(&bridge,&client,3);bridge.servo_write=servo;bridge.save_settings=store;
    remote_bridge_get_settings(&bridge,&s);assert(device_settings_valid(&s));assert(store(&s));
    assert(device_settings_decode(&decoded,flash,sizeof(flash)));assert(!memcmp(&s,&decoded,sizeof(s)));
    for(i=0;i<128;i++){memcpy(bad,flash,128);bad[i]^=1;assert(!device_settings_decode(&decoded,bad,128));}
    assert(!device_settings_decode(&decoded,flash,127));
    cycle(500);assert(bridge.synced_revision==1&&bridge.pid_initialized);assert(!writes);
    assert(send(REMOTE_OPCODE_PID,0,1200,2300,40)==0);assert(saves==2);cycle(500);
    assert(speed.config[3].kp_milli==1200&&speed.config[3].kd_milli==40);
    assert(send(REMOTE_OPCODE_PID,0,1200,2300,40)==0);assert(saves==2); /* Identical values do not wear Flash. */
    remote_bridge_get_settings(&bridge,&before);fail=1;
    assert(send(REMOTE_OPCODE_PID,0,1300,2400,50)==7);remote_bridge_get_settings(&bridge,&s);
    assert(!memcmp(&s,&before,sizeof(s)));fail=0;
    assert(send(REMOTE_OPCODE_SERVO_SETTINGS,0,1300,1600,1422)==0);assert(!writes);cycle(500);
    assert(device_settings_decode(&decoded,flash,128));
    remote_bridge_init(&bridge,&client,4);bridge.servo_write=servo;bridge.save_settings=store;
    assert(remote_bridge_load_settings(&bridge,&decoded));bridge.settings_loaded=1;cycle(600);
    assert(!writes&&!bridge.servo_us[0]&&!bridge.servos[0].target); /* Stored reference is NEVER replayed on boot. */
    assert(bridge.settings.servo_reference[0]==1422&&bridge.synced_revision==decoded.revision);
    assert(speed.config[2].motor_sign==1&&speed.config[3].motor_sign==1&&speed.config[1].motor_sign==-1);
    remote_bridge_status(&bridge,8,status);assert(status[1]==8&&status[2]==3&&link_get_u16(status+8)==1200);
    for(i=0;i<4;i++){remote_bridge_status(&bridge,7,status);assert(status[2]==i&&link_get_u16(status+18)==decoded.revision);}
    client.remote_encoder_counts[0]=123;assert(send(REMOTE_OPCODE_ENCODER_RESET,0,0,0,0)==0);assert(!client.remote_encoder_counts[0]);
    {encoder_origin_t origin;int32_t raw[4]={INT32_MAX,INT32_MIN,42,-42};encoder_origin_set(&origin,raw);
     encoder_origin_apply(&origin,raw);for(i=0;i<4;i++)assert(!raw[i]);
     raw[0]=INT32_MIN;raw[1]=INT32_MAX;raw[2]=44;raw[3]=-44;encoder_origin_apply(&origin,raw);
     assert(raw[0]==1&&raw[1]==-1&&raw[2]==2&&raw[3]==-2);}
    puts("C3 settings: CRC/roundtrip, failed/identical writes, reboot sync, rear signs, no servo boot replay, encoder reset/origin wrap passed");
}
