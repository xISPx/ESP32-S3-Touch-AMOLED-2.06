// ============================================================================
//  Logger.hpp — lightweight, levelled, mutex-protected logger over USB CDC.
//
//  Safe to call from any FreeRTOS task: a static mutex serialises access to
//  the serial port (HWCDC is not documented as thread-safe).
// ============================================================================
#pragma once

#include <cstdint>

namespace core {

enum class LogLevel : uint8_t { Error = 0, Warn = 1, Info = 2, Debug = 3 };

class Logger {
public:
    Logger() = delete;  // namespace-like utility

    static void init(LogLevel maxLevel = LogLevel::Info);

    static void log(LogLevel level, const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    // Rate-limited variant: the same message (by tag) is printed at most once
    // per period_ms.  Useful for I2C retry loops that would otherwise flood.
    static void throttled(LogLevel level, const char* tag, uint32_t period_ms,
                          const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));

private:
    static void emit(LogLevel level, const char* tag, const char* body);
};

}  // namespace core

#define LOGE(tag, ...) ::core::Logger::log(::core::LogLevel::Error, tag, __VA_ARGS__)
#define LOGW(tag, ...) ::core::Logger::log(::core::LogLevel::Warn,  tag, __VA_ARGS__)
#define LOGI(tag, ...) ::core::Logger::log(::core::LogLevel::Info,  tag, __VA_ARGS__)
#define LOGD(tag, ...) ::core::Logger::log(::core::LogLevel::Debug, tag, __VA_ARGS__)
