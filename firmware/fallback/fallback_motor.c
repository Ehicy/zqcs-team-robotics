#include "fallback.h"
#include <string.h>
static int sign(int v) { return (v > 0) - (v < 0); }
void fb_motor_init(fb_motor_t *m, uint32_t now)
{
    memset(m, 0, sizeof(*m));
    m->last_ms = now;
}
void fb_motor_off(fb_motor_t *m, uint32_t now) { fb_motor_init(m, now); }
void fb_motor_stop(fb_motor_t *m, uint16_t pwm, uint16_t ms, uint32_t now)
{
    unsigned i;
    int any = 0;
    if (pwm > 50 || ms > 30)
    {
        fb_motor_off(m, now);
        return;
    }
    /* A held/repeated valid STOP must never extend the reverse window. Invalid
     * parameters still cut outputs, even in the middle of an existing pulse. */
    if (m->phase == FB_MOTOR_COAST || m->phase == FB_MOTOR_BRAKE)
        return;
    for (i = 0; i < 4; i++)
    {
        int v = m->output[i], mag = v < 0 ? -v : v;
        m->reverse[i] =
            (int16_t)(mag >= 100
                          ? -sign(v) *
                                (int)(mag < 1000 ? (uint32_t)pwm * (unsigned)mag / 1000U : pwm)
                          : 0);
        if (m->reverse[i])
            any = 1;
        m->output[i] = 0;
    }
    if (!any || !pwm || !ms)
    {
        fb_motor_off(m, now);
        return;
    }
    m->phase = FB_MOTOR_COAST;
    m->stop_ms = now;
    m->brake_ms = ms;
    m->last_ms = now;
}
void fb_motor_tick(fb_motor_t *m, const int16_t target[4], uint8_t enabled, uint16_t ramp,
                   uint32_t now)
{
    unsigned i;
    uint32_t dt = now - m->last_ms, age;
    int changed = 0, reversing = 0;
    m->last_ms = now;
    if (dt > 50 || !enabled || !target || ramp < 100 || ramp > 1500)
    {
        fb_motor_off(m, now);
        return;
    }
    for (i = 0; i < 4; i++)
        if (target[i] < -1000 || target[i] > 1000)
        {
            fb_motor_off(m, now);
            return;
        }
    if (m->phase == FB_MOTOR_COAST || m->phase == FB_MOTOR_BRAKE)
    {
        age = now - m->stop_ms;
        if (age >= 10U + m->brake_ms)
        {
            fb_motor_off(m, now);
            return;
        }
        if (age >= 10)
        {
            m->phase = FB_MOTOR_BRAKE;
            memcpy(m->output, m->reverse, sizeof(m->output));
        }
        return; /* Expired windows are skipped, never replayed late. */
    }
    for (i = 0; i < 4; i++)
    {
        if (target[i] != m->goal[i])
            changed = 1;
        if (m->output[i] && target[i] && sign(m->output[i]) != sign(target[i]))
            reversing = 1;
    }
    /* Direction changes decelerate to zero before accepting opposite torque.
     * Explicit operator quick-stop uses the separate capped reverse pulse. */
    if (reversing || m->phase == FB_MOTOR_REVERSING)
    {
        int any = 0;
        for (i = 0; i < 4; i++)
        {
            int step = (int)(dt * 4000U / 1000U), v = m->output[i];
            m->output[i] =
                (int16_t)(v > 0 ? (v > step ? v - step : 0) : (v < -step ? v + step : 0));
            any |= m->output[i] != 0;
        }
        memset(m->goal, 0, sizeof(m->goal));
        m->phase = FB_MOTOR_REVERSING;
        if (!any)
        {
            memset(m->reverse, 0, sizeof(m->reverse));
            m->phase = FB_MOTOR_COAST;
            m->stop_ms = now;
            m->brake_ms = 40;
        }
        return;
    }
    if (changed)
    {
        if (m->phase == FB_MOTOR_IDLE)
        {
            memcpy(m->origin, m->output, sizeof(m->origin));
            m->start_ms = now;
            m->duration_ms = ramp;
            m->phase = FB_MOTOR_RAMP;
        }
        memcpy(m->goal, target, sizeof(m->goal));
    }
    if (m->phase == FB_MOTOR_RAMP)
    {
        uint32_t x, w;
        age = now - m->start_ms;
        if (age >= m->duration_ms)
        {
            m->phase = FB_MOTOR_RUN;
        }
        x = age >= m->duration_ms ? 1000U : age * 1000U / m->duration_ms;
        /* Quintic 6x^5-15x^4+10x^3, scaled 1000. 64-bit below avoids overflow. */
        w = (uint32_t)((int64_t)x * x * x * (10000000L - 15000L * x + 6L * x * x) /
                       1000000000000LL);
        for (i = 0; i < 4; i++)
        {
            int candidate = m->origin[i] + (int32_t)(m->goal[i] - m->origin[i]) * (int32_t)w / 1000;
            int delta = candidate - m->output[i], step = (int)(dt * 20U);
            /* Stick retargeting cannot jump to the latest envelope value.
             * 20 permille/ms also bounds the final ramp endpoint. Fixed-target
             * startup remains quintic; moving targets are slew-bounded. */
            m->output[i] += (int16_t)(delta > step ? step : delta < -step ? -step : delta);
        }
    }
    else if (m->phase == FB_MOTOR_RUN)
        for (i = 0; i < 4; i++)
        {
            int delta = m->goal[i] - m->output[i], step = (int)(dt * 2000U / 1000U);
            m->output[i] += (int16_t)(delta > step ? step : delta < -step ? -step : delta);
        }
}
