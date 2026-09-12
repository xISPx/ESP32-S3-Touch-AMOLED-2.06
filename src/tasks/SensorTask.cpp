// ============================================================================
//  SensorTask.cpp — IMU sampling + adaptive pedometer.
// ============================================================================
#include "tasks/SensorTask.hpp"
#include "config/config.hpp"
#include "core/Event.hpp"
#include "core/Logger.hpp"
#include "core/PwrState.hpp"
#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <time.h>

namespace tasks {

namespace {
    constexpr const char* kTag = "Sensor";

    // (year*366 + yday) — a stable per-day key for the step counter.
    uint16_t dayKey() {
        time_t now = time(nullptr);
        struct tm t{};
        localtime_r(&now, &t);
        return static_cast<uint16_t>((t.tm_year + 1900) * 366 + t.tm_yday);
    }
}

void SensorTask::restoreDailySteps() {
    Preferences prefs;
    if (prefs.begin("watch", true)) {
        const uint32_t saved = prefs.getUInt("steps", 0);
        const uint16_t day = prefs.getUShort("steps_day", 0xFFFF);
        prefs.end();
        steps_day_ = dayKey();
        steps_ = (day == steps_day_) ? saved : 0;
        LOGI(kTag, "daily steps restored: %lu (day %u)",
             static_cast<unsigned long>(steps_), steps_day_);
    }
}

void SensorTask::saveDailySteps() {
    steps_day_ = dayKey();
    Preferences prefs;
    if (prefs.begin("watch", false)) {
        prefs.putUInt("steps", steps_);
        prefs.putUShort("steps_day", steps_day_);
        prefs.end();
    }
}

bool SensorTask::detectStep(float ax, float ay, float az, uint32_t now_ms,
                            uint32_t poll_ms) {
    // ---- 1. gravity tracking (slow vector EMA, ~0.5 s time constant) --------
    // Follows slow orientation changes of the wrist but not 1-3 Hz walking.
    // Coefficients scale with the actual poll period (50 Hz awake / 20 Hz
    // asleep), clamped so smoothing never collapses to pass-through.
    float a_g = 0.04f * static_cast<float>(poll_ms) / 20.0f;
    if (a_g > 0.08f) a_g = 0.08f;
    grav_x_ += a_g * (ax - grav_x_);
    grav_y_ += a_g * (ay - grav_y_);
    grav_z_ += a_g * (az - grav_z_);

    // ---- 2. linear acceleration magnitude ------------------------------------
    const float lx = ax - grav_x_;
    const float ly = ay - grav_y_;
    const float lz = az - grav_z_;
    const float dev = sqrtf(lx * lx + ly * ly + lz * lz);

    // ---- 3. light smoothing (~80 ms) ------------------------------------------
    float a_d = 0.4f * static_cast<float>(poll_ms) / 20.0f;
    if (a_d > 0.6f) a_d = 0.6f;
    dev_s_ += a_d * (dev - dev_s_);
    dev_last_ = dev;

    // ---- 4. adaptive threshold: peak-hold with slow decay ---------------------
    // Adapts to the current gait amplitude within a couple of seconds.
    ref_ = (dev_s_ > ref_) ? dev_s_ : ref_ * 0.995f;
    if (ref_ < cfg::kStepThrMinG) ref_ = cfg::kStepThrMinG;
    float thr = ref_ * 0.5f;
    if (thr > cfg::kStepThrMaxG) thr = cfg::kStepThrMaxG;
    const float valley = thr * 0.5f;

    bool counted = false;
    switch (step_state_) {
        case 0:  // idle: wait for a swing crossing the threshold
            if (dev_s_ > thr) {
                step_state_ = 1;
                peak_ = dev_s_;
                peak_t_ = now_ms;
            }
            break;

        case 1: {  // inside a swing: track the peak, close on the valley
            if (dev_s_ > peak_) {
                peak_ = dev_s_;
                peak_t_ = now_ms;
            }
            const bool closed = dev_s_ < valley || (now_ms - peak_t_) > 400;
            if (closed) {
                step_state_ = 0;

                if (peak_ > cfg::kStepPeakMaxG) {
                    // Violent jerk (shaking, door slam): not a step — and it
                    // destroys any pattern collected so far.
                    consec_ = 0;
                    interval_avg_ = 0.0f;
                    break;
                }
                if (peak_ < cfg::kStepPeakMinG) {
                    break;  // too weak to be a step — ignore silently
                }

                const uint32_t interval = peak_t_ - last_step_t_;
                if (interval < cfg::kStepIntervalMinMs) {
                    // bounce/echo of the just-counted step — swallow silently
                    // (last_step_t_ untouched so the next real step measures
                    // its cadence from the true previous step)
                } else if (interval <= cfg::kStepIntervalMaxMs) {
                    // Cadence regularity gate: real walking keeps a steady
                    // rhythm, shaking is arrhythmic.  A step landing far
                    // outside the running average restarts the pattern.
                    if (consec_ >= 1 && interval_avg_ > 0.0f) {
                        const float ratio =
                            static_cast<float>(interval) / interval_avg_;
                        if (ratio < cfg::kStepIntervalTolMin ||
                            ratio > cfg::kStepIntervalTolMax) {
                            consec_ = 1;
                            interval_avg_ = static_cast<float>(interval);
                            last_step_t_ = peak_t_;
                            break;
                        }
                    }
                    interval_avg_ = consec_
                        ? (0.7f * interval_avg_ + 0.3f * interval)
                        : static_cast<float>(interval);
                    if (consec_ < 250) ++consec_;
                    if (consec_ == cfg::kStepConsecStart) {
                        // pattern established — retro-count the opening run
                        steps_ += consec_;
                        counted = true;
                    } else if (consec_ > cfg::kStepConsecStart) {
                        ++steps_;
                        counted = true;
                    }
                    last_step_t_ = peak_t_;
                } else {
                    consec_ = 1;  // cadence break — a new pattern starts
                    interval_avg_ = static_cast<float>(interval);
                    last_step_t_ = peak_t_;
                }
            }
            break;
        }
    }
    return counted;
}

void SensorTask::run() {
    LOGI(kTag, "sampling loop started");
    restoreDailySteps();
    uint32_t last_raw_publish = 0;
    bool was_awake = true;

    while (true) {
        const bool awake = core::pwr::uiAwake.load();
        const uint32_t poll_ms = awake ? cfg::kSensorPollMs
                                       : cfg::kSensorPollOffMs;
        const uint32_t now = millis();
        if (auto sample = hal_.imu.readSample()) {
            if (detectStep(sample->ax, sample->ay, sample->az, now, poll_ms) &&
                (now - last_publish_ms_) > 200) {
                // Retro-counted patterns jump by several steps — publish fast
                // so the UI feels live, but keep at most 5 Hz.
                last_publish_ms_ = now;
                core::EventBus::instance().publish(
                    core::Event::makeSteps(steps_));
            }

            // Shake-to-wake: a strong jerk wakes the screen (throttled).
            if (dev_last_ > cfg::kShakeThresholdG &&
                now - last_shake_ms_ > 2000) {
                last_shake_ms_ = now;
                core::EventBus::instance().publish(core::Event::makeWake());
            }

            // Steps at 1 Hz.
            if (now - last_publish_ms_ >= 1000) {
                last_publish_ms_ = now;
                core::EventBus::instance().publish(
                    core::Event::makeSteps(steps_));
                // Low-noise debug trail: one line per change, quiet at rest.
                static uint32_t logged = 0xFFFFFFFF;
                if (steps_ != logged) {
                    logged = steps_;
                    LOGI(kTag, "steps=%lu", static_cast<unsigned long>(steps_));
                }
            }

            // Persist the daily counter once a minute (and roll the day).
            if (now - last_save_ms_ >= 60000) {
                last_save_ms_ = now;
                if (steps_day_ != dayKey()) steps_ = 0;  // new day, fresh count
                saveDailySteps();
            }
            // Raw samples at 5 Hz for the status/fitness pages — nobody
            // renders them while the screen sleeps, so skip there.
            if (awake && now - last_raw_publish >= 200) {
                last_raw_publish = now;
                core::EventBus::instance().publish(core::Event::makeImu(
                    static_cast<int16_t>(sample->ax * 1000),
                    static_cast<int16_t>(sample->ay * 1000),
                    static_cast<int16_t>(sample->az * 1000),
                    static_cast<int16_t>(sample->gx * 1000),
                    static_cast<int16_t>(sample->gy * 1000),
                    static_cast<int16_t>(sample->gz * 1000)));
            }
        } else if (!hal_.imu.healthy()) {
            core::SystemHealth::mark(core::ModuleBit::Imu);
        }

        if (awake != was_awake) {
            was_awake = awake;
            LOGI(kTag, "screen %s — sampling at %lu Hz",
                 awake ? "awake" : "asleep",
                 static_cast<unsigned long>(1000 / poll_ms));
        }
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
    }
}

}  // namespace tasks
