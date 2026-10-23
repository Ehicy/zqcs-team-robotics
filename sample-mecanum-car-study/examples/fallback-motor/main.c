#include "delay.h"
#include "fallback_wire.h"
#include "motor_hal.h"
#include "rear_probe.h"
#include "stm32f10x.h"
#include "uart.h"
/* Encoder/PID/backend from ../lower-motor/main.c is intentionally not linked.
 * Kept intact for later restoration; no fabricated zero-speed feedback. */
static fw_node_t node;
static fb_motor_t motors;
static rear_probe_t rear_probe;
volatile uint32_t fallback_rear_test_command;
volatile uint32_t fallback_rear_test_status;
volatile uint32_t fallback_firmware_tag = FW_TAG;
int main(void)
{
    uint8_t wire[LINK_WIRE_MAX], b;
    uint32_t arrival, now, token, stops;
    size_t len;
    unsigned budget, i;
    lower_motor_init();
    delay_init();
    fb_motor_init(&motors, delay_millis());
    fw_init(&node, 1, *(const uint32_t *)0x1ffff7e8UL ^ delay_millis(), delay_millis());
    fallback_uart_init();
    token = node.token;
    stops = node.stop_serial;
    lower_motor_watchdog_feed();
    while (1)
    {
        uint8_t probe_stop = 0;
        now = delay_millis();
        fw_tick(&node, now);
        if (fallback_uart_fault())
        {
            probe_stop = 1;
            lower_motor_emergency_stop();
            fb_motor_off(&motors, now);
            fw_fault(&node, now);
        }
        for (budget = 0; budget < 128 && fallback_uart_get(&b, &arrival); budget++)
        {
            uint32_t old_stop = node.stop_serial;
            now = delay_millis();
            fw_receive(&node, b, arrival, now);
            if (node.stop_serial != old_stop)
                probe_stop = 1;
            uint8_t action = fw_motor_events(&node, &motors, &token, &stops,
                                             (uint8_t)!lower_motor_watchdog_fault(), now);
            if (action & FW_OUTPUT_OFF)
                lower_motor_emergency_stop();
            if (action & FW_CLEAR_WATCHDOG)
                lower_motor_watchdog_clear();
        }
        now = delay_millis();
        {
            uint8_t healthy = (uint8_t)!lower_motor_watchdog_fault();
            uint8_t was_probe_running = rear_probe.state == 1;
            int probe = rear_probe_poll(&rear_probe, fallback_rear_test_command,
                                         healthy, probe_stop, now);
            if (probe >= 0)
            {
                const int16_t rear_only[4] = {0, 0, REAR_PROBE_PWM, REAR_PROBE_PWM};
                /* Normal motor ramp and hardware watchdog remain active.
                 * Completion stays latched off until explicit command zero. */
                if (probe && !was_probe_running)
                    fb_motor_off(&motors, now);
                if (probe)
                    fb_motor_tick(&motors, rear_only, 1, 100, now);
                else
                    fb_motor_off(&motors, now);
                lower_motor_write(motors.output, (uint8_t)(probe && healthy));
            }
            else
            {
                fw_motor_step(&node, &motors, healthy, now);
                lower_motor_write(motors.output,
                                  (uint8_t)(node.connected && node.valid && healthy));
            }
            fallback_rear_test_status = rear_probe.state;
        }
        for (i = 0; i < 4; i++)
            node.status_output[i] = motors.output[i];
        node.remote_enabled = lower_motor_is_enabled();
        node.remote_fault = lower_motor_watchdog_fault();
        len = fw_transmit(&node, now, wire, sizeof(wire));
        if (len && !fallback_uart_write(wire, len))
        {
            lower_motor_emergency_stop();
            fb_motor_off(&motors, now);
            fw_fault(&node, now);
        }
        lower_motor_watchdog_feed();
        delay_ms(1);
    }
}
