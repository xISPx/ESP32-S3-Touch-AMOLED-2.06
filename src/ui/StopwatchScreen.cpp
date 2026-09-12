// ============================================================================
//  StopwatchScreen.cpp — 20 Hz label refresh only while running.
// ============================================================================
#include "ui/StopwatchScreen.hpp"
#include "config/config.hpp"

namespace ui {

namespace {
    void start_stop_cb(lv_event_t* e) {
        static_cast<StopwatchScreen*>(lv_event_get_user_data(e))->startStop();
    }
    void reset_cb(lv_event_t* e) {
        static_cast<StopwatchScreen*>(lv_event_get_user_data(e))->reset();
    }
}

void StopwatchScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<StopwatchScreen*>(lv_timer_get_user_data(timer));
    if (self && self->running_) self->updateLabel();
}

void StopwatchScreen::updateLabel() {
    const uint32_t total = elapsed_ms_ +
        (running_ ? (millis() - seg_start_ms_) : 0);
    const uint32_t cs = (total / 10) % 100;
    const uint32_t s = (total / 1000) % 60;
    const uint32_t m = total / 60000;
    lv_label_set_text_fmt(time_label_, "%02lu:%02lu.%02lu",
                          static_cast<unsigned long>(m),
                          static_cast<unsigned long>(s),
                          static_cast<unsigned long>(cs));
}

void StopwatchScreen::startStop() {
    if (running_) {
        elapsed_ms_ += millis() - seg_start_ms_;
        running_ = false;
        lv_label_set_text(start_btn_label_, "Старт");
    } else {
        seg_start_ms_ = millis();
        running_ = true;
        lv_label_set_text(start_btn_label_, "Пауза");
    }
    updateLabel();
}

void StopwatchScreen::reset() {
    running_ = false;
    elapsed_ms_ = 0;
    lv_label_set_text(start_btn_label_, "Старт");
    updateLabel();
}

void StopwatchScreen::create(AppHost* host) {
    App::create(host);

    lv_obj_t* content = createChrome(title());

    time_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(time_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(time_label_, lv_color_white(), 0);
    lv_label_set_text(time_label_, "00:00.00");
    lv_obj_align(time_label_, LV_ALIGN_TOP_MID, 0, 120);

    // ---- controls --------------------------------------------------------------
    lv_obj_t* start_btn = lv_btn_create(content);
    lv_obj_set_size(start_btn, 150, 70);
    lv_obj_align(start_btn, LV_ALIGN_TOP_MID, -85, 240);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x1B5E20), 0);
    lv_obj_set_style_radius(start_btn, 20, 0);
    lv_obj_add_event_cb(start_btn, start_stop_cb, LV_EVENT_CLICKED, this);

    start_btn_label_ = lv_label_create(start_btn);
    lv_obj_set_style_text_font(start_btn_label_, &ui_font_ru_20, 0);
    lv_label_set_text(start_btn_label_, "Старт");
    lv_obj_center(start_btn_label_);

    reset_btn_ = lv_btn_create(content);
    lv_obj_set_size(reset_btn_, 150, 70);
    lv_obj_align(reset_btn_, LV_ALIGN_TOP_MID, 85, 240);
    lv_obj_set_style_bg_color(reset_btn_, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_radius(reset_btn_, 20, 0);
    lv_obj_add_event_cb(reset_btn_, reset_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* reset_label = lv_label_create(reset_btn_);
    lv_obj_set_style_text_font(reset_label, &ui_font_ru_20, 0);
    lv_label_set_text(reset_label, "Сброс");
    lv_obj_center(reset_label);

    tick_ = lv_timer_create(tick_cb, 50, this);
}

}  // namespace ui
