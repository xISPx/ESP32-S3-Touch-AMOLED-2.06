// ============================================================================
//  AudioHal.cpp — official board audio: ES8311 + ES7210 over Wire, I2S 24 kHz.
//
//  Register sequences ported from Espressif's es8311/es7210 components
//  (Apache-2.0) and the official xiaozhi-esp32 BoxAudioCodec configuration:
//    * I2S std, 16-bit, stereo slots, MCLK = 256·fs = 6.144 MHz @ 24 kHz
//    * ES8311: DAC only, master clock from MCLK pin
//    * ES7210: slave, I2S 16-bit, MIC1 differential, 24 dB, doubler on
//      (6.144 MHz × 2 = 12.288 MHz internal ADC clock)
// ============================================================================
#include "hal/AudioHal.hpp"
#include "config/config.hpp"
#include "core/I2cBus.hpp"
#include "core/Settings.hpp"
#include "core/Logger.hpp"
#include <Arduino.h>
#include <ESP_I2S.h>

namespace hal {

namespace {
    constexpr const char* kTag = "Audio";

    I2SClass i2s;

    // ---- ES8311 registers -----------------------------------------------------
    constexpr uint8_t E8_RESET = 0x00, E8_CLK01 = 0x01, E8_CLK02 = 0x02,
        E8_CLK03 = 0x03, E8_CLK04 = 0x04, E8_CLK05 = 0x05, E8_CLK06 = 0x06,
        E8_CLK07 = 0x07, E8_CLK08 = 0x08, E8_SDPIN = 0x09, E8_SDPOUT = 0x0A,
        E8_SYS0D = 0x0D, E8_SYS12 = 0x12, E8_SYS13 = 0x13,
        E8_ADC1C = 0x1C, E8_DAC32 = 0x32, E8_DAC37 = 0x37;

    // ---- ES7210 registers -----------------------------------------------------
    constexpr uint8_t E7_RESET = 0x00, E7_CLOCK = 0x01, E7_MAINCLK = 0x02,
        E7_MODECFG = 0x08, E7_TIME0 = 0x09, E7_TIME1 = 0x0A,
        E7_SDP1 = 0x11, E7_SDP2 = 0x12, E7_ADC12MUTE = 0x15,
        E7_ADC34MUTE = 0x14, E7_HPF1_34 = 0x20, E7_HPF2_34 = 0x21,
        E7_HPF1_12 = 0x22, E7_HPF2_12 = 0x23, E7_ANALOG = 0x40,
        E7_BIAS12 = 0x41, E7_BIAS34 = 0x42, E7_GAIN1 = 0x43, E7_GAIN2 = 0x44,
        E7_GAIN3 = 0x45, E7_GAIN4 = 0x46, E7_MIC1PWR = 0x47, E7_MIC2PWR = 0x48,
        E7_MIC3PWR = 0x49, E7_MIC4PWR = 0x4A, E7_MIC12PWR = 0x4B,
        E7_MIC34PWR = 0x4C, E7_PWRDOWN = 0x06, E7_OSR = 0x07,
        E7_LRCKH = 0x04, E7_LRCKL = 0x05;

