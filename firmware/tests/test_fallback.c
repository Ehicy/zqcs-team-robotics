#include "calibration_flash.h"
#include "fallback_pins.h"
#include "fallback_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        ++checks;                                                                                  \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define K(x) FB_MASK(FB_##x)

/* Flash mock models one-way programming and a torn erase/program. */
#define BASE 0x0800F800UL
static uint8_t flash_bytes[2048];
static int fail_after = -1, operations, unlocked;
static unsigned writes;
static int fail_now(void) { return fail_after >= 0 && operations++ == fail_after; }
uint16_t calibration_flash_read(uint32_t a)
{
    CHECK(a >= BASE && a + 1 < BASE + sizeof(flash_bytes) && !(a & 1));
    return link_get_u16(flash_bytes + a - BASE);
}
void calibration_flash_begin(void)
{
    CHECK(!unlocked);
    unlocked = 1;
}
void calibration_flash_end(void)
{
    CHECK(unlocked);
    unlocked = 0;
}
uint8_t calibration_flash_erase(uint32_t a)
{
    CHECK(unlocked && (a == BASE || a == BASE + 1024));
    ++writes;
    if (fail_now())
    {
        memset(flash_bytes + a - BASE, 255, 512);
        return 0;
    }
    memset(flash_bytes + a - BASE, 255, 1024);
    return 1;
}
uint8_t calibration_flash_program(uint32_t a, uint16_t v)
{
    uint16_t old = calibration_flash_read(a);
    CHECK(unlocked && (old & v) == v);
    ++writes;
    if (fail_now())
    {
        link_put_u16(flash_bytes + a - BASE, (uint16_t)(old & (v | 0xff00U)));
        return 0;
    }
    link_put_u16(flash_bytes + a - BASE, v);
    return 1;
}

