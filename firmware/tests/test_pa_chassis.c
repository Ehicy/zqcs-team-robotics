#include "board_link_config.h"
#include "speed_control.h"
#include "remote_bridge.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const unsigned a[4]=LINK_STM_ENCODER_A_PINS, b[4]=LINK_STM_ENCODER_B_PINS;
    const unsigned timer[4]=LINK_STM_ENCODER_TIMERS, mult[4]=LINK_STM_ENCODER_MULTIPLIERS;
    const unsigned pwm[4]=LINK_STM_MOTOR_PWM_PINS;
    const unsigned in1[4]=LINK_STM_MOTOR_IN1_PINS, in2[4]=LINK_STM_MOTOR_IN2_PINS;
    unsigned used_a=(1U<<11)|(1U<<12)|(1U<<13)|(1U<<14);
    unsigned used_b=(1U<<10)|(1U<<11)|(1U<<15);
    unsigned c3=(1U<<18)|(1U<<19), servo[3]={LINK_ESP_ARM_SERVO_GPIO,LINK_ESP_FOREARM_SERVO_GPIO,LINK_ESP_DOOR_SERVO_GPIO};
    speed_control_t speeds; link_node_t node; remote_bridge_t bridge;
    int32_t counts[4]={0,0,0,0};
    assert(LINK_STM_UART==3 && LINK_STM_TX_PIN==10 && LINK_STM_RX_PIN==11);
    assert(LINK_STM_MOTOR_PWM_TIMER==2);
    assert(LINK_ESP_TX_GPIO==18 && LINK_ESP_RX_GPIO==19);
    assert(servo[0]==7 && servo[1]==6 && servo[2]==10);
    assert(LINK_SERVO_MIN_US(0)==500 && LINK_SERVO_MAX_US(0)==2500);
    assert(LINK_SERVO_MIN_US(1)==1300 && LINK_SERVO_MAX_US(1)==1700);
    for(unsigned i=0;i<4;i++) {
        unsigned motor=(1U<<pwm[i])|(1U<<in1[i])|(1U<<in2[i]);
        unsigned encoder=(1U<<a[i])|(1U<<b[i]);
        assert(!(motor&used_a) && !(encoder&used_b));
        used_a|=motor; used_b|=encoder;
        assert(mult[i]==(i<2?4U:2U)); assert(timer[i]==(i<2?3U+i:0U));
    }
    for(unsigned i=0;i<3;i++){assert(!(c3&(1U<<servo[i])));c3|=1U<<servo[i];}
    speed_control_init(&speeds,0,counts);
    link_node_init(&node,LINK_CLIENT,1,0); remote_bridge_init(&bridge,&node,2);
    assert(!speeds.ready_mask && speeds.pid_initialized && !speeds.active);
    assert(!bridge.control.armed && !bridge.control.active);
    for(unsigned i=0;i<4;i++) {
        unsigned expected=i<2?1768U:884U;
        assert(speeds.config[i].counts_per_rev==expected);
        assert(link_get_u32(bridge.configs[i]+4)==expected);
        assert(!speeds.config[i].confirmed && !speeds.pwm_permille[i]);
        assert(speeds.config[i].motor_sign==(i==1? -1:1));
        assert(!!(bridge.configs[i][1]&1U)==(i==1));
    }
    for(unsigned i=0;i<3;i++) assert(!bridge.servo_us[i]);
    puts("PA chassis: independent motor/encoder/UART/SWD pins, x4/x4/x2/x2, C3/STM CPR agreement, boot disarmed and servo PWM off");
}
