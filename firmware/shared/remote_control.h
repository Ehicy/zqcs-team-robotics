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
#define REMOTE_CONTROL_TARGET_LIMIT 300

enum {
    REMOTE_OPCODE_ARM=1, REMOTE_OPCODE_DRIVE=2, REMOTE_OPCODE_STOP=3,
    REMOTE_OPCODE_LED=4, REMOTE_OPCODE_MECHANICAL=5,
    REMOTE_OPCODE_TUNING=6, REMOTE_OPCODE_CLEAR_FAULT=7
};
enum {
    REMOTE_REASON_READY=0, REMOTE_REASON_LOCKED=1,
    REMOTE_REASON_EXPIRED=2, REMOTE_REASON_LINK=3,
    REMOTE_REASON_STOP=4, REMOTE_REASON_INVALID=5,
    REMOTE_REASON_QUEUE=6, REMOTE_REASON_NOT_READY=7
};
enum {
    REMOTE_ACTION_NONE=0, REMOTE_ACTION_DRIVE=1, REMOTE_ACTION_STOP=2,
    REMOTE_ACTION_LED=3, REMOTE_ACTION_ARM=4,
    REMOTE_ACTION_MECHANICAL=5, REMOTE_ACTION_TUNING=6,
    REMOTE_ACTION_CLEAR_FAULT=7
};

typedef struct {
    uint8_t kind, led_on, wheel, flags;
    int16_t targets[4]; /* M1 RF, M2 LF, M3 RR, M4 LR; requested mm/s */
    uint16_t ttl_ms; /* Remaining ORIGINAL receive-time lease. */
    uint16_t diameter_tenth_mm, kp_milli, ki_milli, max_mm_s;
    uint32_t counts_per_rev, source_sequence;
} remote_action_t;

typedef struct {
    uint32_t session, last_sequence, rejected;
    uint32_t received_ms, expires_ms;
    uint16_t ttl_ms;
    int16_t left_mm_s, right_mm_s;
    /* active denotes a live DRIVE lease, including a zero-speed request;
     * it does not assert that any motor is physically moving. */
    uint8_t have_sequence, armed, active, link_up, drive_ready, reason;
} remote_control_t;

void remote_control_init(remote_control_t *state, uint32_t session);
/* Rotate session, disarm and clear input/replay state. Preserve link/readiness
 * and rejection count. Call on BLE lifecycle/overflow or UART token changes,
 * and cancel the board-link input. No automatic re-arm after any reset. */
void remote_control_reset(remote_control_t *state, uint8_t reason);
/* Returns 1 when an input cancellation is required. Link transitions, loss
 * of drive readiness while armed, and expiration rotate the remote session.
 * An idle readiness change keeps a pending configuration ACK's session. */
int remote_control_tick(remote_control_t *state, uint32_t now,
                        uint8_t link_up, uint8_t drive_ready);
/* Single owner. tick with actual link/readiness before draining BLE events.
 * received_ms is the original callback time, never the drain time.
 * Return 1 only for accepted packets. Always inspect action on rejection:
 * current-session malformed/expired input may produce STOP. Foreign sessions
 * and valid replays do not renew the lease. STOP permits duplicate sequences.
 * ARM requires a neutral payload and idle state, and returns ARM, never STOP.
 * Configuration/clear-fault actions require disarmed&&!active; actual writes
 * and physical standstill checks belong to the STM32 backend and its ACK. */
int remote_control_receive(remote_control_t *state, const uint8_t *bytes,
                           size_t length, uint32_t received_ms, uint32_t now,
                           remote_action_t *action);

#ifdef __cplusplus
}
#endif
#endif
