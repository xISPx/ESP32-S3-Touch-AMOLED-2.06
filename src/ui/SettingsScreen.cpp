// ============================================================================
//  SettingsScreen.cpp — settings UI.  Commits write NVS + publish events.
//
//  Navigation: a full-width bottom bar with a large "‹ Назад" button plus a
//  contextual Save pill — a much easier target than the small chrome arrow
//  (which is disabled for this screen).  The keyboard is a floating rounded
//  card inset from the rounded screen edges (no corner clipping of keys).
//  The Wi-Fi page auto-scans networks (async, non-blocking) and lets the
//  user tap a network to fill the form.
// ============================================================================
#include "ui/SettingsScreen.hpp"
#include "ui/AppHost.hpp"
#include "config/config.hpp"
#include "core/Event.hpp"
#include "core/Settings.hpp"
#include "core/Logger.hpp"
#include <WiFi.h>
#include <vector>
#include <algorithm>
#include <stdio.h>

namespace ui {

namespace {
    constexpr const char* kTag = "SettingsUi";
    constexpr uint32_t kCardBg = 0x141A21;
    constexpr uint32_t kPressed = 0x1D2731;

    lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, int32_t y) {
        lv_obj_t* l = lv_label_create(parent);
        lv_obj_set_style_text_font(l, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(l, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
        lv_label_set_text(l, text);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, y);
        return l;
    }

    lv_obj_t* makeTextarea(lv_obj_t* parent, void* cbUserData, const char* text,
                           bool password, int32_t y) {
        lv_obj_t* ta = lv_textarea_create(parent);
        lv_obj_set_size(ta, cfg::kLcdWidth - 76, 48);
        lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 0, y);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, password);
        lv_obj_set_style_text_font(ta, &ui_font_ru_16, 0);
        lv_obj_set_style_bg_color(ta, lv_color_hex(0x1B242E), 0);
        lv_obj_set_style_border_color(ta, lv_color_hex(0x2E3B49), 0);
        lv_obj_set_style_radius(ta, 14, 0);
        lv_obj_add_event_cb(ta, SettingsScreen::ta_focused_cb, LV_EVENT_FOCUSED,
                            cbUserData);
        if (text && text[0]) lv_textarea_set_text(ta, text);
        return ta;
    }

    // Circular badge with a tinted accent, same visual language as the menu.
    lv_obj_t* makeBadge(lv_obj_t* parent, const char* icon, uint32_t accent,
                        int32_t x, int32_t y) {
        const uint32_t tint = (((accent >> 16) & 0xFF) >> 1 << 16) |
                              (((accent >> 8) & 0xFF) >> 1 << 8) |
                              ((accent & 0xFF) >> 1);
        lv_obj_t* badge = lv_obj_create(parent);
        lv_obj_set_size(badge, 40, 40);
        lv_obj_align(badge, LV_ALIGN_TOP_LEFT, x, y);
        lv_obj_set_style_bg_color(badge, lv_color_hex(0x101418 | tint), 0);
        lv_obj_set_style_border_width(badge, 2, 0);
        lv_obj_set_style_border_color(badge, lv_color_hex(tint), 0);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(badge, static_cast<lv_obj_flag_t>(
                                     LV_OBJ_FLAG_SCROLLABLE |
                                     LV_OBJ_FLAG_CLICKABLE));
        lv_obj_t* l = lv_label_create(badge);
        lv_obj_set_style_text_font(l, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(accent), 0);
        lv_label_set_text(l, icon);
        lv_obj_center(l);
        return badge;
    }

    // Floating rounded keyboard card, inset from the rounded screen edges so
    // no key is clipped by the corner cut-outs.
    void styleKeyboardCard(lv_obj_t* kb) {
        lv_obj_set_size(kb, cfg::kLcdWidth - 40, 196);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_style_bg_color(kb, lv_color_hex(0x1B242E), 0);
        lv_obj_set_style_radius(kb, 20, 0);
        lv_obj_set_style_border_color(kb, lv_color_hex(0x2E3B49), 0);
        lv_obj_set_style_border_width(kb, 1, 0);
        lv_obj_set_style_pad_all(kb, 6, 0);
        lv_obj_set_style_pad_row(kb, 5, 0);
    }
}

// ----------------------------------------------------------------------------
//  Callbacks
// ----------------------------------------------------------------------------

void SettingsScreen::row_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const auto row = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn));
    if (!self) return;
    static const char* const kTitles[] = {
        "Wi-Fi", "Экран", "Дата и время", "ИИ-ассистент", "О часах"};
    if (row < 4) {
        self->ensurePage(static_cast<uint8_t>(row));
        if (self->pages_[row]) self->showPage(self->pages_[row], kTitles[row]);
    } else {
        self->ensureAbout();
        if (self->about_page_) self->showPage(self->about_page_, kTitles[4]);
    }
}

