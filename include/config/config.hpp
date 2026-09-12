// ============================================================================
//  config.hpp — single source of truth for every tunable in the system.
//
//  Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
//  MCU:   ESP32-S3R8 (dual-core LX7 @240 MHz, 16 MB flash, 8 MB octal PSRAM)
//
//  Pin map and device addresses were taken from the vendor wiki and the
//  bundled `Mylibrary/pin_config.h` + vendor demo sketches:
//    https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06
// ============================================================================
#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"  // BaseType_t / UBaseType_t in task topology

namespace cfg {

// Firmware identity — OTA body (AiTask) and the web panel (/status).
inline constexpr const char* kFwVersion = "1.1.0";

// ---------------------------------------------------------------------------
//  Pins — QSPI AMOLED (CO5300, 410x502)
// ---------------------------------------------------------------------------
inline constexpr uint8_t  kLcdSdio0   = 4;
inline constexpr uint8_t  kLcdSdio1   = 5;
inline constexpr uint8_t  kLcdSdio2   = 6;
inline constexpr uint8_t  kLcdSdio3   = 7;
inline constexpr uint8_t  kLcdSclk    = 11;
inline constexpr uint8_t  kLcdCs      = 12;
inline constexpr uint8_t  kLcdReset   = 8;
inline constexpr uint16_t kLcdWidth   = 410;
inline constexpr uint16_t kLcdHeight  = 502;
inline constexpr uint8_t  kLcdRotation = 0;
// Horizontal pixel offset of the CO5300 panel (vendor demo value).
inline constexpr uint8_t  kLcdColOffset = 22;

// ---------------------------------------------------------------------------
//  Pins — shared I2C bus (touch / PMU / RTC / IMU / IO-expander)
// ---------------------------------------------------------------------------
inline constexpr uint8_t  kIicSda  = 15;
inline constexpr uint8_t  kIicScl  = 14;
inline constexpr uint32_t kIicFreq = 400 * 1000;  // FT3168 supports up to 400 kHz

// Device I2C addresses
inline constexpr uint8_t kAddrTouchFt3168 = 0x38;  // FocalTech FT3168
inline constexpr uint8_t kAddrPmuAxp2101  = 0x34;  // X-Powers AXP2101
inline constexpr uint8_t kAddrRtcPcf85063 = 0x51;  // PCF85063
inline constexpr uint8_t kAddrImuQmi8658  = 0x6B;  // QMI8658 (low-address variant)
inline constexpr uint8_t kAddrIoXl9555    = 0x20;  // XL9555 16-bit I/O expander

// Touch interrupt (active-low pulse on touch events) and reset pins
inline constexpr int8_t kTpInt   = 38;
inline constexpr int8_t kTpReset = 9;

// XL9555 expander input that reads the side PWR button (high = pressed).
inline constexpr uint8_t kPwrBtnExpIo = 6;  // EXIO6

// BOOT button (low = pressed), also doubles as download-mode pin
inline constexpr uint8_t kBootBtnPin = 0;

// ---------------------------------------------------------------------------
//  TF card (SDMMC) — unused by the prototype, kept for reference
// ---------------------------------------------------------------------------
inline constexpr uint8_t kSdMmcClk = 2;
inline constexpr uint8_t kSdMmcCmd = 1;
inline constexpr uint8_t kSdMmcDat = 3;
inline constexpr uint8_t kSdMmcCs  = 17;

// ---------------------------------------------------------------------------
//  FreeRTOS task topology
// ---------------------------------------------------------------------------
// Priorities: UI owns the most latency-critical path (render + touch),
// sampling tasks run at lower priority on the opposite core.
inline constexpr UBaseType_t kUiTaskPriority     = 5;
inline constexpr UBaseType_t kSensorTaskPriority = 3;
inline constexpr UBaseType_t kPowerTaskPriority  = 2;
inline constexpr UBaseType_t kNetTaskPriority    = 2;

// Stacks sized with headroom: Logger's vsnprintf (char[256] + newlib internals)
// plus the Arduino Print path consume ~3-4 KB on their own.
inline constexpr uint32_t kUiTaskStack     = 32 * 1024;  // LVGL + 11 apps creation is stack-hungry (was 28K, overflowed on the 11th app)
inline constexpr uint32_t kSensorTaskStack = 14 * 1024;
inline constexpr uint32_t kPowerTaskStack  = 14 * 1024;  // Event is ~520B now (AI text payload)
inline constexpr uint32_t kNetTaskStack    = 20 * 1024;  // WiFi.begin path needs ~18KB — do not reduce
inline constexpr uint32_t kAiTaskStack     = 24 * 1024;  // WS-TLS handshake + JSON need headroom

// Tasks are pinned: keep RF/protocol work away from the UI core.
inline constexpr BaseType_t kUiTaskCore     = 1;
inline constexpr BaseType_t kSensorTaskCore = 0;
inline constexpr BaseType_t kPowerTaskCore  = 0;
inline constexpr BaseType_t kNetTaskCore    = 0;

inline constexpr UBaseType_t kUiQueueLength     = 8;   // drained every 10 ms
inline constexpr UBaseType_t kMonitorQueueLength = 6;
inline constexpr UBaseType_t kAiQueueLength      = 4;

// AI worker: TLS + JSON parsing need generous headroom; runs off the UI core.
inline constexpr UBaseType_t kAiTaskPriority = 2;
inline constexpr BaseType_t  kAiTaskCore     = 0;

// Fallback access point: raised when the saved Wi-Fi cannot be reached
// (or no credentials are set) so the web panel stays reachable.
inline constexpr const char* kApSsidPrefix  = "Watch-";
inline constexpr const char* kApPassword    = "12345678";
inline constexpr uint32_t    kApFailThreshold = 3;   // failed attempts before AP

// Browser access (tabbed panel, uploads, Wi-Fi scan, SD file manager).
// 16 KB: bigger PROGMEM page streams in 1.4 KB chunks + JSON/String building
// (10 KB crashed serving the old 5 KB page).
inline constexpr UBaseType_t kWebTaskPriority = 1;
inline constexpr uint32_t    kWebTaskStack    = 16 * 1024;
inline constexpr BaseType_t  kWebTaskCore     = 0;

// ---------------------------------------------------------------------------
//  Timings (all waits are non-blocking: vTaskDelay / lv_timer periods)
// ---------------------------------------------------------------------------
inline constexpr uint32_t kSensorPollMs     = 20;    // 50 Hz sampling loop
inline constexpr uint32_t kPowerPollMs      = 2000;  // battery telemetry period
inline constexpr uint32_t kButtonPollMs     = 20;    // button debounce period
inline constexpr uint32_t kButtonDebounceMs = 60;
inline constexpr uint32_t kNetRetryMs       = 15000; // Wi-Fi reconnect backoff
inline constexpr uint32_t kHealthReportMs   = 10000; // main-loop watchdog print

// ---------------------------------------------------------------------------
//  UI / display behaviour
// ---------------------------------------------------------------------------
inline constexpr uint8_t  kBrightnessFull  = 200;  // 0..255 -> CO5300 reg 0x51
inline constexpr uint8_t  kBrightnessIdle  = 60;
inline constexpr uint32_t kIdleDimAfterMs  = 15000; // dim without touch
inline constexpr uint32_t kLvglTimerTickMs = 10;    // event-queue drain timer

// ---------------------------------------------------------------------------
//  Networking (credentials are edited on-device: Settings → Wi-Fi)
// ---------------------------------------------------------------------------
// Europe/Kyiv example; adjust to your zone. https://github.com/nayarsystems/posix_tz_db
inline constexpr const char* kNtpServer1   = "pool.ntp.org";
inline constexpr const char* kNtpServer2   = "time.nist.gov";
inline constexpr uint32_t    kNtpWaitMs    = 20000;

// ---------------------------------------------------------------------------
//  Audio — official XiaoZhi board config (main/boards/waveshare/
//  esp32-s3-touch-amoled-2.06): ES8311 DAC (speaker) + ES7210 ADC (mic),
//  duplex I2S at 24 kHz, MCLK 256·fs = 6.144 MHz (ES7210 doubles it), PA 46.
//  AXP2101 ALDO1 3.3 V powers the microphone.
// ---------------------------------------------------------------------------
inline constexpr uint8_t  kAddrAudioEs8311 = 0x18;  // DAC (speaker)
inline constexpr uint8_t  kAddrAudioEs7210 = 0x40;  // ADC (microphone)
inline constexpr uint8_t  kI2sBclk  = 41;
inline constexpr uint8_t  kI2sWs    = 45;
inline constexpr uint8_t  kI2sDout  = 40;  // -> ES8311 DAC
inline constexpr uint8_t  kI2sDin   = 42;  // <- ES7210 ADC
inline constexpr uint8_t  kI2sMclk  = 16;
inline constexpr uint8_t  kAudioPaPin = 46;  // speaker amplifier enable, active high
inline constexpr uint32_t kI2sSampleRate   = 24000;  // official duplex rate
inline constexpr uint32_t kOpusSampleRate  = 16000;  // XiaoZhi opus format
inline constexpr uint32_t kOpusFrameMs     = 60;     // official client frame duration
inline constexpr uint32_t kOpusFrameSamples = kOpusSampleRate * kOpusFrameMs / 1000;   // 960
inline constexpr uint32_t kPttFrameSamples = kI2sSampleRate  * kOpusFrameMs / 1000;   // 1440
inline constexpr uint8_t  kMicGainEs7210   = 8;  // ES7210 gain index (8 = 24 dB, official default)
inline constexpr uint8_t  kSpeakerVolume   = 85;  // 0..100

// ---------------------------------------------------------------------------
//  Fitness / pedometer
// ---------------------------------------------------------------------------
inline constexpr uint32_t kFitnessGoalSteps = 6000;

// ---------------------------------------------------------------------------
//  Pedometer tuning (adaptive detector on |linear accel| after gravity EMA)
// ---------------------------------------------------------------------------
inline constexpr float    kStepThrMinG       = 0.07f;  // threshold floor (g)
inline constexpr float    kStepThrMaxG       = 0.35f;  // adaptive threshold cap (g)
inline constexpr float    kStepPeakMinG      = 0.09f;  // valid swing amplitude (g)
inline constexpr float    kStepPeakMaxG      = 1.6f;   // above = jerk/shake, kills the pattern
inline constexpr uint32_t kStepIntervalMinMs = 280;    // ~3.6 steps/s cap
inline constexpr uint32_t kStepIntervalMaxMs = 2000;   // ~0.5 steps/s floor
inline constexpr uint8_t  kStepConsecStart   = 4;      // steps to arm the pattern
inline constexpr float    kStepIntervalTolMin = 0.55f; // cadence regularity gate:
inline constexpr float    kStepIntervalTolMax = 1.80f; // step-to-step jitter vs running avg
inline constexpr float    kShakeThresholdG   = 2.2f;   // shake-to-wake jerk (g)

// ---------------------------------------------------------------------------
//  Power saving
// ---------------------------------------------------------------------------
// Screen power stages: on -> dim (dimTimeoutS from settings) -> off (panel
// sleep + sensor/HTML rate drops). kScreenOffAfterMs extends the dim stage.
inline constexpr uint32_t kScreenOffAfterMs = 45000;
// IMU poll period when the screen is off (50 Hz awake, 20 Hz asleep).
inline constexpr uint32_t kSensorPollOffMs  = 50;
// Wi-Fi modem sleep kicks in when the screen has been off for a while and no
// HTTP request arrived — the watch's router drops inbound SYNs from a
// dozing radio, so the panel stays reachable for the first minutes only;
// after that, waking the watch (tap/shake/PWR) brings it back.
inline constexpr uint32_t kWebIdleMs        = 300000;

}  // namespace cfg
