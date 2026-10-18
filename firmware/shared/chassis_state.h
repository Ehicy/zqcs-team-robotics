#ifndef CRTC_CHASSIS_STATE_H
#define CRTC_CHASSIS_STATE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    CHASSIS_WAIT_LINK=0, CHASSIS_WAIT_PID=1, CHASSIS_WAIT_CALIBRATION=2,
    CHASSIS_LOCKED=3, CHASSIS_ARMED=4, CHASSIS_RUNNING=5, CHASSIS_FAULT=6
} chassis_state_t;
/* Owner-loop state transition. Every transition is evaluated from fresh
 * backend gates. A reconnect/configuration never creates an ARM request. */
chassis_state_t chassis_state_update(uint8_t link, uint8_t pid, uint8_t calibrated,
                                    uint8_t fault, uint8_t armed, uint8_t moving);
#ifdef __cplusplus
}
#endif
#endif
