// ============================================================================
//  StatusScreen.cpp — diagnostics page: battery detail, IMU, system, network.
// ============================================================================
#include "ui/StatusScreen.hpp"
#include "config/config.hpp"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <time.h>

namespace ui {

namespace {
    constexpr uint32_t kCardBg = 0x141A21;

    // A rounded card row: colored icon on the left, wrapped value text right.
    lv_obj_t* makeCard(lv_obj_t* content, lv_obj_t*& valueLabel,
                       const char* icon, uint32_t color) {
        lv_obj_t* card = lv_obj_create(content);
        lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, lv_color_hex(kCardBg), 0);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(card, 12, 0);
        lv_obj_clear_flag(card, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

        lv_obj_t* ic = lv_label_create(card);
        lv_obj_set_style_text_font(ic, &ui_font_ru_20, 0);
        lv_obj_set_style_text_color(ic, lv_color_hex(color), 0);
        lv_label_set_text(ic, icon);

        valueLabel = lv_label_create(card);
        lv_obj_set_style_text_font(valueLabel, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(valueLabel,
                                    lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
        lv_label_set_text(valueLabel, "-");
        lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(valueLabel, cfg::kLcdWidth - 120);
        return card;
    }
}

void StatusScreen::create(AppHost* host) {
    App::create(host);

    lv_obj_t* content = createChrome(title());
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 12, 0);

    makeCard(content, battery_label_, LV_SYMBOL_BATTERY_FULL, 0x64B5F6);
    makeCard(content, imu_label_, LV_SYMBOL_SHUFFLE, 0x7ED957);
    makeCard(content, net_label_, LV_SYMBOL_WIFI, 0x4FC3F7);
    makeCard(content, sys_label_, LV_SYMBOL_LIST, 0xB0BEC5);
}

void StatusScreen::onModel(const UiModel& m) {
    if (lv_screen_active() != root_) return;  // heavy text rebuilds: active only
    refresh(m);
}

void StatusScreen::refresh(const UiModel& m) {
    char buf[160];

    snprintf(buf, sizeof(buf), "Battery: %s %u%%  %lu mV  temp %d C  vbus %lu mV",
             m.battery_present ? "present" : "absent",
             static_cast<unsigned>(m.battery_percent),
             static_cast<unsigned long>(m.battery_mv),
             static_cast<int>(m.temperature_c),
             static_cast<unsigned long>(m.vbus_mv));
    lv_label_set_text(battery_label_, buf);

    snprintf(buf, sizeof(buf),
             "Accel [mg]: %d %d %d\nGyro  [mdps]: %d %d %d\nSteps: %lu",
             m.ax_mg, m.ay_mg, m.az_mg, m.gx_md, m.gy_md, m.gz_md,
             static_cast<unsigned long>(m.steps));
    lv_label_set_text(imu_label_, buf);

    snprintf(buf, sizeof(buf), "Net: %s  rssi %d dBm\nTime sync: %s",
             m.wifi_connected ? "Wi-Fi" : "offline",
             static_cast<int>(m.rssi),
             m.time_synced ? "SNTP ok" : "RTC only");
    lv_label_set_text(net_label_, buf);

    const uint32_t free_heap = esp_get_free_heap_size();
    const uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    struct tm t{};
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    snprintf(buf, sizeof(buf), "Heap free: %lu KB\nPSRAM free: %lu KB\nUptime: %lu s",
             static_cast<unsigned long>(free_heap / 1024),
             static_cast<unsigned long>(free_psram / 1024),
             static_cast<unsigned long>(millis() / 1000));
    lv_label_set_text(sys_label_, buf);
}

}  // namespace ui
