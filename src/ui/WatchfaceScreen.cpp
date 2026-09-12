// ============================================================================
//  WatchfaceScreen.cpp — the primary clock face.
//
//  "Real smartwatch" look: a thin seconds ring around the screen, a huge
//  time readout, a date line and two circular complications (battery with
//  its own arc, steps) pinned to the bottom corners.
// ============================================================================
#include "ui/WatchfaceScreen.hpp"
#include "config/config.hpp"
#include "core/Logger.hpp"
#include <Arduino.h>
#include <time.h>

namespace ui {

namespace {
    constexpr const char* kWeekDays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    constexpr const char* kMonths[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    constexpr uint32_t kAccent    = 0x4FC3F7;  // seconds ring
    constexpr uint32_t kCardBg    = 0x141A21;
    constexpr uint32_t kTrack     = 0x1B242E;
    constexpr uint32_t kTextDim   = 0x8FA3B5;
}

void WatchfaceScreen::create(lv_obj_t* parent) {
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, cfg::kLcdWidth, cfg::kLcdHeight);
    lv_obj_center(root_);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // ---- seconds ring (screen-edge arc) --------------------------------------
    seconds_ring_ = lv_arc_create(root_);
    lv_obj_set_size(seconds_ring_, 386, 386);
    lv_obj_align(seconds_ring_, LV_ALIGN_TOP_MID, 0, 8);
    lv_arc_set_rotation(seconds_ring_, 135);
    lv_arc_set_bg_angles(seconds_ring_, 0, 270);
    lv_arc_set_range(seconds_ring_, 0, 59);  // full sweep at second 59
    lv_arc_set_value(seconds_ring_, 0);
    lv_obj_remove_style(seconds_ring_, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(seconds_ring_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(seconds_ring_, lv_color_hex(kTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_color(seconds_ring_, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(seconds_ring_, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(seconds_ring_, 5, LV_PART_INDICATOR);

    // ---- status icons (top, inside the ring) -----------------------------------
    // Status icons live INSIDE the seconds ring, above the clock — the ring
    // inner edge at this height leaves ~50 px of clearance around them.
    wifi_label_ = lv_label_create(root_);
    lv_obj_set_style_text_font(wifi_label_, &ui_font_ru_20, 0);
    lv_obj_set_style_text_color(wifi_label_, lv_color_hex(kTextDim), 0);
    lv_label_set_text(wifi_label_, LV_SYMBOL_WIFI);
    lv_obj_align(wifi_label_, LV_ALIGN_TOP_MID, 90, 92);

    charge_label_ = lv_label_create(root_);
    lv_obj_set_style_text_font(charge_label_, &ui_font_ru_20, 0);
    lv_obj_set_style_text_color(charge_label_, lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
    lv_label_set_text(charge_label_, LV_SYMBOL_CHARGE);
    lv_obj_align(charge_label_, LV_ALIGN_TOP_MID, -90, 92);
    lv_obj_add_flag(charge_label_, LV_OBJ_FLAG_HIDDEN);

    // ---- big clock -------------------------------------------------------------
    time_label_ = lv_label_create(root_);
    lv_obj_set_style_text_font(time_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(time_label_, lv_color_white(), 0);
    lv_label_set_text(time_label_, "--:--");
    lv_obj_align(time_label_, LV_ALIGN_TOP_MID, 0, 150);

    // ---- date pill ---------------------------------------------------------------
    date_label_ = lv_label_create(root_);
    lv_obj_set_style_text_font(date_label_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(date_label_, lv_color_hex(kTextDim), 0);
    lv_label_set_text(date_label_, "--- --- --");
    lv_obj_align(date_label_, LV_ALIGN_TOP_MID, 0, 218);

    // ---- complications (bottom-centre pair, clear of the rounded corners) -----
    battery_card_ = lv_obj_create(root_);
    lv_obj_set_size(battery_card_, 112, 112);
    lv_obj_align(battery_card_, LV_ALIGN_BOTTOM_MID, -62, -14);
    lv_obj_set_style_bg_color(battery_card_, lv_color_hex(kCardBg), 0);
    lv_obj_set_style_radius(battery_card_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(battery_card_, 0, 0);
    lv_obj_clear_flag(battery_card_,
                      static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                                                 LV_OBJ_FLAG_CLICKABLE));

    battery_arc_ = lv_arc_create(battery_card_);
    lv_obj_set_size(battery_arc_, 98, 98);
    lv_obj_center(battery_arc_);
    lv_arc_set_rotation(battery_arc_, 135);
    lv_arc_set_bg_angles(battery_arc_, 0, 270);
    lv_arc_set_value(battery_arc_, 0);
    lv_obj_remove_style(battery_arc_, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(battery_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(battery_arc_, lv_color_hex(kTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_color(battery_arc_, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(battery_arc_, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(battery_arc_, 8, LV_PART_INDICATOR);

    battery_label_ = lv_label_create(battery_arc_);
    lv_obj_set_style_text_font(battery_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(battery_label_, lv_color_white(), 0);
    lv_label_set_text(battery_label_, "--%");
    lv_obj_center(battery_label_);

    // ---- steps complication (progress arc, number centred — no overlap) --------
    steps_card_ = lv_obj_create(root_);
    lv_obj_set_size(steps_card_, 112, 112);
    lv_obj_align(steps_card_, LV_ALIGN_BOTTOM_MID, 62, -14);
    lv_obj_set_style_bg_color(steps_card_, lv_color_hex(kCardBg), 0);
    lv_obj_set_style_radius(steps_card_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(steps_card_, 0, 0);
    lv_obj_clear_flag(steps_card_,
                      static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                                                 LV_OBJ_FLAG_CLICKABLE));

    steps_arc_ = lv_arc_create(steps_card_);
    lv_obj_set_size(steps_arc_, 98, 98);
    lv_obj_center(steps_arc_);
    lv_arc_set_rotation(steps_arc_, 135);
    lv_arc_set_bg_angles(steps_arc_, 0, 270);
    lv_arc_set_value(steps_arc_, 0);
    lv_obj_remove_style(steps_arc_, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(steps_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(steps_arc_, lv_color_hex(kTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_color(steps_arc_, lv_color_hex(0x7ED957), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(steps_arc_, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(steps_arc_, 8, LV_PART_INDICATOR);

    steps_label_ = lv_label_create(steps_arc_);
    lv_obj_set_style_text_font(steps_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(steps_label_, lv_color_white(), 0);
    lv_label_set_text(steps_label_, "0");
    lv_obj_align(steps_label_, LV_ALIGN_CENTER, 0, -12);

    lv_obj_t* steps_cap = lv_label_create(steps_arc_);
    lv_obj_set_style_text_font(steps_cap, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(steps_cap, lv_color_hex(kTextDim), 0);
    lv_label_set_text(steps_cap, "шагов");
    lv_obj_align(steps_cap, LV_ALIGN_CENTER, 0, 12);
}

void WatchfaceScreen::refresh(const UiModel& m) {
    // ---- clock from the system time (RTC-seeded, SNTP-corrected) ----------
    struct tm t{};
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    char buf[48];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(time_label_, buf);
    lv_arc_set_value(seconds_ring_, t.tm_sec % 61);

    snprintf(buf, sizeof(buf), "%s %s %d", kWeekDays[t.tm_wday % 7],
             kMonths[t.tm_mon % 12], t.tm_mday);
    lv_label_set_text(date_label_, buf);

    // ---- battery -----------------------------------------------------------
    if (m.battery_present) {
        lv_arc_set_value(battery_arc_, m.battery_percent);
        snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(m.battery_percent));
        lv_obj_set_style_arc_color(
            battery_arc_,
            m.battery_percent < 15 ? lv_palette_main(LV_PALETTE_RED)
                                   : lv_palette_main(LV_PALETTE_BLUE),
            LV_PART_INDICATOR);
    } else {
        lv_arc_set_value(battery_arc_, 0);
        snprintf(buf, sizeof(buf), "—");
    }
    lv_label_set_text(battery_label_, buf);

    lv_obj_add_flag(charge_label_, LV_OBJ_FLAG_HIDDEN);
    if (m.charging) lv_obj_remove_flag(charge_label_, LV_OBJ_FLAG_HIDDEN);

    snprintf(buf, sizeof(buf), "%lu",
             static_cast<unsigned long>(m.steps));
    lv_label_set_text(steps_label_, buf);
    const int32_t goalPct = static_cast<int32_t>(
        (m.steps * 100UL) / cfg::kFitnessGoalSteps);
    lv_arc_set_value(steps_arc_, goalPct > 100 ? 100 : goalPct);

    lv_obj_set_style_text_color(
        wifi_label_,
        m.wifi_connected ? lv_palette_main(LV_PALETTE_LIGHT_GREEN)
                         : lv_color_hex(kTextDim), 0);
}

}  // namespace ui
