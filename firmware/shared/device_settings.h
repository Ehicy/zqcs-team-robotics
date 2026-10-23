#ifndef CRTC_DEVICE_SETTINGS_H
#define CRTC_DEVICE_SETTINGS_H
#include <stdint.h>
#include <stddef.h>
#define DEVICE_SETTINGS_BYTES 128U
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t wheels[4][20];
    uint16_t kp,ki,kd,revision;
    uint16_t servo_min[3],servo_max[3],servo_reference[3];
    uint8_t servo_flags[3]; /* bit0 calibrated, bit1 door orientation inverted */
} device_settings_t;
int device_settings_valid(const device_settings_t *s);
int device_settings_encode(const device_settings_t *s,uint8_t out[DEVICE_SETTINGS_BYTES]);
int device_settings_decode(device_settings_t *s,const uint8_t *bytes,size_t size);
#ifdef __cplusplus
}
#endif
#endif
