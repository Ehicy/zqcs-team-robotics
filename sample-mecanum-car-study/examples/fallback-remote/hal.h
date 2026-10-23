#ifndef FALLBACK_REMOTE_HAL_H
#define FALLBACK_REMOTE_HAL_H
#include <stdint.h>
void remote_hal_init(void);
int remote_servo_write(unsigned ch, uint16_t pulse);
void remote_led(uint8_t on);
#endif