    // I2S read staging: one 60 ms stereo frame at 24 kHz
    constexpr size_t kMaxChunk = 1440;
}

bool AudioHal::i2sBegin(uint32_t rate) {
    i2s.setPins(cfg::kI2sBclk, cfg::kI2sWs, cfg::kI2sDout, cfg::kI2sDin,
                cfg::kI2sMclk);
    if (!i2s.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        return false;
    }
    rate_ = rate;
    return true;
}

void AudioHal::setRate(uint32_t sampleRateHz) {
    if (!healthy_ || sampleRateHz == 0 || sampleRateHz == rate_) return;
    {
        // Serialize against capture/playback users before tearing the bus down.
        if (mtx_) xSemaphoreTake(mtx_, portMAX_DELAY);
        i2s.end();
        const bool ok = i2sBegin(sampleRateHz);
        if (mtx_) xSemaphoreGive(mtx_);
        if (ok) {
            LOGI(kTag, "I2S rate -> %u Hz", static_cast<unsigned>(sampleRateHz));
        } else {
            LOGE(kTag, "I2S re-begin at %u Hz failed", sampleRateHz);
            // Try to recover the previous configuration.
            i2sBegin(rate_);
        }
    }
}

bool AudioHal::es8311Write(uint8_t reg, uint8_t val) {
    core::I2cBus::Guard guard(core::I2cBus::instance());
    auto& w = core::I2cBus::instance().wire();
    w.beginTransmission(cfg::kAddrAudioEs8311);
    w.write(reg);
    w.write(val);
    return w.endTransmission() == 0;
}

bool AudioHal::es8311Read(uint8_t reg, uint8_t& val) {
    core::I2cBus::Guard guard(core::I2cBus::instance());
    auto& w = core::I2cBus::instance().wire();
    w.beginTransmission(cfg::kAddrAudioEs8311);
    w.write(reg);
    if (w.endTransmission(false) != 0) return false;
    if (w.requestFrom(static_cast<uint8_t>(cfg::kAddrAudioEs8311),
                      static_cast<uint8_t>(1)) != 1) return false;
    val = w.read();
    return true;
}

bool AudioHal::es7210Write(uint8_t reg, uint8_t val) {
    core::I2cBus::Guard guard(core::I2cBus::instance());
    auto& w = core::I2cBus::instance().wire();
    w.beginTransmission(cfg::kAddrAudioEs7210);
    w.write(reg);
    w.write(val);
    return w.endTransmission() == 0;
}

bool AudioHal::es7210Read(uint8_t reg, uint8_t& val) {
    core::I2cBus::Guard guard(core::I2cBus::instance());
    auto& w = core::I2cBus::instance().wire();
    w.beginTransmission(cfg::kAddrAudioEs7210);
    w.write(reg);
    if (w.endTransmission(false) != 0) return false;
    if (w.requestFrom(static_cast<uint8_t>(cfg::kAddrAudioEs7210),
                      static_cast<uint8_t>(1)) != 1) return false;
    val = w.read();
    return true;
}

bool AudioHal::es7210UpdateBits(uint8_t reg, uint8_t mask, uint8_t val) {
    uint8_t v = 0;
    if (!es7210Read(reg, v)) return false;
    v = (v & ~mask) | (val & mask);
    return es7210Write(reg, v);
}

bool AudioHal::es8311Init() {
    // Reset / power-on (Espressif es8311_init)
    if (!es8311Write(E8_RESET, 0x1F) || !es8311Write(E8_RESET, 0x00) ||
        !es8311Write(E8_RESET, 0x80)) return false;

    // Clock: all clocks on, MCLK from the MCLK pin (not inverted)
    if (!es8311Write(E8_CLK01, 0x3F)) return false;

    // Coefficients for {mclk 6.144 MHz, fs 24 kHz} — identical register values
    // to the 256·fs rows of the Espressif coefficient table.
    constexpr uint8_t preDiv = 1, preMulti = 0, adcDiv = 1, dacDiv = 1,
        fsMode = 0, lrckH = 0x00, lrckL = 0xFF, bclkDiv = 4,
        adcOsr = 0x10, dacOsr = 0x10;

    uint8_t v;
    if (!es8311Read(E8_CLK02, v)) return false;
    v = (v & 0x07) | ((preDiv - 1) << 5) | (preMulti << 3);
    if (!es8311Write(E8_CLK02, v)) return false;
    if (!es8311Write(E8_CLK03, (fsMode << 6) | adcOsr)) return false;
    if (!es8311Write(E8_CLK04, dacOsr)) return false;
    if (!es8311Write(E8_CLK05, ((adcDiv - 1) << 4) | (dacDiv - 1))) return false;
    if (!es8311Read(E8_CLK06, v)) return false;
    v = (v & 0xE0) | (bclkDiv - 1);
    if (!es8311Write(E8_CLK06, v)) return false;
    if (!es8311Read(E8_CLK07, v)) return false;
    v = (v & 0xC0) | lrckH;
    if (!es8311Write(E8_CLK07, v)) return false;
    if (!es8311Write(E8_CLK08, lrckL)) return false;

    // Format: slave + I2S, 16-bit in/out
    if (!es8311Read(E8_RESET, v)) return false;
    v &= 0xBF;
    if (!es8311Write(E8_RESET, v)) return false;
    if (!es8311Write(E8_SDPIN, 0x0C)) return false;
    if (!es8311Write(E8_SDPOUT, 0x0C)) return false;

    // Power up DAC path (official work mode = DAC only)
    if (!es8311Write(E8_SYS0D, 0x01)) return false;
    if (!es8311Write(E8_SYS12, 0x00)) return false;
    if (!es8311Write(E8_SYS13, 0x10)) return false;
    if (!es8311Write(E8_ADC1C, 0x6A)) return false;
    if (!es8311Write(E8_DAC37, 0x08)) return false;

    return setVolumeConfirmed();
}

bool AudioHal::es7210Init() {
    // es7210_adc_init (Espressif es7210.c)
    if (!es7210Write(E7_RESET, 0xFF)) return false;
    if (!es7210Write(E7_RESET, 0x41)) return false;
    if (!es7210Write(E7_CLOCK, 0x3F)) return false;
    if (!es7210Write(E7_TIME0, 0x30)) return false;
    if (!es7210Write(E7_TIME1, 0x30)) return false;
    if (!es7210Write(E7_HPF2_12, 0x2A)) return false;
    if (!es7210Write(E7_HPF1_12, 0x0A)) return false;
    if (!es7210Write(E7_HPF2_34, 0x0A)) return false;
    if (!es7210Write(E7_HPF1_34, 0x2A)) return false;

    // Slave mode
    if (!es7210UpdateBits(E7_MODECFG, 0x01, 0x00)) return false;

    if (!es7210Write(E7_ANALOG, 0x43)) return false;  // vdda 3.3V, VMID 5k
    if (!es7210Write(E7_BIAS12, 0x70)) return false;  // mic bias 2.87 V
    if (!es7210Write(E7_BIAS34, 0x70)) return false;

    // {mclk 6.144 MHz, fs 24 kHz}: doubler on → 12.288 MHz ADC clock,
    // osr 0x20, LRCK divider 256 (0x100)
    if (!es7210Write(E7_MAINCLK, 0xC1)) return false;
    if (!es7210Write(E7_OSR, 0x20)) return false;
    if (!es7210Write(E7_LRCKH, 0x01)) return false;
    if (!es7210Write(E7_LRCKL, 0x00)) return false;

    // SDP: 16-bit I2S, no TDM
    if (!es7210Write(E7_SDP1, 0x60)) return false;
    if (!es7210Write(E7_SDP2, 0x00)) return false;

    // MIC1 only: power gates on, gain register set
    for (uint8_t r = E7_GAIN1; r <= E7_GAIN4; ++r) {
        if (!es7210UpdateBits(r, 0x10, 0x00)) return false;
    }
    if (!es7210Write(E7_MIC12PWR, 0xFF)) return false;
    if (!es7210Write(E7_MIC34PWR, 0xFF)) return false;
    if (!es7210UpdateBits(E7_CLOCK, 0x0B, 0x00)) return false;
    if (!es7210Write(E7_MIC12PWR, 0x00)) return false;
    if (!es7210UpdateBits(E7_GAIN1, 0x10, 0x10)) return false;
    if (!es7210UpdateBits(E7_GAIN1, 0x0F, core::Settings::instance().d().micGain)) return false;

    // es7210_start: clocks on, power up, mic biases
    if (!es7210Write(E7_CLOCK, 0x3F)) return false;
    if (!es7210Write(E7_PWRDOWN, 0x00)) return false;
    if (!es7210Write(E7_ANALOG, 0x43)) return false;
    if (!es7210Write(E7_MIC1PWR, 0x08)) return false;
    if (!es7210Write(E7_MIC2PWR, 0x08)) return false;
    if (!es7210Write(E7_MIC3PWR, 0x08)) return false;
    if (!es7210Write(E7_MIC4PWR, 0x08)) return false;

    // Unmute ADCs
    if (!es7210UpdateBits(E7_ADC34MUTE, 0x03, 0x00)) return false;
    if (!es7210UpdateBits(E7_ADC12MUTE, 0x03, 0x00)) return false;
    return true;
}

bool AudioHal::setVolumeConfirmed() {
    const int reg32 = (volume_ == 0) ? 0 : ((volume_ * 256 / 100) - 1);
    return es8311Write(E8_DAC32, static_cast<uint8_t>(reg32));
}

void AudioHal::paEnable(bool on) {
    digitalWrite(cfg::kAudioPaPin, on ? HIGH : LOW);
}

void AudioHal::setMicGain(uint8_t gainIndex) {
    if (gainIndex > 8) gainIndex = 8;
    if (!es7210UpdateBits(E7_GAIN1, 0x0F, gainIndex)) return;
    LOGI(kTag, "mic gain -> %u", gainIndex);
}

void AudioHal::setVolume(uint8_t volume0to100) {
    volume_ = volume0to100 > 100 ? 100 : volume0to100;
    setVolumeConfirmed();
}

core::Status AudioHal::init() {
    if (!mtx_) mtx_ = xSemaphoreCreateMutex();

    // Speaker amplifier
    pinMode(cfg::kAudioPaPin, OUTPUT);
    digitalWrite(cfg::kAudioPaPin, LOW);  // mute until playback

    // Duplex I2S, official configuration
    if (!i2sBegin(cfg::kI2sSampleRate)) {
        LOGE(kTag, "I2S begin failed");
        return core::Status::fail(core::ErrorCode::AudioInit);
    }

    const bool ok = es8311Init() && es7210Init();
    if (!ok) {
        LOGE(kTag, "codec init failed (ES8311 0x%02X / ES7210 0x%02X)",
             cfg::kAddrAudioEs8311, cfg::kAddrAudioEs7210);
        return core::Status::fail(core::ErrorCode::AudioInit);
    }

    volume_ = core::Settings::instance().d().volume;
    if (!setVolumeConfirmed()) healthy_ = false;

    healthy_ = true;
    LOGI(kTag, "ES8311+ES7210 ready (%u Hz duplex, vol %u%%)",
         static_cast<unsigned>(cfg::kI2sSampleRate), volume_);
    return core::Status::ok(core::Unit{});
}

size_t AudioHal::readMic(int16_t* dst, size_t maxSamples) {
    if (!healthy_ || maxSamples == 0) return 0;
    // MIC1 rides the left slot; keep it clean (right may carry the AEC
    // reference on boards that wire the speaker loopback).
    static int16_t stereo[2 * kMaxChunk];
    const size_t chunk = maxSamples > kMaxChunk ? kMaxChunk : maxSamples;
    const size_t bytes = i2s.readBytes(reinterpret_cast<char*>(stereo),
                                       chunk * 2 * sizeof(int16_t));
    const size_t got = bytes / (2 * sizeof(int16_t));
    for (size_t i = 0; i < got; ++i) dst[i] = stereo[2 * i];
    return got;
}

void AudioHal::drainMic() {
    static int16_t sink[2 * kMaxChunk];
    for (int i = 0; i < 4; ++i) {  // ~240 ms of DMA ring
        i2s.readBytes(reinterpret_cast<char*>(sink), sizeof(sink));
    }
}

size_t AudioHal::playPcm(const int16_t* src, size_t n) {
    if (!healthy_ || n == 0) return 0;
    digitalWrite(cfg::kAudioPaPin, HIGH);
    static int16_t stereo[2 * kMaxChunk];
    size_t written = 0;
    while (written < n) {
        const size_t chunk = (n - written) > kMaxChunk ? kMaxChunk : (n - written);
        for (size_t i = 0; i < chunk; ++i) {
            stereo[2 * i] = src[written + i];
            stereo[2 * i + 1] = src[written + i];
        }
        if (i2s.write(reinterpret_cast<uint8_t*>(stereo),
                      chunk * 2 * sizeof(int16_t)) == 0) break;
        written += chunk;
    }
    // PA stays on: sessions release it explicitly via paEnable(false).
    return written;
}

}  // namespace hal
