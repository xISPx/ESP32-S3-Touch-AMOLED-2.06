// ============================================================================
//  NetTask.hpp — Wi-Fi station + SNTP time sync + RTC write-back.
//
//  Credentials live in core::Settings (edited on-device in the settings app);
//  an empty SSID means offline mode (RTC time only).  SettingsChanged events
//  trigger reconnect / timezone re-application without a reboot.  Publishes
//  NetStatus and TimeSync events.
// ============================================================================
#pragma once

#include "core/TaskBase.hpp"
#include "hal/Hal.hpp"

namespace tasks {

class NetTask final : public core::TaskBase {
public:
    explicit NetTask(hal::Hal& hal) : hal_(hal) {}

private:
    void run() override;

    bool connectAttempt(uint32_t now_ms);
    void startAP(bool staToo);
    void stopAP();
    void configureSntp();
    void applyTimezone();
    void onSettingsChanged(uint8_t mask);

    hal::Hal& hal_;
    uint32_t attempt_ = 0;
    uint32_t last_attempt_ms_ = 0;
    bool sntp_configured_ = false;
    bool synced_ = false;
    uint8_t tz_applied_ = 0xFF;  // index of the timezone already applied
    bool ap_active_ = false;
    bool modem_sleep_ = false;   // current Wi-Fi power-save state
    uint32_t fail_count_ = 0;
};

}  // namespace tasks
