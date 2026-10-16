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
       LINK_LED_SET=6, LINK_LED_ACK=7 };
#define LINK_LED_TIMEOUT_MS 500U
enum { LINK_CLIENT=0, LINK_CONTROLLER=1 };
enum { LINK_WAITING=0, LINK_CONTROL_EXPIRED=1, LINK_PEER_EXPIRED=2,
       LINK_TRANSPORT_ERROR=3 };
enum { LINK_FLAG_CONNECTED=1, LINK_FLAG_COMMAND_VALID=2,
       LINK_FLAG_OUTPUT_INHIBITED=4 };
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

/* No field represents measured speed or position. Requested targets are
 * diagnostic interfaces only; both firmware targets prohibit actuation. */
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
    uint8_t remote_flags, remote_reason;
    uint32_t remote_last_control_sequence;
    uint8_t encoder_valid_mask, remote_encoder_valid_mask;
    uint32_t encoder_sample_ms, encoder_tx_ms, remote_encoder_rx_ms;
    int32_t encoder_counts[4], remote_encoder_counts[4];
    uint8_t led_on, led_pending, led_waiting_ack, led_ack_pending;
    uint8_t led_expected_on, remote_led_valid, remote_led_on;
    uint32_t led_request_ms, led_request_sequence, remote_led_sequence;
} link_node_t;
void link_node_init(link_node_t *node, uint8_t role, uint32_t boot_id, uint32_t now);
void link_node_tick(link_node_t *node, uint32_t now);
void link_node_receive(link_node_t *node, uint8_t byte, uint32_t now);
void link_node_transport_error(link_node_t *node, uint32_t now);
size_t link_node_transmit(link_node_t *node, uint32_t now, uint8_t *out, size_t cap);
/* Future BLE/input adapters must queue freshly validated events to the owner
 * loop. Never resubmit a cached input with a new source sequence or timestamp.
 * This API is single-owner, not safe to call from concurrent BLE callbacks. */
int link_node_submit_input(link_node_t *node, uint32_t source_sequence,
                           const int16_t targets[4], uint8_t enable,
                           uint16_t ttl_ms, uint32_t now);
void link_node_cancel_input(link_node_t *node);
/* One diagnostic LED transaction at a time, expiring 500 ms from submission.
 * Neither LED requests nor acknowledgements extend the motor command lease. */
int link_node_request_led(link_node_t *node, uint8_t on, uint32_t now);
/* Future real sampling adapter. A set bit says ONLY that that wheel has a
 * real count sample. First firmware has no sampler and always sends mask=0.
 * Counts are raw signed quadrature ticks, with per-wheel 4x/2x conversion. */
int link_node_publish_encoders(link_node_t *node, const int32_t counts[4],
                               uint8_t valid_mask, uint32_t now);
#ifdef __cplusplus
}
#endif
#endif
