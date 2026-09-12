// ============================================================================
//  RtcHal.cpp — PCF85063 <-> system clock bridge.
// ============================================================================
#include "hal/RtcHal.hpp"
#include "config/config.hpp"
#include "core/I2cBus.hpp"
#include "core/Logger.hpp"
#include <ctime>

namespace hal {

namespace {
    constexpr const char* kTag = "Rtc";
    constexpr uint16_t kMinPlausibleYear = 2020;
}

core::Status RtcHal::init() {
    {
        core::I2cBus::Guard guard(core::I2cBus::instance());
        if (!rtc_.begin(core::I2cBus::instance().wire(), cfg::kIicSda, cfg::kIicScl)) {
            LOGE(kTag, "PCF85063 not found at 0x%02X", cfg::kAddrRtcPcf85063);
            return core::Status::fail(core::ErrorCode::RtcInit);
        }
    }
    valid_ = true;
    LOGI(kTag, "PCF85063 ready");
    return core::Status::ok(core::Unit{});
}

bool RtcHal::syncSystemClockFromRtc() {
    if (!valid_) return false;

    RTC_DateTime dt;
    {
        core::I2cBus::Guard guard(core::I2cBus::instance());
        dt = rtc_.getDateTime();
    }
    if (dt.getYear() < kMinPlausibleYear) {
        LOGW(kTag, "RTC holds implausible date %u — ignoring",
             static_cast<unsigned>(dt.getYear()));
        return false;
    }

    struct tm t{};
    t.tm_year = static_cast<int>(dt.getYear()) - 1900;
    t.tm_mon = static_cast<int>(dt.getMonth()) - 1;
    t.tm_mday = static_cast<int>(dt.getDay());
    t.tm_hour = static_cast<int>(dt.getHour());
    t.tm_min = static_cast<int>(dt.getMinute());
    t.tm_sec = static_cast<int>(dt.getSecond());
    t.tm_isdst = -1;
    const time_t epoch = mktime(&t);
    if (epoch < 0) return false;

    struct timeval tv{ .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    LOGI(kTag, "System clock seeded from RTC: %04u-%02u-%02u %02u:%02u:%02u",
         static_cast<unsigned>(dt.getYear()), static_cast<unsigned>(dt.getMonth()),
         static_cast<unsigned>(dt.getDay()), static_cast<unsigned>(dt.getHour()),
         static_cast<unsigned>(dt.getMinute()), static_cast<unsigned>(dt.getSecond()));
    return true;
}

void RtcHal::storeSystemClockToRtc() {
    if (!valid_) return;

    struct tm t{};
    time_t now = time(nullptr);
    localtime_r(&now, &t);

    RTC_DateTime dt(static_cast<uint16_t>(t.tm_year + 1900),
                    static_cast<uint8_t>(t.tm_mon + 1),
                    static_cast<uint8_t>(t.tm_mday),
                    static_cast<uint8_t>(t.tm_hour),
                    static_cast<uint8_t>(t.tm_min),
                    static_cast<uint8_t>(t.tm_sec),
                    static_cast<uint8_t>(t.tm_wday));
    core::I2cBus::Guard guard(core::I2cBus::instance());
    rtc_.setDateTime(dt);
    LOGI(kTag, "RTC updated from system clock");
}

}  // namespace hal
