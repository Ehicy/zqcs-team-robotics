#include "fallback.h"
#include <string.h>
#define B(n) FB_MASK(FB_##n)
int fb_servo_probe_poll(fb_servo_probe_t *s, uint32_t token, uint8_t ready,
                        uint8_t stop, uint32_t now)
{
#if FB_CONTINUOUS_ARM_PROFILE
    unsigned pulse = token & 0xffffU;
    if ((token & 0xff000000UL) != FB_SERVO_PROBE_KEY) {
        int was_active = s->active;
        memset(s, 0, sizeof(*s));
        return was_active ? (int)FB_BIG_ARM_STOP_US : -1;
    }
    if (!s->active) { s->active = 1; s->blocked = 1; }
    if (!ready || stop || pulse < 1470 || pulse > 1530) {
        s->blocked = 1;
        return FB_BIG_ARM_STOP_US;
    }
    if (pulse == FB_BIG_ARM_STOP_US) {
        s->blocked = s->moving = 0;
        s->token = token;
        s->heartbeat_ms = now;
        return FB_BIG_ARM_STOP_US;
    }
    if (s->blocked) return FB_BIG_ARM_STOP_US;
    if (now - s->heartbeat_ms >= FB_SERVO_PROBE_LEASE_MS ||
        (s->moving && now - s->started_ms >= FB_BIG_ARM_BURST_MS)) {
        s->blocked = 1;
        return FB_BIG_ARM_STOP_US;
    }
    if (token != s->token) { s->token = token; s->heartbeat_ms = now; }
    if (!s->moving) { s->moving = 1; s->started_ms = now; }
    return (int)pulse;
#else
    /* An old browser/bridge must never inject an assumed neutral position. */
    (void)token; (void)ready; (void)stop; (void)now;
    memset(s, 0, sizeof(*s));
    return -1;
#endif
}
int fb_bench_poll(fb_bench_t *s, uint32_t command, uint8_t ready, uint8_t stop, uint32_t now)
{
    if (!command)
    {
        int had_test = s->state != FB_BENCH_IDLE;
        s->state = FB_BENCH_IDLE;
        s->last_command = 0;
        return had_test ? 0 : -1;
    }
    if ((command & 0xffff0000UL) != FB_BENCH_KEY || !ready || stop)
    {
        s->state = stop ? FB_BENCH_STOPPED : FB_BENCH_FAULT;
        return 0;
    }
    if (s->state == FB_BENCH_IDLE)
    {
        s->started_ms = s->heartbeat_ms = now;
        s->last_command = command;
        s->state = FB_BENCH_RUN;
    }
    if (s->state != FB_BENCH_RUN)
        return 0;
    /* Check expiry BEFORE a new heartbeat; a delayed host cannot auto-resume. */
    if (now - s->heartbeat_ms >= FB_BENCH_LEASE_MS)
        s->state = FB_BENCH_EXPIRED;
    else if (now - s->started_ms >= FB_BENCH_LIMIT_MS)
        s->state = FB_BENCH_DONE;
    if (s->state != FB_BENCH_RUN)
        return 0;
    if (command != s->last_command)
    {
        s->last_command = command;
        s->heartbeat_ms = now;
    }
    return 1;
}
void fb_l3_led_sample(fb_l3_led_t *s, const fb_input_t *p, uint32_t now)
{
    uint8_t down;
    /* An invalid frame must neither trigger nor re-arm a held button. */
    if (!p || !p->valid || (p->mode != 0x41 && p->mode != 0x73))
        return;
    down = (uint8_t)!!(p->buttons & B(L3));
    if (down && !s->down)
    {
        s->started_ms = now;
        s->active = 1;
    }
    s->down = down;
}
int fb_l3_led_output(fb_l3_led_t *s, uint32_t now)
{
    uint32_t age = now - s->started_ms;
    if (!s->active)
        return -1;
    if (age >= 400)
    {
        s->active = 0;
        return -1;
    }
    /* Off/on/off is visible even when the normal armed LED is solid on. */
    return age >= 80 && age < 280;
}
void fb_config_default(fb_config_t *c)
{
    unsigned i;
    memset(c, 0, sizeof(*c));
    c->max_pwm = 100;
    c->turn_percent = 100;
    c->motor_negative = 10;
    c->jog_us = 5;
    c->ramp_ms = 500;
    c->brake_pwm = 50;
    c->brake_ms = 30;
    for (i = 0; i < 4; i++)
        c->gain[i] = 100;
    for (i = 0; i < 3; i++)
    {
        c->servo_min[i] = i == 2 ? 1000 : 1300;
        c->servo_max[i] = i == 2 ? 2000 : 1700;
        c->servo_reference[i] = 1500;
    }
}
int fb_config_valid(const fb_config_t *c)
{
    unsigned i;
    if (!c || c->max_pwm < 100 || c->max_pwm > 1000 || c->turn_percent < 20 ||
        c->turn_percent > 100 || c->motor_negative > 15 || c->jog_us < 1 || c->jog_us > 20 ||
        c->ramp_ms < 100 || c->ramp_ms > 1500 || c->brake_pwm > 50 || c->brake_ms > 30)
        return 0;
    for (i = 0; i < 4; i++)
        if (c->gain[i] < 50 || c->gain[i] > 100)
            return 0;
    for (i = 0; i < 3; i++)
        if (c->servo_min[i] < 500 || c->servo_max[i] > 2500 ||
            c->servo_min[i] + 20 > c->servo_max[i] || c->servo_reference[i] < c->servo_min[i] ||
            c->servo_reference[i] > c->servo_max[i])
            return 0;
    return 1;
}
static void freeze(fb_remote_t *s)
{
    unsigned i;
    for (i = 0; i < 3; i++)
        servo_motion_freeze(&s->servo[i]);
}
static void zero(fb_remote_t *s) { memset(s->wheels, 0, sizeof(s->wheels)); }
void fb_remote_init(fb_remote_t *s, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    fb_config_default(&s->config);
    s->sample_ms = s->neutral_ms = s->last_change_ms = now;
    /* No boot pulse: physical shaft angle is unknown, not zero or 1500us. */
}
void fb_remote_fault(fb_remote_t *s, uint32_t now)
{
    zero(s);
    freeze(s);
    s->mode = FB_LOCKED;
    s->armed = s->neutral_ready = 0;
    s->neutral_ms = now;
    s->previous_buttons = 0;
    s->hold_chord = 0;
    s->hold_done = 0;
    s->save_request = 0;
    s->stop_event = 0;
    s->repeat_key = 0;
}
static int axis(uint8_t v)
{
    int x = 128 - (int)v, mag = x < 0 ? -x : x;
    if (mag <= 10)
        return 0;
    mag = (mag - 10) * 1000 / 118;
    if (mag > 1000)
        mag = 1000;
    /* Original comfortable stick behavior: linear/cubic blend, monotone. */
    mag = (mag + (int)((int64_t)mag * mag * mag / 1000000)) / 2;
    return x < 0 ? -mag : mag;
}
static int neutral(const fb_input_t *p)
{
    return !axis(p->lx) && !axis(p->ly) && !axis(p->rx) && !axis(p->ry);
}
static int held(fb_remote_t *s, uint16_t chord, uint16_t b, uint32_t ms, uint32_t now)
{
    if (b != chord)
        return 0;
    if (s->hold_chord != chord)
    {
        s->hold_chord = chord;
        s->hold_ms = now;
        s->hold_done = 0;
    }
    if (!s->hold_done && now - s->hold_ms >= ms)
    {
        s->hold_done = 1;
        return 1;
    }
    return 0;
}
static int key_step(fb_remote_t *s, uint16_t key, uint16_t edges, uint32_t now)
{
    if (!key)
    {
        s->repeat_key = 0;
        return 0;
    }
    if (edges & key)
    {
        s->repeat_key = key;
        s->repeat_ms = now + 400;
        return 1;
    }
    if (s->repeat_key == key && (int32_t)(now - s->repeat_ms) >= 0)
    {
        s->repeat_ms = now + 100;
        return 1;
    }
    return 0;
}
int fb_servo_target(fb_remote_t *s, unsigned ch, int32_t target, uint32_t now)
{
    if (ch >= 3 || !s->servo[ch].pulse || target < s->config.servo_min[ch] ||
        target > s->config.servo_max[ch])
        return 0;
    servo_motion_target(&s->servo[ch], (uint16_t)target, now);
    return 1;
}
int fb_servo_enable(fb_remote_t *s, unsigned ch, uint32_t now)
{
    if (ch >= 3 || s->servo[ch].pulse || !fb_config_valid(&s->config))
        return 0;
    /* Explicit takeover only. This reference is NOT a measured shaft angle. */
    servo_motion_reference(&s->servo[ch], s->config.servo_reference[ch], now);
    return 1;
}
/* Jog relative to the current commanded position, not a measured shaft angle.
 * PB6/PB8 need a deliberately established reference before relative jogging;
 * both mechanisms have reported uncontrolled movement. Never guess their pose. */
