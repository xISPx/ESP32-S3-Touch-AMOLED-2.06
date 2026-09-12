// ============================================================================
//  Hal.hpp — HAL object registry: one instance of every device driver,
//  constructed once at boot and referenced by tasks afterwards.
// ============================================================================
#pragma once

#include "hal/DisplayHal.hpp"
#include "hal/TouchHal.hpp"
#include "hal/PowerHal.hpp"
#include "hal/RtcHal.hpp"
#include "hal/ImuHal.hpp"
#include "hal/IoExpanderHal.hpp"
#include "hal/AudioHal.hpp"

namespace hal {

// Aggregates all peripheral drivers; owns them by unique_ptr where the
// underlying library object is heap-allocated.
struct Hal {
    DisplayHal    display;
    TouchHal      touch;
    PowerHal      power;
    RtcHal        rtc;
    ImuHal        imu;
    IoExpanderHal expander;
    AudioHal      audio;
};

}  // namespace hal
