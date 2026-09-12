// ============================================================================
//  main.cpp — Smartwatch prototype for Waveshare ESP32-S3-Touch-AMOLED-2.06.
//
//  Boot sequence:
//    1. USB CDC + logger
//    2. shared I2C bus
//    3. HAL bring-up (display first: it gates everything visual)
//    4. system clock from the PCF85063
//    5. task graph: UiTask / SensorTask / PowerTask / NetTask
//    6. loopTask becomes the health monitor (prints heap + degraded modules)
// ============================================================================
#include <Arduino.h>
#include <esp_heap_caps.h>

#include "config/config.hpp"
#include "core/Error.hpp"
#include "core/Event.hpp"
#include "core/I2cBus.hpp"
#include "core/Logger.hpp"
#include "hal/Hal.hpp"
#include "tasks/Tasks.hpp"

namespace {
using core::ErrorCode;
using core::ModuleBit;
using core::SystemHealth;
using core::Status;

constexpr const char* kTag = "Main";

hal::Hal g_hal;
tasks::UiTask g_ui_task(g_hal);
tasks::SensorTask g_sensor_task(g_hal);
tasks::PowerTask g_power_task(g_hal);
tasks::NetTask g_net_task(g_hal);
tasks::AiTask g_ai_task(g_hal);
tasks::WebTask g_web_task(g_hal);

bool startOrPark(core::TaskBase& task, const char* name, uint32_t stack,
                 UBaseType_t prio, BaseType_t core, UBaseType_t qDepth) {
    if (!task.start(name, stack, prio, core, qDepth)) {
        LOGE(kTag, "failed to start task %s", name);
        return false;
    }
    return true;
}

bool startOrParkExt(core::TaskBase& task, const char* name, uint32_t stack,
                    UBaseType_t prio, BaseType_t core, UBaseType_t qDepth) {
    if (!task.startExternal(name, stack, prio, core, qDepth)) {
        LOGE(kTag, "failed to start task %s", name);
        return false;
    }
    return true;
}

void healthMonitor() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (now - last < cfg::kHealthReportMs) return;
    last = now;

    const uint16_t bits = SystemHealth::raw();
    LOGI(kTag, "health mask=0x%03X heap=%uKB psram=%uKB int=%uKB max=%uKB",
         bits, static_cast<unsigned>(esp_get_free_heap_size() / 1024),
         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
         static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));

    struct { const char* name; TaskHandle_t h; } st[] = {
        {"ui", g_ui_task.handle()},
        {"sensor", g_sensor_task.handle()},
        {"power", g_power_task.handle()},
        {"net", g_net_task.handle()},
        {"ai", g_ai_task.handle()},
    };
    for (auto& t : st) {
        if (t.h) {
            LOGI(kTag, "stack %s min-free=%uB", t.name,
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(t.h)));
        }
    }

    if (bits == 0) return;
    struct { ModuleBit bit; const char* name; } names[] = {
        {ModuleBit::Display,  "display"},
        {ModuleBit::Touch,    "touch"},
        {ModuleBit::Power,    "pmu"},
        {ModuleBit::Rtc,      "rtc"},
        {ModuleBit::Imu,      "imu"},
        {ModuleBit::Expander, "expander"},
        {ModuleBit::Network,  "network"},
        {ModuleBit::Audio,    "audio"},
    };
    for (auto& n : names) {
        if (SystemHealth::isDegraded(n.bit)) {
            LOGW(kTag, "degraded module: %s", n.name);
        }
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);  // let USB CDC enumerate
    core::Logger::init(core::LogLevel::Info);
    LOGI(kTag, "=== ESP32-S3-Touch-AMOLED-2.06 smartwatch prototype ===");
    LOGI(kTag, "flash=%uMB psram=%uKB heap=%uKB",
         static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
         static_cast<unsigned>(ESP.getPsramSize() / 1024),
         static_cast<unsigned>(ESP.getFreeHeap() / 1024));

    core::EventBus::instance();  // construct before tasks subscribe

    // ---- persistent settings (Wi-Fi, display, timezone, AI) ------------------
    core::Settings::instance().load();

    // ---- I2C bus ------------------------------------------------------------
    if (!core::I2cBus::instance().begin(cfg::kIicSda, cfg::kIicScl, cfg::kIicFreq)) {
        SystemHealth::mark(ModuleBit::Touch);  // every I2C device is effectively down
        LOGE(kTag, "I2C bus init failed — I2C devices degraded");
    }

    // ---- HAL bring-up: display gates everything else ------------------------
    if (!g_hal.display.init()) {
        SystemHealth::mark(ModuleBit::Display);
        LOGE(kTag, "display init failed — halting (nothing to show)");
        while (true) { delay(5000); LOGE(kTag, "halted: display unavailable"); }
    }
    if (!g_hal.touch.init()) SystemHealth::mark(ModuleBit::Touch);
    if (!g_hal.power.init()) SystemHealth::mark(ModuleBit::Power);
    if (!g_hal.rtc.init()) {
        SystemHealth::mark(ModuleBit::Rtc);
    } else {
        g_hal.rtc.syncSystemClockFromRtc();
    }
    if (!g_hal.imu.init()) SystemHealth::mark(ModuleBit::Imu);
    if (!g_hal.expander.init()) SystemHealth::mark(ModuleBit::Expander);
    g_hal.power.enableMicPower();  // ALDO1 rail before the audio codecs
    LOGI(kTag, "setup: audio init begin");
    if (!g_hal.audio.init()) SystemHealth::mark(ModuleBit::Audio);
    LOGI(kTag, "setup: audio init done");

    // ---- Task graph ---------------------------------------------------------
    LOGI(kTag, "setup: starting tasks");
    startOrPark(g_ui_task, "ui", cfg::kUiTaskStack, cfg::kUiTaskPriority,
                cfg::kUiTaskCore, cfg::kUiQueueLength);
    startOrPark(g_sensor_task, "sensor", cfg::kSensorTaskStack, cfg::kSensorTaskPriority,
                cfg::kSensorTaskCore, cfg::kMonitorQueueLength);
    startOrPark(g_power_task, "power", cfg::kPowerTaskStack, cfg::kPowerTaskPriority,
                cfg::kPowerTaskCore, cfg::kMonitorQueueLength);
    startOrPark(g_net_task, "net", cfg::kNetTaskStack, cfg::kNetTaskPriority,
                cfg::kNetTaskCore, cfg::kMonitorQueueLength);
    g_ai_task.initClientId();  // NVS access must stay off the PSRAM stack
    startOrPark(g_ai_task, "ai", cfg::kAiTaskStack, cfg::kAiTaskPriority,
                cfg::kAiTaskCore, cfg::kAiQueueLength);
    startOrPark(g_web_task, "web", cfg::kWebTaskStack, cfg::kWebTaskPriority,
                cfg::kWebTaskCore, 1);

    LOGI(kTag, "boot complete (setup returned)");
}

void loop() {
    healthMonitor();
    vTaskDelay(pdMS_TO_TICKS(cfg::kHealthReportMs));
}
