#include "BleAdipometer.h"

#include <cstring>

#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BleAdipometer";

/*
 * BLE_UUID128_INIT recebe bytes em little-endian.
 * Strings equivalentes:
 * - Service:      7a4f0001-2b21-4f37-a602-b29e00000001
 * - Measurement: 7a4f0002-2b21-4f37-a602-b29e00000001
 * - Command:     7a4f0003-2b21-4f37-a602-b29e00000001
 */
static const ble_uuid128_t UUID_SERVICE =
    BLE_UUID128_INIT(0x01, 0x00, 0x00, 0x00, 0x9e, 0xb2, 0x02, 0xa6,
                     0x37, 0x4f, 0x21, 0x2b, 0x01, 0x00, 0x4f, 0x7a);

static const ble_uuid128_t UUID_MEASUREMENT =
    BLE_UUID128_INIT(0x01, 0x00, 0x00, 0x00, 0x9e, 0xb2, 0x02, 0xa6,
                     0x37, 0x4f, 0x21, 0x2b, 0x02, 0x00, 0x4f, 0x7a);

static const ble_uuid128_t UUID_COMMAND =
    BLE_UUID128_INIT(0x01, 0x00, 0x00, 0x00, 0x9e, 0xb2, 0x02, 0xa6,
                     0x37, 0x4f, 0x21, 0x2b, 0x03, 0x00, 0x4f, 0x7a);

// Fonte única do MTU: CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU (sdkconfig.defaults).
#ifndef BLE_ADIPOMETER_PREFERRED_MTU
#define BLE_ADIPOMETER_PREFERRED_MTU CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU
#endif

static uint16_t s_measurement_handle = 0;
static uint16_t s_command_handle = 0;
static BleAdipometer *s_instance = nullptr;

enum class CharacteristicId : uint8_t {
    Measurement = 1,
    Command = 2,
};

static const uint8_t ARG_MEASUREMENT = static_cast<uint8_t>(CharacteristicId::Measurement);
static const uint8_t ARG_COMMAND = static_cast<uint8_t>(CharacteristicId::Command);

int ble_adipometer_gatt_access_cb(uint16_t conn_handle,
                                  uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg) {
    if (s_instance == nullptr) return BLE_ATT_ERR_UNLIKELY;
    return s_instance->_onGattAccess(conn_handle, attr_handle, ctxt, arg);
}

int ble_adipometer_gap_event_cb(struct ble_gap_event *event, void *arg) {
    BleAdipometer *self = static_cast<BleAdipometer *>(arg);
    if (self == nullptr) self = s_instance;
    if (self == nullptr) return 0;
    return self->_onGapEvent(event);
}

void ble_adipometer_on_sync_cb(void) {
    if (s_instance != nullptr) s_instance->_onSync();
}

void ble_adipometer_on_reset_cb(int reason) {
    if (s_instance != nullptr) s_instance->_onReset(reason);
}

void ble_adipometer_host_task(void *param) {
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task iniciado");
    nimble_port_run();
    nimble_port_freertos_deinit();
    ESP_LOGI(TAG, "NimBLE host task finalizado");
}

static const struct ble_gatt_chr_def s_characteristics[] = {
    {
        .uuid = &UUID_MEASUREMENT.u,
        .access_cb = ble_adipometer_gatt_access_cb,
        .arg = const_cast<uint8_t *>(&ARG_MEASUREMENT),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &s_measurement_handle,
    },
    {
        .uuid = &UUID_COMMAND.u,
        .access_cb = ble_adipometer_gatt_access_cb,
        .arg = const_cast<uint8_t *>(&ARG_COMMAND),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = &s_command_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SERVICE.u,
        .includes = nullptr,
        .characteristics = s_characteristics,
    },
    {0},
};

static esp_err_t bleRcToEsp(int rc) {
    if (rc == 0) return ESP_OK;
    if (rc == BLE_HS_ENOMEM) return ESP_ERR_NO_MEM;
    if (rc == BLE_HS_EINVAL) return ESP_ERR_INVALID_ARG;
    if (rc == BLE_HS_ENOTCONN) return ESP_ERR_INVALID_STATE;
    return ESP_FAIL;
}

BleAdipometer::BleAdipometer() {
    const char initMsg[] = "{\"ok\":true,\"status\":\"boot\"}";
    _setLastPayloadLocked(reinterpret_cast<const uint8_t *>(initMsg), std::strlen(initMsg));
}

