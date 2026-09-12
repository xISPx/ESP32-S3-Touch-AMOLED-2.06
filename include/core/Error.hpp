// ============================================================================
//  Error.hpp — error taxonomy, Result<T> carrier and system health registry.
//
//  Every module reports failures through ErrorCode; the health registry keeps
//  an atomic bitmask of permanently-degraded modules so the UI and the
//  watchdog can expose degraded mode instead of crashing silently.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>

namespace core {

enum class ErrorCode : uint16_t {
    None = 0,
    DisplayInit,     // CO5300 / QSPI bring-up failed
    LvglAlloc,       // PSRAM buffer allocation failed
    TouchInit,       // FT3168 not answering on the bus
    PmuInit,         // AXP2101 not answering on the bus
    RtcInit,         // PCF85063 not answering on the bus
    ImuInit,         // QMI8658 not answering on the bus
    ExpanderInit,    // XL9555 not answering on the bus (PWR button unavailable)
    AudioInit,       // ES8311 / I2S bring-up failed
    I2cBusInit,
    WifiConnect,
    SntpTimeout,
    EventQueueFull,  // subscriber queue overflow — event dropped
    TaskCreateFailed,
    InvalidState,    // generic guard against illegal module state
};

constexpr std::string_view toString(ErrorCode e) {
    switch (e) {
        case ErrorCode::None:             return "None";
        case ErrorCode::DisplayInit:      return "DisplayInit";
        case ErrorCode::LvglAlloc:        return "LvglAlloc";
        case ErrorCode::TouchInit:        return "TouchInit";
        case ErrorCode::PmuInit:          return "PmuInit";
        case ErrorCode::RtcInit:          return "RtcInit";
        case ErrorCode::ImuInit:          return "ImuInit";
        case ErrorCode::ExpanderInit:     return "ExpanderInit";
        case ErrorCode::AudioInit:        return "AudioInit";
        case ErrorCode::I2cBusInit:       return "I2cBusInit";
        case ErrorCode::WifiConnect:      return "WifiConnect";
        case ErrorCode::SntpTimeout:      return "SntpTimeout";
        case ErrorCode::EventQueueFull:   return "EventQueueFull";
        case ErrorCode::TaskCreateFailed: return "TaskCreateFailed";
        case ErrorCode::InvalidState:     return "InvalidState";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
//  Result<T> — RAII-friendly error carrier (no exceptions on device).
// ---------------------------------------------------------------------------
template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value), ErrorCode::None); }
    static Result fail(ErrorCode code) { return Result(std::nullopt, code); }

    explicit operator bool() const { return value_.has_value(); }
    ErrorCode error() const { return error_; }
    const T& operator*() const { return *value_; }
    const T* operator->() const { return &*value_; }

private:
    Result(std::optional<T> v, ErrorCode e) : value_(std::move(v)), error_(e) {}
    std::optional<T> value_;
    ErrorCode error_;
};

// Void flavour for operations with no payload.
struct Unit {};
using Status = Result<Unit>;

// ---------------------------------------------------------------------------
//  SystemHealth — bitmask of failed modules (thread-safe, lock-free).
// ---------------------------------------------------------------------------
enum class ModuleBit : uint16_t {
    Display = 1u << 0,
    Touch   = 1u << 1,
    Power   = 1u << 2,
    Rtc     = 1u << 3,
    Imu     = 1u << 4,
    Expander= 1u << 5,
    Network = 1u << 6,
    Audio   = 1u << 7,
};

class SystemHealth {
public:
    static void mark(ModuleBit bit) { state_.fetch_or(static_cast<uint16_t>(bit)); }
    static void clear(ModuleBit bit) { state_.fetch_and(~static_cast<uint16_t>(bit)); }
    static bool isDegraded(ModuleBit bit) {
        return (state_.load() & static_cast<uint16_t>(bit)) != 0;
    }
    static uint16_t raw() { return state_.load(); }

private:
    static inline std::atomic<uint16_t> state_{0};
};

}  // namespace core
