#pragma once
#include <stdint.h>
typedef int gpio_num_t;
enum {GPIO_PULLDOWN_ONLY=1,GPIO_MODE_OUTPUT=2,GPIO_PULLDOWN_ENABLE=3,GPIO_PULLUP_DISABLE=4,GPIO_INTR_DISABLE=5};
struct gpio_config_t{uint64_t pin_bit_mask;int mode,pull_down_en,pull_up_en,intr_type;};
int gpio_config(const gpio_config_t*);
int gpio_set_level(gpio_num_t,int);
int gpio_set_pull_mode(gpio_num_t,int);
int gpio_set_direction(gpio_num_t,int);
