#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * Componente BLE/NimBLE para o adipômetro.
 *
 * Papel BLE:
 * - ESP32-C3: GATT Server / Peripheral
 * - App mobile: GATT Client / Central
 *
 * UUIDs expostos ao app:
 * - Service:      7a4f0001-2b21-4f37-a602-b29e00000001
 * - Measurement: 7a4f0002-2b21-4f37-a602-b29e00000001  READ | NOTIFY
 * - Command:     7a4f0003-2b21-4f37-a602-b29e00000001  READ | WRITE | WRITE_NO_RSP
 *
 * Protocolo esperado:
 * App -> Command:
 *   {"cmd":"medir","dobra":"tricipital"}
 *
 * Firmware -> Measurement notify:
 *   {"ok":true,"dobra":"tricipital","mm":12.34}
 */

#define BLE_ADIPOMETER_MAX_PAYLOAD 256

struct BleAdipometerConfig {
    const char *deviceName = "Adipometro-C3";
    bool initNvs = true;        // NimBLE usa NVS; deixe false se a aplicação já inicializou NVS.
    bool autoAdvertise = true;  // Inicia advertising automaticamente após sync do host.
};

using BleAdipometerCommandCallback = void (*)(const uint8_t *data, size_t len, void *ctx);

class BleAdipometer {
public:
    BleAdipometer();
    ~BleAdipometer() = default;

    BleAdipometer(const BleAdipometer &) = delete;
    BleAdipometer &operator=(const BleAdipometer &) = delete;

    esp_err_t begin(const BleAdipometerConfig &config = BleAdipometerConfig{});
    esp_err_t startAdvertising();
    esp_err_t stopAdvertising();

    // Envia bytes pela characteristic Measurement. Também atualiza o valor lido por READ.
    esp_err_t notifyBytes(const uint8_t *data, size_t len);

    // Atalho para JSON/texto terminado em '\0'. O byte nulo não é enviado.
    esp_err_t notifyText(const char *text);

    // Callback chamado quando o app escreve na characteristic Command.
    // Mantenha o callback curto; idealmente só copie para uma fila.
    void setCommandCallback(BleAdipometerCommandCallback cb, void *ctx = nullptr);

    bool isStarted() const { return _started; }
    bool isSynced() const { return _synced; }
    bool isConnected() const { return _connected; }
    bool isNotifyEnabled() const { return _notifyEnabled; }
    uint16_t connectionHandle() const { return _connHandle; }

private:
    friend int ble_adipometer_gatt_access_cb(uint16_t conn_handle,
                                             uint16_t attr_handle,
                                             struct ble_gatt_access_ctxt *ctxt,
                                             void *arg);
    friend int ble_adipometer_gap_event_cb(struct ble_gap_event *event, void *arg);
    friend void ble_adipometer_on_sync_cb(void);
    friend void ble_adipometer_on_reset_cb(int reason);
    friend void ble_adipometer_host_task(void *param);

    esp_err_t _initNvs();
    void _onSync();
    void _onReset(int reason);
    int _onGapEvent(struct ble_gap_event *event);
    int _onGattAccess(uint16_t conn_handle,
                      uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt,
                      void *arg);

    void _lock();
    void _unlock();
    void _setLastPayloadLocked(const uint8_t *data, size_t len);

    BleAdipometerConfig _config{};
    SemaphoreHandle_t _mutex = nullptr;

    // Escritos pela task do host NimBLE e lidos pela task de comandos do app.
    std::atomic<bool> _started{false};
    std::atomic<bool> _synced{false};
    std::atomic<bool> _advertising{false};
    std::atomic<bool> _connected{false};
    std::atomic<bool> _notifyEnabled{false};

    uint8_t _ownAddrType = 0;
    std::atomic<uint16_t> _connHandle{0};

    uint8_t _lastMeasurement[BLE_ADIPOMETER_MAX_PAYLOAD] = {0};
    size_t _lastMeasurementLen = 0;

    uint8_t _lastCommand[BLE_ADIPOMETER_MAX_PAYLOAD] = {0};
    size_t _lastCommandLen = 0;

    BleAdipometerCommandCallback _commandCb = nullptr;
    void *_commandCtx = nullptr;
};
