#include "delay.h"
#include "fallback_wire.h"
#include "hal.h"
#include "motor_hal.h" /* shared watchdog clock, not a motor output on this board */
#include "ps2.h"
#include "stm32f10x.h"
#include "uart.h"
static fb_remote_t remote;
static fw_node_t node;
static fb_l3_led_t l3_led;
static fb_bench_t bench;
static fb_servo_probe_t servo_probe;
volatile const uint32_t fallback_bench_identity = 0xA19B2026UL;
volatile struct
{
    uint32_t command, status;
    int16_t output[4]; /* Reported by B, not measured wheel speed. */
} fallback_stlink_test;
volatile fb_input_t fallback_ps2_input; /* Latest decoded sample for ST-Link diagnosis. */
volatile uint32_t fallback_firmware_tag = FW_TAG;
static uint32_t note_at, report_at;
static uint8_t note_count, report_active;
static uint16_t report;
static void led_tick(uint32_t now)
{
    int l3_output = fb_l3_led_output(&l3_led, now);
    uint32_t age;
    unsigned i, div = 1000;
    if (l3_output >= 0)
    {
        remote_led(l3_output);
        return;
    }
    if (remote.notice)
    {
        if (!note_count || note_count != remote.notice)
        {
            note_count = remote.notice;
            note_at = now;
        }
        remote.notice = 0;
        report_active = 0;
    }
    if (remote.report_request)
    {
        report = remote.report_value;
        report_at = now;
        report_active = 1;
        note_count = 0;
        remote.report_request = 0;
    }
    if (report_active)
    {
        age = now - report_at;
        if (age < 1600)
        {
            remote_led(age < 1000);
            return;
        }
        age -= 1600;
        for (i = 0; i < 4; i++, div /= 10)
        {
            unsigned d = report / div % 10, duration = d ? d * 200 : 600;
            if (age < duration)
            {
                remote_led(d ? age % 200 < 100 : 1);
                return;
            }
            if (age < duration + 800)
            {
                remote_led(0);
                return;
            }
            age -= duration + 800;
        }
        report_active = 0;
    }
    if (note_count)
    {
        age = now - note_at;
        if (age < note_count * 300U + 800U)
        {
            remote_led(age < note_count * 300U && age % 300U < 150U);
            return;
        }
        note_count = 0;
    }
    if (remote.mode == FB_TUNE)
    {
        unsigned slot = (now / 150) % (2U * (remote.param + 1U) + 12U);
        remote_led(slot < 2U * (remote.param + 1U) && !(slot & 1U));
    }
    else if (remote.mode == FB_SERVO)
        remote_led(now % 800 < 400);
    else if (remote.armed)
        remote_led(1);
    else
        remote_led(now % 2000 < 100);
}
int main(void)
{
    uint8_t wire[LINK_WIRE_MAX], byte;
    uint16_t written[3] = {0, 0, 0};
    uint32_t arrival, now, last_sample, token;
    size_t len;
    unsigned i, budget;
    remote_hal_init();
    delay_init();
    now = delay_millis();
    fb_remote_init(&remote, now);
    (void)fb_store_load(&remote.config);
    fw_init(&node, 0, *(const uint32_t *)0x1ffff7e8UL ^ *(const uint32_t *)0xe0001004UL, now);
    token = node.token;
    last_sample = now - 20;
    ps2_init();
    fallback_uart_init();
    lower_motor_watchdog_feed();
    while (1)
    {
        now = delay_millis();
        fw_tick(&node, now);
        if (fallback_uart_fault())
        {
            fw_fault(&node, now);
            fb_remote_fault(&remote, now);
        }
        for (budget = 0; budget < 128 && fallback_uart_get(&byte, &arrival); budget++)
            fw_receive(&node, byte, arrival, delay_millis());
        now = delay_millis();
        fw_tick(&node, now);
        if (token != node.token)
        {
            fb_remote_fault(&remote, now);
            token = node.token;
        }
        if (now - last_sample >= 20)
        {
            ps2_data p;
            fb_input_t input;
            input.valid = ps2_read_buttons(&p);
            input.mode = p.mode;
            input.buttons = (uint16_t)p.btn1 | ((uint16_t)p.btn2 << 8);
            input.lx = p.LJoy_LR;
            input.ly = p.LJoy_UD;
            input.rx = p.RJoy_LR;
            input.ry = p.RJoy_UD;
            now = delay_millis();
            last_sample = now;
            fallback_ps2_input = input;
            fb_l3_led_sample(&l3_led, &input, now);
            {
                uint8_t healthy = (uint8_t)!lower_motor_watchdog_fault();
                int test = fb_bench_poll(&bench, fallback_stlink_test.command,
                    (uint8_t)(healthy && node.connected && !node.remote_fault),
                    (uint8_t)(input.valid && (input.buttons & FB_MASK(FB_START))), now);
                if (test >= 0)
                {
                    /* Explicit operator bench request bypasses only the faulty
                     * PS2 source. UART expiry, watchdog and startup ramp remain. */
                    fb_remote_fault(&remote, now);
                    if (test)
                    {
                        int16_t target[4];
                        fb_config_t config;
                        fb_config_default(&config);
                        for (i = 0; i < 4; i++)
                            target[i] = (remote.config.motor_negative & (1U << i)) ?
                                        -FB_BENCH_PWM : FB_BENCH_PWM;
                        fw_input(&node, target, 1, &config, now);
                    }
                    else
                        fw_stop(&node, 0);
                }
                else
                    fw_remote_sample(&node, &remote, &input, healthy, now);
                fallback_stlink_test.status = bench.state | (node.connected ? 0x100U : 0) |
                    (node.remote_enabled ? 0x200U : 0) | (!healthy ? 0x400U : 0) |
                    (node.remote_fault ? 0x800U : 0);
                for (i = 0; i < 4; i++)
                    fallback_stlink_test.output[i] = node.status_output[i];
            }
            if (input.valid && (input.buttons & FB_MASK(FB_START)) && lower_motor_watchdog_fault())
            {
                lower_motor_watchdog_clear();
                fw_stop(&node, 0);
            }
        }
        now = delay_millis();
        fb_remote_tick(
            &remote,
            (uint8_t)(node.connected && !node.remote_fault && !lower_motor_watchdog_fault()), now);
        {
            int pulse = fb_servo_probe_poll(&servo_probe, fallback_stlink_test.command,
                (uint8_t)(node.connected && !node.remote_fault && !node.remote_enabled &&
                          !lower_motor_watchdog_fault()),
                (uint8_t)(fallback_ps2_input.valid &&
                          (fallback_ps2_input.buttons & FB_MASK(FB_START))), now);
            /* Probe tokens are non-motor bench tokens: normal input and wheel
             * outputs stay locked. MCU lease sends neutral if the PC disappears. */
            if (pulse >= 0) servo_motion_reference(&remote.servo[0], (uint16_t)pulse, now);
        }
        for (i = 0; i < 3; i++)
            if (remote.servo[i].pulse != written[i])
            {
                if (remote_servo_write(i, remote.servo[i].pulse))
                    written[i] = remote.servo[i].pulse;
                else
                {
                    servo_motion_reference(&remote.servo[i], written[i], now);
                    remote.notice = 5;
                }
            }
        if (remote.save_request)
        {
            remote.save_request = 0;
            /* No encoder: this proves only disabled electrical output, NOT a
             * physically stationary chassis. Operator must park/support it. */
            if (node.connected && !node.remote_enabled && now - node.last_rx < 100 &&
                remote.mode == FB_TUNE)
            {
                int result;
                fw_stop(&node, 0);
                len = fw_transmit(&node, now, wire, sizeof(wire));
                if (len && !fallback_uart_write(wire, len))
                {
                    fw_fault(&node, now);
                    fb_remote_fault(&remote, now);
                    continue;
                }
                for (i = 0; i < 3; i++)
                    servo_motion_freeze(&remote.servo[i]);
                result = fb_store_save(&remote.config);
                now = delay_millis();
                fw_fault(&node, now);
                fb_remote_fault(&remote, now);
                lower_motor_watchdog_clear();
                remote.have_saved = 1;
                remote.last_save_ms = now;
                remote.notice = result ? 3 : 5;
                if (result)
                    remote.dirty = 0;
            }
            else
                remote.notice = 5;
        }
        len = fw_transmit(&node, now, wire, sizeof(wire));
        if (len && !fallback_uart_write(wire, len))
        {
            fw_fault(&node, delay_millis());
            fb_remote_fault(&remote, delay_millis());
        }
        led_tick(delay_millis());
        lower_motor_watchdog_feed();
        delay_ms(1);
    }
}
