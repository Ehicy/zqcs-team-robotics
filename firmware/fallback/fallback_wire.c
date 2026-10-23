#include "fallback_wire.h"
#include <string.h>
static int16_t signed16(const uint8_t *p)
{
    uint16_t u = link_get_u16(p);
    return (int16_t)(u < 32768 ? (int32_t)u : (int32_t)u - 65536);
}
void fw_init(fw_node_t *n, uint8_t role, uint32_t boot, uint32_t now)
{
    memset(n, 0, sizeof(*n));
    n->role = role;
    n->boot = boot ? boot : 1;
    n->token = n->boot;
    n->last_rx = now;
    n->last_tx = now - 100;
    n->ramp = 500;
    n->brake_pwm = 50;
    n->brake_ms = 30;
}
void fw_fault(fw_node_t *n, uint32_t now)
{
    uint32_t boot = n->boot + 0x9e3779b9UL;
    uint8_t role = n->role;
    fw_init(n, role, boot ? boot : 1, now);
}
void fw_tick(fw_node_t *n, uint32_t now)
{
    if (n->connected && now - n->last_rx >= 300)
    {
        fw_fault(n, now);
        return;
    }
    if (n->valid && now - n->input_ms >= n->ttl)
    {
        n->valid = n->enable = 0;
        memset(n->target, 0, sizeof(n->target));
    }
}
int fw_receive(fw_node_t *n, uint8_t byte, uint32_t arrival, uint32_t now)
{
    link_frame_t f;
    unsigned i;
    fw_tick(n, now);
    if (now - arrival >= 50)
    {
        fw_fault(n, now);
        return 0;
    }
    if (!link_parser_feed(&n->parser, byte, arrival, &f))
        return 0;
    if (n->role && f.type == FW_HELLO && f.length == 0 && f.session)
    {
        if (n->peer == f.session && n->have_seq && !link_sequence_newer(f.sequence, n->rxseq))
            return 0;
        n->peer = f.session;
        n->rxseq = f.sequence;
        n->have_seq = 1;
        n->valid = n->enable = 0;
        n->token = (n->token + 0x9e3779b9UL) ^ f.session ^ n->boot;
        if (!n->token)
            n->token = 1;
        n->ack = n->connected = 1;
        n->last_rx = now;
        return 1;
    }
    if (!n->role && f.type == FW_ACK && f.length == 8 && f.session == n->boot &&
        link_get_u32(f.payload + 4) == FW_TAG)
    {
        if (n->have_seq && !link_sequence_newer(f.sequence, n->rxseq))
            return 0;
        if (!link_get_u32(f.payload))
            return 0;
        n->rxseq = f.sequence;
        n->have_seq = 1;
        n->token = link_get_u32(f.payload);
        n->connected = 1;
        n->last_rx = now;
        n->valid = n->enable = 0;
        return 1;
    }
    if (!n->connected || f.session != n->token ||
        (n->have_seq && !link_sequence_newer(f.sequence, n->rxseq)))
        return 0;
    if (n->role && (f.type == FW_CONTROL || f.type == FW_STOP) && f.length == 16)
    {
        uint16_t ttl = link_get_u16(f.payload + 14), ramp = link_get_u16(f.payload + 8),
                 pwm = link_get_u16(f.payload + 10);
        if (!ttl || ttl > FB_INPUT_TTL || ramp < 100 || ramp > 1500 || pwm > 50 ||
            f.payload[12] > 30 || f.payload[13] > 1)
            return 0;
        for (i = 0; i < 4; i++)
        {
            int16_t v = signed16(f.payload + 2 * i);
            if (v < -1000 || v > 1000 || ((f.type == FW_STOP || !f.payload[13]) && v))
                return 0;
        }
        n->enable = f.type == FW_CONTROL ? f.payload[13] : 0;
        n->operator_stop = f.type == FW_STOP ? f.payload[13] : 0;
        for (i = 0; i < 4; i++)
            n->target[i] = signed16(f.payload + 2 * i);
        n->ramp = ramp;
        n->brake_pwm = pwm;
        n->brake_ms = f.payload[12];
        n->ttl = (uint16_t)(ttl > (now - arrival) ? ttl - (now - arrival) : 0);
        n->input_ms = now;
        n->valid = n->ttl != 0;
        if (!n->valid)
        {
            n->enable = 0;
            n->operator_stop = 0;
        }
        n->accepted++;
        if (f.type == FW_STOP)
            n->stop_serial++;
    }
    else if (!n->role && f.type == FW_STATUS && f.length == 10 && f.payload[8] <= 1 &&
             f.payload[9] <= 1)
    {
        for (i = 0; i < 4; i++)
        {
            int v = signed16(f.payload + 2 * i);
            if (v < -1000 || v > 1000)
                return 0;
        }
        for (i = 0; i < 4; i++)
            n->status_output[i] = signed16(f.payload + 2 * i);
        n->remote_enabled = f.payload[8];
        n->remote_fault = f.payload[9];
    }
    else
        return 0;
    n->rxseq = f.sequence;
    n->have_seq = 1;
    n->last_rx = now;
    return 1;
}
void fw_input(fw_node_t *n, const int16_t v[4], uint8_t enabled, const fb_config_t *c,
              uint32_t sampled)
{
    unsigned i;
    if (n->role || !v || enabled > 1 || !fb_config_valid(c))
        return;
    for (i = 0; i < 4; i++)
        if (v[i] < -1000 || v[i] > 1000 || (!enabled && v[i]))
            return;
    memcpy(n->target, v, sizeof(n->target));
    n->enable = enabled;
    n->valid = 1;
    n->ttl = FB_INPUT_TTL;
    n->input_ms = sampled;
    n->ramp = c->ramp_ms;
    n->brake_pwm = c->brake_pwm;
    n->brake_ms = c->brake_ms;
}
void fw_stop(fw_node_t *n, uint8_t op)
{
    if (!n->role)
    {
        n->enable = n->valid = 0;
        memset(n->target, 0, sizeof(n->target));
        n->pending_stop = 1;
        n->pending_operator = op ? 1 : 0;
    }
}
size_t fw_transmit(fw_node_t *n, uint32_t now, uint8_t *out, size_t cap)
{
    link_frame_t f;
    size_t len;
    unsigned i;
    fw_tick(n, now);
    memset(&f, 0, sizeof(f));
    f.sequence = n->txseq;
    if (n->role)
    {
        if (!n->connected)
            return 0;
        if (n->ack)
        {
            f.type = FW_ACK;
            f.session = n->peer;
            f.length = 8;
            link_put_u32(f.payload, n->token);
            link_put_u32(f.payload + 4, FW_TAG);
        }
        else if (now - n->last_tx >= 50)
        {
            f.type = FW_STATUS;
            f.session = n->token;
            f.length = 10;
            for (i = 0; i < 4; i++)
                link_put_u16(f.payload + 2 * i, (uint16_t)n->status_output[i]);
            f.payload[8] = n->remote_enabled;
            f.payload[9] = n->remote_fault;
        }
        else
            return 0;
    }
    else if (!n->connected)
    {
        if (now - n->last_tx < 100)
            return 0;
        f.type = FW_HELLO;
        f.session = n->boot;
    }
    else
    {
        if (!n->pending_stop && now - n->last_tx < 20)
            return 0;
        f.type = n->pending_stop ? FW_STOP : FW_CONTROL;
        f.session = n->token;
        f.length = 16;
        if (!n->pending_stop && n->valid)
        {
            for (i = 0; i < 4; i++)
                link_put_u16(f.payload + 2 * i, (uint16_t)n->target[i]);
            f.payload[13] = n->enable;
        }
        if (n->pending_stop)
            f.payload[13] = n->pending_operator;
        link_put_u16(f.payload + 8, n->ramp);
        link_put_u16(f.payload + 10, n->brake_pwm);
        f.payload[12] = (uint8_t)n->brake_ms;
        link_put_u16(f.payload + 14,
                     n->valid ? (uint16_t)(n->ttl - (now - n->input_ms)) : FB_INPUT_TTL);
    }
    len = link_encode(&f, out, cap);
    if (len)
    {
        n->last_tx = now;
        n->txseq++;
        n->ack = 0;
        n->pending_stop = 0;
    }
    return len;
}
