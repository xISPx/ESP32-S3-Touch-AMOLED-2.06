// ============================================================================
//  Event.hpp — typed, trivially-copyable inter-task messages + EventBus.
//
//  Tasks never call each other directly and share no mutable state.  All
//  asynchronous coordination happens through FreeRTOS queues carrying Event
//  values.  Events are POD so xQueueSend/Receive can memcpy them safely.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace core {

enum class EventType : uint8_t {
    Battery,    // PowerTask  -> UI: telemetry snapshot
    Button,     // PowerTask  -> UI: physical button transition
    TimeSync,   // NetTask    -> UI: SNTP sync done / failed
    NetStatus,  // NetTask    -> UI: link up / down + rssi
    StepCount,  // SensorTask -> UI: accumulated step count
    ImuSample,  // SensorTask -> UI: throttled raw sample for the status page
    SettingsChanged,  // UiTask(settings app) -> tasks: settings subset changed
    AiText,     // UiTask(chat) <-> AiTask: user query / assistant reply / error
    Shutdown,   // PowerTask -> UI: long PWR press — show screen, then power cut
    Wake,       // SensorTask -> UI: shake detected — wake the screen
};

// What changed (SettingsChanged.mask) / what the AI text carries.
enum SettingsMask : uint8_t {
    kSettingsWifi     = 1 << 0,
    kSettingsDisplay  = 1 << 1,
    kSettingsTimezone = 1 << 2,
    kSettingsAi       = 1 << 3,
    kSettingsSound    = 1 << 4,
    // 0x80: request flag — UiTask (internal stack) performs the NVS write,
    // because PSRAM-stack tasks must never touch the SPI flash.
    kSettingsPersist  = 1 << 7,
};

enum class AiMsgKind : uint8_t {
    Query = 0,       // UI -> AiTask: typed text query
    Reply = 1,       // AiTask -> UI: assistant text (tts sentence / completion)
    Error = 2,       // AiTask -> UI: failure notice
    Stt = 3,         // AiTask -> UI: recognized user speech (xiaozhi "stt")
    VoiceStart = 4,  // UI -> AiTask: begin push-to-talk capture
    VoiceStop = 5,   // UI -> AiTask: end push-to-talk capture
    Info = 6,        // AiTask -> UI: neutral notice (activation code, hints)
    Emotion = 7,     // AiTask -> UI: official llm emotion (text = emotion)
    Speaking = 8,    // AiTask -> UI: TTS playback state (text "1"/"0")
    Status = 9,      // AiTask -> UI: connection progress (text = status)
};

// Fixed payload so AiText stays a POD Event member.  Replies are truncated
// to this length on publish (fine for a watch-sized chat bubble).
inline constexpr size_t kAiTextMax = 512;

struct Event {
    EventType type;

    union {
        struct {  // Battery
            uint8_t  percent;
            bool     charging;
            bool     present;
            uint16_t voltage_mv;
            uint16_t vbus_mv;
            int8_t   temperature_c;
        } battery;

        struct {  // Button
            uint8_t  button_id;   // 0 = BOOT, 1 = PWR
            bool     pressed;     // true = pressed edge, false = released
            uint32_t held_ms;
        } button;

        struct {  // TimeSync
            bool     synced;
            uint32_t epoch;
        } time;

        struct {  // NetStatus
            bool     connected;
            int8_t   rssi;
            uint8_t  attempt;
        } net;

        struct {  // StepCount
            uint32_t steps;
        } steps;

        struct {  // ImuSample (fixed-point to stay POD-friendly & small)
            int16_t ax, ay, az;   // milli-g
            int16_t gx, gy, gz;   // milli-dps
        } imu;

        struct {  // SettingsChanged
            uint8_t mask;         // SettingsMask bits
        } settings;

        struct {  // AiText
            uint8_t kind;         // AiMsgKind
            char    text[kAiTextMax];
        } ai;
    };

    // ---- factory helpers keep the union access disciplined -----------------
    static Event makeBattery(uint8_t percent, bool charging, bool present,
                             uint16_t voltage_mv, uint16_t vbus_mv, int8_t temp_c) {
        Event e{};
        e.type = EventType::Battery;
        e.battery = {percent, charging, present, voltage_mv, vbus_mv, temp_c};
        return e;
    }
    static Event makeButton(uint8_t id, bool pressed, uint32_t held_ms) {
        Event e{};
        e.type = EventType::Button;
        e.button = {id, pressed, held_ms};
        return e;
    }
    static Event makeTimeSync(bool synced, uint32_t epoch) {
        Event e{};
        e.type = EventType::TimeSync;
        e.time = {synced, epoch};
        return e;
    }
    static Event makeNetStatus(bool connected, int8_t rssi, uint8_t attempt) {
        Event e{};
        e.type = EventType::NetStatus;
        e.net = {connected, rssi, attempt};
        return e;
    }
    static Event makeSteps(uint32_t steps) {
        Event e{};
        e.type = EventType::StepCount;
        e.steps = {steps};
        return e;
    }
    static Event makeImu(int16_t ax, int16_t ay, int16_t az,
                         int16_t gx, int16_t gy, int16_t gz) {
        Event e{};
        e.type = EventType::ImuSample;
        e.imu = {ax, ay, az, gx, gy, gz};
        return e;
    }
    static Event makeSettingsChanged(uint8_t mask) {
        Event e{};
        e.type = EventType::SettingsChanged;
        e.settings = {mask};
        return e;
    }
    static Event makeShutdown() {
        Event e{};
        e.type = EventType::Shutdown;
        return e;
    }
    static Event makeWake() {
        Event e{};
        e.type = EventType::Wake;
        return e;
    }
    static Event makeAiText(AiMsgKind kind, const char* text) {
        Event e{};
        e.type = EventType::AiText;
        e.ai.kind = static_cast<uint8_t>(kind);
        if (text) {
            strncpy(e.ai.text, text, kAiTextMax - 1);
            e.ai.text[kAiTextMax - 1] = '\0';
        } else {
            e.ai.text[0] = '\0';
        }
        return e;
    }
};

// ---------------------------------------------------------------------------
//  EventBus — fan-out publisher.  Producers publish once; every subscriber
//  queue gets a copy.  Delivery is best-effort with zero blocking: a full
//  queue drops the event (UI lags at most one frame; backpressure inside an
//  ISR-less publisher must never stall the producer).
// ---------------------------------------------------------------------------
class EventBus {
public:
    static constexpr size_t kMaxSubscribers = 6;

    // Must be called once from setup() before any task runs.
    static EventBus& instance();

    bool subscribe(QueueHandle_t queue);
    bool unsubscribe(QueueHandle_t queue);

    // Returns the number of subscribers the event was queued into.
    size_t publish(const Event& e);

private:
    EventBus() = default;

    QueueHandle_t subscribers_[kMaxSubscribers] = {};
    size_t subscriberCount_ = 0;
};

}  // namespace core