void SettingsScreen::ensurePage(uint8_t idx) {
    switch (idx) {
        case 0: if (!pages_[0]) buildWifiPage(); break;
        case 1: if (!pages_[1]) buildDisplayPage(); break;
        case 2: if (!pages_[2]) buildTimePage(); break;
        case 3: if (!pages_[3]) buildAiPage(); break;
    }
}

void SettingsScreen::ensureAbout() {
    if (!about_page_) buildAboutPage();
}

void SettingsScreen::ensureKb() {
    if (kb_) return;
    kb_ = lv_keyboard_create(root_);
    styleKeyboardCard(kb_);
    lv_obj_set_style_text_font(kb_, &ui_font_ru_16, 0);
    lv_obj_add_event_cb(kb_, kb_done_cb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(kb_, kb_done_cb, LV_EVENT_CANCEL, this);
}

void SettingsScreen::destroyPages() {
    // Free every lazily built page + the keyboard and null all pointers that
    // referenced objects inside them (they dangle after lv_obj_delete).
    for (auto*& p : pages_) {
        if (p) {
            lv_obj_delete(p);
            p = nullptr;
        }
    }
    if (about_page_) {
        lv_obj_delete(about_page_);
        about_page_ = nullptr;
    }
    if (kb_) {
        lv_obj_delete(kb_);
        kb_ = nullptr;
    }
    wifi_status_ = ssid_ta_ = pass_ta_ = net_list_ = scan_status_ = nullptr;
    bright_slider_ = bright_val_ = dim_dd_ = nullptr;
    tz_dd_ = backend_dd_ = nullptr;
    xz_group_ = oa_group_ = nullptr;
    xz_url_ta_ = xz_token_ta_ = oa_url_ta_ = oa_key_ta_ = oa_model_ta_ = nullptr;
    ai_status_ = about_label_ = nullptr;
    active_page_ = nullptr;
    nets_.clear();
    scan_pending_ = false;  // the poller must not touch the deleted labels
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    LOGI(kTag, "pages destroyed — LVGL pool free %u B",
         static_cast<unsigned>(m.free_size));
}

void SettingsScreen::page_click_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self) self->hideKb();
}

void SettingsScreen::ta_focused_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    self->ensureKb();
    lv_keyboard_set_textarea(self->kb_,
                             static_cast<lv_obj_t*>(lv_event_get_target(e)));
    lv_obj_remove_flag(self->kb_, LV_OBJ_FLAG_HIDDEN);
}

void SettingsScreen::kb_done_cb(lv_event_t* e) {
    // Close = destroy: the keyboard costs 2-3 KB of the shared LVGL pool and
    // is only needed while typing.  async delete — the event comes from the
    // keyboard object itself.
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self && self->kb_) {
        lv_obj_delete_async(self->kb_);
        self->kb_ = nullptr;
    }
}

void SettingsScreen::bar_back_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self) self->onBack();
}

void SettingsScreen::bar_save_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self) self->saveCurrent();
}

void SettingsScreen::scan_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self) self->startScan();
}

void SettingsScreen::brightness_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    const int32_t v = lv_slider_get_value(self->bright_slider_);
    core::Settings::instance().d().brightness = static_cast<uint8_t>(v);
    lv_label_set_text_fmt(self->bright_val_, "%d", static_cast<int>(v));

    // Live feedback: publish on every change, persist once per gesture.
    core::EventBus::instance().publish(
        core::Event::makeSettingsChanged(core::kSettingsDisplay));
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        core::Settings::instance().save();
    }
}

void SettingsScreen::dim_dd_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    static const uint16_t kVals[] = {15, 30, 60, 300, 0};
    const uint16_t idx = lv_dropdown_get_selected(self->dim_dd_);
    if (idx < 5) {
        core::Settings::instance().d().dimTimeoutS = kVals[idx];
        self->commit(core::kSettingsDisplay);
    }
}

void SettingsScreen::tz_dd_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    core::Settings::instance().d().tzIndex =
        static_cast<uint8_t>(lv_dropdown_get_selected(self->tz_dd_));
    self->commit(core::kSettingsTimezone);
}

void SettingsScreen::backend_dd_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (!self) return;
    const bool oa = lv_dropdown_get_selected(self->backend_dd_) == 1;
    lv_obj_add_flag(self->xz_group_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(self->oa_group_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(oa ? self->oa_group_ : self->xz_group_, LV_OBJ_FLAG_HIDDEN);
    self->refreshAiStatus();
}

void SettingsScreen::net_cb(lv_event_t* e) {
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!self || !btn) return;
    // Rows reference the local snapshot — the WiFi scan results themselves
    // are deleted right after rendering.
    const auto row = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn));
    if (row >= self->nets_.size()) return;
    const String& ssid = self->nets_[row].ssid;
    lv_textarea_set_text(self->ssid_ta_, ssid.c_str());
    lv_label_set_text_fmt(self->scan_status_, "Выбрано: %s — введите пароль",
                          ssid.c_str());
    // Jump straight to the password field with the keyboard open.
    self->ensureKb();
    lv_keyboard_set_textarea(self->kb_, self->pass_ta_);
}

