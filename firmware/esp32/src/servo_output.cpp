#include "servo_output.h"
#include "board_link_config.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

static bool ready;
static bool attached[LINK_SERVO_COUNT];
static uint16_t pulses[LINK_SERVO_COUNT];
static const int pins[LINK_SERVO_COUNT]={LINK_ESP_ARM_SERVO_GPIO,LINK_ESP_FOREARM_SERVO_GPIO,LINK_ESP_DOOR_SERVO_GPIO};
static_assert(LINK_ESP_DOOR_SERVO_GPIO!=LINK_ESP_ARM_SERVO_GPIO && LINK_ESP_DOOR_SERVO_GPIO!=LINK_ESP_FOREARM_SERVO_GPIO,"Door/arm overlap");
static_assert(LINK_ESP_DOOR_SERVO_GPIO!=LINK_ESP_TX_GPIO && LINK_ESP_DOOR_SERVO_GPIO!=LINK_ESP_RX_GPIO,"Door/UART overlap");
static_assert(LINK_ESP_ARM_SERVO_GPIO!=LINK_ESP_FOREARM_SERVO_GPIO,"Servo pin overlap");
static_assert(LINK_ESP_ARM_SERVO_GPIO!=LINK_ESP_TX_GPIO && LINK_ESP_ARM_SERVO_GPIO!=LINK_ESP_RX_GPIO,"Arm/UART overlap");
static_assert(LINK_ESP_FOREARM_SERVO_GPIO!=LINK_ESP_TX_GPIO && LINK_ESP_FOREARM_SERVO_GPIO!=LINK_ESP_RX_GPIO,"Forearm/UART overlap");

bool servo_output_begin()
{
    // Called before Serial/BLE/UART setup. No LEDC pin is attached until an
    // explicit position command. A saved calibration is never a startup target.
    for(unsigned i=0;i<LINK_SERVO_COUNT;i++) {
        if(gpio_set_level(static_cast<gpio_num_t>(pins[i]),0)!=ESP_OK) return false;
        gpio_config_t cfg={}; cfg.pin_bit_mask=1ULL<<pins[i];cfg.mode=GPIO_MODE_OUTPUT;
        cfg.pull_down_en=GPIO_PULLDOWN_ENABLE;cfg.pull_up_en=GPIO_PULLUP_DISABLE;cfg.intr_type=GPIO_INTR_DISABLE;
        // gpio_config also selects GPIO IOMUX, reclaiming IO6/7 from JTAG.
        if(gpio_config(&cfg)!=ESP_OK) return false;
        attached[i]=false; pulses[i]=0;
    }
    ledc_timer_config_t timer={};
    timer.speed_mode=LEDC_LOW_SPEED_MODE; timer.timer_num=LEDC_TIMER_0;
    timer.duty_resolution=LEDC_TIMER_14_BIT; timer.freq_hz=50; timer.clk_cfg=LEDC_AUTO_CLK;
    if(ledc_timer_config(&timer)!=ESP_OK) return false;
    ready=true; return true;
}
int servo_output_write(uint8_t channel,uint16_t pulse_us)
{
    if(!ready || channel>=LINK_SERVO_COUNT ||
       (pulse_us && (pulse_us<LINK_SERVO_MIN_US(channel) || pulse_us>LINK_SERVO_MAX_US(channel)))) return 0;
    const ledc_channel_t c=static_cast<ledc_channel_t>(channel);
    if(!pulse_us) {
        // Stop the peripheral, disconnect its GPIO matrix and actively hold
        // LOW. This removes PWM, but cannot switch off the servo's own power.
        if(attached[channel] && ledc_stop(LEDC_LOW_SPEED_MODE,c,0)!=ESP_OK) return 0;
        if(gpio_set_level(static_cast<gpio_num_t>(pins[channel]),0)!=ESP_OK) return 0;
        esp_rom_gpio_connect_out_signal(pins[channel],SIG_GPIO_OUT_IDX,false,false);
        if(gpio_set_pull_mode(static_cast<gpio_num_t>(pins[channel]),GPIO_PULLDOWN_ONLY)!=ESP_OK ||
           gpio_set_direction(static_cast<gpio_num_t>(pins[channel]),GPIO_MODE_OUTPUT)!=ESP_OK) return 0;
        attached[channel]=false; pulses[channel]=0; return 1;
    }
    if(!attached[channel]) {
        // Attach at zero duty first; never produce an implicit 1500us pulse.
        ledc_channel_config_t cfg={}; cfg.gpio_num=pins[channel];
        cfg.speed_mode=LEDC_LOW_SPEED_MODE; cfg.channel=c;
        cfg.timer_sel=LEDC_TIMER_0; cfg.duty=0; cfg.hpoint=0;
        if(ledc_channel_config(&cfg)!=ESP_OK) return 0;
        attached[channel]=true;
    }
    const uint32_t duty=(static_cast<uint32_t>(pulse_us)*16384U+10000U)/20000U;
    if(ledc_set_duty(LEDC_LOW_SPEED_MODE,c,duty)!=ESP_OK ||
       ledc_update_duty(LEDC_LOW_SPEED_MODE,c)!=ESP_OK) return 0;
    pulses[channel]=pulse_us; return 1;
}
uint16_t servo_output_pulse(uint8_t channel) { return channel<LINK_SERVO_COUNT?pulses[channel]:0; }
