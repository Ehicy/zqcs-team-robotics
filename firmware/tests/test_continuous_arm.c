#include "fallback.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
#if FB_CONTINUOUS_ARM_PROFILE
    fb_servo_probe_t probe = {0};
    assert(fb_servo_probe_poll(&probe, 0x530105c8, 1, 0, 0) == 1500);
    assert(fb_servo_probe_poll(&probe, 0x530205dc, 1, 0, 20) == 1500);
    assert(fb_servo_probe_poll(&probe, 0x530305c8, 1, 0, 40) == 1480);
    assert(fb_servo_probe_poll(&probe, 0x530305c8, 1, 0, 540) == 1500);
    assert(fb_servo_probe_poll(&probe, 0x530405c8, 1, 0, 560) == 1500);
    assert(fb_servo_probe_poll(&probe, 0x530505dc, 1, 0, 580) == 1500);
    assert(fb_servo_probe_poll(&probe, 0x530605f0, 1, 0, 600) == 1520);
    assert(fb_servo_probe_poll(&probe, 0x530705f0, 1, 1, 620) == 1500);
    assert(fb_servo_probe_poll(&probe, 0x530805dc, 1, 0, 640) == 1500);
    assert(fb_servo_probe_poll(&probe, 0x53090600, 1, 0, 660) == 1500);
    assert(fb_servo_probe_poll(&probe, 0, 1, 0, 680) == 1500);
    assert(fb_servo_probe_poll(&probe, 0, 1, 0, 700) == -1);
    fb_remote_t s;
    fb_input_t p = {0, 1, 0x73, 128, 128, 128, 128};
    unsigned t;
    fb_remote_init(&s, 0);
    assert(s.servo[0].pulse == 1500 && !s.servo[1].pulse && !s.servo[2].pulse);
    p.buttons = FB_MASK(FB_L1) | FB_MASK(FB_UP);
    fb_remote_sample(&s, &p, 1, 20);
    assert(s.servo[0].pulse == 1480 && !s.armed);
    for (t = 40; t <= 1020; t += 20) {
        fb_remote_sample(&s, &p, 1, t);
        fb_remote_tick(&s, 1, t);
    }
    assert(s.servo[0].pulse == 1500);
    fb_remote_sample(&s, &p, 1, 1040);
    assert(s.servo[0].pulse == 1500); /* Held key cannot renew burst. */
    p.buttons = FB_MASK(FB_L1);
    fb_remote_sample(&s, &p, 1, 1060);
    p.buttons |= FB_MASK(FB_DOWN);
    fb_remote_sample(&s, &p, 1, 1080);
    assert(s.servo[0].pulse == 1520);
    p.buttons = 0;
    fb_remote_sample(&s, &p, 1, 1100);
    assert(s.servo[0].pulse == 1500);
    p.buttons = FB_MASK(FB_L1) | FB_MASK(FB_UP);
    fb_remote_sample(&s, &p, 1, 1120);
    fb_remote_tick(&s, 1, 1240);
    assert(s.servo[0].pulse == 1500);
    fb_remote_sample(&s, &p, 1, 1260);
    p.valid = 0;
    fb_remote_sample(&s, &p, 1, 1280);
    assert(s.servo[0].pulse == 1500);
    p.valid = 1;
    fb_remote_sample(&s, &p, 1, 1300);
    p.buttons |= FB_MASK(FB_START);
    fb_remote_sample(&s, &p, 1, 1320);
    assert(s.servo[0].pulse == 1500);
    p.buttons = FB_MASK(FB_L1) | FB_MASK(FB_DOWN);
    fb_remote_sample(&s, &p, 1, 1340);
    fb_remote_fault(&s, 1360);
    assert(s.servo[0].pulse == 1500);
    p.buttons = FB_MASK(FB_L1) | FB_MASK(FB_RIGHT);
    fb_remote_sample(&s, &p, 1, 1380);
    assert(s.servo[0].pulse == 1500 && s.servo[1].target == 1505);
    assert(!fb_servo_target(&s, 0, 1600, 1400));
    puts("Continuous arm: neutral, reversed speed, release, burst, START and faults passed.");
#else
    fb_remote_t s;
    fb_servo_probe_t probe = {0};
    fb_input_t p = {0, 1, 0x73, 128, 128, 128, 128};
    unsigned t;
    fb_remote_init(&s, 0);
    assert(!s.servo[0].pulse);
    p.buttons = FB_MASK(FB_L1) | FB_MASK(FB_UP);
    for (t = 20; t < 1200; t += 20) {
        fb_remote_sample(&s, &p, 1, t);
        fb_remote_tick(&s, 1, t);
        assert(!s.servo[0].pulse); /* No guessed first position. */
    }
    assert(fb_servo_probe_poll(&probe, 0x530105dc, 1, 0, 0) == -1);
    assert(fb_servo_probe_poll(&probe, 0x530205c8, 1, 0, 20) == -1);
    assert(fb_servo_probe_poll(&probe, 0x530305f0, 1, 0, 40) == -1);
    fb_remote_fault(&s, 1220);
    assert(!s.servo[0].pulse);
    /* Only after a separately established commanded reference, relative
     * adjustment is permitted; this does not model physical angle feedback. */
    servo_motion_reference(&s.servo[0], 1420, 1240);
    fb_remote_sample(&s, &p, 1, 1260);
    assert(s.servo[0].target == 1425);
    p.buttons = 0;
    fb_remote_sample(&s, &p, 1, 1280);
    assert(s.servo[0].target == s.servo[0].pulse && s.servo[0].pulse == 1420);
    fb_remote_fault(&s, 1300);
    assert(s.servo[0].pulse == 1420); /* Never replace it with 1500. */
    p.buttons = FB_MASK(FB_L2) | FB_MASK(FB_UP);
    fb_remote_sample(&s, &p, 1, 1320);
    assert(!s.servo[2].pulse); /* Door also needs a known, deliberate reference. */
    puts("Unknown-position arm: no boot/first-key reference, old probe rejected, relative known-reference hold passed.");
#endif
    return 0;
}
