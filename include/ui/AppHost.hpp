// ============================================================================
//  AppHost.hpp — screen registry + navigation for the watch UI.
//
//  Slot 0 is the watchface, slot 1 the launcher menu, slots 2+ the apps.
//  Navigation rules (shared by gestures and the BOOT button):
//    * watchface --swipe-left--> menu
//    * menu/app  --swipe-right / back--> menu (watchface when in menu)
//    * menu --tap icon--> app
// ============================================================================
#pragma once

#include <cstdint>
#include "lvgl.h"
#include "ui/UiModel.hpp"
#include "core/Event.hpp"

namespace ui {

class App;

class AppHost {
public:
    static constexpr uint8_t kMaxApps = 12;

    // Registration order defines navigation slots; call before build().
    bool add(App* app);

    // create() every registered app and attach gesture handlers.
    void build();

    void open(uint8_t id);   // animate to slot id
    void back();             // one level up (app -> menu -> watchface)
    void home();             // straight to the watchface

    uint8_t current() const { return current_; }
    App* app(uint8_t id) { return (id < count_) ? apps_[id] : nullptr; }

    // Fold new data into the visible app (chat app is updated even when
    // hidden so that no AI message is lost).
    void onModel(const UiModel& m);
    void dispatchAi(const core::Event& e);

private:
    static void gesture_cb(lv_event_t* e);

    // One navigation per gesture/tap; ignores repeats during the animation.
    bool navAllowed();

    App* apps_[kMaxApps] = {};
    uint8_t count_ = 0;
    uint8_t current_ = 0;
    bool building_ = false;  // gesture callbacks fire on load before current_ settles
    uint32_t last_nav_ms_ = 0;
};

}  // namespace ui
