#ifndef CRTC_CONTROL_INPUT_H
#define CRTC_CONTROL_INPUT_H
#include <stdint.h>
/* Future BLE GATT adapter: enqueue validated events and call these from loop().
 * Source sequence must advance only for NEW input events. All four values are
 * requested wheel mm/s, not measured speeds. No motor backend exists yet. */
bool crtc_submit_control(uint32_t source_sequence, const int16_t targets[4],
                         bool request_enable, uint16_t ttl_ms);
void crtc_cancel_control();
#endif
