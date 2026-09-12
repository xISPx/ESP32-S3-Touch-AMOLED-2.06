// ============================================================================
//  FitnessScreen.hpp — pedometer app: step ring, distance, accel trace.
// ============================================================================
#pragma once

#include <cstdint>
#include "ui/App.hpp"

namespace ui {

class FitnessScreen final : public App {
public:
    void create(AppHost* host) override;
    void onModel(const UiModel& m) override;

    const char* title() const override { return "Шаги"; }
    const char* icon() const override { return LV_SYMBOL_SHUFFLE; }
    uint32_t accent() const override { return 0x7ED957; }

private:
    void refreshActivity(const UiModel& m);
    void pushAccelSample(const UiModel& m);

    lv_obj_t* ring_ = nullptr;
    lv_obj_t* steps_label_ = nullptr;
    lv_obj_t* distance_label_ = nullptr;
    lv_obj_t* calories_label_ = nullptr;
    lv_obj_t* accel_label_ = nullptr;
    lv_obj_t* chart_ = nullptr;
    uint32_t last_push_ms_ = 0;
};

}  // namespace ui
