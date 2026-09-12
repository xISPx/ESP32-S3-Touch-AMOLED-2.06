// ============================================================================
//  AppHost.cpp — navigation + gesture plumbing.
// ============================================================================
#include "ui/AppHost.hpp"
#include "ui/App.hpp"
#include "core/Logger.hpp"
#include <Arduino.h>

namespace ui {

namespace {
    constexpr const char* kTag = "AppHost";
    constexpr uint32_t kNavDebounceMs = 260;  // slightly more than the animation
}

bool AppHost::navAllowed() {
    const uint32_t now = millis();
    if (now - last_nav_ms_ < kNavDebounceMs) return false;
    last_nav_ms_ = now;
    return true;
}

bool AppHost::add(App* app) {
    if (count_ >= kMaxApps || !app) return false;
    apps_[count_++] = app;
    return true;
}

void AppHost::build() {
    building_ = true;
    for (uint8_t i = 0; i < count_; ++i) {
        LOGI(kTag, "creating app %u", static_cast<unsigned>(i));
        apps_[i]->create(this);
        if (!apps_[i]->root()) {
            LOGE(kTag, "app %u created no screen", i);
            continue;
        }
        lv_obj_add_event_cb(apps_[i]->root(), gesture_cb, LV_EVENT_GESTURE, this);
        lv_mem_monitor_t m;
        lv_mem_monitor(&m);
        LOGI(kTag, "app %u done — LVGL pool used %u, free %u (frag %u%%)",
             static_cast<unsigned>(i), static_cast<unsigned>(m.total_size - m.free_size),
             static_cast<unsigned>(m.free_size),
             static_cast<unsigned>(m.frag_pct));
    }
    building_ = false;
    lv_screen_load_anim(apps_[0]->root(), LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    apps_[0]->show();
    current_ = 0;
    last_nav_ms_ = millis();
}

void AppHost::open(uint8_t id) {
    if (id >= count_ || id == current_) return;
    if (!navAllowed()) return;
    LOGI(kTag, "open slot %u", static_cast<unsigned>(id));
    App* prev = apps_[current_];
    App* next = apps_[id];
    current_ = id;
    lv_screen_load_anim(next->root(), LV_SCR_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
    prev->hide();
    next->show();
}

void AppHost::back() {
    if (current_ == 0) return;
    if (!navAllowed()) return;
    if (current_ == 1) {
        home();
    } else {
        // apps slide back to the menu
        App* prev = apps_[current_];
        current_ = 1;
        lv_screen_load_anim(apps_[1]->root(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 220, 0, false);
        prev->hide();
        apps_[1]->show();
    }
}

void AppHost::home() {
    // Guard already applied by back() — this is the internal unwind target.
    if (current_ == 0) return;
    App* prev = apps_[current_];
    current_ = 0;
    lv_screen_load_anim(apps_[0]->root(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 220, 0, false);
    prev->hide();
    apps_[0]->show();
}

void AppHost::onModel(const UiModel& m) {
    if (building_) return;
    for (uint8_t i = 0; i < count_; ++i) apps_[i]->onModel(m);
}

void AppHost::dispatchAi(const core::Event& e) {
    // The chat app buffers messages while the user is elsewhere.
    for (uint8_t i = 0; i < count_; ++i) apps_[i]->onAiText(e);
}

void AppHost::gesture_cb(lv_event_t* e) {
    auto* self = static_cast<AppHost*>(lv_event_get_user_data(e));
    if (!self || self->building_) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    // Events arrive while the gesture's screen is still the active one, and
    // apps with sub-pages unwind their own stack first (onBack).
    const uint8_t cur = self->current_;

    switch (dir) {
        case LV_DIR_RIGHT:
            if (App* a = self->app(cur)) a->onBack();
            break;
        case LV_DIR_LEFT:
        case LV_DIR_BOTTOM:
            if (cur == 0) self->open(1);  // watchface -> launcher
            break;
        default:
            break;
    }
}

}  // namespace ui
