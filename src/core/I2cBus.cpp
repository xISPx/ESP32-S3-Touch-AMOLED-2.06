// ============================================================================
//  I2cBus.cpp
// ============================================================================
#include "core/I2cBus.hpp"
#include "config/config.hpp"
#include "core/Logger.hpp"

namespace core {

namespace {
    constexpr const char* kTag = "I2cBus";
}

I2cBus& I2cBus::instance() {
    static I2cBus bus;
    return bus;
}

bool I2cBus::begin(uint8_t sda, uint8_t scl, uint32_t freqHz) {
    if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
        if (!mutex_) return false;
    }
    Guard guard(*this);
    if (!Wire.begin(sda, scl, freqHz)) {
        LOGE(kTag, "Wire.begin(%u, %u) failed", sda, scl);
        return false;
    }
    LOGI(kTag, "I2C ready: sda=%u scl=%u freq=%lu", sda, scl,
         static_cast<unsigned long>(freqHz));
    return true;
}

}  // namespace core
