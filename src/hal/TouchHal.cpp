// ============================================================================
//  TouchHal.cpp — FT3168 via SensorLib TouchDrvFT6X36 (I2C addr 0x38).
// ============================================================================
#include "hal/TouchHal.hpp"
#include "config/config.hpp"
#include "core/I2cBus.hpp"
#include "core/Logger.hpp"

namespace hal {

namespace {
    constexpr const char* kTag = "Touch";
}

core::Status TouchHal::init() {
    touch_.setPins(cfg::kTpReset, cfg::kTpInt);

    {
        core::I2cBus::Guard guard(core::I2cBus::instance());
        if (!touch_.begin(core::I2cBus::instance().wire(),
                          cfg::kAddrTouchFt3168, cfg::kIicSda, cfg::kIicScl)) {
            LOGE(kTag, "FT3168 not found at 0x%02X", cfg::kAddrTouchFt3168);
            return core::Status::fail(core::ErrorCode::TouchInit);
        }
    }

    LOGI(kTag, "FT3168 ready (model: %s)", touch_.getModelName() ? touch_.getModelName() : "?");
    return core::Status::ok(core::Unit{});
}

bool TouchHal::readPoint(int16_t& x, int16_t& y) {
    core::I2cBus::Guard guard(core::I2cBus::instance());
    if (!touch_.isPressed()) return false;
    int16_t raw_x[1] = {0}, raw_y[1] = {0};
    if (touch_.getPoint(raw_x, raw_y, 1) == 0) return false;
    x = raw_x[0];
    y = raw_y[0];
    return true;
}

}  // namespace hal
