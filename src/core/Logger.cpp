// ============================================================================
//  Logger.cpp
// ============================================================================
#include "core/Logger.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace core {

namespace {
    SemaphoreHandle_t g_mutex = nullptr;
    LogLevel g_max_level = LogLevel::Info;

    const char* levelTag(LogLevel l) {
        switch (l) {
            case LogLevel::Error: return "E";
            case LogLevel::Warn:  return "W";
            case LogLevel::Info:  return "I";
            case LogLevel::Debug: return "D";
        }
        return "?";
    }
}  // namespace

void Logger::init(LogLevel maxLevel) {
    g_max_level = maxLevel;
    if (!g_mutex) {
        g_mutex = xSemaphoreCreateMutex();
    }
}

void Logger::log(LogLevel level, const char* tag, const char* fmt, ...) {
    if (static_cast<uint8_t>(level) > static_cast<uint8_t>(g_max_level)) return;

    char body[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    emit(level, tag, body);
}

void Logger::throttled(LogLevel level, const char* tag, uint32_t period_ms,
                       const char* fmt, ...) {
    if (static_cast<uint8_t>(level) > static_cast<uint8_t>(g_max_level)) return;

    // Simple per-tag rate limit table (few call sites — linear scan is fine).
    struct ThrottleEntry { const char* tag; uint32_t last_ms; };
    static ThrottleEntry entries[8] = {};
    static SemaphoreHandle_t t_mutex = nullptr;
    if (!t_mutex) t_mutex = xSemaphoreCreateMutex();
    if (!t_mutex) return;

    uint32_t now = millis();
    xSemaphoreTake(t_mutex, portMAX_DELAY);
    ThrottleEntry* slot = nullptr;
    for (auto& e : entries) {
        if (!e.tag || strcmp(e.tag, tag) == 0) { slot = &e; break; }
    }
    if (!slot) {  // table full
        xSemaphoreGive(t_mutex);
        return;
    }
    if (slot->tag && (now - slot->last_ms) < period_ms) {
        xSemaphoreGive(t_mutex);
        return;
    }
    slot->tag = tag;
    slot->last_ms = now;
    xSemaphoreGive(t_mutex);

    char body[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    emit(level, tag, body);
}

void Logger::emit(LogLevel level, const char* tag, const char* body) {
    if (!g_mutex) {  // logging before init — still print, best effort
        Serial.printf("[%s][%s] %s\n", levelTag(level), tag, body);
        return;
    }
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    Serial.printf("[%lu][%s][%s] %s\n",
                  static_cast<unsigned long>(millis()), levelTag(level), tag, body);
    Serial.flush();
    xSemaphoreGive(g_mutex);
}

}  // namespace core
