#ifndef CRTC_BLE_REMOTE_H
#define CRTC_BLE_REMOTE_H

#include <stdint.h>

#define BLE_REMOTE_DEVICE_NAME "CRTC-Lower-Car"
#define BLE_REMOTE_SERVICE_UUID "2c4b0001-6f1a-4c9e-9b72-3c06f42b7d11"
#define BLE_REMOTE_CONTROL_UUID "2c4b0002-6f1a-4c9e-9b72-3c06f42b7d11"
#define BLE_REMOTE_STATUS_UUID "2c4b0003-6f1a-4c9e-9b72-3c06f42b7d11"
#define BLE_REMOTE_PACKET_SIZE 20U

enum {
    BLE_REMOTE_CONNECT=1, BLE_REMOTE_DISCONNECT=2,
    BLE_REMOTE_PACKET=3, BLE_REMOTE_OVERFLOW=4
};

struct ble_remote_event_t {
    uint8_t kind, length;
    uint32_t generation, received_ms;
    uint8_t bytes[BLE_REMOTE_PACKET_SIZE];
};

/* Owner-loop APIs. begin starts asynchronous GATT setup; poll also advances
 * setup/advertising. No callback calls board_link or the remote state machine.
 * A session token is freshness protection, NOT Bluetooth authentication. */
bool ble_remote_begin();
bool ble_remote_poll(ble_remote_event_t *event);
bool ble_remote_connected();
uint32_t ble_remote_generation();
/* Clear pending input only; lifecycle/overflow notifications are retained. */
void ble_remote_drop_input();
/* Fixed 20-byte read/notify value, directed to the sole active subscriber.
 * Owner schedules at most one packet every 25 ms and retains important ACKs;
 * this layer never silently rate-limits them. Default MTU is sufficient. */
void ble_remote_publish(const uint8_t packet[BLE_REMOTE_PACKET_SIZE]);

#endif
