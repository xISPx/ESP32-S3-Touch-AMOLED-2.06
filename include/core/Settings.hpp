// ============================================================================
//  Settings.hpp — persistent user settings (NVS via Preferences).
//
//  One flat POD-ish struct owned by the singleton; UI reads/writes it freely
//  (only UiTask mutates it), producers of SettingsChanged events tell the
//  consuming tasks (NetTask: Wi-Fi/timezone, AiTask: backend config) to
//  re-read.  load() at boot, save(mask) on every settings-app commit.
// ============================================================================
#pragma once

#include <cstdint>
#include <stddef.h>

namespace core {

struct SettingsData {
    // Wi-Fi (empty SSID = offline mode until configured on-device)
    char wifiSsid[33] = "";
    char wifiPass[65] = "";

    // Display
    uint8_t  brightness  = 200;  // 0..255, CO5300 reg 0x51
    uint16_t dimTimeoutS = 15;   // 0 = never dim

    // Time
    uint8_t tzIndex = 1;         // index into kTzPresets (Settings.cpp)

    // Sound
    uint8_t volume   = 85;       // ES8311 speaker volume 0..100
    uint8_t micGain  = 6;        // ES7210 ADC gain index 0..8 (step 6 dB)

    // AI assistant
    uint8_t aiBackend = 0;       // 0 = XiaoZhi websocket, 1 = OpenAI-compatible
    char    aiWsUrl[112]  = "wss://api.tenclass.net/xiaozhi/v1/";
    char    aiToken[96]   = "";  // xiaozhi.me access token
    char    aiApiUrl[128] = "https://api.openai.com/v1/chat/completions";
    char    aiApiKey[96]  = "";
    char    aiModel[48]   = "gpt-4o-mini";
};

class Settings {
public:
    static Settings& instance();

    // NVS -> data().  Missing keys keep the struct defaults.  Call once at boot.
    void load();

    // data() -> NVS.  Returns false on write failure.
    bool save();

    SettingsData& d() { return data_; }
    const SettingsData& d() const { return data_; }

private:
    Settings() = default;

    SettingsData data_;
};

// ---- Timezone presets (shared by NetTask and the settings screen) ---------
struct TzPreset {
    const char* label;   // UI (Russian)
    const char* posix;   // POSIX TZ string
};

const TzPreset* tzPresets(size_t& count);
const char* tzPosix(uint8_t index);

}  // namespace core
