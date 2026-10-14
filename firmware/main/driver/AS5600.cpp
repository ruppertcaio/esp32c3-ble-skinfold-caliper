#include "AS5600.h"

AS5600::AS5600(gpio_num_t sda, gpio_num_t scl, i2c_port_num_t port)
    :_sda(sda), _scl(scl), _port(port), _bus(nullptr), _dev(nullptr)
{
}


esp_err_t AS5600::begin() {
    if(_i2cInitialized) return ESP_OK;

    // 1. Descreve o barramento físico
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = _port,
        .sda_io_num        = _sda,
        .scl_io_num        = _scl,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags             = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &_bus);
    if(ret != ESP_OK) return ret;

    // 2. Descreve o dispositivo naquele barramento
    i2c_device_config_t dev_cfg = {
        .dev_addr_length    = I2C_ADDR_BIT_LEN_7,
        .device_address     = AS5600_ADDR,
        .scl_speed_hz       = AS5600_I2C_SPEED_HZ,
        .scl_wait_us     = 0,
    };

    ret = i2c_master_bus_add_device(_bus, &dev_cfg, &_dev);
    if(ret != ESP_OK) return ret;

    _i2cInitialized = true;
    return ESP_OK;
}

esp_err_t AS5600::_readRegister(uint8_t reg, uint8_t *buf, size_t len) {
    // transmit_receive:
    // 1. Envia o endereço do registrador (write)
    // 2. Repeated START automático
    // 3. Lê `len` bytes para `buf` (read)
    return i2c_master_transmit_receive(
        _dev,                       // handle do dispositivo
        &reg,                       // o que escrever — endereço do registrador
        1,                          // tamanho do write — 1 byte
        buf,                        // onde guardar os bytes lidos
        len,                        // quantos bytes ler
        50                          // timeout em ms
    );
}

esp_err_t AS5600::readRaw(uint16_t *out) {
    uint8_t buf[2];

    esp_err_t ret = _readRegister(AS5600_REG_RAW_H, buf, 2);
    if(ret != ESP_OK) return ret;

    // buf[0] = byte alto (bits 11:8)
    // buf[1] = byte baixo (bits 7:0)
    *out = ((uint16_t)buf[0] << 8 | buf[1]) & 0x0FFF; // Máscara para 12 bits
    return ESP_OK;
}

esp_err_t AS5600::readAngleDeg(float *out) {
    uint16_t raw;
    esp_err_t ret = readRaw(&raw);
    if(ret != ESP_OK) return ret;

    *out = (raw / AS5600_RESOLUTION) * 360.0f;
    return ESP_OK;
}

esp_err_t AS5600::readAngleRad(float *out) {
    uint16_t raw;
    esp_err_t ret = readRaw(&raw);
    if(ret != ESP_OK) return ret;

    constexpr float PI = 3.14159265358979323846f;
    *out = (raw / AS5600_RESOLUTION) * (2.0f * PI);
    return ESP_OK;
}

esp_err_t AS5600::readStatus(AS5600Status *out) {
    uint8_t raw;
    esp_err_t ret = _readRegister(AS5600_REG_STATUS, &raw, 1);
    if(ret != ESP_OK) return ret;

    out->magnetDetected = (raw & AS5600_STATUS_MD) != 0;
    out->magnetWeak     = (raw & AS5600_STATUS_ML) != 0;
    out->magnetStrong   = (raw & AS5600_STATUS_MH) != 0;
    return ESP_OK;
}

esp_err_t AS5600::deinit() {
    if (_dev != nullptr) {
        i2c_master_bus_rm_device(_dev);
        _dev = nullptr;
    }
    if (_bus != nullptr) {
        i2c_del_master_bus(_bus);
        _bus = nullptr;
    }

    _i2cInitialized = false;
    return ESP_OK;
}