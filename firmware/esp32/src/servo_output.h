#pragma once
#include <stdint.h>
bool servo_output_begin();
int servo_output_write(uint8_t channel, uint16_t pulse_us);
uint16_t servo_output_pulse(uint8_t channel);
