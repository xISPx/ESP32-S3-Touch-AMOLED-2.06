// ============================================================================
//  RtcHal.hpp — PCF85063 battery-backed real-time clock (SensorLib).
// ============================================================================
#pragma once

#include <cstdint>
#include "core/Error.hpp"
#include "SensorPCF85063.hpp"

namespace hal {

class RtcHal {
public:
    RtcHal() = default;
    ~RtcHal() = default;
    RtcHal(const RtcHal&) = delete;
    RtcHal& operator=(const RtcHal&) = delete;

    core::Status init();

    // Reads the RTC and seeds the system clock (settimeofday) when the
    // register content is plausible (year >= 2020).  Returns the success.
    bool syncSystemClockFromRtc();

    // Writes the current system clock back into the RTC (after SNTP sync).
    void storeSystemClockToRtc();

    bool valid() const { return valid_; }

private:
    SensorPCF85063 rtc_;
    bool valid_ = false;
};

}  // namespace hal
