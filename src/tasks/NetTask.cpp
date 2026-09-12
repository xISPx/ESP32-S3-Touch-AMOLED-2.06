// ============================================================================
//  NetTask.cpp — settings-driven Wi-Fi + SNTP + RTC write-back.
// ============================================================================
#include "tasks/NetTask.hpp"
#include "config/config.hpp"
#include "core/Event.hpp"
#include "core/Logger.hpp"
#include "core/PwrState.hpp"
#include "core/Settings.hpp"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

namespace tasks {

namespace {
    constexpr const char* kTag = "Net";

    void sntp_sync_cb(struct timeval* tv) {
        // Runs in the lwIP task context — publish via the static bus only.
        core::EventBus::instance().publish(
            core::Event::makeTimeSync(true, static_cast<uint32_t>(tv->tv_sec)));
    }
}

void NetTask::applyTimezone() {
    const uint8_t idx = core::Settings::instance().d().tzIndex;
    if (idx == tz_applied_) return;
    setenv("TZ", core::tzPosix(idx), 1);
    tzset();
    tz_applied_ = idx;
    LOGI(kTag, "timezone set to \"%s\"", core::tzPosix(idx));
}

void NetTask::configureSntp() {
    if (sntp_configured_) return;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, cfg::kNtpServer1);
    sntp_setservername(1, cfg::kNtpServer2);
    sntp_set_time_sync_notification_cb(sntp_sync_cb);
    sntp_init();
    sntp_configured_ = true;
    LOGI(kTag, "SNTP configured (%s)", cfg::kNtpServer1);
}

void NetTask::startAP(bool staToo) {
    if (ap_active_) return;
    const String mac = WiFi.macAddress();
    const String ssid = String(cfg::kApSsidPrefix) + mac.substring(12);
    WiFi.mode(staToo ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(ssid.c_str(), cfg::kApPassword);
    ap_active_ = true;
    LOGI(kTag, "AP up: ssid=\"%s\" pass=\"%s\" ip=%s", ssid.c_str(),
         cfg::kApPassword, WiFi.softAPIP().toString().c_str());
}

void NetTask::stopAP() {
    if (!ap_active_) return;
    WiFi.softAPdisconnect(true);
    ap_active_ = false;
    LOGI(kTag, "AP down (station connected)");
}

bool NetTask::connectAttempt(uint32_t now_ms) {
    last_attempt_ms_ = now_ms;
    ++attempt_;

    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        // Already up: only refresh link quality.
        core::EventBus::instance().publish(core::Event::makeNetStatus(
            true, static_cast<int8_t>(WiFi.RSSI()), static_cast<uint8_t>(attempt_)));
        return true;
    }

    const auto& s = core::Settings::instance().d();
    LOGI(kTag, "connect attempt %u to \"%s\"",
         static_cast<unsigned>(attempt_), s.wifiSsid);
    WiFi.mode(ap_active_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(s.wifiSsid, s.wifiPass);

    // Non-blocking wait: poll status for up to kNtpWaitMs.
    const uint32_t deadline = now_ms + cfg::kNtpWaitMs;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    const bool ok = WiFi.status() == WL_CONNECTED;
    if (ok) {
        LOGI(kTag, "Wi-Fi connected, ip=%s rssi=%d",
             WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()));
        configureSntp();
    } else {
        LOGW(kTag, "Wi-Fi connect failed (attempt %u)", static_cast<unsigned>(attempt_));
    }
    core::EventBus::instance().publish(core::Event::makeNetStatus(
        ok, ok ? static_cast<int8_t>(WiFi.RSSI()) : 0, static_cast<uint8_t>(attempt_)));

    if (!ok) {
        core::SystemHealth::mark(core::ModuleBit::Network);
    } else {
        core::SystemHealth::clear(core::ModuleBit::Network);
    }
    return ok;
}

void NetTask::onSettingsChanged(uint8_t mask) {
    if (mask & core::kSettingsTimezone) {
        applyTimezone();
    }
    if (mask & core::kSettingsWifi) {
        // Force a fresh attempt with the new credentials at the next tick.
        if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
        last_attempt_ms_ = 0;  // immediate retry
        synced_ = false;       // allow SNTP watchdog to run again
    }
}

void NetTask::run() {
    // This task runs on a PSRAM stack: flash writes (cache-disable) from
    // here would abort.  Keep Wi-Fi credentials in RAM only.
    WiFi.persistent(false);
    // Inbound connections (web server) need the radio always listening —
    // modem sleep delays/loses incoming SYNs.
    WiFi.setSleep(false);
    applyTimezone();

    const bool haveCreds = core::Settings::instance().d().wifiSsid[0] != '\0';
    if (!haveCreds) {
        LOGI(kTag, "no Wi-Fi credentials — offline watch mode (set them in Settings)");
        core::SystemHealth::mark(core::ModuleBit::Network);
        core::EventBus::instance().publish(core::Event::makeNetStatus(false, 0, 0));
    }

    core::EventBus::instance().subscribe(mailbox());

    core::Event e;
    while (true) {
        // Drain mailbox without blocking (2 s cadence below drives the loop).
        while (waitFor(e, 0)) {
            if (e.type == core::EventType::SettingsChanged) {
                onSettingsChanged(e.settings.mask);
            }
        }

        const uint32_t now = millis();
        const bool haveSsid = core::Settings::instance().d().wifiSsid[0] != '\0';

        if (!haveSsid) {
            if (attempt_ == 0) {
                attempt_ = 1;  // publish offline once, stay quiet afterwards
                core::EventBus::instance().publish(
                    core::Event::makeNetStatus(false, 0, 0));
            }
            startAP(false);  // no credentials: AP is the setup channel
        } else if (WiFi.status() != WL_CONNECTED &&
                   (attempt_ == 0 || (now - last_attempt_ms_) >= cfg::kNetRetryMs)) {
            if (!connectAttempt(now)) {
                ++fail_count_;
                if (fail_count_ >= cfg::kApFailThreshold) startAP(true);
            } else {
                fail_count_ = 0;
                stopAP();
            }
        } else if (WiFi.status() == WL_CONNECTED) {
            // Periodic link-quality beacon while connected.
            core::EventBus::instance().publish(core::Event::makeNetStatus(
                true, static_cast<int8_t>(WiFi.RSSI()), static_cast<uint8_t>(attempt_)));

            // SNTP timeout watchdog: if configured but never synced.
            if (!synced_ && sntp_configured_) {
                static uint32_t sync_wait_start = 0;
                if (sync_wait_start == 0) sync_wait_start = millis();
                if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                    synced_ = true;
                } else if (millis() - sync_wait_start > cfg::kNtpWaitMs) {
                    LOGW(kTag, "SNTP sync timeout");
                    core::EventBus::instance().publish(
                        core::Event::makeTimeSync(false, 0));
                    synced_ = true;  // stop nagging; clock stays on RTC time
                }
            }
        }

        // Power saving: modem sleep while nobody looks at the watch.  The
        // screen is off AND no HTTP request for a while -> let the radio doze
        // (inbound connections still work, just with DTIM latency).  Any web
        // request or screen wake switches the radio back to active.
        const bool want_ps = !core::pwr::uiAwake.load() &&
                             !ap_active_ &&
                             (millis() - core::pwr::lastWebMs.load()) > cfg::kWebIdleMs;
        if (want_ps != modem_sleep_) {
            modem_sleep_ = want_ps;
            WiFi.setSleep(modem_sleep_);
            LOGI(kTag, "Wi-Fi modem sleep %s", modem_sleep_ ? "on (idle)" : "off");
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

}  // namespace tasks
