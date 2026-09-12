// ============================================================================
//  PowerTask.cpp — battery telemetry + button debounce loop.
// ============================================================================
#include "tasks/PowerTask.hpp"
#include "config/config.hpp"
#include "core/Event.hpp"
#include "core/Logger.hpp"
#include <Arduino.h>

namespace tasks {

namespace {
    constexpr const char* kTag = "Power";
}

void PowerTask::pollButtons() {
    const uint32_t now = millis();

    // ---- BOOT (GPIO0, active-low) -------------------------------------------
    const bool boot_pressed = (digitalRead(cfg::kBootBtnPin) == LOW);
    if (boot_pressed != boot_state_ && (now - boot_change_ms_) >= cfg::kButtonDebounceMs) {
        boot_state_ = boot_pressed;
        boot_change_ms_ = now;
        core::EventBus::instance().publish(
            core::Event::makeButton(kBtnBoot, boot_pressed, 0));
    }

    // ---- PWR (AXP2101 PWRON key IRQ: short tap / long press) ----------------
    bool long_press = false;
    if (hal_.power.pollPowerKey(long_press)) {
        if (long_press) {
            // Long hold = power off: let the UI show the shutdown screen for
            // a moment, then cut the power via the PMU.  Powering back on is
            // hardware behaviour — pressing PWR boots the PMU again.
            core::EventBus::instance().publish(core::Event::makeShutdown());
            LOGI(kTag, "long PWR press — powering off");
            vTaskDelay(pdMS_TO_TICKS(2000));
            hal_.power.shutdown();
            // If the PMU refused (degraded), park here and do nothing further.
            while (true) vTaskDelay(pdMS_TO_TICKS(5000));
        }
        core::EventBus::instance().publish(
            core::Event::makeButton(kBtnPwr, true, 0));
    }
}

void PowerTask::pollBattery(uint32_t now_ms) {
    if (now_ms - last_battery_ms_ < cfg::kPowerPollMs) return;
    last_battery_ms_ = now_ms;

    if (core::SystemHealth::isDegraded(core::ModuleBit::Power)) return;

    const auto info = hal_.power.readBattery();
    core::EventBus::instance().publish(core::Event::makeBattery(
        info.percent, info.charging, info.present,
        info.voltage_mv, info.vbus_mv, info.temperature_c));
}

void PowerTask::run() {
    LOGI(kTag, "battery + button monitor running");
    pinMode(cfg::kBootBtnPin, INPUT_PULLUP);

    while (true) {
        const uint32_t now = millis();
        pollButtons();
        pollBattery(now);
        vTaskDelay(pdMS_TO_TICKS(cfg::kButtonPollMs));
    }
}

}  // namespace tasks
