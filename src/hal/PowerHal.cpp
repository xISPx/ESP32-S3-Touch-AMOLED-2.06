// ============================================================================
//  PowerHal.cpp — AXP2101 init + battery snapshot.
// ============================================================================
#include "hal/PowerHal.hpp"
#include "config/config.hpp"
#include "core/I2cBus.hpp"
#include "core/Logger.hpp"

namespace hal {

namespace {
    constexpr const char* kTag = "Power";
}

core::Status PowerHal::init() {
    {
        core::I2cBus::Guard guard(core::I2cBus::instance());
        // XPowersLib 0.2.x exposes the bus hook-up through init().
        if (!pmu_.init(core::I2cBus::instance().wire(), cfg::kIicSda, cfg::kIicScl,
                       cfg::kAddrPmuAxp2101)) {
            LOGE(kTag, "AXP2101 not found at 0x%02X", cfg::kAddrPmuAxp2101);
            return core::Status::fail(core::ErrorCode::PmuInit);
        }
        pmu_.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
        pmu_.clearIrqStatus();
        // The side PWR button is wired to the PMU PWRON key: enable short and
        // long-press IRQs (the 6 s hold remains a hardware power-off).
        pmu_.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
        // Official charging profile (xiaozhi-esp32 Pmic init): CV 4.1 V,
        // precharge 50 mA, charge 400 mA, termination 25 mA — gentle on the
        // 400 mAh cell and safe for always-on charging.
        pmu_.writeRegister(0x64, 0x02);
        pmu_.writeRegister(0x61, 0x02);
        pmu_.writeRegister(0x62, 0x0A);
        pmu_.writeRegister(0x63, 0x01);
        pmu_.enableTemperatureMeasure();
        pmu_.enableBattDetection();
        pmu_.enableVbusVoltageMeasure();
        pmu_.enableBattVoltageMeasure();
        pmu_.enableSystemVoltageMeasure();
    }
    ready_ = true;
    LOGI(kTag, "AXP2101 ready");
    return core::Status::ok(core::Unit{});
}

BatteryInfo PowerHal::readBattery() {
    BatteryInfo info;
    if (!ready_) return info;

    core::I2cBus::Guard guard(core::I2cBus::instance());
    info.voltage_mv = static_cast<uint16_t>(pmu_.getBattVoltage());
    info.vbus_mv = static_cast<uint16_t>(pmu_.getVbusVoltage());
    info.temperature_c = static_cast<int8_t>(pmu_.getTemperature());
    info.present = pmu_.isBatteryConnect();
    info.charging = pmu_.isCharging();
    info.percent = info.present ? static_cast<uint8_t>(pmu_.getBatteryPercent()) : 0;
    return info;
}

void PowerHal::enableMicPower() {
    if (!ready_) return;
    core::I2cBus::Guard guard(core::I2cBus::instance());
    // Official xiaozhi-esp32 Pmic bring-up for this board: ALDO1 (mic rail)
    // and ALDO2 to 3.3 V, then both on (0x90 |= 0x03).
    pmu_.setALDO1Voltage(3300);
    pmu_.setALDO2Voltage(3300);
    pmu_.enableALDO1();
    pmu_.enableALDO2();
    LOGI(kTag, "ALDO1/2 3.3V enabled (microphone rail)");
}

void PowerHal::shutdown() {
    if (!ready_) return;
    core::I2cBus::Guard guard(core::I2cBus::instance());
    LOGI(kTag, "PMU shutdown");
    pmu_.shutdown();
}

bool PowerHal::pollPowerKey(bool& longPress) {    if (!ready_) return false;
    core::I2cBus::Guard guard(core::I2cBus::instance());
    const uint64_t status = pmu_.getIrqStatus();
    if (status == 0) return false;

    bool event = false;
    if (pmu_.isPekeyLongPressIrq()) {
        longPress = true;
        event = true;
    } else if (pmu_.isPekeyShortPressIrq()) {
        longPress = false;
        event = true;
    }
    pmu_.clearIrqStatus();
    return event;
}

}  // namespace hal
