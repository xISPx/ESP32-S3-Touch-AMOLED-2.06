// ============================================================================
//  FitnessScreen.cpp — activity data from SensorTask's UiModel snapshot.
// ============================================================================
#include "ui/FitnessScreen.hpp"
#include "config/config.hpp"
#include <Arduino.h>

namespace ui {

void FitnessScreen::create(AppHost* host) {
    App::create(host);

    lv_obj_t* content = createChrome(title());

    // ---- step ring (left) ----------------------------------------------------
    ring_ = lv_arc_create(content);
    lv_obj_set_size(ring_, 190, 190);
    lv_arc_set_rotation(ring_, 135);
    lv_arc_set_bg_angles(ring_, 0, 270);
    lv_arc_set_value(ring_, 0);
    lv_obj_remove_style(ring_, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(ring_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ring_, LV_ALIGN_TOP_MID, -80, 10);
    lv_obj_set_style_arc_color(ring_, lv_color_hex(accent()), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring_, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring_, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring_, lv_color_hex(0x1B242E), LV_PART_MAIN);

    steps_label_ = lv_label_create(ring_);
    lv_obj_set_style_text_font(steps_label_, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(steps_label_, lv_color_white(), 0);
    lv_label_set_text(steps_label_, "0");
    lv_obj_center(steps_label_);

    // ---- distance / calories stat cards (right column) -----------------------
    lv_obj_t* distCard = lv_obj_create(content);
    lv_obj_set_size(distCard, 128, 84);
    lv_obj_align(distCard, LV_ALIGN_TOP_RIGHT, 0, 18);
    lv_obj_set_style_bg_color(distCard, lv_color_hex(0x141A21), 0);
    lv_obj_set_style_radius(distCard, 18, 0);
    lv_obj_set_style_border_width(distCard, 0, 0);
    lv_obj_clear_flag(distCard, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    lv_obj_t* distIcon = lv_label_create(distCard);
    lv_obj_set_style_text_font(distIcon, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(distIcon, lv_color_hex(0x4FC3F7), 0);
    lv_label_set_text(distIcon, LV_SYMBOL_GPS);
    lv_obj_align(distIcon, LV_ALIGN_TOP_LEFT, 12, 10);

    distance_label_ = lv_label_create(distCard);
    lv_obj_set_style_text_font(distance_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(distance_label_, lv_color_white(), 0);
    lv_obj_align(distance_label_, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_obj_t* kcalCard = lv_obj_create(content);
    lv_obj_set_size(kcalCard, 128, 84);
    lv_obj_align(kcalCard, LV_ALIGN_TOP_RIGHT, 0, 112);
    lv_obj_set_style_bg_color(kcalCard, lv_color_hex(0x141A21), 0);
    lv_obj_set_style_radius(kcalCard, 18, 0);
    lv_obj_set_style_border_width(kcalCard, 0, 0);
    lv_obj_clear_flag(kcalCard, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    lv_obj_t* kcalIcon = lv_label_create(kcalCard);
    lv_obj_set_style_text_font(kcalIcon, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(kcalIcon, lv_color_hex(0xFFB74D), 0);
    lv_label_set_text(kcalIcon, LV_SYMBOL_CHARGE);
    lv_obj_align(kcalIcon, LV_ALIGN_TOP_LEFT, 12, 10);

    calories_label_ = lv_label_create(kcalCard);
    lv_obj_set_style_text_font(calories_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(calories_label_, lv_color_white(), 0);
    lv_obj_align(calories_label_, LV_ALIGN_BOTTOM_MID, 0, -12);

    // ---- live accel ------------------------------------------------------------
    accel_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(accel_label_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(accel_label_, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(accel_label_, "0.00 g");
    lv_obj_align(accel_label_, LV_ALIGN_TOP_MID, 0, 215);

    // ---- accel magnitude trace (last ~12 s at 5 Hz) ----------------------------
    chart_ = lv_chart_create(content);
    lv_obj_set_size(chart_, cfg::kLcdWidth - 52, 170);
    lv_obj_align(chart_, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_, 60);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, 2000);  // mg
    lv_chart_set_update_mode(chart_, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(chart_, lv_color_hex(0x0D1116), 0);
    lv_obj_set_style_border_width(chart_, 0, 0);
    lv_obj_set_style_radius(chart_, 14, 0);
    lv_chart_set_div_line_count(chart_, 3, 4);
    lv_obj_set_style_line_color(chart_, lv_color_hex(0x233240), LV_PART_ITEMS);

    lv_chart_series_t* s = lv_chart_add_series(
        chart_, lv_color_hex(accent()), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_user_data(chart_, s);
    lv_obj_set_style_line_width(chart_, 2, LV_PART_ITEMS);
    lv_obj_set_scrollbar_mode(chart_, LV_SCROLLBAR_MODE_OFF);
}

void FitnessScreen::onModel(const UiModel& m) {
    if (lv_screen_active() != root_) return;

    refreshActivity(m);
    pushAccelSample(m);
}

void FitnessScreen::refreshActivity(const UiModel& m) {
    static uint32_t last_steps = 0xFFFFFFFF;
    if (m.steps != last_steps) {
        last_steps = m.steps;
        lv_label_set_text_fmt(steps_label_, "%lu",
                              static_cast<unsigned long>(m.steps));
        const int32_t pct = static_cast<int32_t>(
            (m.steps * 100UL) / cfg::kFitnessGoalSteps);
        lv_arc_set_value(ring_, static_cast<int32_t>(pct > 100 ? 100 : pct));

        const float km = m.steps * 0.00075f;  // ~0.75 m per step
        lv_label_set_text_fmt(distance_label_, "%.2f км",
                              static_cast<double>(km));
        const float kcal = m.steps * 0.04f;
        lv_label_set_text_fmt(calories_label_, "%.0f ккал",
                              static_cast<double>(kcal));
    }
}

void FitnessScreen::pushAccelSample(const UiModel& m) {
    const uint32_t now = millis();
    if (now - last_push_ms_ < 200) return;  // matches the 5 Hz sample stream
    last_push_ms_ = now;

    const int32_t mag = (abs(m.ax_mg) + abs(m.ay_mg) + abs(m.az_mg));
    lv_chart_series_t* s =
        static_cast<lv_chart_series_t*>(lv_obj_get_user_data(chart_));
    if (!s) return;
    lv_chart_set_next_value(chart_, s, mag);
    lv_label_set_text_fmt(accel_label_, "|a| = %.2f g",
                          static_cast<double>(mag) / 1000.0);
}

}  // namespace ui
