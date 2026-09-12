// ============================================================================
//  StopwatchScreen.hpp — centiseconds stopwatch (runs while screen hidden).
// ============================================================================
#pragma once

#include <cstdint>
#include <Arduino.h>
#include "ui/App.hpp"

namespace ui {

class StopwatchScreen final : public App {
public:
    void create(AppHost* host) override;
    void onModel(const UiModel& m) override { LV_UNUSED(m); }
    void show() override { updateLabel(); }

    const char* title() const override { return "Секундомер"; }
    const char* icon() const override { return "\xEF\x8B\xA2"; }  // FA stopwatch
    uint32_t accent() const override { return 0xFFB74D; }

    // tick cb (anonymous-namespace trampolines) calls these
    void startStop();
    void reset();

private:
    static void tick_cb(lv_timer_t* timer);
    void updateLabel();

    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* start_btn_label_ = nullptr;
    lv_obj_t* reset_btn_ = nullptr;
    lv_timer_t* tick_ = nullptr;

    bool running_ = false;
    uint32_t elapsed_ms_ = 0;   // accumulated finished segments
    uint32_t seg_start_ms_ = 0; // millis() when the current segment began
};

}  // namespace ui
