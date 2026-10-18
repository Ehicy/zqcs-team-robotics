#include "servo_output.h"
#include "board_link_config.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

static bool ready;
static const int pins[LINK_SERVO_COUNT]={LINK_ESP_ARM_SERVO_GPIO,LINK_ESP_FOREARM_SERVO_GPIO,LINK_ESP_DOOR_SERVO_GPIO};
static_assert(LINK_ESP_DOOR_SERVO_GPIO!=LINK_ESP_ARM_SERVO_GPIO && LINK_ESP_DOOR_SERVO_GPIO!=LINK_ESP_FOREARM_SERVO_GPIO,"Door/arm overlap");
static_assert(LINK_ESP_DOOR_SERVO_GPIO!=LINK_ESP_TX_GPIO && LINK_ESP_DOOR_SERVO_GPIO!=LINK_ESP_RX_GPIO,"Door/UART overlap");
static_assert(LINK_ESP_ARM_SERVO_GPIO!=LINK_ESP_FOREARM_SERVO_GPIO,"Servo pin overlap");
static_assert(LINK_ESP_ARM_SERVO_GPIO!=LINK_ESP_TX_GPIO && LINK_ESP_ARM_SERVO_GPIO!=LINK_ESP_RX_GPIO,"Arm/UART overlap");
static_assert(LINK_ESP_FOREARM_SERVO_GPIO!=LINK_ESP_TX_GPIO && LINK_ESP_FOREARM_SERVO_GPIO!=LINK_ESP_RX_GPIO,"Forearm/UART overlap");

bool servo_output_begin()
{
    // Start LOW, never auto-center on boot. LEDC 50Hz, 14-bit => ~1.22us.
    for(unsigned i=0;i<LINK_SERVO_COUNT;i++) {
        gpio_set_level(static_cast<gpio_num_t>(pins[i]),0);
        gpio_set_direction(static_cast<gpio_num_t>(pins[i]),GPIO_MODE_OUTPUT);
    }
    ledc_timer_config_t timer={};
    timer.speed_mode=LEDC_LOW_SPEED_MODE; timer.timer_num=LEDC_TIMER_0;
    timer.duty_resolution=LEDC_TIMER_14_BIT; timer.freq_hz=50; timer.clk_cfg=LEDC_AUTO_CLK;
    if(ledc_timer_config(&timer)!=ESP_OK) return false;
    for(unsigned i=0;i<LINK_SERVO_COUNT;i++) {
        ledc_channel_config_t c={}; c.gpio_num=pins[i];
        c.speed_mode=LEDC_LOW_SPEED_MODE; c.channel=static_cast<ledc_channel_t>(i);
        c.timer_sel=LEDC_TIMER_0; c.duty=0; c.hpoint=0;
        if(ledc_channel_config(&c)!=ESP_OK) {
            for(unsigned j=0;j<i;j++) ledc_stop(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(j),0);
            return false;
        }
    }
    ready=true; return true;
}
int servo_output_write(uint8_t channel,uint16_t pulse_us)
{
    if(!ready || channel>=LINK_SERVO_COUNT ||
       (pulse_us && (pulse_us<LINK_SERVO_MIN_US(channel) || pulse_us>LINK_SERVO_MAX_US(channel)))) return 0;
    const ledc_channel_t c=static_cast<ledc_channel_t>(channel);
    // Zero duty is deliberate PWM release, not a power disconnect.
    const uint32_t duty=(static_cast<uint32_t>(pulse_us)*16384U+10000U)/20000U;
    return ledc_set_duty(LEDC_LOW_SPEED_MODE,c,duty)==ESP_OK &&
           ledc_update_duty(LEDC_LOW_SPEED_MODE,c)==ESP_OK;
}