void SettingsScreen::wifi_timer_cb(lv_timer_t* timer) {
    static_cast<SettingsScreen*>(lv_timer_get_user_data(timer))->pollScan();
}

// ----------------------------------------------------------------------------
//  Save dispatch
// ----------------------------------------------------------------------------

void SettingsScreen::saveCurrent() {
    if (active_page_ == pages_[0]) {
        auto& s = core::Settings::instance().d();
        strlcpy(s.wifiSsid, lv_textarea_get_text(ssid_ta_), sizeof(s.wifiSsid));
        strlcpy(s.wifiPass, lv_textarea_get_text(pass_ta_), sizeof(s.wifiPass));
        hideKb();
        commit(core::kSettingsWifi);
        refreshWifiRow();
        refreshWifiStatus();
        LOGI(kTag, "Wi-Fi credentials saved (ssid=\"%s\")", s.wifiSsid);
    } else if (active_page_ == pages_[3]) {
        auto& s = core::Settings::instance().d();
        s.aiBackend =
            static_cast<uint8_t>(lv_dropdown_get_selected(backend_dd_));
        strlcpy(s.aiWsUrl, lv_textarea_get_text(xz_url_ta_), sizeof(s.aiWsUrl));
        strlcpy(s.aiToken, lv_textarea_get_text(xz_token_ta_), sizeof(s.aiToken));
        strlcpy(s.aiApiUrl, lv_textarea_get_text(oa_url_ta_), sizeof(s.aiApiUrl));
        strlcpy(s.aiApiKey, lv_textarea_get_text(oa_key_ta_), sizeof(s.aiApiKey));
        strlcpy(s.aiModel, lv_textarea_get_text(oa_model_ta_), sizeof(s.aiModel));
        hideKb();
        commit(core::kSettingsAi);
        refreshAiStatus();
        LOGI(kTag, "AI settings saved (backend=%u)", s.aiBackend);
    }
}

// ----------------------------------------------------------------------------
//  Wi-Fi scan (async, non-blocking)
// ----------------------------------------------------------------------------

void SettingsScreen::startScan() {
    if (!scan_pending_) {
        WiFi.scanNetworks(true /*async*/);
        scan_pending_ = true;
        lv_label_set_text(scan_status_, "Сканирование…");
        LOGI(kTag, "Wi-Fi scan started");
    }
}

void SettingsScreen::pollScan() {
    if (!scan_pending_) return;
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;          // keep waiting
    scan_pending_ = false;
    if (n == WIFI_SCAN_FAILED) {
        lv_label_set_text(scan_status_, "Сканирование не удалось");
        return;
    }
    LOGI(kTag, "Wi-Fi scan done: %d networks", n);
    fillNetworks();
    WiFi.scanDelete();
}

