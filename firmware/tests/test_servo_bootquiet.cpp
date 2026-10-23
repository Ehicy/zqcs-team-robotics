#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_rom_gpio.h"
#include "../esp32/src/servo_output.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
static unsigned attaches,stops,detaches;
static uint32_t duty[3];
static int level[22],pull[22],mode[22];
int gpio_set_level(gpio_num_t p,int v){level[p]=v;return 0;}
int gpio_set_pull_mode(gpio_num_t p,int v){pull[p]=v;return 0;}
int gpio_set_direction(gpio_num_t p,int v){mode[p]=v;return 0;}
int gpio_config(const gpio_config_t *c){
    assert(c->pull_down_en==GPIO_PULLDOWN_ENABLE&&c->pull_up_en==GPIO_PULLUP_DISABLE);
    for(int p=0;p<22;p++)if(c->pin_bit_mask&(1ULL<<p)){pull[p]=GPIO_PULLDOWN_ONLY;mode[p]=c->mode;}
    return 0;
}
int ledc_timer_config(const ledc_timer_config_t *t){assert(t->freq_hz==50);return 0;}
int ledc_channel_config(const ledc_channel_config_t *c){assert(!c->duty);attaches++;return 0;}
int ledc_stop(int,ledc_channel_t c,int idle){assert(!idle);stops++;duty[c]=0;return 0;}
int ledc_set_duty(int,ledc_channel_t c,uint32_t d){duty[c]=d;return 0;}
int ledc_update_duty(int,ledc_channel_t){return 0;}
void esp_rom_gpio_connect_out_signal(uint32_t p,uint32_t signal,bool inv,bool oen){assert(!level[p]&&signal==128&&!inv&&!oen);detaches++;}
int main(){
    assert(servo_output_begin());assert(!attaches);
    for(int p:{7,6,10})assert(!level[p]&&pull[p]==GPIO_PULLDOWN_ONLY&&mode[p]==GPIO_MODE_OUTPUT);
    for(unsigned i=0;i<3;i++)assert(!servo_output_pulse(i));
    assert(servo_output_write(0,0));assert(!attaches);
    assert(!servo_output_write(0,499)&&!servo_output_write(1,500)&&!servo_output_write(3,1500));
    assert(servo_output_write(0,1422));assert(attaches==1&&servo_output_pulse(0)==1422);
    assert(duty[0]==(1422U*16384U+10000U)/20000U);
    assert(servo_output_write(0,0));assert(stops==1&&detaches==2&&!servo_output_pulse(0));
    assert(servo_output_write(0,1420));assert(attaches==2&&servo_output_pulse(0)==1420);
    std::puts("Servo HAL: no boot attachment/default 1500us, explicit first target only, low/pulldown, release detaches, reattach passed (mock GPIO/LEDC)");
}
