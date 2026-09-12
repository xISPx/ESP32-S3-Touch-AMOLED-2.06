// ============================================================================
//  Screens — each screen owns its LVGL widget tree and knows how to refresh
//  itself from a UiModel snapshot.  Screens never touch hardware.
// ============================================================================
#pragma once

#include <lvgl.h>
#include "ui/UiModel.hpp"
#include "ui/App.hpp"

namespace ui {

class StatusScreen final : public App {
public:
    void create(AppHost* host) override;
    void onModel(const UiModel& m) override;
    const char* title() const override { return "Статус"; }
    const char* icon() const override { return LV_SYMBOL_LIST; }
    uint32_t accent() const override { return 0x8BC34A; }

private:
    void refresh(const UiModel& m);

    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* imu_label_ = nullptr;
    lv_obj_t* sys_label_ = nullptr;
    lv_obj_t* net_label_ = nullptr;
};

}  // namespace ui