esp_err_t BleAdipometer::begin(const BleAdipometerConfig &config) {
    if (_started) return ESP_OK;
    if (s_instance != nullptr && s_instance != this) return ESP_ERR_INVALID_STATE;

    _config = config;
    if (_config.deviceName == nullptr || std::strlen(_config.deviceName) == 0) {
        _config.deviceName = "Adipometro-C3";
    }

    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
        if (_mutex == nullptr) return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_OK;
    if (_config.initNvs) {
        ret = _initNvs();
        if (ret != ESP_OK) return ret;
    }

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init falhou: %d", rc);
        return bleRcToEsp(rc);
    }

    s_instance = this;

    ble_hs_cfg.reset_cb = ble_adipometer_on_reset_cb;
    ble_hs_cfg.sync_cb = ble_adipometer_on_sync_cb;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_svc_gap_device_name_set(_config.deviceName);
    if (rc != 0) return bleRcToEsp(rc);

    rc = ble_gatts_count_cfg(s_services);
    if (rc != 0) return bleRcToEsp(rc);

    rc = ble_gatts_add_svcs(s_services);
    if (rc != 0) return bleRcToEsp(rc);

    rc = ble_att_set_preferred_mtu(BLE_ADIPOMETER_PREFERRED_MTU);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_att_set_preferred_mtu(%d) falhou: %d", BLE_ADIPOMETER_PREFERRED_MTU, rc);
    } else {
        ESP_LOGI(TAG, "MTU preferencial configurado: %d", BLE_ADIPOMETER_PREFERRED_MTU);
    }

    _started = true;
    nimble_port_freertos_init(ble_adipometer_host_task);

    return ESP_OK;
}

esp_err_t BleAdipometer::_initNvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

void BleAdipometer::_onSync() {
    int rc = ble_hs_id_infer_auto(0, &_ownAddrType);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto falhou: %d", rc);
        return;
    }

    _synced = true;
    ESP_LOGI(TAG, "NimBLE sincronizado. Device name: %s", _config.deviceName);

    if (_config.autoAdvertise) {
        esp_err_t ret = startAdvertising();
        if (ret != ESP_OK) ESP_LOGE(TAG, "startAdvertising falhou: %s", esp_err_to_name(ret));
    }
}

void BleAdipometer::_onReset(int reason) {
    ESP_LOGW(TAG, "NimBLE reset; reason=%d", reason);
    _synced = false;
    _advertising = false;
    _connected = false;
    _notifyEnabled = false;
}

esp_err_t BleAdipometer::startAdvertising() {
    if (!_started || !_synced) return ESP_ERR_INVALID_STATE;
    if (_advertising) return ESP_OK;

    int rc = 0;

    struct ble_hs_adv_fields advFields = {};
    advFields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    advFields.tx_pwr_lvl_is_present = 1;
    advFields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    advFields.uuids128 = &UUID_SERVICE;
    advFields.num_uuids128 = 1;
    advFields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&advFields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields falhou: %d", rc);
        return bleRcToEsp(rc);
    }

    struct ble_hs_adv_fields rspFields = {};
    rspFields.name = reinterpret_cast<const uint8_t *>(_config.deviceName);
    rspFields.name_len = std::strlen(_config.deviceName);
    rspFields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rspFields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields falhou: %d", rc);
        return bleRcToEsp(rc);
    }

    struct ble_gap_adv_params advParams = {};
    advParams.conn_mode = BLE_GAP_CONN_MODE_UND;
    advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;
    advParams.itvl_min = 0;
    advParams.itvl_max = 0;

    rc = ble_gap_adv_start(_ownAddrType,
                           nullptr,
                           BLE_HS_FOREVER,
                           &advParams,
                           ble_adipometer_gap_event_cb,
                           this);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start falhou: %d", rc);
        return bleRcToEsp(rc);
    }

    _advertising = true;
    ESP_LOGI(TAG, "Advertising iniciado");
    return ESP_OK;
}

esp_err_t BleAdipometer::stopAdvertising() {
    if (!_advertising) return ESP_OK;
    int rc = ble_gap_adv_stop();
    if (rc != 0) return bleRcToEsp(rc);
    _advertising = false;
    return ESP_OK;
}