static fb_input_t pad;
static void pad_reset(void)
{
    memset(&pad, 0, sizeof(pad));
    pad.valid = 1;
    pad.mode = 0x73;
    pad.lx = pad.ly = pad.rx = pad.ry = 128;
}
static void sample(fb_remote_t *s, uint32_t now)
{
    fb_remote_sample(s, &pad, 1, now);
    fb_remote_tick(s, 1, now);
}
static void hold(fb_remote_t *s, uint16_t buttons, uint32_t start, uint32_t end)
{
    uint32_t t;
    pad.buttons = buttons;
    for (t = start; t <= end; t += 20)
        sample(s, t);
}
static void arm(fb_remote_t *s)
{
    pad_reset();
    fb_remote_init(s, 0);
    hold(s, 0, 0, 300);
    pad.buttons = 0; /* Centered input is enough; no SELECT unlock. */
    sample(s, 320);
    CHECK(s->armed && s->mode == FB_DRIVE);
    pad.buttons = 0;
    sample(s, 340);
}
static void test_remote(void)
{
    fb_remote_t s;
    unsigned i;
    pad_reset();
    fb_remote_init(&s, 0);
    CHECK(fb_config_valid(&s.config));
    for (i = 0; i < 3; i++)
        CHECK(!s.servo[i].pulse);
    pad.buttons = K(SELECT);
    sample(&s, 20);
    CHECK(!s.armed);
    hold(&s, 0, 40, 340);
    pad.buttons = K(SELECT);
    sample(&s, 360);
    CHECK(s.armed);
    s.config.max_pwm = 1000;
    pad.buttons = 0;
    pad.ly = 0;
    sample(&s, 380);
    CHECK(s.wheels[0] == 1000 && s.wheels[1] == -1000 && s.wheels[2] == 1000 &&
          s.wheels[3] == -1000);
    pad.ly = 255;
    sample(&s, 400);
    CHECK(s.wheels[0] < -980 && s.wheels[1] > 980);
    pad.ly = 128;
    pad.rx = 255;
    sample(&s, 420);
    CHECK(s.wheels[0] < 0 && s.wheels[1] < 0 && s.wheels[2] < 0 && s.wheels[3] < 0);
    pad.rx = 128;
    pad.lx = 0;
    sample(&s, 440);
    CHECK(!s.wheels[0] && !s.wheels[1]);
    pad_reset();
    pad.buttons = K(START);
    sample(&s, 460);
    CHECK(!s.armed && s.stop_event);
    s.stop_event = 0;
    sample(&s, 480);
    CHECK(!s.stop_event);
    s.stop_event = 1;
    pad.valid = 0;
    sample(&s, 500);
    CHECK(!s.stop_event && !s.armed);
    arm(&s);
    pad.mode = 0x41;
    sample(&s, 360);
    CHECK(!s.armed && !s.stop_event);
    arm(&s);
    fb_remote_tick(&s, 1, 460);
    CHECK(!s.armed);
    arm(&s);
    fb_remote_tick(&s, 0, 350);
    CHECK(!s.armed);

#if 0 /* Legacy long-hold servo UI, retained for reference. */
    arm(&s);
    pad.buttons = K(L1);
    sample(&s, 360);
    CHECK(s.mode == FB_SERVO && !s.armed && s.stop_event);
    CHECK(!s.servo[0].pulse);
    hold(&s, K(L1) | K(R1) | K(UP), 380, 1380);
    CHECK(s.servo[0].pulse == 1500);
    hold(&s, K(L1), 1400, 1400);
    pad.buttons = K(L1) | K(UP);
    sample(&s, 1420);
    CHECK(s.servo[0].target == 1505);
    hold(&s, K(L1) | K(UP), 1440, 1800);
    CHECK(s.servo[0].target == 1505);
    sample(&s, 1820);
    CHECK(s.servo[0].target == 1510);
    CHECK(!fb_servo_target(&s, 0, 1701, 1840));
    CHECK(!fb_servo_target(&s, 0, 1299, 1840));
    CHECK(!fb_servo_target(&s, 3, 1500, 1840));
    CHECK(!fb_servo_target(&s, 1, 1500, 1840));
    pad.buttons = 0;
    sample(&s, 1840);
    CHECK(s.mode == FB_LOCKED && !s.servo[0].moving);
    CHECK(s.servo[0].pulse && s.servo[0].pulse == s.servo[0].target);
    hold(&s, K(L1) | K(TRIANGLE), 1860, 1860);
    CHECK(s.selected == 1);
    hold(&s, K(L1) | K(CIRCLE), 1880, 1880);
    CHECK(s.selected == 2);
    hold(&s, K(L1) | K(R1) | K(UP), 1900, 2900);
    CHECK(s.servo[2].pulse == 1500);
    hold(&s, K(L1) | K(R1) | K(DOWN), 2920, 3920);
    CHECK(!s.servo[2].pulse);
    /* Breaking neutral resets the complete takeover hold, even with same buttons. */
    hold(&s, K(L1) | K(R1) | K(UP), 3940, 4540);
    pad.ly = 0;
    sample(&s, 4560);
    pad.ly = 128;
    hold(&s, K(L1) | K(R1) | K(UP), 4580, 5540);
    CHECK(!s.servo[2].pulse);
    sample(&s, 5580);
    CHECK(s.servo[2].pulse == 1500);
    fb_remote_fault(&s, 5600);
    CHECK(s.servo[2].pulse == 1500 && !s.servo[2].moving);
#endif
    fb_remote_init(&s, 0);
    pad_reset();
    hold(&s, K(SELECT) | K(START), 0, 1500);
    pad.rx = 0;
    sample(&s, 1520);
    pad.rx = 128;
    hold(&s, K(SELECT) | K(START), 1540, 3500);
    CHECK(s.mode != FB_TUNE);
    sample(&s, 3540);
    CHECK(s.mode == FB_TUNE);
    hold(&s, 0, 3560, 3560);
    pad.buttons = K(RIGHT);
    sample(&s, 3580);
    CHECK(s.param == 1);
    pad.buttons = K(R3);
    sample(&s, 3600);
    CHECK(s.report_request && s.report_value == 100);
    pad.buttons = K(L2) | K(TRIANGLE);
    sample(&s, 3620);
    CHECK(s.config.motor_negative == 11);
    sample(&s, 3640);
    CHECK(s.config.motor_negative == 11);
    hold(&s, K(R1) | K(SELECT), 3660, 6640);
    CHECK(s.save_request);
    s.save_request = 0;
    sample(&s, 6660);
    CHECK(!s.save_request);
    s.have_saved = 1;
    s.last_save_ms = 6600;
    hold(&s, 0, 6680, 6680);
    hold(&s, K(R1) | K(SELECT), 6700, 36000);
    CHECK(!s.save_request);
    hold(&s, K(R1) | K(SELECT), 36020, 38600);
    CHECK(s.save_request);
    s.save_request = 0;
    hold(&s, K(L2) | K(R2) | K(START), 38620, 41620);
    CHECK(s.config.motor_negative == 10);
    CHECK(s.last_change_ms == 41620 && s.dirty);
    for (i = 0; i < 3; i++)
        CHECK(!s.servo[i].pulse);
    pad.buttons = 0;
    sample(&s, 41640);
    pad.buttons = K(START);
    sample(&s, 41660);
    CHECK(s.mode == FB_LOCKED && !s.armed);
    puts("remote: state transitions, input expiry, mapping, chords and save gates passed");
}
static void test_simple_controls(void)
{
    fb_remote_t s;
    const uint16_t positive[3] = {K(L1)|K(UP), K(L1)|K(RIGHT), K(L2)|K(UP)};
    const uint16_t negative[3] = {K(L1)|K(DOWN), K(L1)|K(LEFT), K(L2)|K(DOWN)};
    unsigned ch, i;
    for (ch = 0; ch < 3; ch++)
    {
        fb_remote_init(&s, 0);
        pad_reset();
        sample(&s, 0);
        CHECK(s.armed);
        for (i = 0; i < 3; i++) CHECK(!s.servo[i].pulse);
        if (ch == 0 || ch == 2) {
            hold(&s, positive[ch], 10, 10);
            CHECK(!s.servo[ch].pulse); /* Unknown physical start must not jump. */
            CHECK(fb_servo_enable(&s, ch, 15)); /* Explicit reference in this model only. */
        }
        hold(&s, positive[ch], 20, 200);
        CHECK(!s.armed && s.selected == ch && s.servo[ch].pulse > 1500);
        CHECK(s.servo[ch].target > s.servo[ch].pulse);
        for (i = 0; i < 3; i++) if (i != ch) CHECK(!s.servo[i].pulse);
        /* Release just the direction, while still holding the shoulder. */
        hold(&s, ch == 2 ? K(L2) : K(L1), 220, 220);
        CHECK(!s.servo[ch].moving && s.servo[ch].pulse == s.servo[ch].target);
        pad.buttons = negative[ch];
        sample(&s, 240);
        CHECK(s.servo[ch].target < s.servo[ch].pulse);
        pad.buttons = 0;
        sample(&s, 260);
        CHECK(!s.servo[ch].moving && s.armed);
        CHECK(!fb_servo_target(&s, ch, s.config.servo_max[ch]+1, 280));
        CHECK(!fb_servo_target(&s, ch, s.config.servo_min[ch]-1, 280));
    }
    fb_remote_init(&s, 0);
    pad_reset();
    hold(&s, K(L1)|K(UP)|K(DOWN), 0, 40);
    hold(&s, K(L1)|K(L2)|K(UP), 60, 100);
    for (i = 0; i < 3; i++) CHECK(!s.servo[i].pulse);
    /* Pressing shoulder after direction also starts immediately. */
    hold(&s, K(UP), 120, 120);
    hold(&s, K(L1)|K(UP), 140, 140);
    CHECK(s.servo[0].target == 0); /* No automatic reference for the big arm. */
    hold(&s, K(L1)|K(RIGHT), 160, 160);
    CHECK(!s.servo[0].moving && s.servo[1].target == 1505);
    hold(&s, K(START)|K(L1)|K(RIGHT), 180, 180);
    CHECK(!s.servo[1].moving && !s.armed);
    pad.buttons = 0;
    pad.ly = 0;
    sample(&s, 200);
    CHECK(!s.armed && !s.wheels[0]);
    pad.ly = 128;
    sample(&s, 220);
    CHECK(s.armed);
    pad.ly = 0;
    sample(&s, 240);
    CHECK(s.wheels[0]);
    pad.valid = 0;
    sample(&s, 260);
    pad.valid = 1;
    sample(&s, 280);
    CHECK(!s.armed && !s.wheels[0]);
    pad.ly = 128;
    sample(&s, 300);
    CHECK(s.armed);
    puts("simple controls: direct three-servo jog, release freeze, limits, centered auto-drive passed");
}
static void test_parameters(void)
{
    fb_remote_t s;
    fb_config_t before;
    unsigned p, ch;
    int count;
    for (ch = 0; ch < 3; ch++)
        for (p = 0; p < FB_PARAM_COUNT; p++)
        {
            fb_remote_init(&s, 0);
            s.selected = (uint8_t)ch;
            s.param = (uint8_t)p;
            count = 0;
            while (fb_param_adjust(&s, 1, 10))
            {
                CHECK(fb_config_valid(&s.config));
                CHECK(++count < 500);
            }
            before = s.config;
            CHECK(!fb_param_adjust(&s, 1, 10));
            CHECK(!memcmp(&before, &s.config, sizeof(before)));
            count = 0;
            while (fb_param_adjust(&s, -1, 20))
            {
                CHECK(fb_config_valid(&s.config));
                CHECK(++count < 500);
            }
            before = s.config;
            CHECK(!fb_param_adjust(&s, -1, 20));
            CHECK(!memcmp(&before, &s.config, sizeof(before)));
        }
    fb_remote_init(&s, 0);
    CHECK(fb_servo_enable(&s, 0, 0));
    s.param = 6;
    s.config.servo_min[0] = 1500;
    CHECK(!fb_param_adjust(&s, 1, 20));
    s.param = 7;
    s.config.servo_max[0] = 1510;
    CHECK(fb_servo_target(&s, 0, 1510, 20));
    CHECK(!fb_param_adjust(&s, -1, 40));
    CHECK(!fb_param_adjust(&s, 0, 40));
    puts("parameters: all 13 parameters, three servo ranges and rejection boundaries passed");
}
static void run_to(fb_motor_t *m, const int16_t v[4], unsigned ramp, uint32_t start, uint32_t end)
{
    uint32_t t;
    for (t = start; t <= end; t++)
        fb_motor_tick(m, v, 1, (uint16_t)ramp, t);
}
static void test_motor(void)
{
    fb_motor_t m;
    int16_t v[4] = {1000, -1000, 500, -500};
    int old = 0;
    uint32_t t;
    fb_motor_init(&m, 0);
    fb_motor_tick(&m, v, 1, 500, 0);
    for (t = 1; t <= 500; t++)
    {
        fb_motor_tick(&m, v, 1, 500, t);
        CHECK(m.output[0] >= old && m.output[0] <= 1000);
        old = m.output[0];
        if (t == 100)
            CHECK(m.output[0] >= 55 && m.output[0] <= 60);
        if (t == 250)
            CHECK(m.output[0] == 500);
    }
    CHECK(m.phase == FB_MOTOR_RUN && m.output[0] == 1000 && m.output[1] == -1000);
    fb_motor_stop(&m, 50, 30, 500);
    CHECK(m.phase == FB_MOTOR_COAST && !m.output[0]);
    run_to(&m, v, 500, 501, 509);
    CHECK(!m.output[0]);
    fb_motor_tick(&m, v, 1, 500, 510);
    CHECK(m.output[0] == -50 && m.output[1] == 50);
    CHECK(m.output[2] == -25 && m.output[3] == 25);
    fb_motor_stop(&m, 50, 30, 520);
    CHECK(m.stop_ms == 500);
    run_to(&m, v, 500, 511, 539);
    CHECK(m.output[0] == -50);
    fb_motor_tick(&m, v, 1, 500, 540);
    CHECK(m.phase == FB_MOTOR_IDLE && !m.output[0]);
    fb_motor_stop(&m, 50, 30, 541);
    CHECK(m.phase == FB_MOTOR_IDLE);
    run_to(&m, v, 100, 542, 642);
    fb_motor_stop(&m, 50, 30, 642);
    fb_motor_tick(&m, v, 1, 500, 682);
    CHECK(m.phase == FB_MOTOR_IDLE && !m.output[0]);
    run_to(&m, v, 100, 683, 783);
    fb_motor_stop(&m, 50, 30, 783);
    fb_motor_tick(&m, v, 0, 500, 793);
    CHECK(m.phase == FB_MOTOR_IDLE && !m.output[0]);
    run_to(&m, v, 500, 794, 1094);
    old = m.output[0];
    v[0] = 100;
    fb_motor_tick(&m, v, 1, 500, 1095);
    CHECK(abs(m.output[0] - old) <= 20);
    run_to(&m, v, 500, 1096, 1500);
    CHECK(m.output[0] == 100);
    /* M1 reaches zero before M2: direction transition stays latched for all wheels. */
    v[0] = -100;
    v[1] = 1000;
    v[2] = -500;
    v[3] = 500;
    for (t = 1501; t < 1750; t++)
    {
        fb_motor_tick(&m, v, 1, 500, t);
        CHECK(m.output[0] >= 0);
    }
    fb_motor_tick(&m, v, 1, 500, 1750);
    CHECK(m.phase == FB_MOTOR_COAST);
    run_to(&m, v, 500, 1751, 1799);
    CHECK(!m.output[0] && !m.output[1]);
    run_to(&m, v, 500, 1800, 2301);
    CHECK(m.output[0] == -100 && m.output[1] == 1000);
    fb_motor_tick(&m, v, 1, 500, 2400);
    CHECK(!m.output[0]);
    v[0] = 1001;
    fb_motor_tick(&m, v, 1, 500, 2401);
    CHECK(!m.output[0]);
    fb_motor_off(&m, 0xfffffff0U);
    v[0] = 1000;
    run_to(&m, v, 100, 0xfffffff0U, 0xfffffffeU);
    fb_motor_tick(&m, v, 1, 100, 10);
    CHECK(m.phase == FB_MOTOR_RAMP);
    puts("motor: quintic ramp, retarget slew, direction coast, one-shot braking and fault-off "
         "passed");
}

