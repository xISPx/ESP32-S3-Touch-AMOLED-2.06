// ============================================================================
//  PwrState.hpp — tiny atomic flags shared across tasks without EventBus
//  churn.  Written by UiTask (screen stage) and WebTask (request activity),
//  read by SensorTask (sampling rate) and NetTask (Wi-Fi modem sleep).
// ============================================================================
#pragma once

#include <atomic>
#include <stdint.h>

namespace core::pwr {

// false = the panel is in deep sleep (displayOff); true = on or dimmed.
inline std::atomic<bool> uiAwake{true};

// millis() of the last served HTTP request (any handler).
inline std::atomic<uint32_t> lastWebMs{0};

}  // namespace core::pwr
