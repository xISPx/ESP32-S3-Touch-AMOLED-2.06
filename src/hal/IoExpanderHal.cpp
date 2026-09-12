// ============================================================================
//  IoExpanderHal.cpp — XL9555: PWR button input (EXIO6, high = pressed).
// ============================================================================
#include "hal/IoExpanderHal.hpp"
#include "config/config.hpp"
#include "core/I2cBus.hpp"
#include "core/Logger.hpp"

namespace hal {

namespace {
    constexpr const char* kTag = "Expander";
}

core::Status IoExpanderHal::init() {
    // Quiet probe of the XL9555 strap range 0x20..0x27 first — only call the
    // driver begin() when a device actually ACKs, to keep the shared bus clean.
    uint8_t found_addr = 0;
    {
        core::I2cBus::Guard guard(core::I2cBus::instance());
        for (uint8_t addr = cfg::kAddrIoXl9555; addr <= 0x27; ++addr) {
            core::I2cBus::instance().wire().beginTransmission(addr);
            if (core::I2cBus::instance().wire().endTransmission() == 0) {
                found_addr = addr;
                break;
            }
        }
    }
    if (found_addr == 0) {
        LOGW(kTag, "no I2C device in 0x%02X..0x27 — expander degraded",
             cfg::kAddrIoXl9555);
        return core::Status::fail(core::ErrorCode::ExpanderInit);
    }

    core::I2cBus::Guard guard(core::I2cBus::instance());
    if (!io_.begin(core::I2cBus::instance().wire(), found_addr,
                   cfg::kIicSda, cfg::kIicScl)) {
        LOGW(kTag, "XL9555 probe ok at 0x%02X but driver begin failed", found_addr);
        return core::Status::fail(core::ErrorCode::ExpanderInit);
    }
    io_.pinMode(ExtensionIOXL9555::IO6, INPUT);
    ready_ = true;
    LOGI(kTag, "XL9555 ready at 0x%02X", found_addr);
    return core::Status::ok(core::Unit{});
}

bool IoExpanderHal::powerButtonPressed() {
    if (!ready_) return false;
    core::I2cBus::Guard guard(core::I2cBus::instance());
    return io_.digitalRead(ExtensionIOXL9555::IO6) == HIGH;
}

}  // namespace hal