static size_t transmit(fw_node_t *from, fw_node_t *to, uint32_t now, uint8_t *copy)
{
    uint8_t b[LINK_WIRE_MAX];
    size_t i, len = fw_transmit(from, now, b, sizeof(b));
    if (copy)
        memcpy(copy, b, len);
    for (i = 0; i < len; i++)
        fw_receive(to, b[i], now, now);
    return len;
}
static void handshake(fw_node_t *a, fw_node_t *b)
{
    fw_init(a, 0, 123, 0);
    fw_init(b, 1, 456, 0);
    CHECK(transmit(a, b, 0, 0));
    CHECK(b->connected && !a->connected);
    CHECK(transmit(b, a, 0, 0));
    CHECK(a->connected && a->token == b->token);
}
static void feed_frame(fw_node_t *n, link_frame_t *f, uint32_t arrival, uint32_t now)
{
    uint8_t b[LINK_WIRE_MAX];
    size_t i, len = link_encode(f, b, sizeof(b));
    CHECK(len);
    for (i = 0; i < len; i++)
        fw_receive(n, b[i], arrival, now);
}
static void test_wire(void)
{
    fw_node_t a, b;
    fb_config_t c;
    fb_motor_t m;
    int16_t v[4] = {1000, -1000, 1000, -1000};
    uint8_t replay[LINK_WIRE_MAX];
    size_t len, i;
    uint32_t t, accepted, oldtoken;
    link_frame_t frame;
    fb_config_default(&c);
    handshake(&a, &b);
    fb_motor_init(&m, 0);
    for (t = 20; t <= 620; t += 20)
    {
        fw_input(&a, v, 1, &c, t);
        CHECK(transmit(&a, &b, t, 0));
        fb_motor_tick(&m, b.target, b.enable, b.ramp, t);
        b.remote_enabled = 1;
        memcpy(b.status_output, m.output, sizeof(m.output));
        transmit(&b, &a, t, 0);
    }
    CHECK(m.output[0] == 1000 && a.remote_enabled);
    fw_stop(&a, 1);
    CHECK(transmit(&a, &b, 621, 0));
    CHECK(b.operator_stop && b.valid && b.stop_serial == 1);
    fb_motor_stop(&m, b.brake_pwm, b.brake_ms, 621);
    fw_input(&a, (const int16_t[4]){0, 0, 0, 0}, 0, &c, 640);
    CHECK(transmit(&a, &b, 641, 0));
    CHECK(b.valid && !b.enable);
    fb_motor_tick(&m, b.target, 1, b.ramp, 641);
    CHECK(m.output[0] == -50);
    fw_stop(&a, 0);
    CHECK(transmit(&a, &b, 642, 0));
    CHECK(!b.operator_stop && b.stop_serial == 2);
    fw_input(&a, v, 1, &c, 660);
    len = transmit(&a, &b, 662, replay);
    CHECK(len && b.enable);
    accepted = b.accepted;
    for (i = 0; i < len; i++)
        fw_receive(&b, replay[i], 663, 663);
    CHECK(b.accepted == accepted);
    replay[3] ^= 0x30;
    for (i = 0; i < len; i++)
        fw_receive(&b, replay[i], 664, 664);
    CHECK(b.accepted == accepted);
    fw_tick(&b, 780);
    CHECK(!b.valid && !b.enable);
    /* A's input expiry yields a disabled heartbeat, never an operator reverse. */
    CHECK(transmit(&a, &b, 781, 0));
    CHECK(!b.enable && !b.operator_stop);
    oldtoken = b.token;
    fw_receive(&b, 0, 800, 850);
    CHECK(!b.connected && b.token != oldtoken);
    handshake(&a, &b);
    fw_tick(&a, 300);
    CHECK(!a.connected);
    /* Expired STOP cannot acquire permission to reverse, even when CRC/session valid. */
    handshake(&a, &b);
    memset(&frame, 0, sizeof(frame));
    frame.type = FW_STOP;
    frame.session = b.token;
    frame.sequence = 1;
    frame.length = 16;
    link_put_u16(frame.payload + 8, 500);
    link_put_u16(frame.payload + 10, 50);
    frame.payload[12] = 30;
    frame.payload[13] = 1;
    link_put_u16(frame.payload + 14, 1);
    feed_frame(&b, &frame, 20, 21);
    CHECK(!b.valid && !b.operator_stop);
    frame.sequence = 2;
    link_put_u16(frame.payload + 14, 120);
    link_put_u16(frame.payload, 1001);
    accepted = b.accepted;
    feed_frame(&b, &frame, 22, 22);
    CHECK(b.accepted == accepted);
    frame.type = FW_CONTROL;
    link_put_u16(frame.payload, 1000);
    frame.session ^= 1;
    feed_frame(&b, &frame, 23, 23);
    CHECK(b.accepted == accepted);
    /* Queue timestamp age is checked before decoding; no stale replay after fault. */
    frame.session = b.token;
    feed_frame(&b, &frame, 24, 74);
    CHECK(!b.connected && !b.enable);
    puts("wire: two-peer motion/stop, CRC, replay, session, timeout and stale-queue tests passed");
}
static void test_store(void)
{
    fb_config_t c, loaded;
    uint8_t snapshot[2048];
    unsigned i, before;
    int result, cut;
    memset(flash_bytes, 255, sizeof(flash_bytes));
    fb_config_default(&c);
    CHECK(!fb_store_load(&loaded));
    CHECK(fb_store_save(&c) == 1);
    CHECK(fb_store_load(&loaded));
    CHECK(!memcmp(&c, &loaded, sizeof(c)));
    before = writes;
    CHECK(fb_store_save(&c) == 2 && writes == before);
    for (i = 0; i < 15; i++)
    {
        c.max_pwm = (uint16_t)(150 + 50 * i);
        CHECK(fb_store_save(&c) == 1);
    }
    memcpy(snapshot, flash_bytes, sizeof(snapshot));
    c.max_pwm = 950;
    /* Every operation boundary of a rollover, including a torn erase/program. */
    for (cut = 0; cut < 40; cut++)
    {
        memcpy(flash_bytes, snapshot, sizeof(snapshot));
        operations = 0;
        fail_after = cut;
        result = fb_store_save(&c);
        CHECK(!unlocked);
        fail_after = -1;
        CHECK(fb_store_load(&loaded));
        CHECK(loaded.max_pwm == 850 || loaded.max_pwm == 950);
        if (result)
            CHECK(loaded.max_pwm == 950);
    }
    memcpy(flash_bytes, snapshot, sizeof(snapshot));
    CHECK(fb_store_save(&c) == 1);
    flash_bytes[1024 + 42] ^= 1;
    CHECK(fb_store_load(&loaded) && loaded.max_pwm == 850);
    /* Partial first page writes cannot shadow the previously committed record. */
    memset(flash_bytes, 255, sizeof(flash_bytes));
    c.max_pwm = 100;
    CHECK(fb_store_save(&c) == 1);
    memcpy(snapshot, flash_bytes, sizeof(snapshot));
    c.max_pwm = 200;
    for (cut = 0; cut < 35; cut++)
    {
        memcpy(flash_bytes, snapshot, sizeof(snapshot));
        operations = 0;
        fail_after = cut;
        (void)fb_store_save(&c);
        fail_after = -1;
        CHECK(fb_store_load(&loaded));
        CHECK(loaded.max_pwm == 100 || loaded.max_pwm == 200);
    }
    c.max_pwm = 1001;
    before = writes;
    CHECK(!fb_store_save(&c) && writes == before);
    puts("flash: unchanged writes, journal rollover, CRC and injected power failures passed");
}
static void test_resources(void)
{
    const unsigned pwm[4] = FB_PWM_PINS, in1[4] = FB_IN1_PINS, in2[4] = FB_IN2_PINS;
    unsigned i, j, used = 0, pins[12];
    for (i = 0; i < 4; i++)
    {
        CHECK(pwm[i] == i); /* TIM2 default mapping PA0..PA3 */
        pins[3 * i] = pwm[i];
        pins[3 * i + 1] = in1[i];
        pins[3 * i + 2] = in2[i];
    }
    for (i = 0; i < 12; i++)
    {
        CHECK(pins[i] < 16 && !(used & (1U << pins[i])));
        CHECK(pins[i] < 11 || pins[i] == 15); /* USB11/12, SWD13/14 reserved */
        used |= 1U << pins[i];
    }
    CHECK(FB_STBY_PIN != FB_MOTOR_TX_PIN && FB_STBY_PIN != FB_MOTOR_RX_PIN);
    CHECK(FB_MOTOR_TX_PIN == 10 && FB_MOTOR_RX_PIN == 11);
    CHECK(FB_REMOTE_TX_PIN == 2 && FB_REMOTE_RX_PIN == 3);
    /* A-board PB6/7/8 servo and PB12/13/14/15 PS2 are disjoint. */
    for (i = 6; i <= 8; i++)
        for (j = 12; j <= 15; j++)
            CHECK(i != j);
    puts("resources: PA-only wheel signals, PB UART/STBY, USB and SWD reservations passed");
}
static void test_transitions_and_wrap(void)
{
    fw_node_t a, b;
    fb_config_t c;
    fb_motor_t m;
    int16_t v[4] = {1000, -1000, 400, -400}, previous[4];
    uint32_t t = 0, random = 0x19f103, base = 0xfffffff0U;
    unsigned i, j;
    fb_config_default(&c);
    fw_init(&a, 0, 111, base);
    fw_init(&b, 1, 222, base);
    CHECK(transmit(&a, &b, base, 0));
    CHECK(transmit(&b, &a, base, 0));
    fw_input(&a, v, 1, &c, base);
    CHECK(transmit(&a, &b, 4, 0));
    CHECK(b.valid && b.ttl == 100);
    fw_tick(&b, 103);
    CHECK(b.valid);
    fw_tick(&b, 104);
    CHECK(!b.valid);
    handshake(&a, &b);
    a.txseq = 0xffffffffU;
    b.rxseq = 0xfffffffeU;
    fw_input(&a, v, 1, &c, 20);
    CHECK(transmit(&a, &b, 20, 0));
    fw_input(&a, v, 1, &c, 40);
    CHECK(transmit(&a, &b, 40, 0));
    CHECK(b.rxseq == 0);
    fb_motor_init(&m, 0);
    for (i = 0; i < 40000; i++)
    {
        uint8_t active;
        random = random * 1664525U + 1013904223U;
        t += 1 + random % 8;
        active = (uint8_t)(random % 101 != 0);
        for (j = 0; j < 4; j++)
        {
            previous[j] = m.output[j];
            if (i % 100 == 0)
                v[j] = (int16_t)((int)((random >> (3 * j)) % 2001) - 1000);
        }
        if (i % 137 == 0)
            fb_motor_stop(&m, 50, 30, t);
        fb_motor_tick(&m, v, active, 500, t);
        for (j = 0; j < 4; j++)
        {
            CHECK(m.output[j] >= -1000 && m.output[j] <= 1000);
            if (previous[j] && m.output[j])
                CHECK((previous[j] > 0) == (m.output[j] > 0));
            if (!active)
                CHECK(!m.output[j]);
        }
    }
    puts("transitions: 40000 bounded state steps, no direct sign flip, clock/sequence wrap passed");
}
/* Exercise the same owner-routing functions called by BOTH MCU mains, not
 * just a synthetic motor target. The byte transport is local and no HAL runs. */
