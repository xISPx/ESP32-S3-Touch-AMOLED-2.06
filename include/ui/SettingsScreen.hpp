// ============================================================================
//  SettingsScreen.hpp — on-device settings: Wi-Fi (+ auto-scan), display,
//  timezone, AI.
//
//  One LVGL screen with a page stack: a list page + one page per section.
//  Navigation is a full-width bottom bar ("‹ К списку / К меню" + contextual
//  Save) — a large corner-safe target that replaces the small chrome arrow
//  (disabled for this screen).  The keyboard is a floating rounded card
//  inset from the rounded screen edges; tapping any non-field area closes
//  it.  Every commit writes NVS (core::Settings) and publishes
//  SettingsChanged so NetTask / AiTask / UiTask pick up new values live.
// ============================================================================
#pragma once

#include <vector>
#include <Arduino.h>  // String
#include "ui/App.hpp"

namespace ui {

class SettingsScreen final : public App {
public:
    void create(AppHost* host) override;
    void show() override;
    void hide() override;
    void onBack() override;

    const char* title() const override { return "Настройки"; }
    const char* icon() const override { return LV_SYMBOL_SETTINGS; }
    uint32_t accent() const override { return 0x90A4AE; }

    // used by the file-local makeTextarea() helper
    static void ta_focused_cb(lv_event_t* e);

private:
    struct NetRow {        // local copy of scan results (WiFi scan gets deleted)
        String ssid;
        int32_t rssi;
        bool secure;
    };

    // page management
    lv_obj_t* makePage(const char* pageTitle);
    // Heavy pages are created lazily on first open: 11 apps share the fixed
    // 80 KB LVGL pool, so the settings screen must not hold every page's
    // widgets alive from boot.  They are destroyed again when the list is
    // shown or the app is left — at most one page + keyboard stay alive.
    void ensurePage(uint8_t idx);   // 0..3 -> buildXxxPage()
    void ensureAbout();
    void ensureKb();
    void destroyPages();
    void showPage(lv_obj_t* page, const char* pageTitle);
    void showList();
    void hideKb();
    void commit(uint8_t mask);
    void updateNavBar();
    void revealField(lv_obj_t* ta);   // lift the page above the keyboard

    // bottom navigation bar (replaces the small chrome arrow)
    void buildBottomBar();
    static void bar_back_cb(lv_event_t* e);
    static void bar_save_cb(lv_event_t* e);
    void saveCurrent();

    // Wi-Fi scan
    void startScan();
    void pollScan();
    void fillNetworks();
    static void scan_cb(lv_event_t* e);
    static void net_cb(lv_event_t* e);
    static void wifi_timer_cb(lv_timer_t* timer);

    // builders
    void buildList(lv_obj_t* content);
    void buildWifiPage();
    void buildDisplayPage();
    void buildTimePage();
    void buildAiPage();
    void buildAboutPage();
    void refreshWifiRow();
    void refreshWifiStatus();
    void refreshAiStatus();
    void refreshAbout();

    // static callbacks
    static void row_cb(lv_event_t* e);
    static void page_click_cb(lv_event_t* e);
    static void brightness_cb(lv_event_t* e);
    static void dim_dd_cb(lv_event_t* e);
    static void tz_dd_cb(lv_event_t* e);
    static void backend_dd_cb(lv_event_t* e);
    static void kb_done_cb(lv_event_t* e);

    lv_obj_t* list_ = nullptr;
    lv_obj_t* pages_[4] = {};       // wifi / display / time / ai
    lv_obj_t* active_page_ = nullptr;
    lv_obj_t* about_page_ = nullptr;
    lv_obj_t* kb_ = nullptr;

    // bottom navigation bar
    lv_obj_t* bar_ = nullptr;
    lv_obj_t* bar_back_ = nullptr;
    lv_obj_t* bar_back_label_ = nullptr;
    lv_obj_t* bar_save_ = nullptr;
    lv_obj_t* bar_save_label_ = nullptr;

    // wifi
    lv_obj_t* wifi_row_sub_ = nullptr;
    lv_obj_t* wifi_status_ = nullptr;
    lv_obj_t* ssid_ta_ = nullptr;
    lv_obj_t* pass_ta_ = nullptr;
    lv_obj_t* net_list_ = nullptr;
    lv_obj_t* scan_status_ = nullptr;
    std::vector<NetRow> nets_;   // snapshot used by net_cb after scanDelete()
    bool scan_pending_ = false;
    int32_t page_shift_ = 0;          // current lift applied for the keyboard
    lv_timer_t* wifi_timer_ = nullptr;
    // display
    lv_obj_t* bright_slider_ = nullptr;
    lv_obj_t* bright_val_ = nullptr;
    lv_obj_t* dim_dd_ = nullptr;
    // time
    lv_obj_t* tz_dd_ = nullptr;
    // ai
    lv_obj_t* backend_dd_ = nullptr;
    lv_obj_t* xz_group_ = nullptr;
    lv_obj_t* oa_group_ = nullptr;
    lv_obj_t* xz_url_ta_ = nullptr;
    lv_obj_t* xz_token_ta_ = nullptr;
    lv_obj_t* oa_url_ta_ = nullptr;
    lv_obj_t* oa_key_ta_ = nullptr;
    lv_obj_t* oa_model_ta_ = nullptr;
    lv_obj_t* ai_status_ = nullptr;
    lv_obj_t* about_label_ = nullptr;
};

}  // namespace ui
