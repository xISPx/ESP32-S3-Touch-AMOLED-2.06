// ============================================================================
//  ImuHal.cpp — QMI8658 init, sampling and self-recovery.
// ============================================================================
#include "hal/ImuHal.hpp"
#include "config/config.hpp"
#include "core/I2cBus.hpp"
#include "core/Logger.hpp"
#include <cmath>

namespace hal {

namespace {
    constexpr const char* kTag = "Imu";
}

core::Status ImuHal::init() {
    {
        core::I2cBus::Guard guard(core::I2cBus::instance());
        if (!imu_.begin(core::I2cBus::instance().wire(), cfg::kAddrImuQmi8658,
                        cfg::kIicSda, cfg::kIicScl)) {
            LOGE(kTag, "QMI8658 not found at 0x%02X", cfg::kAddrImuQmi8658);
            return core::Status::fail(core::ErrorCode::ImuInit);
        }
        imu_.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                 SensorQMI8658::ACC_ODR_250Hz,
                                 SensorQMI8658::LPF_MODE_0);
        imu_.enableAccelerometer();
        imu_.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                             SensorQMI8658::GYR_ODR_224_2Hz,
                             SensorQMI8658::LPF_MODE_0);
        imu_.enableGyroscope();
    }
    LOGI(kTag, "QMI8658 ready (4G / 512dps @ 250Hz)");
    return core::Status::ok(core::Unit{});
}

bool ImuHal::reinit() {
    LOGW(kTag, "attempting QMI8658 recovery");
    {
        core::I2cBus::Guard guard(core::I2cBus::instance());
        imu_.disableAccelerometer();
        imu_.disableGyroscope();
        if (!imu_.begin(core::I2cBus::instance().wire(), cfg::kAddrImuQmi8658,
                        cfg::kIicSda, cfg::kIicScl)) {
            return false;
        }
        imu_.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                 SensorQMI8658::ACC_ODR_250Hz,
                                 SensorQMI8658::LPF_MODE_0);
        imu_.enableAccelerometer();
        imu_.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                             SensorQMI8658::GYR_ODR_224_2Hz,
                             SensorQMI8658::LPF_MODE_0);
        imu_.enableGyroscope();
    }
    LOGI(kTag, "QMI8658 recovery ok");
    return true;
}

std::optional<ImuSample> ImuHal::readSample() {
    bool data_ready = false;
    ImuSample s;
    {
        core::I2cBus::Guard guard(core::I2cBus::instance());
        data_ready = imu_.getDataReady();
        if (data_ready) {
            if (!imu_.getAccelerometer(s.ax, s.ay, s.az) ||
                !imu_.getGyroscope(s.gx, s.gy, s.gz)) {
                data_ready = false;
            }
        }
    }
    s.timestamp_ms = millis();

    if (!data_ready) {
        if (++consecutive_errors_ == kMaxConsecutiveErrors) {
            if (!reinit()) {
                LOGE(kTag, "QMI8658 recovery failed — marking degraded");
                consecutive_errors_ = kMaxConsecutiveErrors + 1;  // stop retrying
            }
        }
        return std::nullopt;
    }
    consecutive_errors_ = 0;
    return s;
}

}  // namespace hal