typedef struct
{
    fw_node_t a, b;
    fb_remote_t remote;
    fb_motor_t motor;
    uint32_t seen_a, seen_b, seen_stop, now;
} system_t;
static system_t system_state;
static void system_bytes(system_t *s, int from_motor, uint32_t now)
{
    uint8_t bytes[LINK_WIRE_MAX];
    fw_node_t *source = from_motor ? &s->b : &s->a;
    fw_node_t *dest = from_motor ? &s->a : &s->b;
    size_t i, length = fw_transmit(source, now, bytes, sizeof(bytes));
    for (i = 0; i < length; i++)
    {
        fw_receive(dest, bytes[i], now, now);
        if (!from_motor)
            (void)fw_motor_events(&s->b, &s->motor, &s->seen_b, &s->seen_stop, 1, now);
    }
}
static void system_to(system_t *s, uint32_t end)
{
    for (; s->now <= end; s->now++)
    {
        uint32_t now = s->now;
        fw_tick(&s->a, now);
        fw_tick(&s->b, now);
        if (s->seen_a != s->a.token)
        {
            fb_remote_fault(&s->remote, now);
            s->seen_a = s->a.token;
        }
        if (now % 20 == 0)
            fw_remote_sample(&s->a, &s->remote, &pad, 1, now);
        fb_remote_tick(&s->remote, (uint8_t)(s->a.connected && !s->a.remote_fault), now);
        system_bytes(s, 0, now);
        fw_motor_step(&s->b, &s->motor, 1, now);
        memcpy(s->b.status_output, s->motor.output, sizeof(s->motor.output));
        s->b.remote_enabled = (uint8_t)(s->motor.output[0] || s->motor.output[1] ||
                                        s->motor.output[2] || s->motor.output[3]);
        system_bytes(s, 1, now);
    }
}
static void test_complete_routing(void)
{
    system_t *s = &system_state;
    fb_motor_t old;
    uint32_t token, stop;
    unsigned i;
    memset(s, 0, sizeof(*s));
    pad_reset();
    fb_remote_init(&s->remote, 0);
    s->remote.config.max_pwm = 1000;
    fb_motor_init(&s->motor, 0);
    fw_init(&s->a, 0, 1234, 0);
    fw_init(&s->b, 1, 5678, 0);
    s->seen_a = s->a.token;
    s->seen_b = s->b.token;
    system_to(s, 300);
    CHECK(s->a.connected && s->b.connected && s->remote.armed);
    for (i = 0; i < 3; i++)
        CHECK(!s->remote.servo[i].pulse);
    pad.buttons = K(SELECT);
    system_to(s, 320);
    CHECK(s->remote.armed);
    pad.buttons = 0;
    pad.ly = 0;
    system_to(s, 840);
    CHECK(s->motor.output[0] == 1000);
    system_to(s, 880);
    pad.ly = 128;
    system_to(s, 900);
    CHECK(s->motor.phase == FB_MOTOR_COAST && !s->motor.output[0]);
    system_to(s, 909);
    CHECK(!s->motor.output[0]);
    system_to(s, 910);
    CHECK(s->motor.output[0] == -50);
    pad.buttons = K(START);
    system_to(s, 920);
    CHECK(!s->remote.armed && s->motor.stop_ms == 900 && s->motor.output[0] == -50);
    system_to(s, 939);
    CHECK(s->motor.output[0] == -50);
    system_to(s, 940);
    CHECK(s->motor.phase == FB_MOTOR_IDLE && !s->motor.output[0]);
    stop = s->b.stop_serial;
    system_to(s, 1000);
    CHECK(s->b.stop_serial == stop);
    pad.buttons = 0;
    system_to(s, 1320);
    pad.buttons = K(SELECT);
    system_to(s, 1340);
    CHECK(s->remote.armed);
    pad.buttons = 0;
    pad.ly = 0;
    system_to(s, 1440);
    CHECK(s->motor.output[0] > 0);
    pad.valid = 0;
    system_to(s, 1460);
    CHECK(!s->remote.armed && s->motor.phase == FB_MOTOR_IDLE && !s->motor.output[0]);
    pad_reset();
    system_to(s, 1500);
    fw_fault(&s->b, 1500);
    system_to(s, 2000);
    CHECK(s->a.connected && s->b.connected && s->remote.armed && !s->motor.output[0]);
    pad.buttons = K(L1) | K(UP);
    system_to(s, 2200);
    CHECK(!s->remote.servo[0].pulse);
    CHECK(fb_servo_enable(&s->remote, 0, 2200)); /* Explicit modeled reference. */
    pad.buttons = K(L1);
    system_to(s, 3020);
    CHECK(!s->remote.servo[0].moving && !s->motor.output[0]);
    pad.buttons = K(L1);
    system_to(s, 3040);
    pad.buttons = K(L1) | K(UP);
    system_to(s, 3060);
    CHECK(s->remote.servo[0].target > s->remote.servo[0].pulse);
    system_to(s, 3200);
    CHECK(s->remote.servo[0].pulse > 1500 && s->remote.servo[0].pulse <= 1700);
    pad.buttons = 0;
    system_to(s, 3220);
    CHECK(!s->remote.servo[0].moving && s->remote.servo[0].target == s->remote.servo[0].pulse);
    CHECK(!s->motor.output[0] && s->remote.armed);
    /* Actual event glue must not clear a watchdog using expired/braking STOP. */
    old = s->motor;
    token = s->b.token;
    stop = s->b.stop_serial;
    s->b.stop_serial++;
    s->b.valid = 0;
    s->b.operator_stop = 0;
    CHECK(fw_motor_events(&s->b, &old, &token, &stop, 0, 3221) == FW_OUTPUT_OFF);
    s->b.stop_serial++;
    s->b.valid = 1;
    s->b.operator_stop = 1;
    CHECK(fw_motor_events(&s->b, &old, &token, &stop, 0, 3222) == FW_OUTPUT_OFF);
    s->b.stop_serial++;
    s->b.operator_stop = 0;
    CHECK(fw_motor_events(&s->b, &old, &token, &stop, 0, 3223) ==
          (FW_OUTPUT_OFF | FW_CLEAR_WATCHDOG));
    puts("integration: PS2->owner routing->UART->motor ramp/brake, reconnect lock and servo "
         "takeover passed");
}
static void test_brake_fault_priority(void)
{
    fb_motor_t m;
    int16_t target[4] = {1000, 1000, 1000, 1000};
    fb_motor_init(&m, 0);
    run_to(&m, target, 100, 0, 100);
    fb_motor_stop(&m, 50, 30, 100);
    fb_motor_tick(&m, target, 1, 100, 110);
    CHECK(m.phase == FB_MOTOR_BRAKE && m.output[0] == -50);
    target[3] = 1001;
    fb_motor_tick(&m, target, 1, 100, 111);
    CHECK(m.phase == FB_MOTOR_IDLE && !m.output[0]);
    target[3] = 1000;
    run_to(&m, target, 100, 112, 212);
    fb_motor_stop(&m, 50, 30, 212);
    fb_motor_tick(&m, target, 1, 100, 222);
    CHECK(m.output[0] == -50);
    fb_motor_stop(&m, 51, 30, 223);
    CHECK(m.phase == FB_MOTOR_IDLE && !m.output[0]);
    puts("brake priority: invalid target or stop parameters cut an ongoing reverse pulse");
}
static void test_l3_led(void)
{
    fb_l3_led_t led = {0};
    fb_input_t input = {0};
    input.valid = 1;
    input.mode = 0x41;
    input.buttons = K(L3);
    CHECK(fb_l3_led_output(&led, 0) == -1);
    fb_l3_led_sample(&led, &input, 100);
    CHECK(fb_l3_led_output(&led, 100) == 0);
    CHECK(fb_l3_led_output(&led, 180) == 1);
    CHECK(fb_l3_led_output(&led, 379) == 1);
    CHECK(fb_l3_led_output(&led, 380) == 0);
    CHECK(fb_l3_led_output(&led, 500) == -1);
    fb_l3_led_sample(&led, &input, 600);
    CHECK(!led.active);
    input.valid = 0;
    input.buttons = 0;
    fb_l3_led_sample(&led, &input, 700);
    input.valid = 1;
    input.buttons = K(L3);
    fb_l3_led_sample(&led, &input, 800);
    CHECK(!led.active); /* Invalid frame did not fake a release. */
    input.mode = 0;
    input.buttons = 0;
    fb_l3_led_sample(&led, &input, 900);
    CHECK(led.down);
    input.mode = 0x73;
    fb_l3_led_sample(&led, &input, 1000);
    input.buttons = K(L3);
    fb_l3_led_sample(&led, &input, UINT32_MAX - 100U);
    CHECK(fb_l3_led_output(&led, 0) == 1);
    CHECK(fb_l3_led_output(&led, 299) == -1);
    fb_l3_led_sample(&led, NULL, 400);
    puts("L3: one pulse, held/invalid-frame suppression, digital/analog and clock wrap passed");
}
static void test_stlink_bench(void)
{
    fb_bench_t s = {0};
    uint32_t t;
    CHECK(fb_bench_poll(&s, 0, 1, 0, 0) == -1);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 1, 1, 0, 10) == 1);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 1, 1, 0, 759) == 1);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 2, 1, 0, 760) == 0);
    CHECK(s.state == FB_BENCH_EXPIRED);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 3, 1, 0, 761) == 0);
    CHECK(fb_bench_poll(&s, 0, 1, 0, 762) == 0);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 4, 1, 0, 1000) == 1);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 5, 0, 0, 1010) == 0);
    CHECK(s.state == FB_BENCH_FAULT);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 6, 1, 0, 1020) == 0);
    fb_bench_poll(&s, 0, 1, 0, 1021);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 7, 1, 1, 1022) == 0);
    CHECK(s.state == FB_BENCH_STOPPED);
    fb_bench_poll(&s, 0, 1, 0, 1023);
    CHECK(fb_bench_poll(&s, 0x12345678, 1, 0, 1024) == 0);
    fb_bench_poll(&s, 0, 1, 0, 1025);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 1, 1, 0, UINT32_MAX - 100U) == 1);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 2, 1, 0, 0) == 1);
    fb_bench_poll(&s, 0, 1, 0, 1);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 1, 1, 0, 1000) == 1);
    for (t = 1200; t < 121000; t += 200)
        CHECK(fb_bench_poll(&s, FB_BENCH_KEY | (t & 0xffffU), 1, 0, t) == 1);
    CHECK(fb_bench_poll(&s, FB_BENCH_KEY | 2, 1, 0, 121000) == 0);
    CHECK(s.state == FB_BENCH_DONE);
    puts("bench: explicit lease, timeout before renewal, no fault restart, STOP and hard duration passed");
}
int main(void)
{
    test_stlink_bench();
    test_l3_led();
    test_resources();
    test_remote();
    test_simple_controls();
    test_parameters();
    test_motor();
    test_wire();
    test_store();
    test_transitions_and_wrap();
    test_complete_routing();
    test_brake_fault_priority();
    printf("Fallback: %u assertions passed (host only; no physical hardware test).\n", checks);
    return 0;
}
