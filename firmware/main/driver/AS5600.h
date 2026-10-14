#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "driver/i2c_master.h"

/*============================== Defs ==============================*/

#define AS5600_ADDR         0x36        // Endereço do AS5600 no barramento I2C

#define AS5600_REG_STATUS   0x0B        // MD -> bit 5 || em high -> imã detectado
						                // ML -> bit 4 || em high -> imã fraco
						                // MH -> bit 3 || em high -> imã forte

#define AS5600_REG_RAW_H    0x0C        // 11 MSB -> bit 3 | 8 LBS  -> bit 0 | REG(0x0C)
                                        // 7 MSB  -> bit 7 | 0 LBS  -> bit 0 | REG(0x0D)
                                        // Total 12 bits de resolução

#define AS5600_STATUS_MD    (1 << 5)    // Máscara 00100000
#define AS5600_STATUS_ML    (1 << 4)    // Máscara 00010000
#define AS5600_STATUS_MH    (1 << 3)    // Máscara 00001000

#define AS5600_RESOLUTION   4096.0f     // 12 bits de resolução
#define AS5600_I2C_SPEED_HZ 400000      // 400kHz



/*============================ Structs ============================*/

struct AS5600Status {
    bool magnetDetected;
    bool magnetWeak;
    bool magnetStrong;
};



/*============================ Classe ============================*/

class AS5600 {
public:

    AS5600(gpio_num_t sda, gpio_num_t scl, i2c_port_num_t port = I2C_NUM_0);   // Construtor

    esp_err_t begin();                          // Inicializador

    esp_err_t readRaw(uint16_t *out);           // 0 - 4095
    esp_err_t readAngleDeg(float *out);         // 0.0 - 359.9°
    esp_err_t readAngleRad(float *out);         // 0.0 – 2π
    esp_err_t readStatus(AS5600Status *out);    // Status do imã

    esp_err_t deinit();                         // Libera recursos do barramento

private:

    gpio_num_t _sda;
    gpio_num_t _scl;
    i2c_port_num_t _port;

    i2c_master_bus_handle_t _bus;
    i2c_master_dev_handle_t _dev;

    bool _i2cInitialized = false;

    esp_err_t _readRegister(uint8_t reg, uint8_t *buf, size_t len);
};