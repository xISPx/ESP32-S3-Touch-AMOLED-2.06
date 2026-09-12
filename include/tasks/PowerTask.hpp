// ============================================================================
//  PowerTask.hpp — battery telemetry + physical buttons + brightness policy.
//
//  * polls AXP2101 every 2 s and publishes a Battery event,
//  * debounces BOOT (GPIO0) and PWR (XL9555 EXIO6) and publishes edges,
//  * nothing else is allowed to touch the buttons or the PMU.
// ============================================================================
#pragma once

#include "core/TaskBase.hpp"
#include "hal/Hal.hpp"

namespace tasks {

enum ButtonId : uint8_t { kBtnBoot = 0, kBtnPwr = 1 };

class PowerTask final : public core::TaskBase {
public:
    explicit PowerTask(hal::Hal& hal) : hal_(hal) {}

private:
    void run() override;

    void pollButtons();
    void pollBattery(uint32_t now_ms);

    hal::Hal& hal_;
    uint32_t last_battery_ms_ = 0;

    // BOOT debounce state (PWR comes from the PMU IRQ, no debouncing needed)
    bool boot_state_ = false;
    uint32_t boot_change_ms_ = 0;
};

}  // namespace tasks
