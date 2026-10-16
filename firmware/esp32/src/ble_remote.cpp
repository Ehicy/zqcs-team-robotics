#include "ble_remote.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <string.h>

namespace {
constexpr uint16_t kAppId = 0x4c01;
constexpr uint16_t kNoConnection = 0xffff;
constexpr unsigned kQueueSize = 8, kStopSize = 2;
enum { SERVICE, CONTROL_DECL, CONTROL_VALUE, STATUS_DECL, STATUS_VALUE, CCCD, ATTR_COUNT };
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
ble_remote_event_t queue[kQueueSize], stops[kStopSize], lifecycle, overflow;
unsigned queueHead, queueCount, stopHead, stopCount;
bool lifecyclePending, overflowPending, connected, subscribed, congested;
uint32_t generation, disconnectedAt;
uint16_t connection = kNoConnection;
uint8_t peer[6];
esp_gatt_if_t interfaceId = ESP_GATT_IF_NONE;
uint16_t handles[ATTR_COUNT];
bool begun, failed, registered, tableRequested, tableReady;
bool serviceRequested, serviceReady, advReady, scanReady, advPending, advertising;
uint32_t setupAt;

uint16_t serviceType = ESP_GATT_UUID_PRI_SERVICE;
uint16_t declarationType = ESP_GATT_UUID_CHAR_DECLARE;
uint16_t cccdType = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
uint8_t serviceUuid[16] = {0x11,0x7d,0x2b,0xf4,0x06,0x3c,0x72,0x9b,
                           0x9e,0x4c,0x1a,0x6f,0x01,0x00,0x4b,0x2c};
uint8_t controlUuid[16] = {0x11,0x7d,0x2b,0xf4,0x06,0x3c,0x72,0x9b,
                           0x9e,0x4c,0x1a,0x6f,0x02,0x00,0x4b,0x2c};
uint8_t statusUuid[16] = {0x11,0x7d,0x2b,0xf4,0x06,0x3c,0x72,0x9b,
                          0x9e,0x4c,0x1a,0x6f,0x03,0x00,0x4b,0x2c};
uint8_t controlProperties = ESP_GATT_CHAR_PROP_BIT_WRITE;
uint8_t statusProperties = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
uint8_t initialPacket[BLE_REMOTE_PACKET_SIZE], initialCccd[2];
esp_gatts_attr_db_t attributes[ATTR_COUNT];
uint8_t advertisingData[21] = {2, ESP_BLE_AD_TYPE_FLAG, 6, 17, ESP_BLE_AD_TYPE_128SRV_CMPL};
uint8_t scanData[sizeof(BLE_REMOTE_DEVICE_NAME) + 1];

void clearInputLocked()
{
    queueHead = queueCount = stopHead = stopCount = 0;
}

void overflowLocked(uint32_t now)
{
    clearInputLocked();
    if (!overflowPending) {
        overflow = {};
        overflow.kind = BLE_REMOTE_OVERFLOW;
        overflow.generation = generation;
        overflow.received_ms = now;
        overflowPending = true;
    }
}

void failSetup()
{
    const uint32_t now = millis();
    portENTER_CRITICAL(&mux);
    failed = true;
    overflowLocked(now);
    portEXIT_CRITICAL(&mux);
}

bool ownerLocked(uint16_t id, const uint8_t *address)
{
    return connected && connection == id && memcmp(peer, address, sizeof(peer)) == 0;
}

bool enqueueLocked(const uint8_t *value, uint16_t length, uint32_t now)
{
    if (overflowPending) return false;
    ble_remote_event_t event = {};
    event.kind = BLE_REMOTE_PACKET;
    event.generation = generation;
    event.received_ms = now;
    event.length = length > 255U ? 255U : static_cast<uint8_t>(length);
    const size_t copied = length > BLE_REMOTE_PACKET_SIZE ? BLE_REMOTE_PACKET_SIZE : length;
    if (copied) memcpy(event.bytes, value, copied);
    if (length >= 2U && value[1] == 3U) {
        // STOP gets its own priority queue; no DRIVE backlog can delay it.
        queueHead = queueCount = 0;
        if (stopCount == kStopSize) { overflowLocked(now); return false; }
        stops[(stopHead + stopCount++) % kStopSize] = event;
    } else {
        if (queueCount == kQueueSize) { overflowLocked(now); return false; }
        queue[(queueHead + queueCount++) % kQueueSize] = event;
    }
    return true;
}

void gattsCallback(esp_gatts_cb_event_t event, esp_gatt_if_t iface,
                   esp_ble_gatts_cb_param_t *p)
{
    const uint32_t now = millis();
    if (event == ESP_GATTS_REG_EVT) {
        if (p->reg.app_id != kAppId) return;
        if (p->reg.status != ESP_GATT_OK) { failSetup(); return; }
        portENTER_CRITICAL(&mux);
        interfaceId = iface;
        registered = true;
        portEXIT_CRITICAL(&mux);
        return;
    }
    portENTER_CRITICAL(&mux);
    const bool ours = iface == interfaceId;
    portEXIT_CRITICAL(&mux);
    if (!ours) return;

    switch (event) {
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (p->add_attr_tab.status != ESP_GATT_OK ||
            p->add_attr_tab.num_handle != ATTR_COUNT) { failSetup(); break; }
        portENTER_CRITICAL(&mux);
        memcpy(handles, p->add_attr_tab.handles, sizeof(handles));
        tableReady = true;
        portEXIT_CRITICAL(&mux);
        break;
    case ESP_GATTS_START_EVT:
        if (p->start.status != ESP_GATT_OK) { failSetup(); break; }
        portENTER_CRITICAL(&mux);
        serviceReady = true;
        portEXIT_CRITICAL(&mux);
        break;
    case ESP_GATTS_CONNECT_EVT: {
        bool extra;
        portENTER_CRITICAL(&mux);
        extra = connected;
        if (!extra) {
            connected = true;
            connection = p->connect.conn_id;
            memcpy(peer, p->connect.remote_bda, sizeof(peer));
            subscribed = congested = advertising = advPending = false;
            ++generation;
            clearInputLocked();
            overflowPending = false;
            lifecycle = {};
            lifecycle.kind = BLE_REMOTE_CONNECT;
            lifecycle.generation = generation;
            lifecycle.received_ms = now;
            lifecyclePending = true;
        }
        portEXIT_CRITICAL(&mux);
        if (extra) esp_ble_gatts_close(iface, p->connect.conn_id);
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        portENTER_CRITICAL(&mux);
        if (ownerLocked(p->disconnect.conn_id, p->disconnect.remote_bda)) {
            connected = subscribed = congested = advertising = advPending = false;
            connection = kNoConnection;
            disconnectedAt = now;
            ++generation;
            clearInputLocked();
            overflowPending = false;
            lifecycle = {};
            lifecycle.kind = BLE_REMOTE_DISCONNECT;
            lifecycle.generation = generation;
            lifecycle.received_ms = now;
            lifecyclePending = true;
        }
        portEXIT_CRITICAL(&mux);
        break;
    case ESP_GATTS_WRITE_EVT: {
        esp_gatt_status_t result = ESP_GATT_OK;
        portENTER_CRITICAL(&mux);
        if (!ownerLocked(p->write.conn_id, p->write.bda)) {
            result = ESP_GATT_WRITE_NOT_PERMIT;
        } else if (p->write.handle == handles[CONTROL_VALUE]) {
            if (p->write.is_prep || p->write.offset != 0 || !p->write.need_rsp) {
                // No prepared-write assembly or write-without-response control.
                overflowLocked(now);
                result = ESP_GATT_REQ_NOT_SUPPORTED;
            } else if (!enqueueLocked(p->write.value, p->write.len, now)) {
                result = ESP_GATT_INSUF_RESOURCE;
            } else if (p->write.len != BLE_REMOTE_PACKET_SIZE) {
                result = ESP_GATT_INVALID_ATTR_LEN;
            }
        } else if (p->write.handle == handles[CCCD]) {
            if (p->write.is_prep || p->write.offset != 0) {
                result = ESP_GATT_REQ_NOT_SUPPORTED;
            } else if (p->write.len != 2U) {
                result = ESP_GATT_INVALID_ATTR_LEN;
            } else if (p->write.value[1] != 0 || p->write.value[0] > 1U) {
                result = ESP_GATT_CCC_CFG_ERR;
            } else {
                subscribed = p->write.value[0] != 0;
            }
        } else {
            result = ESP_GATT_WRITE_NOT_PERMIT;
        }
        portEXIT_CRITICAL(&mux);
        if (p->write.need_rsp)
            esp_ble_gatts_send_response(iface, p->write.conn_id, p->write.trans_id, result, nullptr);
        break;
    }
    case ESP_GATTS_READ_EVT: {
        // Status is stack-owned AUTO_RSP. CCCD is per active connection.
        esp_gatt_rsp_t response = {};
        esp_gatt_status_t result = ESP_GATT_OK;
        bool respond;
        portENTER_CRITICAL(&mux);
        respond = p->read.handle == handles[CCCD];
        if (!ownerLocked(p->read.conn_id, p->read.bda)) {
            result = ESP_GATT_READ_NOT_PERMIT;
        } else if (p->read.offset > 2U) {
            result = ESP_GATT_INVALID_OFFSET;
        } else {
            response.attr_value.handle = p->read.handle;
            response.attr_value.offset = p->read.offset;
            response.attr_value.len = 2U - p->read.offset;
            if (p->read.offset == 0) response.attr_value.value[0] = subscribed ? 1 : 0;
        }
        portEXIT_CRITICAL(&mux);
        if (respond && p->read.need_rsp)
            esp_ble_gatts_send_response(iface, p->read.conn_id, p->read.trans_id, result, &response);
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        portENTER_CRITICAL(&mux);
        if (ownerLocked(p->exec_write.conn_id, p->exec_write.bda)) overflowLocked(now);
        portEXIT_CRITICAL(&mux);
        esp_ble_gatts_send_response(iface, p->exec_write.conn_id, p->exec_write.trans_id,
                                   ESP_GATT_REQ_NOT_SUPPORTED, nullptr);
        break;
    case ESP_GATTS_CONGEST_EVT:
        portENTER_CRITICAL(&mux);
        if (connected && connection == p->congest.conn_id) {
            congested = p->congest.congested;
            if (congested) overflowLocked(now);
        }
        portEXIT_CRITICAL(&mux);
        break;
    case ESP_GATTS_SET_ATTR_VAL_EVT:
        if (p->set_attr_val.status != ESP_GATT_OK) {
            portENTER_CRITICAL(&mux);
            overflowLocked(now);
            portEXIT_CRITICAL(&mux);
        }
        break;
    default: break;
    }
}

void gapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p)
{
    const uint32_t now = millis();
    portENTER_CRITICAL(&mux);
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        advReady = p->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS;
        if (!advReady) { failed = true; overflowLocked(now); }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        scanReady = p->scan_rsp_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS;
        if (!scanReady) { failed = true; overflowLocked(now); }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        advPending = false;
        advertising = p->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS && !connected;
        if (!advertising) disconnectedAt = now;
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        advertising = advPending = false;
        disconnectedAt = now;
        break;
    default: break;
    }
    portEXIT_CRITICAL(&mux);
}

void attribute(unsigned i, uint8_t response, uint16_t uuidSize, uint8_t *uuid,
               uint16_t permissions, uint16_t maxLength, uint16_t length, uint8_t *value)
{
    attributes[i].attr_control.auto_rsp = response;
    attributes[i].att_desc = {uuidSize, uuid, permissions, maxLength, length, value};
}

void advanceSetup()
{
    bool createTable = false, startService = false, startAdvertising = false;
    esp_gatt_if_t iface;
    uint16_t serviceHandle;
    const uint32_t now = millis();
    portENTER_CRITICAL(&mux);
    if (!begun || failed) { portEXIT_CRITICAL(&mux); return; }
    if ((!serviceReady || !advReady || !scanReady) && now - setupAt > 3000U) {
        failed = true;
        overflowLocked(now);
        portEXIT_CRITICAL(&mux);
        return;
    }
    iface = interfaceId;
    serviceHandle = handles[SERVICE];
    if (registered && !tableRequested) createTable = tableRequested = true;
    if (tableReady && !serviceRequested) startService = serviceRequested = true;
    if (serviceReady && advReady && scanReady && !connected && !advPending &&
        !advertising && now - disconnectedAt >= 100U)
        startAdvertising = advPending = true;
    portEXIT_CRITICAL(&mux);
    if (createTable && esp_ble_gatts_create_attr_tab(attributes, iface, ATTR_COUNT, 0) != ESP_OK)
        failSetup();
    if (startService && esp_ble_gatts_start_service(serviceHandle) != ESP_OK) failSetup();
    if (startAdvertising) {
        esp_ble_adv_params_t params = {};
        params.adv_int_min = 0x40;
        params.adv_int_max = 0x80;
        params.adv_type = ADV_TYPE_IND;
        params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
        params.channel_map = ADV_CHNL_ALL;
        params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
        if (esp_ble_gap_start_advertising(&params) != ESP_OK) {
            portENTER_CRITICAL(&mux);
            advPending = false;
            disconnectedAt = now;
            portEXIT_CRITICAL(&mux);
        }
    }
}
} // namespace