int BleAdipometer::_onGapEvent(struct ble_gap_event *event) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            _connHandle = event->connect.conn_handle;
            _connected = true;
            _advertising = false;
            ESP_LOGI(TAG, "Cliente conectado; conn_handle=%d", event->connect.conn_handle);
        } else {
            _connected = false;
            _notifyEnabled = false;
            ESP_LOGW(TAG, "Conexão falhou; status=%d", event->connect.status);
            startAdvertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Cliente desconectado; reason=%d", event->disconnect.reason);
        _connected = false;
        _notifyEnabled = false;
        _connHandle = 0;
        startAdvertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        _advertising = false;
        ESP_LOGI(TAG, "Advertising completo; reason=%d", event->adv_complete.reason);
        startAdvertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_measurement_handle) {
            _notifyEnabled = event->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "Notify Measurement %s", _notifyEnabled ? "habilitado" : "desabilitado");
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU atualizado; conn_handle=%d mtu=%d",
                 event->mtu.conn_handle,
                 event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

int BleAdipometer::_onGattAccess(uint16_t conn_handle,
                                 uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt,
                                 void *arg) {
    (void)conn_handle;
    (void)attr_handle;

    if (arg == nullptr) return BLE_ATT_ERR_UNLIKELY;
    const auto id = static_cast<CharacteristicId>(*static_cast<const uint8_t *>(arg));

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t copy[BLE_ADIPOMETER_MAX_PAYLOAD] = {0};
        size_t len = 0;

        _lock();
        if (id == CharacteristicId::Measurement) {
            len = _lastMeasurementLen;
            std::memcpy(copy, _lastMeasurement, len);
        } else if (id == CharacteristicId::Command) {
            len = _lastCommandLen;
            std::memcpy(copy, _lastCommand, len);
        }
        _unlock();

        int rc = os_mbuf_append(ctxt->om, copy, len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (id != CharacteristicId::Command) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

        uint8_t buffer[BLE_ADIPOMETER_MAX_PAYLOAD] = {0};
        uint16_t len = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer), &len);
        if (rc != 0 || len == 0 || len >= BLE_ADIPOMETER_MAX_PAYLOAD) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        BleAdipometerCommandCallback cb = nullptr;
        void *cbCtx = nullptr;

        _lock();
        _lastCommandLen = len;
        std::memcpy(_lastCommand, buffer, len);
        _lastCommand[len] = '\0';
        cb = _commandCb;
        cbCtx = _commandCtx;
        _unlock();

        if (cb != nullptr) cb(buffer, len, cbCtx);
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

esp_err_t BleAdipometer::notifyBytes(const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0 || len >= BLE_ADIPOMETER_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }

    _lock();
    _setLastPayloadLocked(data, len);
    _unlock();

    if (!_connected || !_notifyEnabled || s_measurement_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == nullptr) return ESP_ERR_NO_MEM;

    int rc = ble_gatts_notify_custom(_connHandle, s_measurement_handle, om);
    if (rc != 0) {
        os_mbuf_free_chain(om);
        ESP_LOGW(TAG, "notify falhou: rc=%d", rc);
        return bleRcToEsp(rc);
    }

    return ESP_OK;
}

esp_err_t BleAdipometer::notifyText(const char *text) {
    if (text == nullptr) return ESP_ERR_INVALID_ARG;
    return notifyBytes(reinterpret_cast<const uint8_t *>(text), std::strlen(text));
}

void BleAdipometer::setCommandCallback(BleAdipometerCommandCallback cb, void *ctx) {
    _lock();
    _commandCb = cb;
    _commandCtx = ctx;
    _unlock();
}

void BleAdipometer::_setLastPayloadLocked(const uint8_t *data, size_t len) {
    if (data == nullptr || len >= BLE_ADIPOMETER_MAX_PAYLOAD) {
        _lastMeasurementLen = 0;
        return;
    }

    _lastMeasurementLen = len;
    std::memcpy(_lastMeasurement, data, len);
    _lastMeasurement[len] = '\0';
}

void BleAdipometer::_lock() {
    if (_mutex != nullptr) xSemaphoreTake(_mutex, portMAX_DELAY);
}

void BleAdipometer::_unlock() {
    if (_mutex != nullptr) xSemaphoreGive(_mutex);
}
