#ifndef CRTC_FALLBACK_WIRE_H
#define CRTC_FALLBACK_WIRE_H
#include "board_link.h"
#include "fallback.h"
/* Separate packet types: this profile cannot accidentally command old PID firmware. */
enum
{
    FW_HELLO = 64,
    FW_ACK,
    FW_CONTROL,
    FW_STOP,
    FW_STATUS
};
#define FW_TAG 0x19F103F1UL
typedef struct
{
    link_parser_t parser;
    uint32_t boot, token, peer, txseq, rxseq, last_rx, last_tx, input_ms;
    uint32_t accepted, stop_serial;
    uint8_t role, connected, have_seq, ack, valid, enable, operator_stop;
    uint8_t pending_stop, pending_operator, remote_enabled, remote_fault;
    uint16_t ttl, ramp, brake_pwm, brake_ms;
    int16_t target[4], status_output[4];
} fw_node_t;
void fw_init(fw_node_t *n, uint8_t controller, uint32_t boot, uint32_t now);
void fw_fault(fw_node_t *n, uint32_t now);
void fw_tick(fw_node_t *n, uint32_t now);
int fw_receive(fw_node_t *n, uint8_t byte, uint32_t arrival, uint32_t now);
void fw_input(fw_node_t *n, const int16_t target[4], uint8_t enable, const fb_config_t *c,
              uint32_t sampled);
void fw_stop(fw_node_t *n, uint8_t operator_stop);
size_t fw_transmit(fw_node_t *n, uint32_t now, uint8_t *out, size_t cap);
/* Portable production owner logic: no GPIO, serial or Flash side effects. */
#define FW_OUTPUT_OFF 1U
#define FW_CLEAR_WATCHDOG 2U
void fw_remote_sample(fw_node_t *n, fb_remote_t *r, const fb_input_t *input, uint8_t healthy,
                      uint32_t now);
uint8_t fw_motor_events(fw_node_t *n, fb_motor_t *m, uint32_t *seen_token, uint32_t *seen_stop,
                        uint8_t healthy, uint32_t now);
void fw_motor_step(fw_node_t *n, fb_motor_t *m, uint8_t healthy, uint32_t now);
#endif
