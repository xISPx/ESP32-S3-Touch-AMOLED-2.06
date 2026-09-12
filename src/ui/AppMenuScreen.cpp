// ============================================================================
//  AppMenuScreen.cpp — 2-column launcher grid built from the app registry.
// ============================================================================
#include "ui/AppMenuScreen.hpp"
#include "ui/AppHost.hpp"
#include "config/config.hpp"
#include "core/Logger.hpp"

namespace ui {

namespace {
    constexpr int32_t kTileW   = 178;
    constexpr int32_t kTileH   = 182;
    constexpr int32_t kTileGap = 10;
}

void AppMenuScreen::open_app_cb(lv_event_t* e) {
    auto* self = static_cast<AppMenuScreen*>(lv_event_get_user_data(e));
    auto* tile = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* target = static_cast<App*>(lv_obj_get_user_data(tile));
    if (!self || !target || !self->host_) return;

    // Resolve the registered slot of the tapped app.
    for (uint8_t i = 2; i < self->host_->kMaxApps; ++i) {
        if (self->host_->app(i) == target) {
            LOGI("Menu", "tile tapped: %s (slot %u)", target->title(),
                 static_cast<unsigned>(i));
            self->host_->open(i);
            return;
        }
    }
    LOGW("Menu", "tapped app not registered: %s", target->title());
}

void AppMenuScreen::create(AppHost* host) {
    App::create(host);

    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(root_);
    lv_obj_set_style_text_font(title, &ui_font_ru_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Приложения");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

    // Scrollable wrap container; gesture handler lives on the screen root, so
    // swipes still work while the grid scrolls vertically.  The bottom inset
    // keeps the last row clear of the rounded corner cut-outs.
    lv_obj_t* grid = lv_obj_create(root_);
    lv_obj_set_size(grid, cfg::kLcdWidth - 2 * kTileGap,
                    cfg::kLcdHeight - 112);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(grid, lv_color_black(), 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, kTileGap, 0);
    lv_obj_set_style_pad_bottom(grid, 40, 0);  // keep tiles off the corner cut-outs
    lv_obj_set_style_pad_row(grid, kTileGap, 0);
    lv_obj_set_style_pad_column(grid, kTileGap, 0);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Tiles for every registered app (slots 2+; 0 watchface, 1 menu itself).
    for (uint8_t i = 2; i < host->kMaxApps; ++i) {
        App* app = host->app(i);
        if (!app || app->title()[0] == '\0') continue;

        lv_obj_t* tile = lv_obj_create(grid);
        lv_obj_set_size(tile, kTileW, kTileH);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x141A21), 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x1D2731), LV_STATE_PRESSED);
        lv_obj_set_style_radius(tile, 24, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(tile, app);
        lv_obj_add_event_cb(tile, open_app_cb, LV_EVENT_CLICKED, this);

        // Circular icon badge: dark disc tinted by the app accent.
        const uint32_t acc = app->accent();
        const uint32_t tint =
            (((acc >> 16) & 0xFF) >> 1 << 16) |
            (((acc >> 8) & 0xFF) >> 1 << 8) |
            ((acc & 0xFF) >> 1);

        lv_obj_t* badge = lv_obj_create(tile);
        lv_obj_set_size(badge, 96, 96);
        lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 16);
        lv_obj_set_style_bg_color(badge, lv_color_hex(0x101418 | tint), 0);
        lv_obj_set_style_border_width(badge, 2, 0);
        lv_obj_set_style_border_color(badge, lv_color_hex(tint), 0);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(badge, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

        lv_obj_t* icon = lv_label_create(badge);
        lv_obj_set_style_text_font(icon, &ui_font_ru_28, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(acc), 0);
        lv_obj_set_style_transform_scale(icon, 768, 0);  // 3x around the centre
        lv_label_set_text(icon, app->icon());
        lv_obj_center(icon);

        lv_obj_t* label = lv_label_create(tile);
        lv_obj_set_style_text_font(label, &ui_font_ru_20, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_label_set_text(label, app->title());
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -24);
    }
}

}  // namespace ui
