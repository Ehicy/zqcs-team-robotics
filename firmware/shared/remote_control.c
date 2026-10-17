#include "remote_control.h"
#include <string.h>

static uint16_t get_u16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t get_i16(const uint8_t *p)
{
    uint16_t raw = get_u16(p);
    return raw < 32768U ? (int32_t)raw : (int32_t)raw - 65536;
}
static int sequence_newer(uint32_t current, uint32_t previous)
{
    uint32_t distance = current - previous;
    return distance != 0U && distance < 0x80000000UL;
}
static int32_t magnitude(int32_t value) { return value < 0 ? -value : value; }
static int all_zero(const uint8_t *p, unsigned start)
{
    unsigned i;
    for (i = start; i < REMOTE_CONTROL_PACKET_SIZE; ++i)
        if (p[i] != 0U) return 0;
    return 1;
}
static void clear_input(remote_control_t *s)
{
    s->active = 0;
    s->left_mm_s = s->right_mm_s = 0;
    s->received_ms = s->expires_ms = 0;
    s->ttl_ms = 0;
}

void remote_control_init(remote_control_t *s, uint32_t session)
{
    memset(s, 0, sizeof(*s));
    s->session = session != 0U ? session : 1U;
    s->reason = REMOTE_REASON_LOCKED;
}

void remote_control_reset(remote_control_t *s, uint8_t reason)
{
    s->session += 0x9e3779b9UL;
    if (s->session == 0U) s->session = 1U;
    s->armed = s->have_sequence = 0;
    s->last_sequence = 0;
    s->reason = reason;
    clear_input(s);
}

int remote_control_tick(remote_control_t *s, uint32_t now,
                        uint8_t link_up, uint8_t drive_ready)
{
    uint8_t old_link = s->link_up;
    s->link_up = link_up != 0U;
    s->drive_ready = drive_ready != 0U;
    if (old_link != s->link_up || (!s->link_up && (s->armed || s->active))) {
        remote_control_reset(s, REMOTE_REASON_LINK);
        return 1;
    }
    if (!s->drive_ready && (s->armed || s->active)) {
        remote_control_reset(s, REMOTE_REASON_NOT_READY);
        return 1;
    }
    if (s->active && (uint32_t)(now - s->received_ms) >= s->ttl_ms) {
        remote_control_reset(s, REMOTE_REASON_EXPIRED);
        return 1;
    }
    return 0;
}

static int reject(remote_control_t *s)
{ ++s->rejected; return 0; }

static int stop_reject(remote_control_t *s, remote_action_t *a, uint8_t reason)
{
    remote_control_reset(s, reason);
    a->kind = REMOTE_ACTION_STOP;
    return reject(s);
}

