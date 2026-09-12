// ============================================================================
//  AudioHal.hpp — official XiaoZhi audio stack for this board:
//  ES8311 (DAC → speaker) + ES7210 (ADC ← microphone) behind one duplex I2S,
//  exactly like main/boards/waveshare/esp32-s3-touch-amoled-2.06.
//
//  Both codecs are driven register-level over Wire (the legacy-I2C drivers
//  cannot share port 0 with Arduino Wire 3.x).  Mic power is AXP2101 ALDO1
//  (enabled by PowerHal::enableMicPower before init).
//
//  The I2S rate is switchable (setRate): the codecs are slaves clocked from
//  the MCLK pin at a fixed 256·fs ratio, so one register configuration serves
//  any rate — only the I2S peripheral is re-started.  This lets the music
//  player match the file rate while the AI sessions pin 24 kHz.
//
//  Half-duplex by design: readMic() during push-to-talk, playPcm() during
//  playback; a mutex serializes capture/playback/rate changes between the
//  music player and AiTask.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "core/Error.hpp"

namespace hal {

class AudioHal {
public:
    AudioHal() = default;
    ~AudioHal() = default;
    AudioHal(const AudioHal&) = delete;
    AudioHal& operator=(const AudioHal&) = delete;

    core::Status init();

    // Re-start I2S at a new sample rate (no-op when unchanged).  Codec
    // registers stay valid: they only assume the 256·fs MCLK ratio.
    void setRate(uint32_t sampleRateHz);
    uint32_t rate() const { return rate_; }

    // Blocking capture of `maxSamples` mono samples at the current rate from
    // ES7210 MIC1 (left slot).  Returns samples actually read.
    size_t readMic(int16_t* dst, size_t maxSamples);

    // Blocking playback of `n` mono samples at the current rate through the
    // ES8311 (duplicated to both slots).  Enables the PA for the duration.
    size_t playPcm(const int16_t* src, size_t n);

    // Drop any buffered capture data (call right after PTT starts).
    void drainMic();

    // Speaker amplifier gate.  playPcm() turns it on; long playback sessions
    // (music player / TTS) keep it on for their duration and release it
    // explicitly — per-chunk toggling produces audible pops.
    void paEnable(bool on);

    void setVolume(uint8_t volume0to100);
    uint8_t volume() const { return volume_; }
    void setMicGain(uint8_t gainIndex);  // 0..8, ES7210 ADC gain (step 6 dB)

    bool healthy() const { return healthy_; }

private:
    // Wire register access helpers (shared-bus mutex inside)
    bool es8311Write(uint8_t reg, uint8_t val);
    bool es8311Read(uint8_t reg, uint8_t& val);
    bool es7210Write(uint8_t reg, uint8_t val);
    bool es7210Read(uint8_t reg, uint8_t& val);
    bool es7210UpdateBits(uint8_t reg, uint8_t mask, uint8_t val);
    bool es8311Init();
    bool es7210Init();
    bool setVolumeConfirmed();
    bool i2sBegin(uint32_t rate);

    SemaphoreHandle_t mtx_ = nullptr;  // serializes I2S users (AI / music)
    bool healthy_ = false;
    uint32_t rate_ = 0;
    uint8_t volume_ = 85;
};

}  // namespace hal
