#ifndef CRTC_BOARD_LINK_H
#define CRTC_BOARD_LINK_H
#include <stdint.h>
#include <stddef.h>
#include "board_link_config.h"
#ifdef __cplusplus
extern "C" {
#endif

#define LINK_HEADER_SIZE 12U
#define LINK_BODY_MAX (LINK_HEADER_SIZE + LINK_MAX_PAYLOAD + 2U)
#define LINK_WIRE_MAX (LINK_BODY_MAX + 3U)
enum { LINK_HELLO=1, LINK_HELLO_ACK=2, LINK_CONTROL=3, LINK_STATUS=4, LINK_ENCODERS=5,
       LINK_LED_SET=6, LINK_LED_ACK=7, LINK_PWM=8,
       LINK_SPEED_CONFIG=9, LINK_CONFIG_ACK=10, LINK_CLEAR_FAULT=11, LINK_SPEED=12 };
#define LINK_LED_TIMEOUT_MS 500U
enum { LINK_CLIENT=0, LINK_CONTROLLER=1 };
enum { LINK_WAITING=0, LINK_CONTROL_EXPIRED=1, LINK_PEER_EXPIRED=2,
       LINK_TRANSPORT_ERROR=3 };
enum { LINK_FLAG_CONNECTED=1, LINK_FLAG_COMMAND_VALID=2,
       LINK_FLAG_OUTPUT_INHIBITED=4, LINK_FLAG_PWM_SUPPORTED=8, LINK_FLAG_SPEED_SUPPORTED=16 };
typedef struct {
    uint8_t type, length;
    uint32_t sequence, session;
    uint8_t payload[LINK_MAX_PAYLOAD];
} link_frame_t;
typedef struct {
    uint8_t bytes[LINK_BODY_MAX+1U];
    uint8_t used, discard;
    uint32_t last_byte_ms, errors;
} link_parser_t;
uint16_t link_crc16(const uint8_t *data, size_t length);
size_t link_encode(const link_frame_t *frame, uint8_t *out, size_t capacity);
void link_parser_init(link_parser_t *parser);
int link_parser_feed(link_parser_t *parser, uint8_t byte, uint32_t now,
                     link_frame_t *frame);
void link_put_u16(uint8_t *p, uint16_t value);
void link_put_u32(uint8_t *p, uint32_t value);
uint16_t link_get_u16(const uint8_t *p);
uint32_t link_get_u32(const uint8_t *p);
int link_sequence_newer(uint32_t candidate, uint32_t previous);

/* CONTROL is closed-loop wheel mm/s; PWM is open-loop signed permille.
 * They must never share a physical interpretation. PWM requires an explicitly
 * enabled controller backend and its advertised capability. */
typedef struct {
    link_parser_t parser;
    uint8_t role, connected, command_valid, requested_enable, reason;
    uint8_t have_peer_sequence, have_input_sequence, input_enable, input_valid;
    uint8_t ack_pending;
    uint32_t boot_id, token, peer_id, tx_sequence, peer_sequence, input_sequence;
    uint32_t last_rx_ms, last_tx_ms, command_ms, input_ms;
    uint32_t accepted_controls, rejected_frames, last_control_sequence;
    uint16_t command_ttl_ms, input_ttl_ms;
    int16_t requested_mm_s[4], input_mm_s[4];
    int16_t requested_pwm[4], input_pwm[4];
    uint8_t requested_mode, input_mode, pwm_supported, output_inhibited;
    uint8_t stop_pending, pwm_stop_latched;
    uint8_t remote_flags, remote_reason;
    uint32_t remote_last_control_sequence;
    uint8_t encoder_valid_mask, remote_encoder_valid_mask;
    uint32_t encoder_sample_ms, encoder_tx_ms, remote_encoder_rx_ms;
    int32_t encoder_counts[4], remote_encoder_counts[4];
    uint8_t led_on, led_pending, led_waiting_ack, led_ack_pending;
    uint8_t led_expected_on, remote_led_valid, remote_led_on;
    uint32_t led_request_ms, led_request_sequence, remote_led_sequence;
    uint8_t speed_supported, speed_ready_mask, speed_fault, speed_valid_mask;
    uint8_t remote_speed_ready_mask, remote_speed_fault, remote_speed_valid_mask;
    int16_t speed_measured[4], speed_targets[4], remote_speed_measured[4], remote_speed_targets[4];
    uint32_t speed_sample_ms, speed_tx_ms, remote_speed_rx_ms;
    /* One configuration transaction, applied by the hardware owner loop only.
     * Payload: wheel, flags, diameter(0.1mm), CPR(u32), Kp, Ki, max, accel, reserved4.
     * flags: motor negative=1, encoder negative=2, physically confirmed=4.
     * wheel=255 requests stopped-only fault clear with no payload. */
    uint8_t config_payload[20], config_wheel, config_result;
    uint8_t config_pending, config_waiting, config_received, config_ack_pending;
    uint8_t remote_config_valid, config_ready_mask, config_fault;
    uint32_t config_request_ms, config_sequence;
} link_node_t;
void link_node_init(link_node_t *node, uint8_t role, uint32_t boot_id, uint32_t now);
void link_node_tick(link_node_t *node, uint32_t now);
void link_node_receive(link_node_t *node, uint8_t byte, uint32_t now);
void link_node_transport_error(link_node_t *node, uint32_t now);
size_t link_node_transmit(link_node_t *node, uint32_t now, uint8_t *out, size_t cap);
/* BLE/input adapters queue freshly validated events to the owner
 * loop. Never resubmit a cached input with a new source sequence or timestamp.
 * This API is single-owner, not safe to call from concurrent BLE callbacks. */
int link_node_submit_input(link_node_t *node, uint32_t source_sequence,
                           const int16_t targets[4], uint8_t enable,
                           uint16_t ttl_ms, uint32_t now);
int link_node_submit_pwm(link_node_t *node, uint32_t source_sequence,
                         const int16_t permille[4], uint8_t enable,
                         uint16_t ttl_ms, uint32_t now);
void link_node_set_motor_state(link_node_t *node, uint8_t pwm_supported,
                                uint8_t output_inhibited);
void link_node_cancel_input(link_node_t *node);
/* Explicit operator stop: send disabled PWM before other frames, reject new
 * drive inputs until the controller issues a new session. */
void link_node_request_stop(link_node_t *node);
/* One diagnostic LED transaction at a time, expiring 500 ms from submission.
 * Neither LED requests nor acknowledgements extend the motor command lease. */
int link_node_request_led(link_node_t *node, uint8_t on, uint32_t now);
/* A set bit says ONLY that that wheel has a real count sample from the driver,
 * not that wiring, direction, calibration or physical movement is verified.
 * Counts are raw signed quadrature ticks, with per-wheel 4x/2x conversion. */
int link_node_publish_encoders(link_node_t *node, const int32_t counts[4],
                               uint8_t valid_mask, uint32_t now);
void link_node_publish_speed(link_node_t *node, const int16_t measured[4],
                             const int16_t targets[4], uint8_t valid_mask,
                             uint8_t ready_mask, uint8_t fault, uint32_t now);
int link_node_request_config(link_node_t *node, const uint8_t payload[20],
                             uint8_t clear_fault, uint32_t now);
void link_node_finish_config(link_node_t *node, uint8_t result,
                             uint8_t ready_mask, uint8_t fault);
#ifdef __cplusplus
}
#endif
#endif
