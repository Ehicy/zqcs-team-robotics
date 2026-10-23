#pragma once
#include <stdint.h>
typedef int ledc_channel_t;
enum{ESP_OK=0,LEDC_LOW_SPEED_MODE,LEDC_TIMER_0,LEDC_TIMER_14_BIT,LEDC_AUTO_CLK};
struct ledc_timer_config_t{int speed_mode,timer_num,duty_resolution,freq_hz,clk_cfg;};
struct ledc_channel_config_t{int gpio_num,speed_mode;ledc_channel_t channel;int timer_sel;uint32_t duty,hpoint;};
int ledc_timer_config(const ledc_timer_config_t*);
int ledc_channel_config(const ledc_channel_config_t*);
int ledc_stop(int,ledc_channel_t,int);
int ledc_set_duty(int,ledc_channel_t,uint32_t);
int ledc_update_duty(int,ledc_channel_t);