int remote_control_receive(remote_control_t *s, const uint8_t *p, size_t length,
                           uint32_t received_ms, uint32_t now, remote_action_t *a)
{
    uint8_t opcode;
    uint32_t sequence, age;
    uint16_t limit = 0, ttl = 0;
    int32_t throttle = 0, steer = 0, left, right, scale;
    memset(a, 0, sizeof(*a));
    if (remote_control_tick(s, now, s->link_up, s->drive_ready)) {
        a->kind = REMOTE_ACTION_STOP;
        return reject(s);
    }
    /* Unknown/foreign sessions cannot revoke the current phone's live lease. */
    if (p == NULL || length < 8U || get_u32(p + 4) != s->session) return reject(s);
    if (length != REMOTE_CONTROL_PACKET_SIZE || p[0] != REMOTE_CONTROL_VERSION ||
        p[2] != 0U || p[3] != 0U)
        return stop_reject(s, a, REMOTE_REASON_INVALID);
    opcode = p[1];
    sequence = get_u32(p + 8);
    a->source_sequence = sequence;
    switch (opcode) {
    case REMOTE_OPCODE_ARM:
    case REMOTE_OPCODE_STOP:
    case REMOTE_OPCODE_CLEAR_FAULT:
        if (!all_zero(p, 12U)) return stop_reject(s, a, REMOTE_REASON_INVALID);
        break;
    case REMOTE_OPCODE_DRIVE:
        throttle = get_i16(p + 12);
        steer = get_i16(p + 14);
        limit = get_u16(p + 16);
        ttl = get_u16(p + 18);
        if (throttle < -1000 || throttle > 1000 || steer < -1000 || steer > 1000 ||
            limit > REMOTE_CONTROL_TARGET_LIMIT || ttl < REMOTE_CONTROL_MIN_TTL_MS ||
            ttl > REMOTE_CONTROL_MAX_TTL_MS)
            return stop_reject(s, a, REMOTE_REASON_INVALID);
        break;
    case REMOTE_OPCODE_LED:
        if (p[12] > 1U || !all_zero(p, 13U))
            return stop_reject(s, a, REMOTE_REASON_INVALID);
        break;
    case REMOTE_OPCODE_MECHANICAL:
        if (p[12] > 3U || (p[13] & ~7U) != 0U || get_u16(p + 14) == 0U ||
            get_u16(p + 14) > 5000U || get_u32(p + 16) == 0U ||
            get_u32(p + 16) > 1000000UL)
            return stop_reject(s, a, REMOTE_REASON_INVALID);
        break;
    case REMOTE_OPCODE_TUNING:
        if (p[12] > 3U || p[13] != 0U || get_u16(p + 14) > 20000U ||
            get_u16(p + 16) > 20000U ||
            (get_u16(p + 14) == 0U && get_u16(p + 16) == 0U) ||
            get_u16(p + 18) == 0U || get_u16(p + 18) > REMOTE_CONTROL_TARGET_LIMIT)
            return stop_reject(s, a, REMOTE_REASON_INVALID);
        break;
    default:
        return stop_reject(s, a, REMOTE_REASON_INVALID);
    }
    /* A valid STOP is safe even if old in time or duplicated in sequence. */
    if (opcode == REMOTE_OPCODE_STOP) {
        remote_control_reset(s, REMOTE_REASON_STOP);
        a->kind = REMOTE_ACTION_STOP;
        return 1;
    }
    if (s->have_sequence && !sequence_newer(sequence, s->last_sequence)) return reject(s);
    age = now - received_ms;
    if (age >= REMOTE_CONTROL_MAX_AGE_MS || (opcode == REMOTE_OPCODE_DRIVE && age >= ttl))
        return stop_reject(s, a, REMOTE_REASON_EXPIRED);
    if (!s->link_up) {
        s->reason = REMOTE_REASON_LINK;
        return reject(s);
    }
    if (opcode == REMOTE_OPCODE_MECHANICAL || opcode == REMOTE_OPCODE_TUNING ||
        opcode == REMOTE_OPCODE_CLEAR_FAULT) {
        if (s->armed || s->active) return stop_reject(s, a, REMOTE_REASON_LOCKED);
    }
    if (opcode == REMOTE_OPCODE_ARM || opcode == REMOTE_OPCODE_DRIVE) {
        if (!s->drive_ready) {
            s->reason = REMOTE_REASON_NOT_READY;
            return reject(s);
        }
        if (opcode == REMOTE_OPCODE_DRIVE && !s->armed) return reject(s);
        if (opcode == REMOTE_OPCODE_ARM &&
            (s->active || s->left_mm_s != 0 || s->right_mm_s != 0))
            return stop_reject(s, a, REMOTE_REASON_LOCKED);
    }
    s->have_sequence = 1;
    s->last_sequence = sequence;
    switch (opcode) {
    case REMOTE_OPCODE_ARM:
        clear_input(s);
        s->armed = 1;
        s->reason = REMOTE_REASON_READY;
        a->kind = REMOTE_ACTION_ARM;
        return 1;
    case REMOTE_OPCODE_LED:
        a->kind = REMOTE_ACTION_LED;
        a->led_on = p[12];
        return 1;
    case REMOTE_OPCODE_MECHANICAL:
        a->kind = REMOTE_ACTION_MECHANICAL;
        a->wheel = p[12];
        a->flags = p[13];
        a->diameter_tenth_mm = get_u16(p + 14);
        a->counts_per_rev = get_u32(p + 16);
        return 1;
    case REMOTE_OPCODE_TUNING:
        a->kind = REMOTE_ACTION_TUNING;
        a->wheel = p[12];
        a->kp_milli = get_u16(p + 14);
        a->ki_milli = get_u16(p + 16);
        a->max_mm_s = get_u16(p + 18);
        return 1;
    case REMOTE_OPCODE_CLEAR_FAULT:
        a->kind = REMOTE_ACTION_CLEAR_FAULT;
        return 1;
    default: /* Validated DRIVE. */
        left = throttle + steer;
        right = throttle - steer;
        scale = 1000;
        if (magnitude(left) > scale) scale = magnitude(left);
        if (magnitude(right) > scale) scale = magnitude(right);
        s->left_mm_s = (int16_t)(left * (int32_t)limit / scale);
        s->right_mm_s = (int16_t)(right * (int32_t)limit / scale);
        s->active = 1;
        s->received_ms = received_ms;
        s->ttl_ms = ttl;
        s->expires_ms = received_ms + ttl;
        s->reason = REMOTE_REASON_READY;
        a->kind = REMOTE_ACTION_DRIVE;
        a->ttl_ms = (uint16_t)(ttl - age);
        /* Measured chassis order: M1 RF, M2 LF, M3 RR, M4 LR. */
        a->targets[0] = a->targets[2] = s->right_mm_s;
        a->targets[1] = a->targets[3] = s->left_mm_s;
        return 1;
    }
}
