// ============================================================================
//  App.cpp — shared screen chrome for all apps.
//
//  The 2.06" AMOLED panel has rounded corners (~38 px), so the chrome is a
//  floating design: a circular back button and a title instead of a full-
//  width header bar, and the content area keeps a bottom inset so nothing
//  interactive lands in the corner cut-outs.
// ============================================================================
#include "ui/App.hpp"
#include "ui/AppHost.hpp"
#include "config/config.hpp"

namespace ui {

void App::onBack() {
    if (host_) host_->back();
}

namespace {
    void back_btn_cb(lv_event_t* e) {
        auto* host = static_cast<AppHost*>(lv_event_get_user_data(e));
        if (!host) return;
        if (App* cur = host->app(host->current())) cur->onBack();
    }
}

lv_obj_t* App::createChrome(const char* titleText) {
    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // ---- floating back button (round, corner-safe; optional) -----------------
    if (chrome_back_) {
        lv_obj_t* back = lv_btn_create(root_);
        lv_obj_set_size(back, 56, 56);
        lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 10);
        lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(0x1B242E), 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(0x2A3642), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(back, 0, 0);
        lv_obj_set_style_shadow_width(back, 0, 0);
        lv_obj_add_event_cb(back, back_btn_cb, LV_EVENT_CLICKED, host_);

        lv_obj_t* backLabel = lv_label_create(back);
        lv_obj_set_style_text_font(backLabel, &ui_font_ru_20, 0);
        lv_obj_set_style_text_color(backLabel, lv_color_white(), 0);
        lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
        lv_obj_center(backLabel);
    }

    // ---- title ----------------------------------------------------------------
    lv_obj_t* title = lv_label_create(root_);
    lv_obj_set_style_text_font(title, &ui_font_ru_20, 0);
    lv_obj_set_style_text_color(title, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_label_set_text(title, titleText);
    lv_obj_align(title, LV_ALIGN_TOP_MID, chrome_back_ ? 36 : 0, 22);
    chrome_title_ = title;

    // ---- content container (inset from the bottom corner cut-outs) ------------
    lv_obj_t* content = lv_obj_create(root_);
    lv_obj_set_size(content, cfg::kLcdWidth, cfg::kLcdHeight - 84);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(content, lv_color_black(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 14, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    return content;
}

}  // namespace ui
