// ============================================================================
//  I2cBus.hpp — single shared I2C peripheral behind a FreeRTOS mutex.
//
//  Touch (UiTask), PMU (PowerTask) and IMU (SensorTask) all sit on the same
//  Wire peripheral.  Arduino's TwoWire is not thread-safe, so every HAL
//  operation must be bracketed by I2cBus::Guard (RAII lock).
// ============================================================================
#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Wire.h"

namespace core {

class I2cBus {
public:
    I2cBus(const I2cBus&) = delete;
    I2cBus& operator=(const I2cBus&) = delete;

    static I2cBus& instance();

    bool begin(uint8_t sda, uint8_t scl, uint32_t freqHz);

    TwoWire& wire() { return Wire; }

    // RAII lock — the only sanctioned way to touch the bus from a task.
    class Guard {
    public:
        explicit Guard(I2cBus& bus) : bus_(bus) {
            if (bus_.mutex_) xSemaphoreTake(bus_.mutex_, portMAX_DELAY);
        }
        ~Guard() {
            if (bus_.mutex_) xSemaphoreGive(bus_.mutex_);
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        I2cBus& bus_;
    };

private:
    I2cBus() = default;
    SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace core
