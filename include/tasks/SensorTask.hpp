// ============================================================================
//  SensorTask.hpp — QMI8658 sampling + adaptive pedometer.
//
//  Samples the IMU at 50 Hz (20 Hz with the screen asleep) and runs a
//  fitness-band style step counter:
//    1. a slow vector estimate of gravity separates orientation from motion,
//    2. |linear acceleration| is peak-detected against an adaptive threshold
//       (peak-hold with decay — adapts to the wearer's gait),
//    3. a candidate step is validated by cadence windows (0.5..3.6 steps/s),
//       an amplitude cap (violent jerks kill the pattern) and a cadence
//       regularity gate (shaking is arrhythmic — the pattern resets when the
//       step-to-step interval deviates too far from the running average),
//    4. a "4 consecutive steps" pattern gate swallows single jerks, hand
//       waves and vehicle vibration.
//  Publishes throttled events to the bus (steps @1 Hz, raw samples @5 Hz —
//  raw samples only while the screen is awake).
// ============================================================================
#pragma once

#include "core/TaskBase.hpp"
#include "hal/Hal.hpp"

namespace tasks {

class SensorTask final : public core::TaskBase {
public:
    explicit SensorTask(hal::Hal& hal) : hal_(hal) {}

private:
    void run() override;

    // Feed one accel sample (in g, gravity included); poll_ms is the current
    // sampling period (EMA coefficients are tuned by it).  Returns true when
    // a step was counted on this sample.
    bool detectStep(float ax, float ay, float az, uint32_t now_ms,
                    uint32_t poll_ms);

    hal::Hal& hal_;
    uint32_t steps_ = 0;
    uint32_t last_publish_ms_ = 0;
    uint32_t last_save_ms_ = 0;
    uint16_t steps_day_ = 0;  // (year*366+yday) the counter belongs to

    void restoreDailySteps();
    void saveDailySteps();

    // pedometer state
    float grav_x_ = 0.0f, grav_y_ = 0.0f, grav_z_ = 1.0f;  // gravity estimate
    float dev_s_ = 0.0f;        // smoothed |linear accel|
    float ref_ = 0.0f;          // adaptive peak-hold reference
    uint8_t step_state_ = 0;    // 0 = idle, 1 = inside a swing
    float peak_ = 0.0f;         // current swing peak (g)
    uint32_t peak_t_ = 0;       // time of the current swing peak
    uint32_t last_step_t_ = 0;  // peak time of the last accepted step
    uint8_t consec_ = 0;        // consecutive cadence-valid steps
    float interval_avg_ = 0.0f; // running average step interval (ms)
    float dev_last_ = 0.0f;     // last |linear accel| (for shake detection)
    uint32_t last_shake_ms_ = 0;
};

}  // namespace tasks
