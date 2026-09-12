// ============================================================================
//  UiTask.hpp — owns LVGL, the display flush, touch input and all apps.
//
//  Rationale: LVGL is single-threaded; every UI mutation and every lv_* call
//  happens here.  Other tasks communicate exclusively through the mailbox
//  queue, which is drained by a 10 ms lv_timer, so no locking is needed.
//  Apps (watchface, launcher menu, status, fitness, stopwatch, chat,
//  settings) are managed by ui::AppHost; navigation = gestures + BOOT button.
// ============================================================================
#pragma once

#include "core/TaskBase.hpp"
#include "core/Settings.hpp"
#include "hal/Hal.hpp"
#include "ui/AppHost.hpp"
#include "ui/WatchfaceScreen.hpp"
#include "ui/AppMenuScreen.hpp"
#include "ui/StatusScreen.hpp"
#include "ui/FitnessScreen.hpp"
#include "ui/StopwatchScreen.hpp"
#include "ui/ChatScreen.hpp"
#include "ui/MusicScreen.hpp"
#include "ui/SnakeScreen.hpp"
#include "ui/Game2048Screen.hpp"
#include "ui/ViewerScreen.hpp"
#include "ui/SettingsScreen.hpp"

namespace tasks {

class UiTask final : public core::TaskBase {
public:
    explicit UiTask(hal::Hal& hal) : hal_(hal) {}

    // Needed by the static LVGL callbacks (flush/touch glue).
    hal::Hal& hal() { return hal_; }
    void noteTouch();  // called from the LVGL touch read callback

    // -- lv_timer callbacks (invoked from static trampolines in the .cpp) ----
    void onDrainTimer();   // fold mailbox events into the model + refresh apps
    void onClockTimer();   // 1 Hz clock/label refresh + idle dim

private:
    void run() override;

    // -- setup steps (called once inside run) --------------------------------
    bool initLvgl();
    bool createApps();

    void updateIdleDim();
    void showShutdownScreen();
    void createBootScreen();
    static void finish_boot_cb(lv_timer_t* timer);

    hal::Hal& hal_;
    ui::UiModel model_;
    ui::AppHost host_;

    ui::WatchfaceApp watchface_;
    ui::AppMenuScreen menu_;
    ui::StatusScreen status_;
    ui::FitnessScreen fitness_;
    ui::StopwatchScreen stopwatch_;
    ui::ChatScreen chat_;
    ui::MusicScreen music_{hal_};
    ui::SnakeScreen snake_;
    ui::Game2048Screen game2048_;
    ui::ViewerScreen viewer_;
    ui::SettingsScreen settings_;

    lv_display_t* disp_ = nullptr;
    lv_indev_t* indev_ = nullptr;
    lv_timer_t* drain_timer_ = nullptr;
    lv_timer_t* clock_timer_ = nullptr;
    uint16_t* draw_buf_ = nullptr;      // full-frame buffer in PSRAM
    lv_obj_t* shutdown_screen_ = nullptr;
    lv_obj_t* boot_screen_ = nullptr;   // power-on animation
    lv_obj_t* boot_arc_ = nullptr;
    lv_obj_t* boot_logo_ = nullptr;
    lv_obj_t* boot_sub_ = nullptr;
    uint32_t last_touch_ms_ = 0;
    bool dimmed_ = false;
    bool screen_off_ = false;           // deep stage: panel in sleep (displayOff)
    bool frame_dirty_ = true;           // LVGL rendered something — push the frame
    bool shutting_down_ = false;        // power-off requested (long PWR press)
};

}  // namespace tasks
