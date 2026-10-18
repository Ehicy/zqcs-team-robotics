#ifndef CRTC_REMOTE_BRIDGE_H
#define CRTC_REMOTE_BRIDGE_H
#include "board_link.h"
#include "remote_control.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    remote_control_t control;
    link_node_t *link;
    uint32_t uart_token, config_session, config_source_sequence, config_ms;
    uint8_t configs[4][20], pending_config[20], config_pending, config_wheel;
    uint8_t ack[20], ack_pending;
    /* PWM backend runs synchronously in the owner task, never BLE callbacks.
     * No autonomous ramp: STOP/disconnect retain the last commanded pulse. */
    int (*servo_write)(uint8_t channel, uint16_t pulse_us);
    uint16_t servo_us[2];
    uint32_t servo_changed_ms[2];
    uint8_t servo_changed[2];
} remote_bridge_t;
void remote_bridge_init(remote_bridge_t *b,link_node_t *link,uint32_t session);
void remote_bridge_stop(remote_bridge_t *b,uint8_t reason,uint32_t now);
void remote_bridge_tick(remote_bridge_t *b,uint32_t now);
int remote_bridge_receive(remote_bridge_t *b,const uint8_t *p,size_t size,uint32_t received_ms,uint32_t now);
/* kind 0 main, 1 raw counts, 2 wheel speeds, 4 local servo pulses. Configuration ACK always wins. */
void remote_bridge_status(remote_bridge_t *b,uint8_t kind,uint8_t p[20]);
#ifdef __cplusplus
}
#endif
#endif
