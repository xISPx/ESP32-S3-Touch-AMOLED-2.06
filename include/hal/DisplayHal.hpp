// ============================================================================
//  DisplayHal.hpp — CO5300 410x502 AMOLED over QSPI (Arduino_GFX).
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include "config/config.hpp"
#include "core/Error.hpp"
#include "Arduino_GFX_Library.h"

namespace hal {

class DisplayHal {
public:
    DisplayHal() = default;
    ~DisplayHal() = default;
    DisplayHal(const DisplayHal&) = delete;
    DisplayHal& operator=(const DisplayHal&) = delete;

    core::Status init();

    Arduino_GFX* gfx() { return gfx_.get(); }
    uint16_t width() const { return cfg::kLcdWidth; }
    uint16_t height() const { return cfg::kLcdHeight; }

    // CO5300 register 0x51, 0..255.
    void setBrightness(uint8_t level);

private:
    std::unique_ptr<Arduino_DataBus> bus_;
    std::unique_ptr<Arduino_CO5300> gfx_;  // concrete type: setBrightness() lives here
    uint8_t brightness_ = cfg::kBrightnessFull;
};

}  // namespace hal