static void servo_jog(fb_remote_t *s, unsigned ch, int direction, uint16_t keys, uint32_t now)
{
    if (s->repeat_key != keys || s->selected != ch)
    {
        freeze(s);
        s->selected = (uint8_t)ch;
        s->repeat_key = keys;
        s->repeat_ms = now;
    }
#if FB_CONTINUOUS_ARM_PROFILE
    if (ch == 0)
    {
        /* Reverse the previously observed direction. Never accumulate an
         * angle target. Bound each press because there is no position sensor. */
        uint16_t pulse = FB_BIG_ARM_STOP_US;
        if (now - s->repeat_ms < FB_BIG_ARM_BURST_MS)
            pulse = (uint16_t)((int)FB_BIG_ARM_STOP_US - direction * FB_BIG_ARM_JOG_US);
        servo_motion_reference(&s->servo[0], pulse, now);
        return;
    }
#else
    if ((ch == 0 || ch == 2) && !s->servo[ch].pulse)
    {
        /* Do not silently move to a fixed reference on the first key press. */
        s->notice = 5;
        return;
    }
#endif
    if (!s->servo[ch].pulse && !fb_servo_enable(s, ch, now))
    {
        s->notice = 5;
        return;
    }
    if ((int32_t)(now - s->repeat_ms) >= 0)
    {
        s->repeat_ms = now + 50;
        if (!fb_servo_target(s, ch, (int32_t)s->servo[ch].target +
                            direction * s->config.jog_us, now))
            s->notice = 5;
    }
}
uint16_t fb_param_value(const fb_remote_t *s)
{
    const fb_config_t *c = &s->config;
    if (s->param == 0)
        return c->max_pwm / 10;
    if (s->param == 1)
        return c->turn_percent;
    if (s->param < 6)
        return c->gain[s->param - 2];
    switch (s->param)
    {
    case 6:
        return c->servo_min[s->selected];
    case 7:
        return c->servo_max[s->selected];
    case 8:
        return c->servo_reference[s->selected];
    case 9:
        return c->jog_us;
    case 10:
        return c->ramp_ms;
    case 11:
        return c->brake_pwm / 10;
    default:
        return c->brake_ms;
    }
}
int fb_param_adjust(fb_remote_t *s, int d, uint32_t now)
{
    fb_config_t c = s->config;
    uint16_t *p;
    int step = 1, value;
    unsigned i;
    if (d != 1 && d != -1)
        return 0;
    if (s->param == 0)
    {
        p = &c.max_pwm;
        step = 50;
    }
    else if (s->param == 1)
    {
        p = &c.turn_percent;
        step = 5;
    }
    else if (s->param < 6)
        p = &c.gain[s->param - 2];
    else
        switch (s->param)
        {
        case 6:
            p = &c.servo_min[s->selected];
            step = 10;
            break;
        case 7:
            p = &c.servo_max[s->selected];
            step = 10;
            break;
        case 8:
            p = &c.servo_reference[s->selected];
            step = 10;
            break;
        case 9:
            p = &c.jog_us;
            break;
        case 10:
            p = &c.ramp_ms;
            step = 50;
            break;
        case 11:
            p = &c.brake_pwm;
            step = 10;
            break;
        default:
            p = &c.brake_ms;
            step = 5;
            break;
        }
    value = (int)*p + d * step;
    if (value < 0 || value > 65535)
        return 0;
    *p = (uint16_t)value;
    if (!fb_config_valid(&c))
        return 0;
    for (i = 0; i < 3; i++)
        if (s->servo[i].pulse &&
            (s->servo[i].pulse < c.servo_min[i] || s->servo[i].pulse > c.servo_max[i] ||
             s->servo[i].target < c.servo_min[i] || s->servo[i].target > c.servo_max[i]))
            return 0;
    s->config = c;
    s->dirty = 1;
    s->last_change_ms = now;
    return 1;
}
void fb_remote_tick(fb_remote_t *s, uint8_t ready, uint32_t now)
{
    unsigned i;
    if (!ready || now - s->sample_ms >= FB_INPUT_TTL)
    {
        fb_remote_fault(s, now);
        return;
    }
    for (i = 0; i < 3; i++)
    {
        servo_motion_t candidate = s->servo[i];
        servo_motion_tick(&candidate, now);
        if (candidate.pulse &&
            (candidate.pulse < s->config.servo_min[i] || candidate.pulse > s->config.servo_max[i]))
        {
            servo_motion_freeze(&s->servo[i]);
            s->notice = 5;
        }
        else
            s->servo[i] = candidate;
    }
}
void fb_remote_sample(fb_remote_t *s, const fb_input_t *p, uint8_t ready, uint32_t now)
{
    uint16_t b, edge, chord, key;
    unsigned i;
    int is_neutral;
    if (!p || !p->valid || p->mode != 0x73 || !ready)
    {
        fb_remote_fault(s, now);
        return;
    }
    s->sample_ms = now;
    b = p->buttons;
    edge = b & ~s->previous_buttons;
    s->previous_buttons = b;
    is_neutral = neutral(p);
    if (!is_neutral)
    {
        s->neutral_ready = 0;
        s->neutral_ms = now;
        /* A hold must be continuously centered, not accumulated over excursions. */
        s->hold_chord = 0;
        s->hold_done = 0;
    }
    else if (now - s->neutral_ms >= 300)
        s->neutral_ready = 1;
    if (b != s->hold_chord)
    {
        s->hold_chord = 0;
        s->hold_done = 0;
    }
    if (b == (B(SELECT) | B(START)))
    {
        if (s->armed)
            s->stop_event = 1;
        zero(s);
        freeze(s);
        s->armed = 0;
        if (is_neutral && held(s, b, b, 2000, now))
        {
            s->mode = FB_TUNE;
            s->notice = 2;
        }
        return;
    }
    if (s->mode == FB_TUNE)
    {
        zero(s);
        s->armed = 0;
        if ((b & (B(L2) | B(R2) | B(START))) == (B(L2) | B(R2) | B(START)))
        {
            chord = B(L2) | B(R2) | B(START);
            if (is_neutral && held(s, chord, b, 3000, now))
            {
                fb_config_default(&s->config);
                s->dirty = 1;
                s->last_change_ms = now;
                s->notice = 3;
                freeze(s);
                for (i = 0; i < 3; i++)
                    servo_motion_reference(&s->servo[i], 0, now);
            }
            return;
        }
        if (b == (B(R1) | B(SELECT)))
        {
            int moving = 0;
            for (i = 0; i < 3; i++)
                moving |= s->servo[i].moving;
            if (is_neutral && !moving && now - s->last_change_ms >= 1000 &&
                (!s->have_saved || now - s->last_save_ms >= 30000) && held(s, b, b, 2000, now))
                s->save_request = 1;
            if (moving || now - s->last_change_ms < 1000 ||
                (s->have_saved && now - s->last_save_ms < 30000))
            {
                s->hold_chord = 0;
                s->hold_done = 0;
            }
            return;
        }
        if (b & B(L1))
        {
            if (edge & B(SQUARE))
                s->selected = 0;
            if (edge & B(TRIANGLE))
                s->selected = 1;
            if (edge & B(CIRCLE))
                s->selected = 2;
            return;
        }
        if (b & B(L2))
        {
            static const unsigned buttons[4] = {FB_TRIANGLE, FB_SQUARE, FB_CIRCLE, FB_CROSS};
            for (i = 0; i < 4; i++)
                if (edge & FB_MASK(buttons[i]))
                {
                    s->config.motor_negative ^= (uint16_t)(1U << i);
                    s->dirty = 1;
                    s->last_change_ms = now;
                }
            return;
        }
        if (edge & (B(START) | B(SELECT)))
        {
            s->mode = FB_LOCKED;
            s->neutral_ready = 0;
            s->neutral_ms = now;
            return;
        }
        if (edge & B(LEFT))
            s->param = (s->param + FB_PARAM_COUNT - 1) % FB_PARAM_COUNT;
        if (edge & B(RIGHT))
            s->param = (s->param + 1) % FB_PARAM_COUNT;
        if (edge & B(R3))
        {
            s->report_value = fb_param_value(s);
            s->report_request = 1;
        }
        key = b & (B(UP) | B(DOWN));
        if (key == (B(UP) | B(DOWN)))
            key = 0;
        if (key_step(s, key, edge, now) && !fb_param_adjust(s, key == B(UP) ? 1 : -1, now))
            s->notice = 5;
        return;
    }
    if (b & B(START))
    {
        if (edge & B(START))
            s->stop_event = 1;
        zero(s);
        freeze(s);
        s->armed = 0;
        s->mode = FB_LOCKED;
        return;
    }
    if (b & (B(L1) | B(L2)))
    {
        if (s->mode != FB_SERVO)
        {
            if (s->armed)
                s->stop_event = 1;
            zero(s);
            freeze(s);
            s->armed = 0;
            s->repeat_key = 0;
            s->mode = FB_SERVO;
        }
        /* One exact chord controls one channel. Opposite directions, diagonal
         * D-pad presses and simultaneous shoulders cannot enable any servo. */
        if (b == (B(L1) | B(UP)))
            servo_jog(s, 0, 1, b, now);       /* PB6: big arm */
        else if (b == (B(L1) | B(DOWN)))
            servo_jog(s, 0, -1, b, now);
        else if (b == (B(L1) | B(RIGHT)))
            servo_jog(s, 1, 1, b, now);       /* PB7: small arm */
        else if (b == (B(L1) | B(LEFT)))
            servo_jog(s, 1, -1, b, now);
        else if (b == (B(L2) | B(UP)))
            servo_jog(s, 2, 1, b, now);       /* PB8: door */
        else if (b == (B(L2) | B(DOWN)))
            servo_jog(s, 2, -1, b, now);
        else
        {
            freeze(s);
            s->repeat_key = 0;
        }
        return;
    }
#if 0 /* Retained legacy selection / long-hold takeover, superseded above. */
    if (b & B(L1))
    {
        if (s->mode != FB_SERVO)
        {
            if (s->armed)
                s->stop_event = 1;
            zero(s);
            freeze(s);
            s->armed = 0;
            s->mode = FB_SERVO;
        }
        if (edge & B(SQUARE))
            s->selected = 0;
        if (edge & B(TRIANGLE))
            s->selected = 1;
        if (edge & B(CIRCLE))
            s->selected = 2;
        if (b & B(R1))
        {
            chord = B(L1) | B(R1) | B(UP);
            if (is_neutral && held(s, chord, b, 1000, now) && !fb_servo_enable(s, s->selected, now))
                s->notice = 5;
            chord = B(L1) | B(R1) | B(DOWN);
            if (is_neutral && held(s, chord, b, 1000, now))
                servo_motion_reference(&s->servo[s->selected], 0, now);
            return;
        }
        key = b & (B(UP) | B(DOWN));
        if (key == (B(UP) | B(DOWN)))
            key = 0;
        if (key_step(s, key, edge, now) &&
            !fb_servo_target(s, s->selected,
                             (int32_t)s->servo[s->selected].target +
                                 (key == B(UP) ? 1 : -1) * s->config.jog_us,
                             now))
            s->notice = 5;
        return;
    }
#endif
    if (s->mode == FB_SERVO)
    {
        freeze(s);
        s->mode = FB_LOCKED;
        s->neutral_ms = now;
        s->neutral_ready = 0;
        s->repeat_key = 0;
    }
    /* No SELECT unlock. A clean centered sample allows driving immediately;
     * a deflected stick at power-up/reconnect/START release cannot restart it. */
    if (!s->armed && !b && is_neutral)
    {
        s->armed = 1;
        s->mode = FB_DRIVE;
    }
    zero(s);
    if (s->armed)
    {
        int t = axis(p->ly), r = -axis(p->rx) * (int)s->config.turn_percent / 100;
        int left = t + r, right = t - r, peak = left < 0 ? -left : left,
            other = right < 0 ? -right : right;
        if (other > peak)
            peak = other;
        if (peak < 1000)
            peak = 1000;
        for (i = 0; i < 4; i++)
        {
            int v = (i & 1) ? left : right;
            s->wheels[i] =
                (int16_t)((int32_t)v * s->config.max_pwm / peak * s->config.gain[i] / 100);
            if (s->config.motor_negative & (1U << i))
                s->wheels[i] = -s->wheels[i];
        }
        /* Main tracks moving->neutral and emits one operator stop event. */
    }
}
