// ============================================================================
//  Screens — each screen owns its LVGL widget tree and knows how to refresh
//  itself from a UiModel snapshot.  Screens never touch hardware.
// ============================================================================
#pragma once

#include <lvgl.h>
#include "ui/UiModel.hpp"
#include "ui/App.hpp"

namespace ui {

class WatchfaceScreen {
public:
    void create(lv_obj_t* parent);
    void refresh(const UiModel& m);

    lv_obj_t* root() { return root_; }

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* seconds_ring_ = nullptr;
    lv_obj_t* battery_card_ = nullptr;
    lv_obj_t* battery_arc_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* charge_label_ = nullptr;
    lv_obj_t* steps_card_ = nullptr;
    lv_obj_t* steps_arc_ = nullptr;
    lv_obj_t* steps_label_ = nullptr;
    lv_obj_t* wifi_label_ = nullptr;
};

// App wrapper: the watchface is slot 0 of the AppHost (no launcher tile).
class WatchfaceApp final : public App {
public:
    void create(AppHost* host) override {
        App::create(host);
        wf_.create(nullptr);
        root_ = wf_.root();
    }
    void onModel(const UiModel& m) override { wf_.refresh(m); }
    void show() override {}
    void hide() override {}

private:
    WatchfaceScreen wf_;
};

}  // namespace ui
