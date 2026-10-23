#include "fallback_wire.h"

static int moving(const int16_t v[4]) { return v[0] || v[1] || v[2] || v[3]; }

/* The production owner and host integration tests use the same routing. A
 * fault always supersedes an operator brake, including a pending stop edge. */
void fw_remote_sample(fw_node_t *n, fb_remote_t *r, const fb_input_t *input, uint8_t healthy,
                      uint32_t now)
{
    int was_moving = moving(r->wheels);
    uint8_t ready;
    fw_tick(n, now);
    ready = (uint8_t)(n->connected && !n->remote_fault && healthy);
    fb_remote_sample(r, input, ready, now);
    fw_input(n, r->wheels, (uint8_t)(r->armed && moving(r->wheels)), &r->config, now);
    if (!input || !input->valid || input->mode != 0x73 || !ready)
        fw_stop(n, 0);
    else if (r->stop_event || (was_moving && !moving(r->wheels)))
        fw_stop(n, 1);
    r->stop_event = 0;
}

/* Consume every decoded frame, before a later CONTROL can overwrite the
 * last STOP's operator flag. Return the hardware actions to the board owner. */
uint8_t fw_motor_events(fw_node_t *n, fb_motor_t *m, uint32_t *seen_token, uint32_t *seen_stop,
                        uint8_t healthy, uint32_t now)
{
    uint8_t action = 0;
    if (*seen_token != n->token)
    {
        fb_motor_off(m, now);
        *seen_token = n->token;
        *seen_stop = n->stop_serial;
        action = FW_OUTPUT_OFF;
    }
    if (*seen_stop != n->stop_serial)
    {
        *seen_stop = n->stop_serial;
        if (n->operator_stop && n->valid && healthy)
            fb_motor_stop(m, n->brake_pwm, n->brake_ms, now);
        else
        {
            fb_motor_off(m, now);
            action |= FW_OUTPUT_OFF;
            /* Only a fresh explicit non-braking stop can acknowledge a
             * watchdog latch; an expired STOP must not clear the latch. */
            if (n->connected && n->valid && !n->operator_stop)
                action |= FW_CLEAR_WATCHDOG;
        }
    }
    return action;
}

void fw_motor_step(fw_node_t *n, fb_motor_t *m, uint8_t healthy, uint32_t now)
{
    fw_tick(n, now);
    if (!n->connected || !n->valid || !healthy)
        fb_motor_off(m, now);
    else
    {
        uint8_t braking = m->phase == FB_MOTOR_COAST || m->phase == FB_MOTOR_BRAKE;
        fb_motor_tick(m, n->target, (uint8_t)(n->enable || braking), n->ramp, now);
    }
}