bool ble_remote_begin()
{
    portENTER_CRITICAL(&mux);
    if (begun) { const bool ok = !failed; portEXIT_CRITICAL(&mux); return ok; }
    begun = true;
    setupAt = disconnectedAt = millis();
    portEXIT_CRITICAL(&mux);
    // Use Arduino's installed stack bootstrap, but no BLEServer/Characteristic
    // wrappers: their connection map and std::string values cross task contexts.
    BLEDevice::init(BLE_REMOTE_DEVICE_NAME);
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
        failSetup(); return false;
    }
    BLEDevice::setCustomGattsHandler(gattsCallback);
    BLEDevice::setCustomGapHandler(gapCallback);
    attribute(SERVICE, ESP_GATT_AUTO_RSP, ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&serviceType),
              ESP_GATT_PERM_READ, 16, 16, serviceUuid);
    attribute(CONTROL_DECL, ESP_GATT_AUTO_RSP, ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&declarationType),
              ESP_GATT_PERM_READ, 1, 1, &controlProperties);
    // Let the bounded callback see oversized packets to fail closed for a known
    // session. It copies at most 20 bytes, never assembles long writes.
    attribute(CONTROL_VALUE, ESP_GATT_RSP_BY_APP, ESP_UUID_LEN_128, controlUuid,
              ESP_GATT_PERM_WRITE, ESP_GATT_MAX_ATTR_LEN, 20, initialPacket);
    attribute(STATUS_DECL, ESP_GATT_AUTO_RSP, ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&declarationType),
              ESP_GATT_PERM_READ, 1, 1, &statusProperties);
    attribute(STATUS_VALUE, ESP_GATT_AUTO_RSP, ESP_UUID_LEN_128, statusUuid,
              ESP_GATT_PERM_READ, 20, 20, initialPacket);
    attribute(CCCD, ESP_GATT_RSP_BY_APP, ESP_UUID_LEN_16, reinterpret_cast<uint8_t *>(&cccdType),
              ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, initialCccd);
    memcpy(advertisingData + 5, serviceUuid, 16);
    scanData[0] = sizeof(BLE_REMOTE_DEVICE_NAME);
    scanData[1] = ESP_BLE_AD_TYPE_NAME_CMPL;
    memcpy(scanData + 2, BLE_REMOTE_DEVICE_NAME, sizeof(BLE_REMOTE_DEVICE_NAME) - 1);
    if (esp_ble_gap_config_adv_data_raw(advertisingData, sizeof(advertisingData)) != ESP_OK ||
        esp_ble_gap_config_scan_rsp_data_raw(scanData, sizeof(scanData)) != ESP_OK ||
        esp_ble_gatts_app_register(kAppId) != ESP_OK) {
        failSetup(); return false;
    }
    return true;
}

