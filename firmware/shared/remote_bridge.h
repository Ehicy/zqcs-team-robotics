#ifndef CRTC_REMOTE_BRIDGE_H
#define CRTC_REMOTE_BRIDGE_H
#include "board_link.h"
#include "remote_control.h"
#include "chassis_state.h"
#include "servo_motion.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    remote_control_t control;
    link_node_t *link;
    uint32_t uart_token, config_session, config_source_sequence, config_ms;
    uint8_t configs[4][20], pending_config[20], config_pending, config_wheel;
    uint8_t ack[20], ack_pending;
    uint16_t kp_milli, ki_milli, kd_milli;
    uint8_t pid_initialized, pid_init_failed, config_initializing;
    uint8_t manual_pwm; /* Fresh PWM input bypasses PID readiness; no manual ARM. */
    chassis_state_t phase;
    /* PWM and trajectories run only in the owner task. STOP freezes at the
     * last successful PWM write, retaining holding torque. */
    int (*servo_write)(uint8_t channel, uint16_t pulse_us);
    uint16_t servo_us[LINK_SERVO_COUNT];
    servo_motion_t servos[LINK_SERVO_COUNT];
    uint8_t servo_error_mask;
} remote_bridge_t;
void remote_bridge_init(remote_bridge_t *b,link_node_t *link,uint32_t session);
void remote_bridge_stop(remote_bridge_t *b,uint8_t reason,uint32_t now);
void remote_bridge_tick(remote_bridge_t *b,uint32_t now);
int remote_bridge_receive(remote_bridge_t *b,const uint8_t *p,size_t size,uint32_t received_ms,uint32_t now);
/* kind 0 main, 1 raw counts, 2 wheel speeds, 4 legacy servos,
 * 5 three current PWM commands, 6 servo targets/trajectory. ACK always wins. */
void remote_bridge_status(remote_bridge_t *b,uint8_t kind,uint8_t p[20]);
#ifdef __cplusplus
}
#endif
#endif