void SettingsScreen::fillNetworks() {
    if (!net_list_) return;
    lv_obj_clean(net_list_);

    // Best-RSSI entry per SSID, sorted by signal; keep a local snapshot —
    // the raw scan results are freed by scanDelete() right after this.
    const int16_t n = WiFi.scanComplete();
    nets_.clear();
    for (int32_t i = 0; i < n; ++i) {
        bool dup = false;
        for (auto& r : nets_) {
            if (r.ssid == WiFi.SSID(i)) {
                if (WiFi.RSSI(i) > r.rssi) {
                    r.rssi = WiFi.RSSI(i);
                    r.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
                }
                dup = true;
                break;
            }
        }
        if (!dup && WiFi.SSID(i).length()) {
            nets_.push_back({WiFi.SSID(i), WiFi.RSSI(i),
                             WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
        }
    }
    std::sort(nets_.begin(), nets_.end(),
              [](const NetRow& a, const NetRow& b) { return a.rssi > b.rssi; });

    int32_t shown = 0;
    for (const auto& net : nets_) {
        if (shown >= 8) break;  // keep the list (and the LVGL pool) small

        lv_obj_t* btn = lv_btn_create(net_list_);
        lv_obj_set_size(btn, lv_pct(100), 44);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B242E), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A3642), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(shown)));
        lv_obj_add_event_cb(btn, net_cb, LV_EVENT_CLICKED, this);

        if (net.secure) {
            lv_obj_t* lock = lv_label_create(btn);
            lv_obj_set_style_text_font(lock, &ui_font_ru_16, 0);
            lv_obj_set_style_text_color(lock, lv_palette_main(LV_PALETTE_GREY), 0);
            lv_label_set_text(lock, "\xEF\x80\xA3");  // FA lock
            lv_obj_align(lock, LV_ALIGN_LEFT_MID, 10, 0);
        }

        lv_obj_t* name = lv_label_create(btn);
        lv_obj_set_style_text_font(name, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_label_set_text(name, net.ssid.c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, cfg::kLcdWidth - 200);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 34, 0);

        lv_obj_t* rssi = lv_label_create(btn);
        lv_obj_set_style_text_font(rssi, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(rssi,
            net.rssi > -60 ? lv_palette_main(LV_PALETTE_LIGHT_GREEN)
                           : lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text_fmt(rssi, "%d", static_cast<int>(net.rssi));
        lv_obj_align(rssi, LV_ALIGN_RIGHT_MID, -14, 0);
        ++shown;
    }

    if (!shown) {
        lv_obj_t* empty = lv_label_create(net_list_);
        lv_obj_set_style_text_font(empty, &ui_font_ru_16, 0);
        lv_obj_set_style_text_color(empty, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(empty, "Сети не найдены");
        lv_obj_center(empty);
    } else {
        lv_label_set_text_fmt(scan_status_, "Найдено сетей: %d — тапните",
                              static_cast<int>(shown));
    }
}

// ----------------------------------------------------------------------------
//  Page plumbing
// ----------------------------------------------------------------------------

lv_obj_t* SettingsScreen::makePage(const char* pageTitle) {
    // Pages live on the chrome root (children after `content` -> drawn on top)
    // and cover exactly the content area below the header.
    lv_obj_t* p = lv_obj_create(root_);
    lv_obj_set_size(p, cfg::kLcdWidth, cfg::kLcdHeight - 84);
    lv_obj_align(p, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_black(), 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 16, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(p, page_click_cb, LV_EVENT_CLICKED, this);
    return p;
}

void SettingsScreen::buildBottomBar() {
    bar_ = lv_obj_create(root_);
    lv_obj_set_size(bar_, cfg::kLcdWidth - 32, 54);
    lv_obj_align(bar_, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar_, 0, 0);
    lv_obj_set_style_pad_all(bar_, 0, 0);
    lv_obj_clear_flag(bar_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                                                       LV_OBJ_FLAG_CLICKABLE));

    bar_back_ = lv_btn_create(bar_);
    lv_obj_set_size(bar_back_, 120, 54);
    lv_obj_align(bar_back_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(bar_back_, lv_color_hex(0x1B242E), 0);
    lv_obj_set_style_bg_color(bar_back_, lv_color_hex(0x2A3642), LV_STATE_PRESSED);
    lv_obj_set_style_radius(bar_back_, 27, 0);
    lv_obj_set_style_shadow_width(bar_back_, 0, 0);
    lv_obj_add_event_cb(bar_back_, bar_back_cb, LV_EVENT_CLICKED, this);
    bar_back_label_ = lv_label_create(bar_back_);
    lv_obj_set_style_text_font(bar_back_label_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(bar_back_label_, lv_color_white(), 0);
    lv_label_set_text(bar_back_label_, LV_SYMBOL_LEFT " Назад");
    lv_obj_center(bar_back_label_);

    bar_save_ = lv_btn_create(bar_);
    lv_obj_set_size(bar_save_, 214, 54);
    lv_obj_align(bar_save_, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(bar_save_, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_bg_color(bar_save_, lv_color_hex(0x1E78D2), LV_STATE_PRESSED);
    lv_obj_set_style_radius(bar_save_, 27, 0);
    lv_obj_set_style_shadow_width(bar_save_, 0, 0);
    lv_obj_add_event_cb(bar_save_, bar_save_cb, LV_EVENT_CLICKED, this);
    bar_save_label_ = lv_label_create(bar_save_);
    lv_obj_set_style_text_font(bar_save_label_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(bar_save_label_, lv_color_white(), 0);
    lv_label_set_text(bar_save_label_, "Сохранить");
    lv_obj_center(bar_save_label_);
    lv_obj_add_flag(bar_save_, LV_OBJ_FLAG_HIDDEN);
}

void SettingsScreen::updateNavBar() {
    const bool onSub = active_page_ != nullptr;
    lv_label_set_text(bar_back_label_,
                      onSub ? (LV_SYMBOL_LEFT " К списку")
                            : (LV_SYMBOL_LEFT " К меню"));
    const bool save = onSub && (active_page_ == pages_[0] ||
                                active_page_ == pages_[3]);
    if (save) lv_obj_remove_flag(bar_save_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(bar_save_, LV_OBJ_FLAG_HIDDEN);
}

void SettingsScreen::showPage(lv_obj_t* page, const char* pageTitle) {
    hideKb();
    if (list_) lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
    for (auto* p : pages_)
        if (p) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    if (about_page_) lv_obj_add_flag(about_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
    active_page_ = page;
    if (chrome_title_) lv_label_set_text(chrome_title_, pageTitle);
    updateNavBar();
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    LOGI(kTag, "page \"%s\" open — LVGL pool free %u B",
         pageTitle, static_cast<unsigned>(m.free_size));

    if (page == pages_[0]) {
        refreshWifiStatus();
        startScan();  // auto-scan on entry
    } else if (page == pages_[3]) refreshAiStatus();
    else if (page == about_page_) refreshAbout();
}

void SettingsScreen::showList() {
    hideKb();
    for (auto* p : pages_)
        if (p) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    if (about_page_) lv_obj_add_flag(about_page_, LV_OBJ_FLAG_HIDDEN);
    if (list_) lv_obj_remove_flag(list_, LV_OBJ_FLAG_HIDDEN);
    active_page_ = nullptr;
    if (chrome_title_) lv_label_set_text(chrome_title_, title());
    updateNavBar();
    destroyPages();  // back to the cheap screen: one heavy page alive at most
}

void SettingsScreen::hideKb() {
    if (kb_) {
        lv_obj_delete_async(kb_);
        kb_ = nullptr;
    }
}

void SettingsScreen::commit(uint8_t mask) {
    core::Settings::instance().save();
    core::EventBus::instance().publish(core::Event::makeSettingsChanged(mask));
}

void SettingsScreen::onBack() {
    hideKb();
    if (active_page_) {
        showList();
    } else if (host_) {
        host_->back();
    }
}

void SettingsScreen::show() {
    refreshWifiRow();
    // NOTE: pages_ may legitimately be null (lazy pages) — a bare
    // `active_page_ == pages_[0]` compares null == null and fires refreshes
    // aimed at widgets that were never built (refreshAbout crashed on it).
    if (pages_[0] && active_page_ == pages_[0]) refreshWifiStatus();
    if (pages_[3] && active_page_ == pages_[3]) refreshAiStatus();
    if (about_page_ && active_page_ == about_page_) refreshAbout();
}

void SettingsScreen::hide() {
    // Leaving the app: free every lazily built page — the pool must be
    // available to the whole UI the moment another screen opens.
    destroyPages();
}

// ----------------------------------------------------------------------------
//  Builders
// ----------------------------------------------------------------------------

void SettingsScreen::create(AppHost* host) {
    App::create(host);
    chrome_back_ = false;  // the bottom bar replaces the small arrow

    lv_obj_t* content = createChrome(title());
    lv_obj_set_style_pad_all(content, 0, 0);

    // Pages and the keyboard are built lazily on first use (see ensurePage /
    // ensureKb) — the 80 KB LVGL pool is shared by every app on the watch.
    buildList(content);  // last: row user_data maps to the built pages
    buildBottomBar();    // on top of pages

    // ---- async scan poller -----------------------------------------------------
    wifi_timer_ = lv_timer_create(wifi_timer_cb, 1000, this);

    showList();
}

void SettingsScreen::buildList(lv_obj_t* content) {
    list_ = lv_obj_create(content);
    lv_obj_set_size(list_, cfg::kLcdWidth, cfg::kLcdHeight - 84);
    lv_obj_set_style_bg_color(list_, lv_color_black(), 0);
    lv_obj_set_style_border_width(list_, 0, 0);
    lv_obj_set_style_pad_all(list_, 14, 0);
    lv_obj_set_style_pad_bottom(list_, 64, 0);  // keep rows above the nav bar
    lv_obj_set_style_pad_row(list_, 10, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(list_, page_click_cb, LV_EVENT_CLICKED, this);

    struct RowDef { const char* icon; const char* name; uint32_t color; };
    const RowDef rows[] = {
        {LV_SYMBOL_WIFI,     "Wi-Fi",         0x4FC3F7},
        {LV_SYMBOL_IMAGE,    "Экран",         0xFFB74D},
        {LV_SYMBOL_REFRESH,  "Дата и время",  0x81C784},
        {"\xEF\x82\x86",     "ИИ-ассистент",  0x64B5F6},
        {LV_SYMBOL_LIST,     "О часах",       0xB0BEC5},
    };

    for (uint32_t i = 0; i < 5; ++i) {
        lv_obj_t* btn = lv_btn_create(list_);
        lv_obj_set_size(btn, lv_pct(100), 68);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kCardBg), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kPressed), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 20, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        makeBadge(btn, rows[i].icon, rows[i].color, 12, 14);

        lv_obj_t* name = lv_label_create(btn);
        lv_obj_set_style_text_font(name, &ui_font_ru_20, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_label_set_text(name, rows[i].name);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 66, i == 0 ? -13 : 0);

        lv_obj_t* sub = nullptr;
        if (i == 0) { wifi_row_sub_ = lv_label_create(btn); sub = wifi_row_sub_; }
        if (sub) {
            lv_obj_set_style_text_font(sub, &ui_font_ru_16, 0);
            lv_obj_set_style_text_color(sub, lv_palette_main(LV_PALETTE_GREY), 0);
            lv_label_set_text(sub, "");
            lv_obj_align(sub, LV_ALIGN_LEFT_MID, 66, 15);
        }

        lv_obj_t* chev = lv_label_create(btn);
        lv_obj_set_style_text_font(chev, &ui_font_ru_20, 0);
        lv_obj_set_style_text_color(chev, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -18, 0);

        // Row index -> page (pages_ order + about).
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(btn, row_cb, LV_EVENT_CLICKED, this);
    }
}

void SettingsScreen::buildWifiPage() {
    lv_obj_t* p = pages_[0] = makePage("Wi-Fi");

    wifi_status_ = lv_label_create(p);
    lv_obj_set_style_text_font(wifi_status_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(wifi_status_, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(wifi_status_, "");
    lv_label_set_long_mode(wifi_status_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wifi_status_, cfg::kLcdWidth - 76);
    lv_obj_align(wifi_status_, LV_ALIGN_TOP_LEFT, 0, 0);

    // scan row
    lv_obj_t* scanTitle = lv_label_create(p);
    lv_obj_set_style_text_font(scanTitle, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(scanTitle, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_label_set_text(scanTitle, "Сети рядом");
    lv_obj_align(scanTitle, LV_ALIGN_TOP_LEFT, 0, 40);

    lv_obj_t* refresh = lv_btn_create(p);
    lv_obj_set_size(refresh, 96, 32);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, -8, 36);
    lv_obj_set_style_bg_color(refresh, lv_color_hex(0x1B242E), 0);
    lv_obj_set_style_radius(refresh, 16, 0);
    lv_obj_set_style_shadow_width(refresh, 0, 0);
    lv_obj_add_event_cb(refresh, scan_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* rl = lv_label_create(refresh);
    lv_obj_set_style_text_font(rl, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(rl, lv_color_hex(0x4FC3F7), 0);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_center(rl);

    scan_status_ = lv_label_create(p);
    lv_obj_set_style_text_font(scan_status_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(scan_status_, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(scan_status_, "Нажмите обновить");
    lv_obj_align(scan_status_, LV_ALIGN_TOP_LEFT, 0, 76);

    net_list_ = lv_obj_create(p);
    lv_obj_set_size(net_list_, cfg::kLcdWidth - 76, 118);
    lv_obj_align(net_list_, LV_ALIGN_TOP_LEFT, 0, 98);
    lv_obj_set_style_bg_color(net_list_, lv_color_hex(0x0F1419), 0);
    lv_obj_set_style_border_color(net_list_, lv_color_hex(0x222C36), 0);
    lv_obj_set_style_border_width(net_list_, 1, 0);
    lv_obj_set_style_radius(net_list_, 16, 0);
    lv_obj_set_style_pad_all(net_list_, 6, 0);
    lv_obj_set_style_pad_row(net_list_, 5, 0);
    lv_obj_set_flex_flow(net_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(net_list_, LV_SCROLLBAR_MODE_OFF);

    makeLabel(p, "Имя сети (SSID)", 226);
    ssid_ta_ = makeTextarea(p, this, core::Settings::instance().d().wifiSsid, false, 248);

    makeLabel(p, "Пароль", 306);
    pass_ta_ = makeTextarea(p, this, core::Settings::instance().d().wifiPass, true, 328);
}

void SettingsScreen::buildDisplayPage() {
    lv_obj_t* p = pages_[1] = makePage("Экран");
    const auto& s = core::Settings::instance().d();

    // Brightness card
    lv_obj_t* card = lv_obj_create(p);
    lv_obj_set_size(card, cfg::kLcdWidth - 76, 92);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, 0, 4);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCardBg), 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                                                       LV_OBJ_FLAG_CLICKABLE));

    lv_obj_t* bl = lv_label_create(card);
    lv_obj_set_style_text_font(bl, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(bl, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_label_set_text(bl, "Яркость");
    lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 0, 0);

    bright_slider_ = lv_slider_create(card);
    lv_obj_set_size(bright_slider_, cfg::kLcdWidth - 210, 20);
    lv_obj_align(bright_slider_, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    lv_slider_set_range(bright_slider_, 10, 255);
    lv_slider_set_value(bright_slider_, s.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bright_slider_, lv_color_hex(0x2E3B49), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bright_slider_, lv_color_hex(0xFFB74D), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bright_slider_, lv_color_hex(0xFFB74D), LV_PART_KNOB);
    lv_obj_add_event_cb(bright_slider_, brightness_cb, LV_EVENT_ALL, this);

    bright_val_ = lv_label_create(card);
    lv_obj_set_style_text_font(bright_val_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(bright_val_, lv_color_white(), 0);
    lv_label_set_text_fmt(bright_val_, "%d", static_cast<int>(s.brightness));
    lv_obj_align(bright_val_, LV_ALIGN_BOTTOM_RIGHT, 0, -8);

    makeLabel(p, "Таймаут подсветки", 104);
    dim_dd_ = lv_dropdown_create(p);
    lv_obj_align(dim_dd_, LV_ALIGN_TOP_LEFT, 0, 130);
    lv_obj_set_width(dim_dd_, cfg::kLcdWidth - 76);
    lv_dropdown_set_options(dim_dd_, "15 с\n30 с\n1 мин\n5 мин\nВыкл");
    lv_obj_set_style_text_font(dim_dd_, &ui_font_ru_16, 0);
    const uint16_t vals[] = {15, 30, 60, 300, 0};
    uint16_t sel = 0;
    for (uint16_t i = 0; i < 5; ++i)
        if (vals[i] == s.dimTimeoutS) sel = i;
    lv_dropdown_set_selected(dim_dd_, sel);
    lv_obj_add_event_cb(dim_dd_, dim_dd_cb, LV_EVENT_VALUE_CHANGED, this);
}

void SettingsScreen::buildTimePage() {
    lv_obj_t* p = pages_[2] = makePage("Дата и время");
    const auto& s = core::Settings::instance().d();

    makeLabel(p, "Часовой пояс", 4);
    tz_dd_ = lv_dropdown_create(p);
    lv_obj_align(tz_dd_, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_width(tz_dd_, cfg::kLcdWidth - 76);

    // Build the "\n"-joined option list once (persisted in static storage).
    static char opts[160];
    size_t count = 0;
    const core::TzPreset* presets = core::tzPresets(count);
    size_t off = 0;
    for (size_t i = 0; i < count; ++i) {
        off += static_cast<size_t>(
            snprintf(opts + off, sizeof(opts) - off, "%s%s",
                     i ? "\n" : "", presets[i].label));
    }
    lv_dropdown_set_options(tz_dd_, opts);
    lv_obj_set_style_text_font(tz_dd_, &ui_font_ru_16, 0);
    lv_dropdown_set_selected(tz_dd_, s.tzIndex < count ? s.tzIndex : 0);
    lv_obj_add_event_cb(tz_dd_, tz_dd_cb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* hint = lv_label_create(p);
    lv_obj_set_style_text_font(hint, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(hint,
        "Время идёт от RTC; при Wi-Fi\nкорректируется по NTP.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, cfg::kLcdWidth - 76);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 100);
}

void SettingsScreen::buildAiPage() {
    lv_obj_t* p = pages_[3] = makePage("ИИ-ассистент");
    const auto& s = core::Settings::instance().d();

    backend_dd_ = lv_dropdown_create(p);
    lv_obj_align(backend_dd_, LV_ALIGN_TOP_LEFT, 0, 4);
    lv_obj_set_width(backend_dd_, cfg::kLcdWidth - 76);
    lv_dropdown_set_options(backend_dd_, "XiaoZhi (официальный)\nOpenAI-совместимый");
    lv_obj_set_style_text_font(backend_dd_, &ui_font_ru_16, 0);
    lv_dropdown_set_selected(backend_dd_, s.aiBackend == 1 ? 1 : 0);
    lv_obj_add_event_cb(backend_dd_, backend_dd_cb, LV_EVENT_VALUE_CHANGED, this);

    // ---- XiaoZhi group ---------------------------------------------------------
    xz_group_ = lv_obj_create(p);
    lv_obj_set_size(xz_group_, cfg::kLcdWidth - 32, 152);
    lv_obj_align(xz_group_, LV_ALIGN_TOP_LEFT, 0, 52);
    lv_obj_set_style_bg_opa(xz_group_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(xz_group_, 0, 0);
    lv_obj_set_style_pad_all(xz_group_, 0, 0);
    lv_obj_clear_flag(xz_group_, static_cast<lv_obj_flag_t>(
                                     LV_OBJ_FLAG_SCROLLABLE |
                                     LV_OBJ_FLAG_CLICKABLE));

    makeLabel(xz_group_, "WebSocket URL", 0);
    xz_url_ta_ = makeTextarea(xz_group_, this, s.aiWsUrl, false, 24);
    makeLabel(xz_group_, "Токен (заполнится после активации)", 84);
    xz_token_ta_ = makeTextarea(xz_group_, this, s.aiToken, true, 108);

    // ---- OpenAI group ------------------------------------------------------------
    oa_group_ = lv_obj_create(p);
    lv_obj_set_size(oa_group_, cfg::kLcdWidth - 32, 212);
    lv_obj_align(oa_group_, LV_ALIGN_TOP_LEFT, 0, 52);
    lv_obj_set_style_bg_opa(oa_group_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(oa_group_, 0, 0);
    lv_obj_set_style_pad_all(oa_group_, 0, 0);
    lv_obj_clear_flag(oa_group_, static_cast<lv_obj_flag_t>(
                                     LV_OBJ_FLAG_SCROLLABLE |
                                     LV_OBJ_FLAG_CLICKABLE));

    makeLabel(oa_group_, "API URL (…/v1/chat/completions)", 0);
    oa_url_ta_ = makeTextarea(oa_group_, this, s.aiApiUrl, false, 24);
    makeLabel(oa_group_, "API-ключ", 84);
    oa_key_ta_ = makeTextarea(oa_group_, this, s.aiApiKey, true, 108);
    makeLabel(oa_group_, "Модель", 172);
    oa_model_ta_ = makeTextarea(oa_group_, this, s.aiModel, false, 196);

    // Apply initial group visibility per the stored backend.
    lv_obj_add_flag(oa_group_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(xz_group_, LV_OBJ_FLAG_HIDDEN);
    if (s.aiBackend == 1) lv_obj_remove_flag(oa_group_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(xz_group_, LV_OBJ_FLAG_HIDDEN);

    ai_status_ = lv_label_create(p);
    lv_obj_set_style_text_font(ai_status_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(ai_status_, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_long_mode(ai_status_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ai_status_, cfg::kLcdWidth - 76);
    lv_obj_set_style_text_align(ai_status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ai_status_, LV_ALIGN_TOP_MID, 0, 272);
}

void SettingsScreen::buildAboutPage() {
    about_page_ = makePage("О часах");
    about_label_ = lv_label_create(about_page_);
    lv_obj_set_style_text_font(about_label_, &ui_font_ru_16, 0);
    lv_obj_set_style_text_color(about_label_, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_label_set_long_mode(about_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(about_label_, cfg::kLcdWidth - 76);
    lv_obj_align(about_label_, LV_ALIGN_TOP_LEFT, 0, 4);
}

// ----------------------------------------------------------------------------
//  Status refreshers
// ----------------------------------------------------------------------------

void SettingsScreen::refreshWifiRow() {
    if (!wifi_row_sub_) return;
    const bool conn = (WiFi.status() == WL_CONNECTED);
    if (conn) {
        lv_label_set_text(wifi_row_sub_, WiFi.SSID().c_str());
        lv_obj_set_style_text_color(wifi_row_sub_,
                                    lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
    } else {
        lv_label_set_text(wifi_row_sub_, "нет подключения");
        lv_obj_set_style_text_color(wifi_row_sub_,
                                    lv_palette_main(LV_PALETTE_GREY), 0);
    }
}

void SettingsScreen::refreshWifiStatus() {
    if (!wifi_status_) return;
    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text_fmt(wifi_status_,
            LV_SYMBOL_WIFI " Подключено: %s  —  IP %s",
            WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        lv_obj_set_style_text_color(wifi_status_,
                                    lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
    } else {
        lv_label_set_text(wifi_status_,
            "Нет подключения. Выберите сеть из списка,\n"
            "введите пароль и нажмите «Сохранить».");
        lv_obj_set_style_text_color(wifi_status_,
                                    lv_palette_main(LV_PALETTE_GREY), 0);
    }
}

void SettingsScreen::refreshAiStatus() {
    if (!ai_status_) return;
    const auto& s = core::Settings::instance().d();
    if (s.aiBackend == 0) {
        lv_label_set_text(ai_status_, s.aiToken[0]
            ? "Токен задан — XiaoZhi готов к работе."
            : "Токена нет: откройте чат, часы получат\nкод активации автоматически.");
    } else {
        lv_label_set_text(ai_status_, s.aiApiKey[0]
            ? "Ключ задан — можно писать в чат."
            : "Введите API-ключ, чтобы включить бэкенд.");
    }
}

void SettingsScreen::refreshAbout() {
    if (!about_label_) return;
    const uint32_t heap = ESP.getFreeHeap() / 1024;
    const uint32_t psram = ESP.getFreePsram() / 1024;
    lv_label_set_text_fmt(about_label_,
        "Smartwatch v%s\n"
        "Waveshare ESP32-S3-Touch-AMOLED-2.06\n\n"
        "MAC: %s\nIP: %s\n\n"
        "Свободно: heap %lu КБ, PSRAM %lu КБ\n"
        "Работает: %lu с",
        cfg::kFwVersion,
        WiFi.macAddress().c_str(),
        WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "—",
        static_cast<unsigned long>(heap),
        static_cast<unsigned long>(psram),
        static_cast<unsigned long>(millis() / 1000));
}

}  // namespace ui
