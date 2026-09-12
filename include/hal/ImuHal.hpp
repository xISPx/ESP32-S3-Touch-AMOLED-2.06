// ============================================================================
//  ImuHal.hpp — QMI8658 6-axis IMU (SensorLib): accel+gyro for the pedometer
//  and the live-sensor status page.
// ============================================================================
#pragma once

#include <cstdint>
#include "core/Error.hpp"
#include "SensorQMI8658.hpp"

namespace hal {

struct ImuSample {
    float ax = 0, ay = 0, az = 0;  // g
    float gx = 0, gy = 0, gz = 0;  // dps
    uint32_t timestamp_ms = 0;
};

class ImuHal {
public:
    ImuHal() = default;
    ~ImuHal() = default;
    ImuHal(const ImuHal&) = delete;
    ImuHal& operator=(const ImuHal&) = delete;

    core::Status init();

    // Returns a fresh sample when data is ready; std::nullopt otherwise.
    // Consecutive failures are counted; after kMaxConsecutiveErrors the IMU
    // is re-initialised once (self-recovery), then marked degraded.
    std::optional<ImuSample> readSample();

    bool healthy() const { return consecutive_errors_ < kMaxConsecutiveErrors; }

private:
    bool reinit();

    SensorQMI8658 imu_;
    uint32_t consecutive_errors_ = 0;
    static constexpr uint32_t kMaxConsecutiveErrors = 50;  // ~1 s at 50 Hz
};

}  // namespace hal
