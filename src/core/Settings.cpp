// ============================================================================
//  Settings.cpp — NVS-backed settings + timezone presets.
// ============================================================================
#include "core/Settings.hpp"
#include "core/Logger.hpp"
#include <Arduino.h>
#include <Preferences.h>

namespace core {

namespace {
    constexpr const char* kTag = "Settings";
    constexpr const char* kNvsNamespace = "watch";

    const TzPreset kTzPresets[] = {
        {"UTC",        "UTC0"},
        {"Киев",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Москва",     "MSK-3"},
        {"Берлин",     "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Лондон",     "GMT0BST,M3.5.0/1,M10.5.0"},
        {"Нью-Йорк",   "EST5EDT,M3.2.0,M11.1.0"},
        {"Дубай",      "GST-4"},
        {"Дели",       "IST-5:30"},
        {"Пекин",      "CST-8"},
        {"Токио",      "JST-9"},
    };
    constexpr size_t kTzPresetCount = sizeof(kTzPresets) / sizeof(kTzPresets[0]);
}

const TzPreset* tzPresets(size_t& count) {
    count = kTzPresetCount;
    return kTzPresets;
}

const char* tzPosix(uint8_t index) {
    return kTzPresets[index % kTzPresetCount].posix;
}

Settings& Settings::instance() {
    static Settings s;
    return s;
}

void Settings::load() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        LOGW(kTag, "NVS namespace missing — defaults");
        return;
    }
    SettingsData defaults{};

    auto getString = [&](const char* key, char* dst, size_t cap) {
        String v = prefs.getString(key, "");
        strlcpy(dst, v.c_str(), cap);
    };

    getString("wifi_ssid", data_.wifiSsid, sizeof(data_.wifiSsid));
    getString("wifi_pass", data_.wifiPass, sizeof(data_.wifiPass));
    data_.brightness  = prefs.getUChar("brightness", defaults.brightness);
    data_.dimTimeoutS = prefs.getUShort("dim_timeout", defaults.dimTimeoutS);
    data_.tzIndex     = prefs.getUChar("tz_index", defaults.tzIndex);
    data_.volume      = prefs.getUChar("volume", defaults.volume);
    data_.micGain     = prefs.getUChar("mic_gain", defaults.micGain);
    data_.aiBackend   = prefs.getUChar("ai_backend", defaults.aiBackend);
    getString("ai_ws_url",  data_.aiWsUrl,  sizeof(data_.aiWsUrl));
    getString("ai_token",   data_.aiToken,  sizeof(data_.aiToken));
    getString("ai_api_url", data_.aiApiUrl, sizeof(data_.aiApiUrl));
    getString("ai_api_key", data_.aiApiKey, sizeof(data_.aiApiKey));
    getString("ai_model",   data_.aiModel,  sizeof(data_.aiModel));

    prefs.end();
    LOGI(kTag, "loaded: ssid=\"%s\" brightness=%u tz=%s backend=%u",
         data_.wifiSsid, data_.brightness, tzPosix(data_.tzIndex), data_.aiBackend);
}

bool Settings::save() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        LOGE(kTag, "NVS open for write failed");
        return false;
    }
    prefs.putString("wifi_ssid",  data_.wifiSsid);
    prefs.putString("wifi_pass",  data_.wifiPass);
    prefs.putUChar("brightness",  data_.brightness);
    prefs.putUShort("dim_timeout", data_.dimTimeoutS);
    prefs.putUChar("tz_index",    data_.tzIndex);
    prefs.putUChar("volume",      data_.volume);
    prefs.putUChar("mic_gain",    data_.micGain);
    prefs.putUChar("ai_backend",  data_.aiBackend);
    prefs.putString("ai_ws_url",  data_.aiWsUrl);
    prefs.putString("ai_token",   data_.aiToken);
    prefs.putString("ai_api_url", data_.aiApiUrl);
    prefs.putString("ai_api_key", data_.aiApiKey);
    prefs.putString("ai_model",   data_.aiModel);
    prefs.end();
    return true;
}

}  // namespace core
