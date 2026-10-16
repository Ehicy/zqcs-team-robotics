#ifndef CRTC_MOTOR_COMMAND_H
#define CRTC_MOTOR_COMMAND_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Strict USB bench command: PWM <8-hex-session> <M1> <M2> <M3> <M4>, permille.
 * No implicit units/conversion, no partial result on malformed input. */
int motor_command_parse(const char *line, uint32_t *session, int16_t targets[4]);
#ifdef __cplusplus
}
#endif
#endif