bool ble_remote_poll(ble_remote_event_t *event)
{
    advanceSetup();
    if (!event) return false;
    bool available = true;
    portENTER_CRITICAL(&mux);
    if (lifecyclePending) {
        *event = lifecycle;
        lifecyclePending = false;
    } else if (overflowPending) {
        *event = overflow;
        overflowPending = false;
        clearInputLocked();
    } else if (stopCount) {
        *event = stops[stopHead];
        stopHead = (stopHead + 1) % kStopSize;
        --stopCount;
    } else if (queueCount) {
        *event = queue[queueHead];
        queueHead = (queueHead + 1) % kQueueSize;
        --queueCount;
    } else available = false;
    portEXIT_CRITICAL(&mux);
    return available;
}

bool ble_remote_connected()
{
    portENTER_CRITICAL(&mux);
    const bool result = connected;
    portEXIT_CRITICAL(&mux);
    return result;
}

uint32_t ble_remote_generation()
{
    portENTER_CRITICAL(&mux);
    const uint32_t result = generation;
    portEXIT_CRITICAL(&mux);
    return result;
}

void ble_remote_drop_input()
{
    portENTER_CRITICAL(&mux);
    clearInputLocked();
    portEXIT_CRITICAL(&mux);
}

void ble_remote_publish(const uint8_t packet[BLE_REMOTE_PACKET_SIZE])
{
    if (!packet) return;
    esp_gatt_if_t iface;
    uint16_t id, handle;
    bool send, ready;
    uint32_t epoch;
    portENTER_CRITICAL(&mux);
    ready = serviceReady && !failed;
    send = ready && connected && subscribed && !congested;
    id = connection;
    iface = interfaceId;
    handle = handles[STATUS_VALUE];
    epoch = generation;
    portEXIT_CRITICAL(&mux);
    if (!ready) return;
    // Both ESP-IDF APIs copy payloads into the BT task; no shared mutable string.
    esp_err_t result = esp_ble_gatts_set_attr_value(handle, BLE_REMOTE_PACKET_SIZE, packet);
    if (send && result == ESP_OK)
        result = esp_ble_gatts_send_indicate(iface, id, handle, BLE_REMOTE_PACKET_SIZE,
                                           const_cast<uint8_t *>(packet), false);
    if (result != ESP_OK) {
        const uint32_t now = millis();
        portENTER_CRITICAL(&mux);
        if (generation == epoch) overflowLocked(now);
        portEXIT_CRITICAL(&mux);
    }
}
