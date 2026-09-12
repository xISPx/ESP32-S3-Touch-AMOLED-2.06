// ============================================================================
//  PowerHal.hpp — X-Powers AXP2101 PMIC: battery telemetry + charge status.
// ============================================================================
#pragma once

#include <cstdint>
#include "core/Error.hpp"
#include "XPowersLib.h"

namespace hal {

struct BatteryInfo {
    uint8_t  percent = 0;
    uint16_t voltage_mv = 0;
    uint16_t vbus_mv = 0;
    int8_t   temperature_c = 0;
    bool     present = false;
    bool     charging = false;
};

class PowerHal {
public:
    PowerHal() = default;
    ~PowerHal() = default;
    PowerHal(const PowerHal&) = delete;
    PowerHal& operator=(const PowerHal&) = delete;

    core::Status init();

    // Snapshot of battery telemetry.  Guarded by the shared I2C mutex.
    BatteryInfo readBattery();

    // Pops a pending PWRON-key event from the PMU IRQ registers.
    // Returns true when an event was pending; longPress distinguishes
    // short (tap) from long (>=1 s, before the 6 s hardware cutoff).
    bool pollPowerKey(bool& longPress);

    // Official board bring-up: ALDO1 (and ALDO2) to 3.3 V — ALDO1 powers the
    // microphone (xiaozhi-esp32 Pmic init: reg 0x92 = 28, reg 0x90 |= 0x03).
    void enableMicPower();

    // Cut the system power via the PMU (AXP2101 shutdown).  Power returns by
    // pressing the PWR key (hardware PMU behaviour).
    void shutdown();

private:
    // XPowersLib 0.2.6 exposes the concrete class (no XPowersPMU alias).
    XPowersAXP2101 pmu_;
    bool ready_ = false;
};

}  // namespace hal
