#ifndef CRTC_REMOTE_CONTROL_H
#define CRTC_REMOTE_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REMOTE_CONTROL_PACKET_SIZE 20U
#define REMOTE_CONTROL_VERSION 1U
#define REMOTE_CONTROL_MAX_AGE_MS 200U
#define REMOTE_CONTROL_MIN_TTL_MS 50U
#define REMOTE_CONTROL_MAX_TTL_MS 200U
#define REMOTE_CONTROL_TARGET_LIMIT 1000

enum {
    REMOTE_OPCODE_ARM=1, REMOTE_OPCODE_DRIVE=2,
    REMOTE_OPCODE_STOP=3, REMOTE_OPCODE_LED=4
};
enum {
    REMOTE_REASON_READY=0, REMOTE_REASON_LOCKED=1,
    REMOTE_REASON_EXPIRED=2, REMOTE_REASON_LINK=3,
    REMOTE_REASON_STOP=4, REMOTE_REASON_INVALID=5,
    REMOTE_REASON_QUEUE=6
};
enum {
    REMOTE_ACTION_NONE=0, REMOTE_ACTION_DRIVE=1,
    REMOTE_ACTION_STOP=2, REMOTE_ACTION_LED=3
};

typedef struct {
    uint8_t kind, led_on;
    int16_t targets[4]; /* left, right, left, right; requested mm/s */
    uint16_t ttl_ms; /* Remaining lease at receive(now), never the original TTL. */
} remote_action_t;

typedef struct {
    uint32_t session, last_sequence, rejected;
    uint32_t received_ms, expires_ms;
    uint16_t ttl_ms;
    int16_t left_mm_s, right_mm_s;
    uint8_t have_sequence, armed, active, link_up, reason;
} remote_control_t;

/* Single owner only. The BLE adapter must serialize callbacks into the owner
 * loop, invalidate its queue on connection changes, and protect lifecycle/STOP
 * signals from queue overflow. Session generations are NOT authentication.
 * Seed session from device randomness; zero is normalized to one. */
void remote_control_init(remote_control_t *state, uint32_t session);
/* Rotate generation, disarm, and clear input/replay state. Preserves link_up
 * and rejection counters. Caller must also cancel the board-link input. */
void remote_control_reset(remote_control_t *state, uint8_t reason);
/* Call before processing queued events and when UART connected/token changes.
 * Return nonzero when the caller must cancel the board-link input. A token
 * change with link_up unchanged requires an explicit reset(LINK). */
int remote_control_tick(remote_control_t *state, uint32_t now, uint8_t link_up);
/* Call tick with the actual UART state before receive. received_ms is the time
 * the complete BLE packet was first received, not when the queue was drained.
 * Return one only for accepted packets. ALWAYS inspect action, even on zero:
 * a rejected current-session packet may return STOP to enforce fail-closed.
 * Short packets without a complete session and foreign sessions only reject.
 * Rejected duplicates never renew the lease. STOP permits duplicate sequences.
 * ARM has a strictly zero (neutral) payload and returns STOP to clear old input.
 * The adapter supplies its own persistent source_sequence to the board link. */
int remote_control_receive(remote_control_t *state, const uint8_t *bytes,
                           size_t length, uint32_t received_ms, uint32_t now,
                           remote_action_t *action);

#ifdef __cplusplus
}
#endif
#endif
